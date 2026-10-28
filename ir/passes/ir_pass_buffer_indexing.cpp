#include <algorithm>

#include "ir_pass_buffer_indexing.h"
#include "ir_pass_scalarize.h"

#include "../ir_builder.h"
#include "../ir_utils.h"


#include "../../util/util_log.h"

namespace dxbc_spv::ir {

BufferIndexingPass::BufferIndexingPass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {

}


BufferIndexingPass::~BufferIndexingPass() {

}


bool BufferIndexingPass::run() {
  if (!m_options.allowDynamicIndexing)
    return false;

  auto buffers = getBuffers();
  bool feedback = false;

  for (auto buffer : buffers) {
    const auto& op = m_builder.getOp(buffer);

    if (op.getOpCode() == OpCode::eDescriptorLoad)
      feedback |= optimizeBuffer(op.getDef());
  }

  if (feedback)
    ScalarizePass::runResolveRedundantCompositesPass(m_builder);

  return feedback;
}


bool BufferIndexingPass::runPass(Builder& builder, const Options& options) {
  return BufferIndexingPass(builder, options).run();
}


bool BufferIndexingPass::optimizeBuffer(SsaDef descriptor) {
  auto loads = getIndexedLoads(descriptor);
  bool feedback = false;

  while (!loads.empty()) {
    auto next = loads.back();
    loads.pop_back();

    if (loads.empty())
      break;

    const auto& prev = loads.back();

    if (!next.isNextOf(prev))
      continue;

    auto stride = next.indexOffset - prev.indexOffset;
    feedback |= rewriteLoadAsIndexed(next, stride);
    feedback |= rewriteLoadAsIndexed(prev, stride);
    loads.pop_back();
  }

  return feedback;
}


bool BufferIndexingPass::rewriteLoadAsIndexed(const IndexedLoad& load, uint32_t stride) {
  if (load.indexOffset < load.compareValue * stride)
    return false;

  /* Rewrite index as base + stride * index */
  auto compareVar = load.compareVar;

  if (m_builder.getOp(compareVar).getType() != ScalarType::eU32)
    compareVar = m_builder.addBefore(load.selectOp, Op::ConvertItoI(ScalarType::eU32, compareVar));

  auto newIndex = m_builder.addBefore(load.selectOp, Op::IMul(ScalarType::eU32, compareVar, m_builder.makeConstant(stride)));
  newIndex = m_builder.addBefore(load.selectOp, Op::IAdd(ScalarType::eU32, newIndex, m_builder.makeConstant(load.indexOffset - load.compareValue * stride)));

  if (load.indexBase)
    newIndex = m_builder.addBefore(load.selectOp, Op::IAdd(ScalarType::eU32, load.indexBase, newIndex));

  /* Assemble load index vector as necessary */
  const auto& oldIndex = m_builder.getOpForOperand(load.loadOp, 1u);

  if (oldIndex.getType().isVectorType()) {
    auto indexOp = Op(OpCode::eCompositeConstruct, oldIndex.getType()).addOperand(newIndex);

    for (uint32_t i = 1u; i < oldIndex.getOperandCount(); i++) {
      if (oldIndex.isConstant()) {
        indexOp.addOperand(m_builder.makeConstant(uint32_t(oldIndex.getOperand(i))));
      } else {
        dxbc_spv_assert(oldIndex.getOpCode() == OpCode::eCompositeConstruct);
        indexOp.addOperand(oldIndex.getOperand(i));
      }
    }

    newIndex = m_builder.addBefore(load.selectOp, std::move(indexOp));
  }

  /* Emit load. Let CSE deduplicate it as necessary. */
  auto newLoad = m_builder.addBefore(load.selectOp,
    Op(m_builder.getOp(load.loadOp)).setOperand(1u, newIndex));

  /* Rewrite select op in place to swap out the load */
  m_builder.rewriteOp(load.selectOp, Op(m_builder.getOp(load.selectOp)).setOperand(1u, newLoad));
  return true;
}


small_vector<SsaDef, 256> BufferIndexingPass::getBuffers() {
  small_vector<SsaDef, 256> result;

  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    bool isReadOnlyBuffer = false;

    switch (iter->getOpCode()) {
      case OpCode::eDclCbv: {
        isReadOnlyBuffer = iter->getType().isArrayType();
      } break;

      case OpCode::eDclUav: {
        /* Can only easily handle read-only resources here because
         * we might otherwise move loads past an overlapping store */
        auto uavFlags = UavFlags(iter->getOperand(5u));

        if (!(uavFlags & UavFlag::eReadOnly))
          break;
      } [[fallthrough]];

      case OpCode::eDclSrv: {
        auto kind = ResourceKind(iter->getOperand(4u));
        auto isArray = kind == ResourceKind::eBufferTyped || iter->getType().isArrayType();
        isReadOnlyBuffer = resourceIsBuffer(kind) && isArray;
      } break;

      default:
        break;
    }

    if (isReadOnlyBuffer)
      m_builder.getUses(iter->getDef(), result);
  }

