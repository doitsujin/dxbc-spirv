#include "ir_pass_lower_io.h"
#include "ir_pass_remove_unused.h"

#include "../ir_utils.h"

#include "../../util/util_bit.h"
#include "../../util/util_log.h"

namespace dxbc_spv::ir {

IoMap::IoMap() {

}


IoMap::~IoMap() {

}


void IoMap::add(IoLocation entry) {
  /* Maintain order based on type, location etc */
  auto iter = m_entries.begin();

  while (iter != m_entries.end() && iter->isOrderedBefore(entry))
    iter++;

  m_entries.insert(iter, entry);
}


IoMap IoMap::forInputs(const Builder& builder) {
  IoMap result = { };
  ShaderStage stage = getStageForBuilder(builder);

  auto [a, b] = builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclInput)
      result.add(getEntryForLocation(stage, *iter));
    else if (iter->getOpCode() == OpCode::eDclInputBuiltIn)
      result.add(getEntryForBuiltIn(*iter));
  }

  return result;
}


IoMap IoMap::forOutputs(const Builder& builder, uint32_t stream) {
  IoMap result = { };
  ShaderStage stage = getStageForBuilder(builder);

  auto [a, b] = builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclOutput) {
      if (stage != ShaderStage::eGeometry || stream == uint32_t(iter->getOperand(3u)))
        result.add(getEntryForLocation(stage, *iter));
    } else if (iter->getOpCode() == OpCode::eDclOutputBuiltIn) {
      if (stage != ShaderStage::eGeometry || stream == uint32_t(iter->getOperand(2u)))
        result.add(getEntryForBuiltIn(*iter));
    }
  }

  return result;
}


bool IoMap::checkCompatibility(ShaderStage prevStage, const IoMap& prevStageOut, ShaderStage stage, const IoMap& stageIn) {
  auto w = prevStageOut.m_entries.begin();

  for (const auto& r : stageIn.m_entries) {
    if (r.getType() == IoEntryType::eBuiltIn) {
      if (builtInIsGenerated(r.getBuiltIn(), prevStage, stage))
        continue;
    }

    if (w == prevStageOut.m_entries.end())
      return false;

    /* Find matching item in the output I/O map */
    while (w->isOrderedBefore(r)) {
      if (++w == prevStageOut.m_entries.end())
        return false;
    }

    /* None found if this check succeeds */
    if (r.isOrderedBefore(*w))
      return false;

    /* For built-ins, we want an exact match */
    if (r.getType() == IoEntryType::eBuiltIn && r.getComponentMask() != w->getComponentMask())
      return false;

    /* Check whether there are any components read but not written */
    if (!w->covers(r))
      return false;
  }

  return true;
}


bool IoMap::builtInIsGenerated(BuiltIn builtIn, ShaderStage prevStage, ShaderStage stage) {
  switch (builtIn) {
    /* Special semantic in PS */
    case BuiltIn::ePosition:
      return stage == ShaderStage::ePixel;

    /* Generated value for the first stage after the
     * vertex shader, as well as the domain shader */
    case BuiltIn::ePrimitiveId:
      return prevStage == ShaderStage::eVertex ||
             prevStage == ShaderStage::eHull;

    /* Must always be written by the previous stage */
    case BuiltIn::eClipDistance:
    case BuiltIn::eCullDistance:
    case BuiltIn::eTessFactorInner:
    case BuiltIn::eTessFactorOuter:
      return false;

    default:
      /* Everything else is system-generated */
      return true;
  }
}


IoLocation IoMap::getEntryForOp(ShaderStage stage, const Op& op) {
  bool isBuiltIn = op.getOpCode() == OpCode::eDclInputBuiltIn ||
                   op.getOpCode() == OpCode::eDclOutputBuiltIn;

  if (isBuiltIn)
    return getEntryForBuiltIn(op);
  else
    return getEntryForLocation(stage, op);
}


IoLocation IoMap::getEntryForBuiltIn(const Op& op) {
  auto builtIn = BuiltIn(op.getOperand(1u));

  if (builtIn == BuiltIn::eClipDistance ||
      builtIn == BuiltIn::eCullDistance ||
      builtIn == BuiltIn::eTessFactorInner ||
      builtIn == BuiltIn::eTessFactorOuter) {
    /* Array, length can vary for clip/cull */
    auto arraySize = op.getType().getArraySize(0u);
    return IoLocation(builtIn, (1u << arraySize) - 1u);
  } else {
    /* Otherwise, use the vector size as usual */
    auto vectorSize = op.getType().getBaseType(0u).getVectorSize();
    return IoLocation(builtIn, (1u << vectorSize) - 1u);
  }
}


IoLocation IoMap::getEntryForLocation(ShaderStage stage, const Op& op) {
  /* Parse declaration operands */
  auto locationIndex = uint32_t(op.getOperand(1u));
  auto componentIndex = uint32_t(op.getOperand(2u));

  uint32_t vectorSize = op.getType().getBaseType(0u).getVectorSize();
  uint32_t componentMask = (1u << vectorSize) - 1u;

  /* Non-arrayed hull shader outputs and domain shader inputs are patch constants */
  IoEntryType type = IoEntryType::ePerVertex;

  if (stage == ShaderStage::eHull || stage == ShaderStage::eDomain) {
    bool isInput = op.getOpCode() == OpCode::eDclInput ||
                   op.getOpCode() == OpCode::eDclInputBuiltIn;

    if (isInput == (stage == ShaderStage::eDomain) && !op.getType().isArrayType())
      type = IoEntryType::ePerPatch;
  }

  return IoLocation(type, locationIndex, componentMask <<componentIndex);
}


ShaderStage IoMap::getStageForBuilder(const Builder& builder) {
  auto [a, b] = builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eEntryPoint)
      return ShaderStage(iter->getOperand(iter->getFirstLiteralOperandIndex()));
  }

  dxbc_spv_unreachable();
  return ShaderStage::eFlagEnum;
}




LowerIoPass::LowerIoPass(Builder& builder)
: m_builder(builder) {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eEntryPoint) {
      m_entryPoint = iter->getDef();
      m_stage = ShaderStage(iter->getOperand(iter->getFirstLiteralOperandIndex()));
      break;
    }
  }
}


