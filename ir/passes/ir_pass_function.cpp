#include <algorithm>

#include "ir_pass_function.h"
#include "ir_pass_propagate_types.h"

namespace dxbc_spv::ir {

FunctionCleanupPass::FunctionCleanupPass(Builder& builder)
: m_builder(builder) {

}


FunctionCleanupPass::~FunctionCleanupPass() {

}


void FunctionCleanupPass::resolveSharedTemps() {
  gatherSharedTempUses();
  propagateSharedTempUses();
  determineFunctionCallDepth();

  /* Order functions by call depth in decending order. This is necessary
   * since processing function calls will insert new temp load/store ops,
   * so we need to process most deeply nested functions first. */
  util::small_vector<std::pair<SsaDef, uint32_t>, 64u> functions;

  for (const auto& e : m_callDepth) {
    if (!isEntryPointFunction(e.first))
      functions.push_back(e);
  }

  std::sort(functions.begin(), functions.end(),
    [] (const std::pair<SsaDef, uint32_t>& a, const std::pair<SsaDef, uint32_t>& b) {
      return a.second > b.second;
    });

  for (const auto& e : functions)
    resolveSharedTempsForFunction(e.first);
}


void FunctionCleanupPass::runResolveSharedTempPass(Builder& builder) {
  FunctionCleanupPass(builder).resolveSharedTemps();
}


void FunctionCleanupPass::removeUnusedParameters() {
  std::unordered_set<ParamEntry, ParamEntryHash> usedParams;
  std::unordered_set<SsaDef> functions;

  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclParam) {
      auto [a, b] = m_builder.getUses(iter->getDef());

      for (auto i = a; i != b; i++) {
        if (i->getOpCode() == OpCode::eParamLoad) {
          auto function = SsaDef(i->getOperand(0u));
          usedParams.insert({ function, iter->getDef() });
        }

        if (i->getOpCode() == OpCode::eFunction)
          functions.insert(i->getDef());
      }
    }
  }

  for (auto function : functions) {
    auto oldFunctionOp = m_builder.getOp(function);
    auto newFunctionOp = Op::Function(ScalarType::eVoid);

    util::small_vector<SsaDef, 64u> uses;
    m_builder.getUses(function, uses);

    /* Remove unused parameters from the declaration */
    for (uint32_t i = 0u; i < oldFunctionOp.getFirstLiteralOperandIndex(); i++) {
      auto param = SsaDef(oldFunctionOp.getOperand(i));

      if (usedParams.find({ function, param }) != usedParams.end())
        newFunctionOp.addParam(param);
    }

    /* Void returns aren't technically 'used', but we can't
     * actually eliminate anything either */
    bool usesReturnValue = oldFunctionOp.getType().isVoidType();

    for (auto use : uses) {
      const auto& useOp = m_builder.getOp(use);

      if (useOp.getOpCode() != OpCode::eFunctionCall)
        continue;

      if (!usesReturnValue && m_builder.getUseCount(use))
        usesReturnValue = true;
    }

    /* Remove return values if the function result is never used */
    if (usesReturnValue)
      newFunctionOp.setType(oldFunctionOp.getType());
    else
      removeReturnValue(function);

    /* Skip expensive stuff below if the function doesn't change */
    if (newFunctionOp.isEquivalent(oldFunctionOp))
      continue;

    /* Remove unused parameters from function calls and adjust type */
    for (auto use : uses) {
      const auto& oldCall = m_builder.getOp(use);

      if (oldCall.getOpCode() != OpCode::eFunctionCall)
        continue;

      Op newCall(OpCode::eFunctionCall, Type());

      if (usesReturnValue)
        newCall.setType(oldFunctionOp.getType());

      dxbc_spv_assert(function == SsaDef(oldCall.getOperand(0u)));
      newCall.addOperand(function);

      for (uint32_t i = 0u; i < oldFunctionOp.getFirstLiteralOperandIndex(); i++) {
        auto paramDef = SsaDef(oldFunctionOp.getOperand(i));
        auto paramValue = SsaDef(oldCall.getOperand(i + 1u));

        if (usedParams.find({ function, paramDef }) != usedParams.end())
          newCall.addOperand(paramValue);
      }

      m_builder.rewriteOp(use, std::move(newCall));
    }

    m_builder.rewriteOp(function, std::move(newFunctionOp));
  }
}


void FunctionCleanupPass::runRemoveParameterPass(Builder& builder) {
  FunctionCleanupPass(builder).removeUnusedParameters();
}