  return result;
}


small_vector<BufferIndexingPass::IndexedLoad, 256> BufferIndexingPass::getIndexedLoads(SsaDef descriptor) {
  small_vector<IndexedLoad, 256> result;

  auto [loadA, loadB] = m_builder.getUses(descriptor);

  for (auto load = loadA; load != loadB; load++) {
    size_t resultCount = result.size();

    if (load->getOpCode() == OpCode::eBufferLoad) {
      const auto& indexOp = m_builder.getOpForOperand(*load, 1u);
      auto [useA, useB] = m_builder.getUses(load->getDef());

      for (auto use = useA; use != useB; use++) {
        bool isIndexedSelect = false;

        if (use->getOpCode() == OpCode::eSelect && SsaDef(use->getOperand(1u)) == load->getDef()) {
            const auto& cond = m_builder.getOpForOperand(*use, 0u);

          if (cond.getOpCode() == OpCode::eIEq) {
            const auto& condA = m_builder.getOpForOperand(cond, 0u);
            const auto& condB = m_builder.getOpForOperand(cond, 1u);

            if (condB.isConstant() && condB.getType().getBaseType(0u).isUnsignedIntType()) {
              isIndexedSelect = true;

              auto& loadInfo = result.emplace_back();
              loadInfo.selectOp = use->getDef();
              loadInfo.compareVar = condA.getDef();
              loadInfo.compareValue = uint32_t(condB.getOperand(0u));
              loadInfo.loadOp = load->getDef();

              if (indexOp.isConstant()) {
                loadInfo.indexOffset = uint32_t(indexOp.getOperand(0u));
              } else {
                const auto& scalarOp = indexOp.getOpCode() == OpCode::eCompositeConstruct
                  ? m_builder.getOpForOperand(indexOp, 0u)
                  : indexOp;

                if (!scalarOp.getType().isScalarType()) {
                  isIndexedSelect = false;
                  break;
                }

                if (scalarOp.getOpCode() == OpCode::eIAdd && m_builder.getOpForOperand(scalarOp, 1u).isConstant()) {
                  loadInfo.indexBase = m_builder.getOpForOperand(scalarOp, 0u).getDef();
                  loadInfo.indexOffset = uint32_t(m_builder.getOpForOperand(scalarOp, 1u).getOperand(0u));
                } else {
                  loadInfo.indexBase = scalarOp.getDef();
                  loadInfo.indexOffset = 0;
                }
              }
            }
          }
        }

        /* If the load is otherwise used, try not to pessimize it */
        if (!isIndexedSelect) {
          result.resize(resultCount);
          break;
        }
      }
    }
  }

  std::sort(result.begin(), result.end(), IndexedLoad::compare);
  return result;
}


bool BufferIndexingPass::IndexedLoad::isNextOf(const IndexedLoad& other) const {
  return compareVar   == other.compareVar
      && indexBase    == other.indexBase
      && compareValue == other.compareValue + 1u
      && indexOffset  >  other.indexOffset;
}


bool BufferIndexingPass::IndexedLoad::compare(const IndexedLoad& a, const IndexedLoad& b) {
  /* Ensure that batchable loads are grouped together */
  if (a.compareVar != b.compareVar)
    return a.compareVar < b.compareVar;

  if (a.indexBase != b.indexBase)
    return a.indexBase < b.indexBase;

  if (a.indexOffset != b.indexOffset)
    return a.indexOffset < b.indexOffset;

  if (a.compareValue != b.compareValue)
    return a.compareValue < b.compareValue;

  return false;
}

}