LowerIoPass::~LowerIoPass() {

}


std::optional<IoLocation> LowerIoPass::getSemanticInfo(const char* name, uint32_t index, IoSemanticType type, uint32_t stream) const {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eSemantic) {
      const auto& ioDeclaration = m_builder.getOpForOperand(*iter, 0u);

      bool match = false;

      if (ioDeclaration.getOpCode() == OpCode::eDclInput ||
          ioDeclaration.getOpCode() == OpCode::eDclInputBuiltIn)
        match = type == IoSemanticType::eInput;

      if (ioDeclaration.getOpCode() == OpCode::eDclOutput ||
          ioDeclaration.getOpCode() == OpCode::eDclOutputBuiltIn)
        match = type == IoSemanticType::eOutput;

      if (match)
        match = index == uint32_t(iter->getOperand(1u));

      if (match) {
        auto literal = iter->getLiteralString(2u);

        for (size_t i = 0u; i <= literal.size() && match; i++)
          match = util::compareCharsCaseInsensitive(i < literal.size() ? literal[i] : '\0', name[i]);
      }

      if (match && type == IoSemanticType::eOutput) {
        uint32_t operandIndex = ioDeclaration.getOpCode() == OpCode::eDclOutputBuiltIn
          ? ioDeclaration.getFirstLiteralOperandIndex() + 1u
          : ioDeclaration.getFirstLiteralOperandIndex() + 2u;

        uint32_t ioStream = operandIndex < ioDeclaration.getOperandCount()
          ? uint32_t(ioDeclaration.getOperand(operandIndex))
          : 0u;

        match = stream == ioStream;
      }

      if (match)
        return IoMap::getEntryForOp(m_stage, ioDeclaration);
    }
  }

  return std::nullopt;
}


bool LowerIoPass::changeGsInputPrimitiveType(PrimitiveType primitiveType) {
  auto [a, b] = m_builder.getDeclarations();

  /* Scan primitive type declaration */
  bool foundPrimType = false;

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eSetGsInputPrimitive) {
      if (PrimitiveType(iter->getOperand(iter->getFirstLiteralOperandIndex())) == primitiveType)
        return true;

      m_builder.rewriteOp(iter->getDef(),
        Op::SetGsInputPrimitive(SsaDef(iter->getOperand(0u)), primitiveType));

      foundPrimType = true;
      break;
    }
  }

  if (!foundPrimType)
    return false;

  /* Change input declarations */
  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclInput ||
        iter->getOpCode() == OpCode::eDclInputBuiltIn) {
      if (!iter->getType().isArrayType())
        continue;

      Type type = iter->getType().getBaseType(0u);

      for (uint32_t i = 0u; i + 1u < iter->getType().getArrayDimensions(); i++)
        type.addArrayDimension(iter->getType().getArraySize(i));

      type.addArrayDimension(primitiveVertexCount(primitiveType));
      m_builder.setOpType(iter->getDef(), type);
    }
  }

  return true;
}


bool LowerIoPass::resolveUnusedOutputs(const IoMap& consumedOutputs) {
  auto iter = m_builder.getDeclarations().first;
  bool progress = false;

  while (iter != m_builder.getDeclarations().second) {
    if (iter->getOpCode() == OpCode::eDclOutput) {
      auto location = IoMap::getEntryForOp(m_stage, *iter);
      bool isUsed = false;

      for (const auto& e : consumedOutputs) {
        if (e.overlaps(location)) {
          isUsed = true;
          break;
        }
      }

      if (!isUsed) {
        iter = removeOutput(iter);
        progress = true;
        continue;
      }
    }

    ++iter;
  }

  if (progress)
    RemoveUnusedPass::runPass(m_builder);

  return progress;
}


bool LowerIoPass::resolveXfbOutputs(size_t entryCount, const IoXfbInfo* entries, int32_t rasterizedStream) {
  /* Mask out output locations that we cannot use for xfb */
  XfbComponentMap componentIndices = { };

  if (rasterizedStream >= 0) {
    auto output = IoMap::forOutputs(m_builder, uint32_t(rasterizedStream));

    for (const auto& e : output) {
      if (e.getType() == IoEntryType::ePerVertex) {
        auto& map = componentIndices.at(e.getLocationIndex());
        map.buffer = 0xffu;
        map.componentIndex = 0x0u;
      }
    }
  }

  /* Declare actual streamout variables */
  auto iter = m_builder.getDeclarations().first;

  while (iter != m_builder.getDeclarations().second) {
    if (iter->getOpCode() == OpCode::eDclOutput || iter->getOpCode() == OpCode::eDclOutputBuiltIn) {
      auto stream = getStreamForIoVariable(*iter);

      if (!emitXfbForOutput(entryCount, entries, iter, componentIndices))
        return false;

      if (int32_t(stream) != rasterizedStream) {
        iter = removeOutput(iter);
        continue;
      }
    }

    ++iter;
  }

  removeUnusedStreams();
  return true;
}


bool LowerIoPass::resolvePatchConstantLocations(const IoMap& hullOutput) {
  /* Gather defined patch constants and regular outputs */
  uint32_t perVertexIoMask = 0u;
  uint32_t perPatchIoMask = 0u;

  for (auto e : hullOutput) {
    if (e.getType() == IoEntryType::ePerVertex)
      perVertexIoMask |= 1u << e.getLocationIndex();
    else if (e.getType() == IoEntryType::ePerPatch)
      perPatchIoMask |= 1u << e.getLocationIndex();
  }

  /* Rewrite patch constant inputs depending on the stage */
  auto [a, b] = m_builder.getDeclarations();
  ShaderStage stage = { };

  for (auto iter = a; iter != b; iter++) {
    bool status = true;

    switch (iter->getOpCode()) {
      case OpCode::eEntryPoint: {
        stage = ShaderStage(iter->getOperand(iter->getFirstLiteralOperandIndex()));
      } break;

      case OpCode::eDclInput: {
        if (stage == ShaderStage::eDomain)
          status = remapTessIoLocation(iter, perPatchIoMask, perVertexIoMask);
      } break;

      case OpCode::eDclOutput: {
        if (stage == ShaderStage::eHull)
          status = remapTessIoLocation(iter, perPatchIoMask, perVertexIoMask);
      } break;

      default:
        break;
    }

    if (!status)
      return false;
  }

  return true;
}