void FunctionCleanupPass::gatherSharedTempUses() {
  auto [a, b] = m_builder.getCode();

  SsaDef function = { };
  bool isEntryPoint = false;

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eFunction: {
        function = iter->getDef();
        isEntryPoint = isEntryPointFunction(function);

        m_callDepth.insert({ function, 0u });
      } break;

      case OpCode::eFunctionEnd: {
        function = SsaDef();
        isEntryPoint = false;
      } break;

      case OpCode::eTmpLoad:
      case OpCode::eTmpStore: {
        const auto& temp = m_builder.getOpForOperand(*iter, 0u);
        auto e = m_sharedTemps.find(temp.getDef());

        if (e == m_sharedTemps.end())
          m_sharedTemps.insert({ temp.getDef(), function });
        else if (e->second && e->second != function)
          e->second = SsaDef();

        if (!isEntryPoint)
          insertUnique(m_functionTemps, function, temp.getDef());
      } break;

      case OpCode::eFunctionCall: {
        const auto& callee = m_builder.getOpForOperand(*iter, 0u);
        insertUnique(m_functionCalls, function, callee.getDef());
      } break;

      default:
        break;
    }
  }
}


void FunctionCleanupPass::propagateSharedTempUses() {
  removeLocalTempsFromLookupTables();

  while (propagateSharedTempUsesRound())
    continue;
}


bool FunctionCleanupPass::propagateSharedTempUsesRound() {
  bool progress = false;

  for (auto e : m_functionCalls) {
    auto [caller, callee] = e;
    auto [a, b] = m_functionTemps.equal_range(callee);

    for (auto iter = a; iter != b; iter++)
      progress |= insertUnique(m_functionTemps, caller, iter->second);
  }

  return progress;
}


void FunctionCleanupPass::removeLocalTempsFromLookupTables() {
  /* Remove temporaries that are only used in one function */
  for (auto iter = m_sharedTemps.begin(); iter != m_sharedTemps.end(); ) {
    if (iter->second)
      iter = m_sharedTemps.erase(iter);
    else
      ++iter;
  }

  /* Remove function to shared temp entries for non-shared temporaries */
  for (auto iter = m_functionTemps.begin(); iter != m_functionTemps.end(); ) {
    if (m_sharedTemps.find(iter->second) == m_sharedTemps.end())
      iter = m_functionTemps.erase(iter);
    else
      ++iter;
  }
}


void FunctionCleanupPass::resolveSharedTempsForFunction(SsaDef fn) {
  auto [a, b] = m_functionTemps.equal_range(fn);

  if (a == b)
    return;

  /* Cache previous return value of the struct, which will be
   * relevant when rewriting function calls */
  auto oldReturnType = m_builder.getOp(fn).getType();
  dxbc_spv_assert(!oldReturnType.isArrayType());

  /* Build new return type by adding temporaries to the struct,
   * and add function parameters for each temporary used. */
  util::small_vector<TmpParamInfo, 64u> sharedTemps;

  for (auto iter = a; iter != b; iter++) {
    auto tempType = m_builder.getOp(iter->second).getType();
    dxbc_spv_assert(tempType.isBasicType());

    auto& e = sharedTemps.emplace_back();
    e.sharedTemp = iter->second;
    e.localTemp = m_builder.add(Op::DclTmp(tempType, m_builder.getOpForOperand(iter->second, 0u).getDef()));
  }

  /* Rewrite all uses of shared temporaries within the function */
  rewriteSharedTempUses(fn, sharedTemps.begin(), sharedTemps.end());
  resolveLocalTempTypes(sharedTemps.begin(), sharedTemps.end());

  /* Rewrite calls to the function to pass in shared temporaries as
   * parameters, and write the result back to the shared temps. */
  adjustFunctionCallsForSharedTemps(fn, sharedTemps.begin(), sharedTemps.end());

  /* Rewrite the function itself to adjust the declaration and to
   * load and return shared temps via parameters and return values. */
  adjustFunctionForSharedTemps(fn, sharedTemps.begin(), sharedTemps.end());
}


bool FunctionCleanupPass::isEntryPointFunction(SsaDef function) const {
  dxbc_spv_assert(m_builder.getOp(function).getOpCode() == OpCode::eFunction);

  auto [a, b] = m_builder.getUses(function);

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eEntryPoint)
      return true;
  }

  return false;
}