bool LowerIoPass::resolveMismatchedIo(ShaderStage prevStage, const IoMap& prevStageOut) {
  /* Scalarize all input loads first so that we have an
   * easier time rewriting mismatched loads later. */
  scalarizeInputLoads();

  /* Gather combined write masks of all output locations */
  OutputComponentMap perVertexOutputs = { };
  OutputComponentMap perPatchOutputs = { };

  for (const auto& e : prevStageOut) {
    if (e.getType() == IoEntryType::eBuiltIn)
      continue;

    auto& l = (e.getType() == IoEntryType::ePerVertex ? perVertexOutputs : perPatchOutputs)
      .at(e.getLocationIndex());

    l.componentCounts.at(e.getFirstComponentIndex()) = e.computeComponentCount();
  }

  /* Determine which input slots have compatibility issues */
  uint64_t mismatchedLocationMask = 0u;

  auto iter = m_builder.getDeclarations().first;

  while (iter != m_builder.getDeclarations().second) {
    if (iter->getOpCode() == OpCode::eDclInput) {
      auto info = IoMap::getEntryForOp(m_stage, *iter);

      /* Gather basic info about the input slot */
      auto& l = (info.getType() == IoEntryType::ePerVertex ? perVertexOutputs : perPatchOutputs)
        .at(info.getLocationIndex());

      if (iter->getType().getArrayDimensions())
        l.arraySize = iter->getType().getArraySize(0u);

      if (m_stage == ShaderStage::ePixel)
        l.interpolation = InterpolationModes(iter->getOperand(iter->getFirstLiteralOperandIndex() + 2u));

      auto scalarType = iter->getType().getBaseType(0u).getBaseType();

      if (l.scalarType == ScalarType::eVoid) {
        l.scalarType = scalarType;
      } else if (l.scalarType != scalarType) {
        l.scalarType = (l.interpolation & InterpolationMode::eFlat)
          ? ScalarType::eU32 : ScalarType::eF32;
      }

      /* Check whether this input has a compatible output */
      auto componentIndex = info.getFirstComponentIndex();
      auto componentCount = info.computeComponentCount();

      if (l.componentCounts.at(componentIndex) < componentCount) {
        uint32_t shift = info.getType() == IoEntryType::ePerPatch
          ? info.getLocationIndex() + IoLocationCount
          : info.getLocationIndex();
        mismatchedLocationMask |= 1ull << shift;
      }
    } else if (iter->getOpCode() == OpCode::eDclInputBuiltIn) {
      /* Fix up undefined or mismatched built-ins right away */
      iter = resolveMismatchedBuiltIn(prevStage, prevStageOut, iter);
      continue;
    }

    ++iter;
  }

  /* Fix up input locations with compatibility issues */
  while (mismatchedLocationMask) {
    auto mismatchedLocationIndex = util::tzcnt(mismatchedLocationMask);
    mismatchedLocationMask &= mismatchedLocationMask - 1u;

    auto index = mismatchedLocationIndex % IoLocationCount;

    auto [type, entry] = (mismatchedLocationIndex < IoLocationCount)
      ? std::make_pair(IoEntryType::ePerVertex, perVertexOutputs.at(index))
      : std::make_pair(IoEntryType::ePerPatch, perPatchOutputs.at(index));

    resolveMismatchedLocation(type, index, entry);
  }

  /* Nuke unused variables out of existence */
  RemoveUnusedPass::runPass(m_builder);
  return true;
}


bool LowerIoPass::demoteMultisampledSrv() {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclSrv) {
      auto kind = ResourceKind(iter->getOperand(iter->getFirstLiteralOperandIndex() + 3u));

      if (!resourceIsMultisampled(kind))
        continue;

      /* Rewrite image loads and sample queries */
      auto [aUse, bUse] = m_builder.getUses(iter->getDef());

      for (auto use = aUse; use != bUse; use++) {
        if (!rewriteMultisampledDescriptorUse(use->getDef()))
          return false;
      }

      /* Rewrite resource declaration to not be multisampled */
      kind = resourceIsLayered(kind)
        ? ResourceKind::eImage2DArray
        : ResourceKind::eImage2D;

      auto dclOp = *iter;
      dclOp.setOperand(iter->getFirstLiteralOperandIndex() + 3u, kind);
      m_builder.rewriteOp(iter->getDef(), std::move(dclOp));
    }
  }

  return true;
}


void LowerIoPass::enableFlatInterpolation(uint32_t locationMask) {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclInput) {
      auto op = *iter;

      auto location = uint32_t(op.getOperand(op.getFirstLiteralOperandIndex()));
      op.setOperand(op.getFirstLiteralOperandIndex() + 2u, InterpolationMode::eFlat);

      if (locationMask & (1u << location))
        m_builder.rewriteOp(iter->getDef(), std::move(op));
    }
  }
}


void LowerIoPass::enableSampleInterpolation() {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eDclInputBuiltIn: {
        auto builtIn = BuiltIn(iter->getOperand(iter->getFirstLiteralOperandIndex()));

        if (builtIn != BuiltIn::eClipDistance &&
            builtIn != BuiltIn::eCullDistance)
          break;
      } [[fallthrough]];

      case OpCode::eDclInput: {
        bool isBuiltIn = iter->getOpCode() == OpCode::eDclInputBuiltIn;

        auto op = *iter;
        auto operandIndex = op.getFirstLiteralOperandIndex() + (isBuiltIn ? 1u : 2u);
        auto interpolation = InterpolationModes(op.getOperand(operandIndex));

        if (!(interpolation & (InterpolationMode::eFlat | InterpolationMode::eCentroid))) {
          op.setOperand(operandIndex, interpolation | InterpolationMode::eSample);
          m_builder.rewriteOp(iter->getDef(), std::move(op));
        }
      } break;

      default:
        break;
    }
  }
}


bool LowerIoPass::swizzleOutputs(uint32_t outputCount, const IoOutputSwizzle* swizzles) {
  /* Ensure that the swizzles aren't all just identities */
  uint32_t nonIdentityMask = 0u;

  for (uint32_t i = 0u; i < outputCount; i++) {
    const auto& e = swizzles[i];

    if (e.x != IoOutputComponent::eX || e.y != IoOutputComponent::eY ||
        e.z != IoOutputComponent::eZ || e.w != IoOutputComponent::eW)
      nonIdentityMask |= 1u << i;
  }

  if (!nonIdentityMask)
    return true;

  /* Find entry point and replace it with a new function */
  auto [a, b] = m_builder.getDeclarations();
  auto entryPointFunction = SsaDef();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eEntryPoint) {
      dxbc_spv_assert(ShaderStage(iter->getOperand(iter->getFirstLiteralOperandIndex())) == ShaderStage::ePixel);

      entryPointFunction = SsaDef(iter->getOperand(0u));
      break;
    }
  }

  if (!entryPointFunction) {
    Logger::err("No entry point found.");
    return false;
  }

  auto wrappedFunction = m_builder.addAfter(entryPointFunction, Op::Function(ScalarType::eVoid));
  m_builder.add(Op::DebugName(wrappedFunction, "main_pre_swizzle"));

  /* Rewrite entry point function to call the wrapped 'main' function */
  m_builder.setCursor(entryPointFunction);
  m_builder.add(Op::Label());
  m_builder.add(Op::FunctionCall(ScalarType::eVoid, wrappedFunction));

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclOutput) {
      auto location = uint32_t(iter->getOperand(iter->getFirstLiteralOperandIndex()));

      if (nonIdentityMask & (1u << location)) {
        const auto& e = swizzles[location];
        auto type = iter->getType().getBaseType(0u);

        /* Load individual components and apply swizzle */
        std::array<IoOutputComponent, 4u> swizzle = { e.x, e.y, e.z, e.w };
        std::array<SsaDef, 4u> scalars = { };

        for (uint32_t i = 0u; i < type.getVectorSize(); i++) {
          auto& scalar = scalars.at(i);

          switch (swizzle.at(i)) {
            case IoOutputComponent::eZero:
            case IoOutputComponent::eOne: {
              uint32_t value = swizzle.at(i) == IoOutputComponent::eOne ? 1u : 0u;

              scalar = [&] {
                switch (type.getBaseType()) {
                  case ScalarType::eF16: return m_builder.makeConstant(float16_t(float(value)));
                  case ScalarType::eF32: return m_builder.makeConstant(float(value));
                  case ScalarType::eF64: return m_builder.makeConstant(double(value));
                  case ScalarType::eU8:  return m_builder.makeConstant(uint8_t(value));
                  case ScalarType::eU16: return m_builder.makeConstant(uint16_t(value));
                  case ScalarType::eU32: return m_builder.makeConstant(uint32_t(value));
                  case ScalarType::eU64: return m_builder.makeConstant(uint64_t(value));
                  case ScalarType::eI8:  return m_builder.makeConstant(int8_t(value));
                  case ScalarType::eI16: return m_builder.makeConstant(int16_t(value));
                  case ScalarType::eI32: return m_builder.makeConstant(int32_t(value));
                  case ScalarType::eI64: return m_builder.makeConstant(int64_t(value));
                  default: break;
                }

                dxbc_spv_unreachable();
                return SsaDef();
              } ();
            } break;

            case IoOutputComponent::eX:
            case IoOutputComponent::eY:
            case IoOutputComponent::eZ:
            case IoOutputComponent::eW: {
              uint32_t index = uint32_t(swizzle.at(i)) - uint32_t(IoOutputComponent::eX);

              if (index < type.getVectorSize()) {
                SsaDef address = { };

                if (type.isVector())
                  address = m_builder.makeConstant(index);

                scalar = m_builder.add(Op::OutputLoad(type.getBaseType(), iter->getDef(), address));
              } else {
                /* Out-of-bounds component, use zero */
                scalar = m_builder.makeConstantZero(type.getBaseType());
              }
            } break;
          }

          dxbc_spv_assert(scalar);
        }

        /* Put result vector back together */
        for (uint32_t i = 0u; i < type.getVectorSize(); i++) {
          SsaDef address = { };

          if (type.isVector())
            address = m_builder.makeConstant(i);

          m_builder.add(Op::OutputStore(iter->getDef(), address, scalars.at(i)));
        }
      }
    }
  }

  m_builder.add(Op::Return());
  auto entryPointFunctionEnd = m_builder.add(Op::FunctionEnd());

  /* Move function to the end of the code block so that the wrapped
   * function is defined before the new entry point function. */
  m_builder.reorderBefore(SsaDef(), entryPointFunction, entryPointFunctionEnd);
  return true;
}


void LowerIoPass::lowerSampleCountToSpecConstant(uint32_t specId) {
  auto iter = m_builder.getDeclarations().first;

  SsaDef constant = { };

  while (iter != m_builder.getDeclarations().second) {
    if (iter->getOpCode() == OpCode::eDclInputBuiltIn) {
      auto builtIn = BuiltIn(iter->getOperand(iter->getFirstLiteralOperandIndex()));

      if (builtIn == BuiltIn::eSampleCount) {
        if (!constant) {
          constant = m_builder.add(Op::DclSpecConstant(ScalarType::eU32,
            SsaDef(iter->getOperand(0u)), specId, 4u));

          m_builder.add(Op::DebugName(constant, "SampleCount"));
        }

        util::small_vector<SsaDef, 256u> uses;
        m_builder.getUses(iter->getDef(), uses);

        for (auto use : uses) {
          if (m_builder.getOp(use).getOpCode() == OpCode::eInputLoad)
            m_builder.rewriteDef(use, constant);
          else
            m_builder.remove(use);
        }

        iter = m_builder.iter(m_builder.remove(iter->getDef()));
        continue;
      }
    }

    ++iter;
  }
}