void FunctionCleanupPass::rewriteSharedTempUses(SsaDef function, FunctionCleanupPass::TmpParamInfo* a, FunctionCleanupPass::TmpParamInfo* b) {
  auto iter = findFunctionStart(function);

  while (iter->getOpCode() != OpCode::eFunctionEnd) {
    if (iter->getOpCode() == OpCode::eTmpLoad || iter->getOpCode() == OpCode::eTmpStore) {
      TmpParamInfo* param = nullptr;

      for (auto i = a; i != b && !param; i++) {
        if (m_builder.getOpForOperand(*iter, 0u).getDef() == i->sharedTemp)
          param = i;
      }

      if (param) {
        auto op = *iter;
        op.setOperand(0u, param->localTemp);
        m_builder.rewriteOp(iter->getDef(), std::move(op));

        param->type = determineLocalTempType(param->type, *iter);
      }
    }

    ++iter;
  }
}


void FunctionCleanupPass::resolveLocalTempTypes(TmpParamInfo* a, TmpParamInfo* b) {
  for (auto i = a; i != b; i++) {
    if (i->type.isVoidType())
      i->type = m_builder.getOp(i->sharedTemp).getType().getBaseType(0u);

    if (i->type.isUnknownType())
      i->type = BasicType(ScalarType::eU32, i->type.getVectorSize());
  }
}


void FunctionCleanupPass::adjustFunctionCallsForSharedTemps(SsaDef function, TmpParamInfo* a, TmpParamInfo* b) {
  /* The function itself still uses the old declaration etc */
  auto oldReturnType = m_builder.getOp(function).getType();
  auto newReturnType = oldReturnType;

  for (auto i = a; i != b; i++)
    newReturnType.addStructMember(i->type);

  util::small_vector<SsaDef, 256u> uses = { };
  m_builder.getUses(function, uses);

  for (auto use : uses) {
    auto callOp = m_builder.getOp(use);

    if (callOp.getOpCode() != OpCode::eFunctionCall)
      continue;

    /* Load temporaries and pass them in as parameters */
    for (auto i = a; i != b; i++) {
      auto value = m_builder.addBefore(use, Op::TmpLoad(m_builder.getOp(i->sharedTemp).getType(), i->sharedTemp));
      callOp.addOperand(m_builder.addBefore(use, Op::ConsumeAs(i->type, value)));
    }

    callOp.setType(newReturnType);
    auto result = m_builder.addBefore(use, std::move(callOp));

    /* Write back shared temporaries */
    uint32_t returnIndex = oldReturnType.getStructMemberCount();

    for (auto i = a; i != b; i++) {
      auto value = result;

      if (newReturnType.isStructType()) {
        value = m_builder.addBefore(use, Op::CompositeExtract(
          i->type, value, m_builder.makeConstant(returnIndex++)));
      }

      value = m_builder.addBefore(use, Op::ConsumeAs(m_builder.getOp(i->sharedTemp).getType(), value));
      m_builder.addBefore(use, Op::TmpStore(i->sharedTemp, value));
    }

    /* For the return struct, there are different scenarios:
     * - The function returned void previously, in which case we can simply ignore
     *   the previous return value, but the return type may not be a struct.
     * - The function returned a basic value before, in which case we need to extract
     *   it from the returned struct.
     * - The function returned a struct before, in which case we need to re-assemble
     *   it by extracting the individual constituents. */
    if (oldReturnType.isVoidType()) {
      m_builder.remove(use);
    } else if (oldReturnType.isBasicType()) {
      m_builder.rewriteOp(use, Op::CompositeExtract(
        oldReturnType.getBaseType(0u), result, m_builder.makeConstant(0u)));
    } else {
      dxbc_spv_assert(oldReturnType.isStructType());

      Op compositeOp(OpCode::eCompositeConstruct, oldReturnType);

      for (uint32_t i = 0u; i < oldReturnType.getStructMemberCount(); i++) {
        compositeOp.addOperand(m_builder.addBefore(use, Op::CompositeExtract(
          oldReturnType.getBaseType(i), result, m_builder.makeConstant(i))));
      }

      m_builder.rewriteOp(use, std::move(compositeOp));
    }
  }
}