void LowerIoPass::scalarizeInputLoads() {
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eDclInput ||
        iter->getOpCode() == OpCode::eDclInputBuiltIn)
      scalarizeInputLoadsForInput(iter);
  }
}


void LowerIoPass::scalarizeInputLoadsForInput(Builder::iterator op) {
  util::small_vector<SsaDef, 256u> uses;
  m_builder.getUses(op->getDef(), uses);

  for (auto use : uses) {
    const auto& useOp = m_builder.getOp(use);

    if (useOp.getOpCode() == OpCode::eInputLoad) {
      const auto& inputOp = m_builder.getOpForOperand(useOp, 0u);
      const auto& addressOp = m_builder.getOpForOperand(useOp, 1u);

      auto vectorType = useOp.getType().getBaseType(0u);
      auto scalarType = vectorType.getBaseType();

      if (vectorType.isVector()) {
        Op compositeOp(OpCode::eCompositeConstruct, vectorType);

        for (uint32_t i = 0u; i < vectorType.getVectorSize(); i++) {
          auto addressDef = m_builder.makeConstant(i);

          if (addressOp) {
            auto addressType = addressOp.getType().getBaseType(0u);

            Op addressComposite(OpCode::eCompositeConstruct,
              BasicType(addressType.getBaseType(), addressType.getVectorSize() + 1u));

            if (addressType.isVector()) {
              for (uint32_t j = 0u; j < addressType.getVectorSize(); j++) {
                addressComposite.addOperand(m_builder.addBefore(use,
                  Op::CompositeExtract(addressType.getBaseType(), addressOp.getDef(), m_builder.makeConstant(j))));
              }
            } else {
              addressComposite.addOperand(addressOp.getDef());
            }

            addressComposite.addOperand(addressDef);
            addressDef = m_builder.addBefore(use, std::move(addressComposite));
          }

          auto scalar = m_builder.addBefore(use,
            Op::InputLoad(scalarType, inputOp.getDef(), addressDef));

          compositeOp.addOperand(scalar);
        }

        m_builder.rewriteOp(use, std::move(compositeOp));
      }
    }
  }
}


Builder::iterator LowerIoPass::resolveMismatchedBuiltIn(ShaderStage prevStage, const IoMap& prevStageOut, Builder::iterator op) {
  auto inputInfo = IoMap::getEntryForOp(m_stage, *op);

  /* Ignore built-ins that are always well-defined */
  if (IoMap::builtInIsGenerated(inputInfo.getBuiltIn(), prevStage, m_stage))
    return ++op;

  /* Scan output mask for matching output */
  uint8_t outputMask = 0u;

  for (const auto& e : prevStageOut) {
    if (inputInfo.overlaps(e))
      outputMask = e.getComponentMask();
  }

  /* If the array size differs, fix up the the declaration. */
  if (outputMask && outputMask != inputInfo.getComponentMask()) {
    dxbc_spv_assert(op->getType().isArrayType());

    Type inputType(op->getType().getBaseType(0u));
    inputType.addArrayDimension(util::popcnt(outputMask));

    for (uint32_t i = 1u; i < op->getType().getArrayDimensions(); i++)
      inputType.addArrayDimensions(op->getType().getArraySize(i));

    m_builder.setOpType(op->getDef(), inputType);
  }

  /* If there are no unwritten components, we're done */
  uint32_t undefinedMask = inputInfo.getComponentMask() & ~outputMask;

  if (!undefinedMask)
    return ++op;

  rewriteBuiltInInputToZero(op, util::tzcnt(undefinedMask));

  if (!outputMask) {
    /* Remove all remaining uses of the input, which by now
     * must all be debug or semantic names. */
    util::small_vector<SsaDef, 16u> uses;
    m_builder.getUses(op->getDef(), uses);

    for (auto use : uses) {
      dxbc_spv_assert(m_builder.getOp(use).isDeclarative());
      m_builder.remove(use);
    }

    return m_builder.iter(m_builder.removeOp(*op));
  }

  return ++op;
}


void LowerIoPass::resolveMismatchedLocation(IoEntryType type, uint32_t location, const OutputInfo& outputs) {
  /* Rewrite input variables so that match the outputs from the previous
   * stage exactly. The second component of the pair is the constant vector
   * component index, if necessary. */
  std::array<std::pair<SsaDef, SsaDef>, 4u> newInputs = { };

  for (uint32_t i = 0u; i < outputs.componentCounts.size(); i++) {
    auto vectorSize = outputs.componentCounts.at(i);

    if (!vectorSize)
      continue;

    /* Determine actual output type */
    auto type = Type(outputs.scalarType, vectorSize);

    if (outputs.arraySize)
      type.addArrayDimension(outputs.arraySize);

    auto input = m_builder.add((m_stage == ShaderStage::ePixel)
      ? Op::DclInput(type, m_entryPoint, location, i, outputs.interpolation)
      : Op::DclInput(type, m_entryPoint, location, i));

    for (uint32_t j = 0u; j < vectorSize; j++) {
      newInputs.at(i + j) = std::make_pair(input,
        vectorSize > 1u ? m_builder.makeConstant(j) : SsaDef());
    }
  }

  /* Rewrite loads of overlapping input variables. */
  auto iter = m_builder.getDeclarations().first;

  while (iter != m_builder.getDeclarations().second) {
    if (iter->getOpCode() == OpCode::eDclInput) {
      bool isNew = false;

      for (auto e : newInputs)
        isNew = isNew || e.first == iter->getDef();

      if (!isNew) {
        auto inputInfo = IoMap::getEntryForOp(m_stage, *iter);

        if (inputInfo.getType() == type && inputInfo.getLocationIndex() == location) {
          auto componentIndex = inputInfo.getFirstComponentIndex();

          for (uint32_t i = 0u; i < inputInfo.computeComponentCount(); i++) {
            const auto& e = newInputs.at(componentIndex + i);
            resolveMismatchedIoVar(*iter, i, e.first, e.second);
          }
        }
      }
    }

    ++iter;
  }
}


void LowerIoPass::resolveMismatchedIoVar(const Op& oldVar, uint32_t oldComponent, SsaDef newVar, SsaDef newComponent) {
  util::small_vector<SsaDef, 256u> uses = { };
  m_builder.getUses(oldVar.getDef(), uses);

  /* Check whether we need to check the vector component index */
  bool checkComponent = oldVar.getType().getBaseType(0u).isVector();

  for (auto use : uses) {
    const auto& useOp = m_builder.getOp(use);

    if (useOp.getOpCode() == OpCode::eInputLoad ||
        useOp.getOpCode() == OpCode::eInterpolateAtCentroid ||
        useOp.getOpCode() == OpCode::eInterpolateAtOffset ||
        useOp.getOpCode() == OpCode::eInterpolateAtSample) {
      /* Check whether this load actually covers the problematic component */
      const auto& addressOp = m_builder.getOpForOperand(use, 1u);
      auto addressType = addressOp.getType().getBaseType(0u);

      if (addressOp && checkComponent) {
        bool matchesComponent = false;

        if (addressOp.isConstant()) {
          matchesComponent = uint32_t(addressOp.getOperand(addressType.getVectorSize() - 1u)) == oldComponent;
        } else if (addressOp.getOpCode() == OpCode::eCompositeConstruct) {
          const auto& componentOp = m_builder.getOpForOperand(addressOp, addressType.getVectorSize() - 1u);
          matchesComponent = uint32_t(componentOp.getOperand(0u)) == oldComponent;
        } else {
          dxbc_spv_unreachable();
        }

        if (!matchesComponent)
          continue;
      }

      if (newVar) {
        /* Rewrite address vector as necessary */
        auto oldAddressVectorSize = 0u;

        if (addressOp)
          oldAddressVectorSize = addressType.getVectorSize() - (checkComponent ? 1u : 0u);

        util::small_vector<SsaDef, 4u> newAddressScalars;

        for (uint32_t i = 0u; i < oldAddressVectorSize; i++) {
          newAddressScalars.push_back(addressType.isVector()
            ? m_builder.addBefore(use, Op::CompositeExtract(addressType.getBaseType(), addressOp.getDef(), m_builder.makeConstant(i)))
            : addressOp.getDef());
        }

        if (newComponent)
          newAddressScalars.push_back(newComponent);

        auto newAddressType = BasicType(addressType.getBaseType(), newAddressScalars.size());

        /* Build new address vector */
        SsaDef newAddress = { };

        if (!newAddressScalars.empty()) {
          newAddress = newAddressScalars.front();

          if (newAddressScalars.size() > 1u) {
            Op addressComposite(OpCode::eCompositeConstruct, newAddressType);

            for (auto s : newAddressScalars)
              addressComposite.addOperand(s);

            newAddress = m_builder.addBefore(use, std::move(addressComposite));
          }
        }

        /* Build new load or interpolation op */
        const auto& newVarOp = m_builder.getOp(newVar);

        auto newOp = Op(useOp.getOpCode(), newVarOp.getType().getBaseType(0u).getBaseType())
          .setFlags(useOp.getFlags())
          .addOperand(newVarOp.getDef())
          .addOperand(newAddress);

        for (uint32_t i = 2u; i < useOp.getOperandCount(); i++)
          newOp.addOperand(useOp.getOperand(i));

        /* Insert type cast if the variable types differ */
        if (newOp.getType() == useOp.getType()) {
          m_builder.rewriteOp(use, std::move(newOp));
        } else {
          auto load = m_builder.addBefore(use, std::move(newOp));
          m_builder.rewriteOp(use, Op::Cast(useOp.getType(), load));
        }
      } else {
        /* Component entirely undefined, rewrite load as a zero constant */
        m_builder.rewriteDef(use, m_builder.makeConstantZero(useOp.getType()));
      }
    }
  }
}


void LowerIoPass::rewriteBuiltInInputToZero(Builder::iterator op, uint32_t firstComponent) {
  dxbc_spv_assert(!firstComponent || builtInIsArray(IoMap::getEntryForOp(m_stage, *op).getBuiltIn()));

  util::small_vector<SsaDef, 256u> uses = { };
  m_builder.getUses(op->getDef(), uses);

  for (auto use : uses) {
    const auto& useOp = m_builder.getOp(use);

    if (useOp.getOpCode() == OpCode::eInputLoad ||
        useOp.getOpCode() == OpCode::eInterpolateAtCentroid ||
        useOp.getOpCode() == OpCode::eInterpolateAtOffset ||
        useOp.getOpCode() == OpCode::eInterpolateAtSample) {
      SsaDef inBoundsCond = { };

      if (firstComponent) {
        /* Bound-check innermost array index. Don't bother constant-folding
         * here since this should be exceptionally rare in practice. */
        auto addressOp = m_builder.getOpForOperand(use, 1u);
        auto addressDef = addressOp.getDef();
        auto addressType = addressOp.getType().getBaseType(0u);

        if (addressType.isVector()) {
          addressDef = m_builder.addBefore(use, Op::CompositeExtract(addressType.getBaseType(),
            addressDef, m_builder.makeConstant(uint32_t(addressType.getVectorSize() - 1u))));
        }

        inBoundsCond = m_builder.addBefore(use, Op::ULt(ScalarType::eBool,
          addressDef, m_builder.makeConstant(uint32_t(firstComponent))));
      }

      if (inBoundsCond) {
        /* Conditionally zero address to avoid out-of-bounds reads */
        auto addressOp = m_builder.getOpForOperand(use, 1u);
        auto addressDef = m_builder.addBefore(use, Op::Select(addressOp.getType(),
          inBoundsCond, addressOp.getDef(), m_builder.makeConstantZero(addressOp.getType())));

        auto loadOp = Op(useOp.getOpCode(), useOp.getType())
          .addOperand(SsaDef(useOp.getOperand(0u)))
          .addOperand(addressDef);

        for (uint32_t i = 2u; i < useOp.getOperandCount(); i++)
          loadOp.addOperand(useOp.getOperand(i));

        m_builder.rewriteOp(use, Op::Select(useOp.getType(), inBoundsCond,
          m_builder.addBefore(use, std::move(loadOp)),
          m_builder.makeConstantZero(useOp.getType())));
      } else {
        /* Rewrite entire load as constant zero */
        m_builder.rewriteDef(use, m_builder.makeConstantZero(useOp.getType()));
      }
    }
  }
}