void FunctionCleanupPass::adjustFunctionForSharedTemps(SsaDef function, TmpParamInfo* a, TmpParamInfo* b) {
  auto oldReturnType = m_builder.getOp(function).getType();
  auto newReturnType = oldReturnType;

  for (auto i = a; i != b; i++)
    newReturnType.addStructMember(i->type);

  /* Rewrite function op to use the new types */
  auto functionOp = m_builder.getOp(function);
  functionOp.setType(newReturnType);

  /* Copy parameter values to local temps */
  auto iter = findFunctionStart(function);

  for (auto i = a; i != b; i++) {
    auto param = m_builder.add(Op::DclParam(i->type));
    functionOp.addParam(param);

    auto value = m_builder.addBefore(iter->getDef(),
      Op::ParamLoad(i->type, function, param));

    value = m_builder.addBefore(iter->getDef(),
      Op::ConsumeAs(m_builder.getOp(i->localTemp).getType(), value));

    m_builder.addBefore(iter->getDef(), Op::TmpStore(i->localTemp, value));
  }

  while (iter->getOpCode() != OpCode::eFunctionEnd) {
    if (iter->getOpCode() == OpCode::eReturn) {
      Op compositeOp(OpCode::eCompositeConstruct, newReturnType);

      if (!oldReturnType.isVoidType()) {
        /* Add previous return value to the return struct */
        const auto& oldValue = m_builder.getOpForOperand(*iter, 0u);

        if (oldReturnType.isStructType()) {
          for (uint32_t i = 0u; i < oldReturnType.getStructMemberCount(); i++) {
            compositeOp.addOperand(m_builder.addBefore(iter->getDef(), Op::CompositeExtract(
              oldReturnType.getBaseType(i), oldValue.getDef(), m_builder.makeConstant(i))));
          }
        } else {
          compositeOp.addOperand(oldValue.getDef());
        }
      }

      /* Add local temps to return struct */
      for (auto i = a; i != b; i++) {
        auto value = m_builder.addBefore(iter->getDef(), Op::TmpLoad(m_builder.getOp(i->localTemp).getType(), i->localTemp));
        compositeOp.addOperand(m_builder.addBefore(iter->getDef(), Op::ConsumeAs(i->type, value)));
      }

      /* Rewrite return op itself */
      auto value = SsaDef(compositeOp.getOperand(0u));

      if (compositeOp.getOperandCount() > 1u)
        value = m_builder.addBefore(iter->getDef(), std::move(compositeOp));

      m_builder.rewriteOp(iter->getDef(), Op::Return(newReturnType, value));
    }

    ++iter;
  }

  /* Rewrite function op to use the new types */
  m_builder.rewriteOp(function, std::move(functionOp));
}


void FunctionCleanupPass::determineFunctionCallDepth() {
  bool progress;

  do {
    progress = false;

    for (const auto& call : m_functionCalls) {
      auto [caller, callee] = call;

      auto& calleeDepth = m_callDepth.at(callee);
      auto& callerDepth = m_callDepth.at(caller);

      if (calleeDepth <= callerDepth) {
        calleeDepth = callerDepth + 1u;
        progress = true;
      }
    }
  } while (progress);
}


void FunctionCleanupPass::removeReturnValue(SsaDef function) {
  auto iter = findFunctionStart(function);

  while (iter->getOpCode() != OpCode::eFunctionEnd) {
    if (iter->getOpCode() == OpCode::eReturn)
      m_builder.rewriteOp(iter->getDef(), Op::Return());

    ++iter;
  }
}


Builder::iterator FunctionCleanupPass::findFunctionStart(SsaDef function) {
  auto iter = m_builder.iter(function);
  ++iter;

  if (iter->getOpCode() == OpCode::eLabel)
    ++iter;

  return iter;
}


BasicType FunctionCleanupPass::determineLocalTempType(BasicType type, const Op& op) const {
  /* This runs before regular type propagation, so try to determine
   * the local type for this temp based on consume uses */
  BasicType result(ScalarType::eUnknown, type.getVectorSize());

  if (op.getOpCode() == OpCode::eTmpLoad) {
    auto [a, b] = m_builder.getUses(op.getDef());

    for (auto iter = a; iter != b; iter++) {
      if (iter->getOpCode() == OpCode::eConsumeAs)
        type = PropagateTypesPass::resolveTypeForUnknownOp(type, iter->getType().getBaseType(0u));
    }

    return type;
  } else if (op.getOpCode() == OpCode::eTmpStore) {
    const auto& arg = m_builder.getOpForOperand(op, 1u);
    auto argType = arg.getType().getBaseType(0u);

    if (arg.getOpCode() == OpCode::eConsumeAs)
      argType = m_builder.getOpForOperand(arg, 0u).getType().getBaseType(0u);

    return PropagateTypesPass::resolveTypeForUnknownOp(type, argType);
  }

  return type;
}


bool FunctionCleanupPass::insertUnique(std::multimap<SsaDef, SsaDef>& map, SsaDef fn, SsaDef value) {
  auto [a, b] = map.equal_range(fn);

  for (auto i = a; i != b; i++) {
    if (i->second == value)
      return false;
  }

  map.insert({ fn, value });
  return true;
}

}