bool LowerIoPass::emitXfbForOutput(size_t entryCount, const IoXfbInfo* entries, Builder::iterator output, XfbComponentMap& map) {
  uint32_t outputLocation = 0u;
  uint32_t outputComponent = 0u;

  auto entry = findXfbEntry(entryCount, entries, output);

  if (!entry)
    return true;

  auto ioVariable = getSemanticInfo(entry->semanticName.c_str(),
    entry->semanticIndex, IoSemanticType::eOutput, entry->stream);

  if (!ioVariable)
    return false;

  auto outputInfo = IoMap::getEntryForOp(ShaderStage::eGeometry, *output);
  auto outputType = output->getType();

  auto xfbOffset = entry->offset;
  auto componentMask = entry->componentMask << ioVariable->getFirstComponentIndex();
  componentMask &= ioVariable->getComponentMask();

  auto [a, b] = m_builder.getUses(output->getDef());

  while (componentMask) {
    /* Compute component index relative to the actual output */
    auto componentBit = componentMask & -componentMask;
    auto componentIndex = util::popcnt(outputInfo.getComponentMask() & (componentBit - 1u));

    /* Declare scalar output and xfb info for the exported component.
     * Deliberately insert the declarations before the original one
     * so we don't process it in subsequent iterations. */
    std::tie(outputLocation, outputComponent) = allocXfbOutput(map, entry->buffer);

    auto scalarOutput = m_builder.addBefore(output->getDef(),
      Op::DclOutput(outputType.getBaseType(0u).getBaseType(),
        SsaDef(output->getOperand(0u)), outputLocation, outputComponent, entry->stream));

    m_builder.addBefore(output->getDef(),
      Op::DclXfb(scalarOutput, entry->buffer, entry->stride, xfbOffset));

    xfbOffset += sizeof(uint32_t);

    /* Redirect matching stores to the new output */
    for (auto iter = a; iter != b; iter++) {
      if (iter->getOpCode() != OpCode::eOutputStore)
        continue;

      if (outputType.isScalarType()) {
        /* Simply duplicate the store op */
        m_builder.addBefore(iter->getDef(), Op::OutputStore(
          scalarOutput, SsaDef(), SsaDef(iter->getOperand(2u))));
      } else {
        const auto& addressOp = m_builder.getOpForOperand(*iter, 1u);
        const auto& valueOp = m_builder.getOpForOperand(*iter, 2u);

        if (addressOp && addressOp.isConstant() && uint32_t(addressOp.getOperand(0u)) != componentIndex)
          continue;

        auto valueDef = valueOp.getDef();

        if (!valueOp.getType().isScalarType()) {
          valueDef = m_builder.addBefore(iter->getDef(), Op::CompositeExtract(
            outputType.getBaseType(0u).getBaseType(), valueDef,
            m_builder.makeConstant(uint32_t(componentIndex))));
        }

        if (addressOp && !addressOp.isConstant()) {
          /* Make write conditional if the index is dynamic, which can
           * potentially happen for clip/cull distance outputs. */
          auto cond = m_builder.addBefore(iter->getDef(), Op::IEq(ScalarType::eBool,
            addressOp.getDef(), m_builder.makeConstant(uint32_t(componentIndex))));

          auto load = m_builder.addBefore(iter->getDef(), Op::OutputLoad(
            outputType.getBaseType(0u).getBaseType(), scalarOutput, SsaDef()));

          valueDef = m_builder.addBefore(iter->getDef(), Op::Select(
            outputType.getBaseType(0u).getBaseType(), cond, valueDef, load));
        }

        m_builder.addBefore(iter->getDef(), Op::OutputStore(
          scalarOutput, SsaDef(), valueDef));
      }
    }

    componentMask &= componentMask - 1u;
  }

  return true;
}


const IoXfbInfo* LowerIoPass::findXfbEntry(size_t entryCount, const IoXfbInfo* entries, Builder::iterator op) {
  auto info = IoMap::getEntryForOp(ShaderStage::eGeometry, *op);
  auto stream = getStreamForIoVariable(*op);

  for (size_t i = 0u; i < entryCount; i++) {
    const auto& e = entries[i];

    if (e.stream != stream)
      continue;

    auto ioVariable = getSemanticInfo(e.semanticName.c_str(),
      e.semanticIndex, IoSemanticType::eOutput, e.stream);

    if (!ioVariable)
      continue;

    if (ioVariable->getType() == info.getType()) {
      bool match = (info.getType() == IoEntryType::eBuiltIn)
        ? (info.getBuiltIn() == ioVariable->getBuiltIn())
        : (info.getLocationIndex() == ioVariable->getLocationIndex());

      match = match && (info.getComponentMask() & ioVariable->getComponentMask());

      if (match)
        return &e;
    }
  }

  return nullptr;
}


std::pair<uint32_t, uint32_t> LowerIoPass::allocXfbOutput(XfbComponentMap& map, uint32_t buffer) {
  for (uint32_t i = 0u; i < map.size(); i++) {
    if (map[i].componentIndex == 0xffu) {
      map[i].buffer = buffer;
      map[i].componentIndex = 0u;
    }

    if (map[i].buffer == buffer &&
        map[i].componentIndex < 4u)
      return std::make_pair(i, map[i].componentIndex++);
  }

  return std::make_pair(-1u, -1u);
}


Builder::iterator LowerIoPass::removeOutput(Builder::iterator op) {
  /* Determine whether the output is being read back, and replace it with
   * a scratch variable in that case. This should basically never happen
   * in practice. */
  bool hasRead = false;

  util::small_vector<SsaDef, 256u> uses;
  m_builder.getUses(op->getDef(), uses);

  for (auto use : uses) {
    if (m_builder.getOp(use).getOpCode() == OpCode::eOutputLoad) {
      hasRead = true;
      break;
    }
  }

  if (hasRead) {
    m_builder.rewriteOp(op->getDef(),
      Op::DclScratch(op->getType(), SsaDef(op->getOperand(0u))));

    for (auto use : uses) {
      const auto& useOp = m_builder.getOp(use);

      switch (useOp.getOpCode()) {
        case OpCode::eOutputLoad: {
          m_builder.rewriteOp(use, Op::ScratchLoad(useOp.getType(),
            op->getDef(), SsaDef(useOp.getOperand(1u))));
        } break;

        case OpCode::eOutputStore: {
          m_builder.rewriteOp(use, Op::ScratchStore(op->getDef(),
            SsaDef(useOp.getOperand(1u)), SsaDef(useOp.getOperand(2u))));
        } break;

        case OpCode::eSemantic: {
          m_builder.remove(use);
        } break;

        default:
          break;
      }
    }

    return ++op;
  } else {
    /* Otherwise, remove the output and all its uses entirely */
    for (auto use : uses) {
      dxbc_spv_assert(m_builder.getOp(use).isDeclarative() ||
                      m_builder.getOp(use).getOpCode() == OpCode::eOutputStore);

      m_builder.remove(use);
    }

    return m_builder.iter(m_builder.removeOp(*op));
  }
}


void LowerIoPass::removeUnusedStreams() {
  /* Eliminate unused streams */
  uint32_t desiredStreamMask = 0u;
  uint32_t expectedStreamMask = 0u;

  SsaDef primitiveDeclaration = { };

  auto iter = m_builder.getDeclarations().first;

  while (iter != m_builder.getDeclarations().second) {
    switch (iter->getOpCode()) {
      case OpCode::eSetGsOutputPrimitive: {
        expectedStreamMask = uint32_t(iter->getOperand(iter->getFirstLiteralOperandIndex() + 1u));
        primitiveDeclaration = iter->getDef();
      } break;

      case OpCode::eDclOutput: {
        desiredStreamMask |= 1u << uint32_t(iter->getOperand(iter->getFirstLiteralOperandIndex() + 2u));
      } break;

      case OpCode::eDclOutputBuiltIn: {
        desiredStreamMask |= 1u << uint32_t(iter->getOperand(iter->getFirstLiteralOperandIndex() + 1u));
      } break;

      default:
        break;
    }

    ++iter;
  }

  if (expectedStreamMask != desiredStreamMask && primitiveDeclaration) {
    /* Make sure to declare at least one stream even if the shader is empty */
    if (!desiredStreamMask)
      desiredStreamMask = 0x1;

    m_builder.rewriteOp(primitiveDeclaration, Op::SetGsOutputPrimitive(
      SsaDef(m_builder.getOp(primitiveDeclaration).getOperand(0u)),
      PrimitiveType(m_builder.getOp(primitiveDeclaration).getOperand(1u)),
      desiredStreamMask));

    iter = m_builder.getCode().first;

    while (iter != m_builder.end()) {
      if (iter->getOpCode() == OpCode::eEmitVertex ||
          iter->getOpCode() == OpCode::eEmitPrimitive) {
        uint32_t stream = uint32_t(iter->getOperand(iter->getFirstLiteralOperandIndex()));

        if (!(desiredStreamMask & (1u << stream))) {
          iter = m_builder.iter(m_builder.removeOp(*iter));
          continue;
        }
      }

      ++iter;
    }
  }
}


bool LowerIoPass::remapTessIoLocation(Builder::iterator op, uint32_t perPatchMask, uint32_t perVertexMask) {
  /* Control point outputs are arrayed */
  bool isPatchConstant = !op->getType().isArrayType();

  /* Compute patch constant index */
  auto locationMask = (1u << uint32_t(op->getOperand(1u))) - 1u;

  auto newLocation = isPatchConstant
    ? util::popcnt(perVertexMask) + util::popcnt(perPatchMask & locationMask)
    : util::popcnt(perVertexMask & locationMask);

  /* Find a location index not used for regular I/O */
  if (newLocation >= IoLocationCount) {
    Logger::err("No free patch constant location found.");
    return false;
  }

  /* Rewrite op to use the new location */
  m_builder.rewriteOp(op->getDef(), Op(*op).setOperand(1u, newLocation));
  return true;
}


bool LowerIoPass::rewriteMultisampledDescriptorUse(SsaDef descriptorDef) {
  small_vector<SsaDef, 256u> uses;
  m_builder.getUses(descriptorDef, uses);

  for (auto use : uses) {
    auto useOp = m_builder.getOp(use);

    switch (useOp.getOpCode()) {
      case OpCode::eImageLoad: {
        /* Set mip to 0, remove sample operand */
        useOp.setOperand(1u, m_builder.makeConstant(0u));
        useOp.setOperand(4u, SsaDef());

        m_builder.rewriteOp(use, std::move(useOp));
      } break;

      case OpCode::eImageQuerySize: {
        /* Add mip operand */
        useOp.setOperand(1u, m_builder.makeConstant(0u));
        m_builder.rewriteOp(use, std::move(useOp));
      } break;

      case OpCode::eImageQuerySamples: {
        dxbc_spv_assert(useOp.getType() == ScalarType::eU32);
        m_builder.rewriteDef(use, m_builder.makeConstant(1u));
      } break;

      default:
        break;
    }
  }

  return true;
}


uint32_t LowerIoPass::getStreamForIoVariable(const Op& op) {
  bool isBuiltIn = op.getOpCode() == OpCode::eDclOutputBuiltIn;
  auto operand = op.getFirstLiteralOperandIndex() + (isBuiltIn ? 1u : 2u);
  return uint32_t(op.getOperand(operand));
}


bool LowerIoPass::builtInIsArray(BuiltIn builtIn) {
  return builtIn == BuiltIn::eClipDistance ||
         builtIn == BuiltIn::eCullDistance ||
         builtIn == BuiltIn::eTessFactorInner ||
         builtIn == BuiltIn::eTessFactorOuter;
}


bool LowerIoPass::inputNeedsComponentIndex(const Op& op) {
  if (op.getType().isVectorType())
    return true;

  if (op.getOpCode() == OpCode::eDclInputBuiltIn)
    return builtInIsArray(BuiltIn(op.getOperand(op.getFirstLiteralOperandIndex())));

  /* Ignore the component index for scalar inputs */
  return false;
}

}
