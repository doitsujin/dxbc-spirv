#include <algorithm>
#include <unordered_map>

#include "ir_pass_propagate_resource_types.h"
#include "ir_pass_lower_consume.h"
#include "ir_pass_scalarize.h"

#include "../../util/util_log.h"

namespace dxbc_spv::ir {

ScalarType promoteTo32BitType(ScalarType type) {
  switch (type) {
    case ScalarType::eUnknown:
      return ScalarType::eUnknown;

    case ScalarType::eU8:
    case ScalarType::eU16:
    case ScalarType::eU32:
      return ScalarType::eU32;

    case ScalarType::eI8:
    case ScalarType::eI16:
    case ScalarType::eI32:
      return ScalarType::eI32;

    case ScalarType::eF16:
    case ScalarType::eF32:
      return ScalarType::eF32;

    default:
      dxbc_spv_unreachable();
      return ScalarType::eUnknown;
  }
}


BasicType promoteTo32BitType(BasicType type) {
  return BasicType(promoteTo32BitType(type.getBaseType()), type.getVectorSize());
}


BasicType combineTypes(BasicType base, BasicType incoming) {
  /* Void is used as a placeholder for when we have no type
   * info yet, override it with the incoming type. */
  if (base.isVoidType() || base == incoming)
    return incoming;

  /* Incoming Void means unused element, ignore it. Also do
   * not resolve any further if the type is ambiguous. */
  if (base.isUnknownType() || incoming.isVoidType())
    return base;

  /* If both types are int, or if both types are float, keep
   * the larger one of the two and ignore signedness. */
  if ((base.isIntType() && incoming.isIntType()) || (base.isFloatType() && incoming.isFloatType()))
    return base.byteSize() >= incoming.byteSize() ? base : incoming;

  /* If the incoming type is unknown, simply promote the base
   * type to 32-bit and do not assume ambiguity just yet. */
  if (incoming.isUnknownType())
    return promoteTo32BitType(base);

  /* Otherwise, the type is entirely ambiguous */
  return BasicType(ScalarType::eUnknown, base.getVectorSize());
}


ScalarType combineTypes(ScalarType base, ScalarType incoming) {
  return combineTypes(BasicType(base), BasicType(incoming)).getBaseType();
}




Type PropagateResourceTypeRewriteInfo::getOldInnerType() const {
  return traverseType(oldType, oldOuterArrayDims);
}


Type PropagateResourceTypeRewriteInfo::getNewInnerType() const {
  return traverseType(newType, newOuterArrayDims);
}


Type PropagateResourceTypeRewriteInfo::traverseType(Type t, uint32_t n) {
  for (uint32_t i = 0u; i < n; i++)
    t = t.getSubType(0u);

  return t;
}


void PropagateResourceTypeRewriteInfo::processLocalLayout(bool flatten, bool allowSubDword, bool removeTrivialArrays) {
  normalizeElementAccess();

  if (!allowSubDword)
    normalizeTypesTo32Bit();

  handleLdsUnusedElements();

  bool needsArrayType = true;

  if (!isDynamicallyIndexed && setupLocalType()) {
    needsArrayType = false;

    if (removeTrivialArrays)
      flattenTrivialArrays();
  }

  if (needsArrayType)
    setupArrayType();

  if (flatten)
    flattenStructureType();

  normalizeFinalElementTypes();
}


void PropagateResourceTypeRewriteInfo::processConstantBufferLayout(bool structured) {
  normalizeElementAccess();
  normalizeTypesTo32Bit();
  handleConstantBufferUnusedElements();

  bool useStructuredType = structured && !isDynamicallyIndexed;

  if (!useStructuredType || !setupConstantBufferType())
    setupArrayType();

  normalizeFinalElementTypes();

  dxbc_spv_assert(!isDynamicallyIndexed || newType.byteSize() == oldType.byteSize());
}


void PropagateResourceTypeRewriteInfo::processResourceBufferLayout(bool structured) {
  normalizeElementAccess();
  normalizeTypesTo32Bit();
  handleResourceBufferUnusedElements();

  bool useStructuredType = structured && !isDynamicallyIndexed;

  if (!useStructuredType || !setupResourceBufferType())
    setupArrayType();

  normalizeFinalElementTypes();
}


void PropagateResourceTypeRewriteInfo::normalizeElementAccess() {
  /* Make sure all accessed elements have a non-void type */
  auto vectorType = ScalarType::eVoid;
  auto vectorSize = 0u;

  for (auto& e : elements) {
    if (vectorSize)
      vectorSize -= 1u;

    if (e.resolvedType == ScalarType::eVoid) {
      if (e.accessSize)
        e.resolvedType = ScalarType::eUnknown;

      if (vectorSize)
        e.resolvedType = combineTypes(e.resolvedType, vectorType);
    }

    if (e.accessSize > 1u) {
      vectorType = e.resolvedType;
      vectorSize = e.accessSize;
    }
  }

  /* Make sure access sizes don't exceed the structure. This can happen
   * with raw buffers since those can load vectors from a scalar array. */
  for (size_t i = 1u; i <= std::min<size_t>(4u, elements.size()); i++) {
    auto& e = elements.at(elements.size() - i);

    if (e.accessSize > i)
      e.accessSize = uint8_t(i);
  }

  /* Do another pass and ensure that all elements used in a single
   * vector access have the same type. Ignore overlapping vector
   * access for now, we will demote those to array types. */
  for (size_t i = 0u; i < elements.size(); i++) {
    auto& e = elements.at(i);

    if (e.accessSize <= 1u)
      continue;

    if (!e.isAtomicallyAccessed) {
      for (size_t j = 1u; j < size_t(e.accessSize); j++) {
        const auto& next = elements.at(i + j);
        e.resolvedType = combineTypes(e.resolvedType, next.resolvedType);

        if (next.isAtomicallyAccessed) {
          e.resolvedType = next.resolvedType;
          break;
        }
      }
    }

    for (size_t j = 1u; j < size_t(e.accessSize); j++)
      elements.at(i + j).resolvedType = e.resolvedType;
  }
}


void PropagateResourceTypeRewriteInfo::normalizeTypesTo32Bit() {
  for (auto& e : elements) {
    if (e.resolvedType != ScalarType::eVoid)
      e.resolvedType = promoteTo32BitType(e.resolvedType);
  }

  if (fallbackType != ScalarType::eVoid)
    fallbackType = promoteTo32BitType(fallbackType);
}


void PropagateResourceTypeRewriteInfo::handleLdsUnusedElements() {
  /* Only mark everything as used if there is dynamic indexing going on */
  if (!isDynamicallyIndexed)
    return;

  for (auto& e : elements) {
    if (e.resolvedType == ScalarType::eVoid)
      e.resolvedType = ScalarType::eUnknown;
  }
}


void PropagateResourceTypeRewriteInfo::handleConstantBufferUnusedElements() {
  /* Find the last used element and round up to a multiple of four in
   * order to respect vec4 boundaries. */
  size_t usedElementCount = 0u;

  if (isDynamicallyIndexed) {
    usedElementCount = elements.size();
  } else {
    for (size_t i = 0u; i < elements.size(); i++) {
      if (elements.at(i).resolvedType == ScalarType::eVoid)
        elements.at(i).resolvedType = ScalarType::eUnknown;
      else
        usedElementCount = (i + 4u) & ~3u;
    }
  }

  /* Nuke all unused elements from the structure */
  dxbc_spv_assert(usedElementCount <= elements.size());
  elements.resize(usedElementCount);
}


void PropagateResourceTypeRewriteInfo::handleResourceBufferUnusedElements() {
  /* Need to keep the layout intact, so mark everything that's unused
   * as unknown so that it gets included in the final structure. */
  for (auto& e : elements) {
    if (e.resolvedType == ScalarType::eVoid)
      e.resolvedType = ScalarType::eUnknown;
  }
}


bool PropagateResourceTypeRewriteInfo::setupLocalType() {
  if (hasOverlappingVectorAccess())
    return false;

  if (getOldInnerType().isVectorType()) {
    /* Keep incoming type as a scalar or vector, but remove any
     * unused components. Relevant for certain scratch arrays. */
    auto scalarType = determineCommonScalarType();
    auto vectorSize = 0u;

    if (scalarType == ScalarType::eUnknown)
      scalarType = ScalarType::eU32;

    for (auto& e : elements) {
      if (e.resolvedType != ScalarType::eVoid)
        vectorSize++;
    }

    /* Assign component indices */
    uint8_t componentIndex = 0u;

    for (auto& e : elements) {
      if (e.resolvedType != ScalarType::eVoid) {
        e.memberIndex = -1;
        e.componentIndex = vectorSize > 1u ? componentIndex++ : int8_t(-1);
      }
    }

    /* Set up actual type */
    newType = Type(scalarType, vectorSize);
    newOuterArrayDims = oldOuterArrayDims;

    for (uint32_t i = oldType.getArrayDimensions() - oldOuterArrayDims; i < oldType.getArrayDimensions(); i++)
      newType.addArrayDimension(oldType.getArraySize(i));
  } else {
    /* Keep things simple and add each accessed element as a dedicated member */
    util::small_vector<BasicType, Type::MaxStructMembers> types;

    for (size_t i = 0u; i < elements.size(); ) {
      const auto& e = elements.at(i);

      auto n = std::max(1u, uint32_t(e.accessSize));

      if (e.resolvedType != ScalarType::eVoid) {
        auto scalarType = e.resolvedType;

        if (scalarType == ScalarType::eUnknown)
          scalarType = ScalarType::eU32;

        for (uint32_t j = 0u; j < n; j++) {
          auto& component = elements.at(i + j);
          component.memberIndex = int16_t(types.size());
          component.componentIndex = n > 1u ? int8_t(j) : int8_t(-1);
        }

        types.emplace_back(scalarType, n);
      }

      i += n;
    }

    /* If we get a trivial struct, normalize the indices */
    if (types.size() == 1u) {
      for (auto& e : elements)
        e.memberIndex = -1;
    }

    /* Build actual type */
    if (types.size() > Type::MaxStructMembers)
      return false;

    newType = Type();

    for (const auto& t : types)
      newType.addStructMember(t);

    for (uint32_t i = oldType.getArrayDimensions() - oldOuterArrayDims; i < oldType.getArrayDimensions(); i++)
      newType.addArrayDimension(oldType.getArraySize(i));

    newOuterArrayDims = oldOuterArrayDims;
  }

  return true;
}


bool PropagateResourceTypeRewriteInfo::setupConstantBufferType() {
  /* Exit early if the buffer is too large */
  if (elements.size() > 4u * Type::MaxStructMembers)
    return false;

  /* For constant buffers, the incoming type is generally going to be
   * a vector array, so keep vectors together when assembling the type.
   * This way, we can even handle cases of overlapping vector access
   * since accesses will happen inside a vec4 at most. */
  util::small_vector<BasicType, 4u * Type::MaxStructMembers> types;

  for (size_t i = 0u; i < elements.size(); i += 4u) {
    /* Work out access properties within this vec4 */
    bool hasOverlap = false;
    auto accessMask = 0x0u;

    for (size_t j = 0u; j < 4u; j++) {
      const auto& component = elements.at(i + j);

      if (component.accessSize > 1u) {
        auto mask = ((1u << component.accessSize) - 1u) << j;
        hasOverlap = hasOverlap || (accessMask & mask);
        accessMask |= mask;
      }
    }

    /* Vector access straddles vec4 boundary somehow? */
    if (accessMask > 0xfu)
      return false;

    if (hasOverlap) {
      /* If there is any vector overlap, emit a single
       * vec4 with a consistent scalar type */
      auto memberIndex = int16_t(types.size());
      auto type = ScalarType::eVoid;

      for (size_t j = 0u; j < 4u; j++) {
        auto& component = elements.at(i + j);
        component.componentIndex = int8_t(j);
        component.memberIndex = memberIndex;

        type = combineTypes(type, component.resolvedType);
      }

      if (type == ScalarType::eVoid || type == ScalarType::eUnknown)
        type = ScalarType::eU32;

      types.emplace_back(type, 4u);
    } else {
      /* Otherwise, merge all scalars of the same type */
      for (size_t j = 0u; j < 4u; ) {
        const auto& e = elements.at(i + j);
        auto scalarType = e.accessSize ? e.resolvedType : ScalarType::eVoid;

        size_t n = std::max<size_t>(e.accessSize, 1u);

        if (n == 1u) {
          while (j + n < 4u) {
            const auto& component = elements.at(i + j + n);

            /* Don't merge if the next element is used and has a
             * different type, or if it is accessed as a vector */
            if (component.accessSize > 1u)
              break;

            if (component.accessSize) {
              if (scalarType == ScalarType::eVoid)
                scalarType = component.resolvedType;
              else if (scalarType != component.resolvedType)
                break;
            }

            n++;
          }
        }

        for (size_t k = 0u; k < n; k++) {
          auto& component = elements.at(i + j + k);
          component.componentIndex = n > 1u ? int8_t(k) : int8_t(-1);
          component.memberIndex = uint16_t(types.size());
        }

        if (scalarType == ScalarType::eVoid || scalarType == ScalarType::eUnknown)
          scalarType = ScalarType::eU32;

        types.emplace_back(scalarType, n);

        j += n;
      }
    }
  }

  /* Handle special case where we only have one element */
  if (types.size() == 1u) {
    for (auto& e : elements)
      e.memberIndex = -1;
  }

  /* Give up if there are too many unique elements */
  if (types.size() > Type::MaxStructMembers)
    return false;

  /* Create struct type */
  newType = Type();

  for (const auto& t : types)
    newType.addStructMember(t);

  return true;
}


bool PropagateResourceTypeRewriteInfo::setupResourceBufferType() {
  /* Ignore raw buffers here and just promote the scalar type if possible */
  if (hasOverlappingVectorAccess() || !getOldInnerType().isArrayType())
    return false;

  /* Assume a fully scalar block layout w.r.t. aligment and batch as many
   * elements that are only accessed in a scalar fashion together in order
   * to reduce the struct member count. */
  util::small_vector<BasicType, Type::MaxStructMembers> types;

  for (size_t i = 0u; i < elements.size(); ) {
    const auto& e = elements.at(i);

    auto vectorSize = e.accessSize;
    auto scalarType = e.accessSize ? e.resolvedType : ScalarType::eVoid;

    if (e.accessSize <= 1u) {
      /* Merge subsequent unused or scalar elements with the same type */
      while (vectorSize < 4u && i + vectorSize < elements.size()) {
        const auto& next = elements.at(i + vectorSize);

        if (next.accessSize > 1u)
          break;

        if (next.accessSize) {
          if (scalarType == ScalarType::eVoid)
            scalarType = next.resolvedType;
          else if (scalarType != next.resolvedType)
            break;
        }

        vectorSize++;
      }
    }

    if (scalarType == ScalarType::eVoid || scalarType == ScalarType::eUnknown)
      scalarType = ScalarType::eU32;

    /* Add mapping entries */
    for (uint32_t j = 0u; j < vectorSize; j++) {
      auto& component = elements.at(i + j);
      component.memberIndex = int16_t(types.size());
      component.componentIndex = vectorSize > 1u ? int8_t(j) : int8_t(-1);
    }

    types.emplace_back(scalarType, vectorSize);

    i += vectorSize;
  }

  /* Handle single struct member case */
  if (types.size() == 1u) {
    for (auto& e : elements)
      e.memberIndex = -1;
  }

  /* Build actual type */
  if (types.size() > Type::MaxStructMembers)
    return false;

  newType = Type();

  for (const auto& t : types)
    newType.addStructMember(t);

  for (uint32_t i = oldType.getArrayDimensions() - oldOuterArrayDims; i < oldType.getArrayDimensions(); i++)
    newType.addArrayDimension(oldType.getArraySize(i));

  newOuterArrayDims = oldOuterArrayDims;
  return true;
}


void PropagateResourceTypeRewriteInfo::setupArrayType() {
  auto scalarType = determineCommonScalarType();

  if (scalarType == ScalarType::eUnknown)
    scalarType = ScalarType::eU32;

  /* Iterate over structure members and map used vectors to
   * the destination type, while skipping unused ones. */
  auto vectorSize = oldType.getBaseType(0u).getVectorSize();
  auto vectorCount = 0u;

  for (size_t i = 0u; i < elements.size(); i += vectorSize) {
    bool vectorUsed = isDynamicallyIndexed;

    for (size_t j = 0u; j < vectorSize && !vectorUsed; j++)
      vectorUsed = elements.at(i + j).resolvedType != ScalarType::eVoid;

    if (vectorUsed) {
      for (size_t j = 0u; j < vectorSize; j++) {
        auto& e = elements.at(i + j);
        e.componentIndex = vectorSize > 1u ? int8_t(j) : int8_t(-1);
        e.memberIndex = int16_t(vectorCount);
      }

      vectorCount += 1u;
    }
  }

  vectorCount = std::max(vectorCount, 1u);
  newType = Type(scalarType, vectorSize);

  if (getOldInnerType().isArrayType() || vectorCount > 1u) {
    /* Set inner array dimension to the compacted vector count */
    newType.addArrayDimension(vectorCount);
  } else {
    /* If the base type is already not an array, ignore the array
     * dimension and get rid of the member indices. */
    for (auto& e : elements)
      e.memberIndex = -1;
  }

  /* Copy outer array dimensions from the base type, */
  for (uint32_t i = oldType.getArrayDimensions() - oldOuterArrayDims; i < oldType.getArrayDimensions(); i++)
    newType.addArrayDimension(oldType.getArraySize(i));

  newOuterArrayDims = oldOuterArrayDims;
}


void PropagateResourceTypeRewriteInfo::flattenStructureType() {
  dxbc_spv_assert(oldOuterArrayDims);

  auto innerType = getNewInnerType();

  /* Make sure we have at least one array dimension to fold stuff into */
  if (!newOuterArrayDims) {
    newType = newType.addArrayDimension(1u);
    newOuterArrayDims = 1u;
  }

  /* Count the total number of scalars in the inner type */
  auto scalarCount = 0u;

  for (uint32_t i = 0u; i < innerType.getStructMemberCount(); i++)
    scalarCount += innerType.getBaseType(i).getVectorSize();

  for (uint32_t i = 0u; i < innerType.getArrayDimensions(); i++)
    scalarCount *= innerType.getArraySize(i);

  /* Determine scalar type to use as a base */
  auto scalarType = determineCommonScalarType();

  if (scalarType == ScalarType::eUnknown)
    scalarType = ScalarType::eU32;

  /* Build new inner type by folding the scalar count
   * into the innermost outer array dimension */
  auto dimIndex = newType.getArrayDimensions() - newOuterArrayDims;
  newType = Type(scalarType).addArrayDimension(scalarCount * newType.getArraySize(dimIndex));

  for (uint32_t i = dimIndex + 1u; i < newType.getArrayDimensions(); i++)
    newType.addArrayDimension(newType.getArraySize(i));

  /* Assign the scalar offset as the member index for every
   * element that is actually being used */
  uint32_t scalarIndex = 0u;

  for (auto& e : elements) {
    if (e.isUsed())
      e.memberIndex = int16_t(scalarIndex++);

    e.componentIndex = -1;
  }

  dxbc_spv_assert(scalarIndex == scalarCount);

  /* Write back type metadata */
  isFlattened = true;
  flattenedScalarCount = scalarCount;
}


void PropagateResourceTypeRewriteInfo::flattenTrivialArrays() {
  while (newOuterArrayDims && newType.getArraySize(newOuterArrayDims - 1u) == 1u) {
    newType = newType.getSubType(0u);
    newOuterArrayDims -= 1u;
  }
}


void PropagateResourceTypeRewriteInfo::normalizeFinalElementTypes() {
  Type innerType = getNewInnerType();

  for (auto& e : elements) {
    if (!e.isUsed())
      continue;

    Type t = innerType;

    if (e.memberIndex >= 0 && !isFlattened)
      t = t.getSubType(e.memberIndex);

    if (e.componentIndex >= 0)
      t = t.getSubType(e.componentIndex);

    e.resolvedType = t.getBaseType(0u).getBaseType();

    dxbc_spv_assert(e.resolvedType != ScalarType::eVoid &&
                    e.resolvedType != ScalarType::eUnknown);
  }
}


bool PropagateResourceTypeRewriteInfo::hasOverlappingVectorAccess() const {
  uint8_t vectorSize = 0u;

  for (const auto& e : elements) {
    /* Also don't allow overlapping vector access with atomics
     * since things may get sketchy if we do. */
    if (vectorSize && (e.accessSize > 1u || e.isAtomicallyAccessed))
      return true;

    if (e.accessSize > 1u)
      vectorSize = e.accessSize;

    if (vectorSize)
      vectorSize -= 1u;
  }

  return false;
}


ScalarType PropagateResourceTypeRewriteInfo::determineCommonScalarType() const {
  if (isAtomicallyAccessed)
    return fallbackType;

  ScalarType type = fallbackType;

  for (auto e : elements)
    type = combineTypes(type, e.resolvedType);

  return type;
}




PropagateResourceTypesPass::PropagateResourceTypesPass(Builder& builder, const Options& options)
: m_builder(builder), m_options(options) {

}


PropagateResourceTypesPass::~PropagateResourceTypesPass() {

}


void PropagateResourceTypesPass::run() {
  std::unordered_map<SsaDef, PropagateResourceTypeRewriteInfo> types;

  /* Infer types for any unknown resource or scratch/lds declaration */
  auto [a, b] = m_builder.getDeclarations();

  for (auto iter = a; iter != b; iter++) {
    if (isUntypedDeclaration(*iter)) {
      auto entry = types.emplace(std::piecewise_construct,
        std::tuple(iter->getDef()), std::tuple());
      determineDeclarationType(*iter, entry.first->second);
    }
  }

  /* Rewrite resource declarations and access operations */
  for (const auto& e : types)
    rewriteDeclaration(e.first, e.second);

  /* Legalize partial vector loads to load full vectors */
  rewritePartialVectorLoads();

  /* Clean up consume chains and composite ops that we created */
  while (true) {
    bool a = ScalarizePass::runResolveRedundantCompositesPass(m_builder);
    bool b = LowerConsumePass::runResolveCastChainsPass(m_builder);

    if (!a && !b)
      break;
  }
}


void PropagateResourceTypesPass::rewritePartialVectorLoads() {
  auto iter = m_builder.getDeclarations().first;

  while (iter != m_builder.getDeclarations().second) {
    switch (iter->getOpCode()) {
      case OpCode::eDclSrv:
      case OpCode::eDclUav: {
        auto kind = ResourceKind(iter->getOperand(iter->getFirstLiteralOperandIndex() + 3u));

        if (kind != ResourceKind::eBufferStructured && kind != ResourceKind::eBufferRaw)
          break;
      } [[fallthrough]];

      case OpCode::eDclCbv: {
        util::small_vector<SsaDef, 256u> uses = { };
        m_builder.getUses(iter->getDef(), uses);

        for (auto use : uses) {
          if (m_builder.getOp(use).getOpCode() == OpCode::eDescriptorLoad)
            rewritePartialVectorLoadsForDescriptor(iter->getType(), use);
        }
      } break;

      default:
        break;
    }

    ++iter;
  }
}


void PropagateResourceTypesPass::runPass(Builder& builder, const Options& options) {
  PropagateResourceTypesPass(builder, options).run();
}


void PropagateResourceTypesPass::runPartialVectorLoadRewritePass(Builder& builder) {
  PropagateResourceTypesPass(builder, Options()).rewritePartialVectorLoads();
}


void PropagateResourceTypesPass::rewriteLoad(SsaDef def, const PropagateResourceTypeRewriteInfo& info) {
  const auto& addressOp = m_builder.getOpForOperand(def, 1u);

  m_builder.setCursor(def);

  /* Determine load type. For sparse feedback, we need to keep the
   * struct intact and only change the actual data element. */
  auto mappedType = getMappedType(info, addressOp.getDef());

  auto loadOp = m_builder.getOp(def);
  bool hasSparseFeedback = bool(loadOp.getFlags() & OpFlag::eSparseFeedback);

  auto originalType = loadOp.getType();
  auto remappedType = Type();

  if (hasSparseFeedback)
    remappedType.addStructMember(originalType.getBaseType(0u));

  auto vectorSize = originalType.getBaseType(hasSparseFeedback ? 1u : 0u).getVectorSize();
  remappedType.addStructMember(BasicType(mappedType, vectorSize));

  /* Rewrite load address and insert as a new load op */
  loadOp.setType(remappedType);
  loadOp.setOperand(1u, rewriteAddress(info, addressOp.getDef(), vectorSize));

  auto loadDef = m_builder.add(std::move(loadOp));

  /* Insert scalarized conversion back to unknown since that is
   * what uses of this instruction are going to expect. */
  loadDef = rewriteValue(loadDef, ScalarType::eUnknown);

  m_builder.rewriteDef(def, loadDef);
}


void PropagateResourceTypesPass::rewriteStore(SsaDef def, const PropagateResourceTypeRewriteInfo& info) {
  /* Insert scalarized conversion from Unknown to the new type */
  const auto& addressOp = m_builder.getOpForOperand(def, 1u);
  const auto& valueOp = m_builder.getOpForOperand(def, 2u);

  m_builder.setCursor(m_builder.getPrev(def));

  /* Rewrite store op with the address and value rewritten */
  auto storeOp = m_builder.getOp(def);
  auto value = rewriteValue(valueOp.getDef(), getMappedType(info, addressOp.getDef()));
  auto vectorSize = m_builder.getOp(value).getType().getBaseType(0u).getVectorSize();

  storeOp.setOperand(1u, rewriteAddress(info, addressOp.getDef(), vectorSize));
  storeOp.setOperand(2u, value);

  m_builder.rewriteOp(def, std::move(storeOp));
}


void PropagateResourceTypesPass::rewriteAtomic(SsaDef def, const PropagateResourceTypeRewriteInfo& info) {
  /* We only need to rewrite the address here since the atomic type
   * must already match the scalar type at the given address. */
  auto atomicOp = m_builder.getOp(def);

  m_builder.setCursor(m_builder.getPrev(def));

  atomicOp.setOperand(1u, rewriteAddress(info,
    m_builder.getOpForOperand(atomicOp, 1u).getDef(), 1u));

  m_builder.rewriteOp(def, std::move(atomicOp));
}


ScalarType PropagateResourceTypesPass::getMappedType(const PropagateResourceTypeRewriteInfo& info, SsaDef address) {
  if (info.isDynamicallyIndexed || info.isFlattened)
    return info.newType.getBaseType(0u).getBaseType();

  auto element = lookupElement(info, address);
  return element.resolvedType;
}


SsaDef PropagateResourceTypesPass::rewriteAddress(const PropagateResourceTypeRewriteInfo& info, SsaDef address, uint32_t accessVectorSize) {
  /* Addresses are compatible in the dynamically indexed case */
  if (info.isDynamicallyIndexed && !info.isFlattened)
    return address;

  Op newOp(OpCode::eCompositeConstruct);

  if (info.newOuterArrayDims <= info.oldOuterArrayDims) {
    /* Copy the innermost array indices still included in the destination type */
    uint32_t first = info.oldOuterArrayDims - info.newOuterArrayDims;

    for (uint32_t i = 0u; i < info.newOuterArrayDims; i++)
      newOp.addOperand(extractIndexFromAddress(address, first + i));
  } else {
    /* Add index components for newly added outer array wrappers */
    for (uint32_t i = info.oldOuterArrayDims; i < info.newOuterArrayDims; i++)
      newOp.addOperand(m_builder.makeConstant(0u));
  }

  if (info.isFlattened) {
    /* If flattened, scale and offset the innermost array index */
    dxbc_spv_assert(newOp.getOperandCount() && info.flattenedScalarCount);

    auto base = SsaDef(newOp.getOperand(newOp.getOperandCount() - 1u));

    if (info.flattenedScalarCount > 1u) {
      base = m_builder.add(Op::IMul(ScalarType::eU32, base,
        m_builder.makeConstant(info.flattenedScalarCount)));
    }

    base = m_builder.add(Op::IAdd(ScalarType::eU32, base,
      computeFlattenedScalarIndex(info, address)));

    newOp.setOperand(newOp.getOperandCount() - 1u, base);
  } else {
    dxbc_spv_assert(!info.isDynamicallyIndexed);

    Type newType = info.getNewInnerType();

    PropagateResourceTypeElementInfo element = lookupElement(info, address);

    if (element.memberIndex >= 0) {
      newOp.addOperand(m_builder.makeConstant(uint32_t(element.memberIndex)));
      newType = newType.getSubType(element.memberIndex);
    }

    bool isFullVectorAccess = !element.componentIndex && accessVectorSize > 1u &&
      newType.isBasicType() && newType.getBaseType(0u).getVectorSize() == accessVectorSize;

    if (element.componentIndex >= 0 && !isFullVectorAccess)
      newOp.addOperand(m_builder.makeConstant(uint32_t(element.componentIndex)));
  }

  if (newOp.getOperandCount() == 0u) {
    /* Addressed object is scalar or at least a basic type
     * that can be used directly, no addressing needed */
    return SsaDef();
  } else if (newOp.getOperandCount() == 1u) {
    /* Address became scalar */
    return SsaDef(newOp.getOperand(0u));
  } else {
    newOp.setType(BasicType(ScalarType::eU32, newOp.getOperandCount()));
    return m_builder.add(std::move(newOp));
  }
}


SsaDef PropagateResourceTypesPass::rewriteValue(SsaDef value, ScalarType type) {
  const auto& valueOp = m_builder.getOp(value);
  const auto& valueType = valueOp.getType();

  if (valueOp.getFlags() & OpFlag::eSparseFeedback) {
    /* Things get extra annoying if the source op is a sparse feedback load
     * since we need to extract the actual data value first, convert it, and
     * then put the sparse feedback struct back together. */
    dxbc_spv_assert(valueType.isStructType());

    auto feedbackValue = m_builder.add(Op::CompositeExtract(valueType.getSubType(0u), value, m_builder.makeConstant(0u)));
    auto dataValue = m_builder.add(Op::CompositeExtract(valueType.getSubType(1u), value, m_builder.makeConstant(1u)));

    dataValue = rewriteValue(dataValue, type);

    auto resultType = Type()
      .addStructMember(m_builder.getOp(feedbackValue).getType().getBaseType(0u))
      .addStructMember(m_builder.getOp(dataValue).getType().getBaseType(0u));

    return m_builder.add(Op::CompositeConstruct(resultType, feedbackValue, dataValue));
  } else {
    dxbc_spv_assert(valueType.isBasicType());

    if (valueType.isVectorType()) {
      /* Scalarize conversions manually to avoid having to run scalarization again */
      BasicType srcType = valueType.getBaseType(0u);
      BasicType dstType = BasicType(type, srcType.getVectorSize());

      Op compositeOp = Op::CompositeConstruct(dstType);

      for (uint32_t i = 0u; i < dstType.getVectorSize(); i++) {
        auto scalar = m_builder.add(Op::CompositeExtract(srcType.getBaseType(),
          valueOp.getDef(), m_builder.makeConstant(i)));
        compositeOp.addOperand(m_builder.add(Op::ConsumeAs(type, scalar)));
      }

      return m_builder.add(std::move(compositeOp));
    } else {
      /* Convert scalar as-is */
      return m_builder.add(Op::ConsumeAs(type, valueOp.getDef()));
    }
  }
}


SsaDef PropagateResourceTypesPass::computeFlattenedScalarIndex(const PropagateResourceTypeRewriteInfo& info, SsaDef address) {
  const auto& addressOp = m_builder.getOp(address);
  auto addressType = addressOp.getType().getBaseType(0u);

  uint32_t component = info.oldOuterArrayDims;

  /* Might have finished traversing the type already if it's a scalar
   * or vector that essentially has already been flattened */
  auto oldType = info.getOldInnerType();

  if (component == addressType.getVectorSize())
    return m_builder.makeConstant(0u);

  if (info.isDynamicallyIndexed) {
    SsaDef def = { };

    if (oldType.isArrayType() && component < addressType.getVectorSize()) {
      /* Work out array index and scale by vector component count */
      def = extractIndexFromAddress(address, component++);

      oldType = oldType.getSubType(0u);
      dxbc_spv_assert(oldType.isBasicType());

      uint32_t componentCount = oldType.getBaseType(0u).getVectorSize();

      if (componentCount > 1u) {
        def = m_builder.add(Op::IMul(ScalarType::eU32, def,
          m_builder.makeConstant(componentCount)));
      }
    }

    if (oldType.isVectorType() && component < addressType.getVectorSize()) {
      /* Bias index by vector component index */
      auto index = extractIndexFromAddress(address, component++);

      def = def
        ? m_builder.add(Op::IAdd(ScalarType::eU32, def, index))
        : index;
    }

    /* We must have fully traversed the address vector by now */
    dxbc_spv_assert(def && addressType.getVectorSize() == component);
    return def;
  } else {
    /* We know that the flattened index must be using a scalar array */
    auto element = lookupElement(info, address);

    dxbc_spv_assert(element.memberIndex >= 0 && element.componentIndex < 0);
    dxbc_spv_assert(uint32_t(element.memberIndex) < info.flattenedScalarCount);

    return m_builder.makeConstant(uint32_t(element.memberIndex));
  }
}


std::optional<uint32_t> PropagateResourceTypesPass::extractConstantFromAddress(SsaDef address, uint32_t component) const {
  const auto& addressOp = m_builder.getOp(address);

  if (component < addressOp.getOperandCount()) {
    if (addressOp.isConstant())
      return uint32_t(addressOp.getOperand(component));

    if (addressOp.getOpCode() == OpCode::eCompositeConstruct) {
      const auto& constant = m_builder.getOpForOperand(addressOp, component);

      if (constant.isConstant())
        return uint32_t(constant.getOperand(0u));
    }
  }

  return std::nullopt;
}


SsaDef PropagateResourceTypesPass::extractIndexFromAddress(SsaDef address, uint32_t component) const {
  const auto& addressOp = m_builder.getOp(address);

  dxbc_spv_assert(addressOp.getType().isBasicType());
  dxbc_spv_assert(component < addressOp.getType().getBaseType(0u).getVectorSize());

  if (addressOp.getType().isScalarType())
    return address;

  if (addressOp.isConstant())
    return m_builder.makeConstant(uint32_t(addressOp.getOperand(component)));

  if (addressOp.getOpCode() == OpCode::eCompositeConstruct)
    return SsaDef(addressOp.getOperand(component));

  auto type = addressOp.getType().getBaseType(0u).getBaseType();
  return m_builder.add(Op::CompositeExtract(type, address, m_builder.makeConstant(component)));
}


std::optional<uint32_t> PropagateResourceTypesPass::computeDwordIndex(Type type, SsaDef address, uint32_t firstComponent) {
  const auto& addressOp = m_builder.getOp(address);
  auto addressType = addressOp.getType().getBaseType(0u);

  uint32_t byteOffset = 0u;

  /* Traverse address vector and nope out if we encounter a non-constant index */
  for (uint32_t i = firstComponent; i < addressType.getVectorSize(); i++) {
    auto index = extractConstantFromAddress(address, i);

    if (!index)
      return std::nullopt;

    byteOffset += type.byteOffset(*index);
    type = type.getSubType(*index);
  }

  return byteOffset / sizeof(uint32_t);
}


PropagateResourceTypeElementInfo PropagateResourceTypesPass::lookupElement(const PropagateResourceTypeRewriteInfo& info, SsaDef address) {
  auto index = computeDwordIndex(info.getOldInnerType(), address, info.oldOuterArrayDims);
  dxbc_spv_assert(index && (*index) < info.elements.size());

  return info.elements.at(*index);
}


BasicType PropagateResourceTypesPass::determineAccessTypeForLoad(const Op& op) {
  /* For loads, we actually need to check consume instructions that use the loaded
   * value in order to determine types. If the load has sparse feedback, we need to
   * check the extracted data value instead. */
  bool hasSparseFeedback = bool(op.getFlags() & OpFlag::eSparseFeedback);

  auto [a, b] = m_builder.getUses(op.getDef());

  BasicType resolvedType = { };
  BasicType providedType = op.getType().getBaseType(hasSparseFeedback ? 1u : 0u);

  for (auto iter = a; iter != b; iter++) {
    if (iter->getOpCode() == OpCode::eConsumeAs && !hasSparseFeedback) {
      /* Use the consumed type */
      dxbc_spv_assert(iter->getType().isBasicType());
      resolvedType = combineTypes(resolvedType, iter->getType().getBaseType(0u));
    } else if (iter->getOpCode() == OpCode::eCompositeExtract) {
      /* Recursively check uses of the extract op */
      const auto& componentOp = m_builder.getOpForOperand(*iter, 1u);

      if (!hasSparseFeedback || uint32_t(componentOp.getOperand(0u)) == 1u) {
        auto scalarType = combineTypes(resolvedType, determineAccessTypeForLoad(*iter)).getBaseType();
        resolvedType = BasicType(scalarType, providedType.getVectorSize());
      }
    } else {
      /* Something consumes the unknown value directly */
      resolvedType = combineTypes(resolvedType, providedType);
    }
  }

  if (resolvedType.isVoidType())
    resolvedType = providedType;

  return resolvedType;
}


BasicType PropagateResourceTypesPass::determineAccessTypeForStore(const Op& op) {
  /* All store ops we care about here have their value type as the third operand.
   * If the value type is a consume op, use the type of the source operand. */
  auto value = ir::SsaDef(op.getOperand(2u));

  auto valueType = m_builder.getOp(value).getType();
  dxbc_spv_assert(valueType.isBasicType());

  if (m_builder.getOp(value).getOpCode() == OpCode::eCompositeConstruct)
    value = m_builder.getOpForOperand(value, 0u).getDef();

  if (m_builder.getOp(value).getOpCode() == OpCode::eConsumeAs) {
    const auto& srcOp = m_builder.getOpForOperand(value, 0u);

    return BasicType(srcOp.getType().getBaseType(0u).getBaseType(),
      valueType.getBaseType(0u).getVectorSize());
  }

  return valueType.getBaseType(0u);
}


BasicType PropagateResourceTypesPass::determineAccessTypeForAtomic(const Op& op) {
  /* For atomics, the return value may be null in which case we need to check
   * an operand type. */
  if (!op.getType().isVoidType()) {
    dxbc_spv_assert(op.getType().isBasicType());
    return op.getType().getBaseType(0u);
  }

  /* All atomic ops we support here have two operands before the atomic inputs */
  dxbc_spv_assert(op.getFirstLiteralOperandIndex() > 2u);

  const auto& valueOp = m_builder.getOpForOperand(op, 2u);
  dxbc_spv_assert(valueOp.getType().isBasicType());

  return valueOp.getType().getBaseType(0u);
}


void PropagateResourceTypesPass::determineElementTypeForAccess(const Op& op, PropagateResourceTypeRewriteInfo& info, BasicType accessType) {
  /* All ops we care about have the address as operand 1 */
  const auto& addressOp = m_builder.getOpForOperand(op, 1u);

  /* Write back atomic type info so it does not get overwritten */
  bool isAtomic = op.getOpCode() == OpCode::eLdsAtomic ||
                  op.getOpCode() == OpCode::eBufferAtomic;

  if (isAtomic) {
    info.isAtomicallyAccessed = true;
    info.fallbackType = accessType.getBaseType();
  }

  auto dwordIndex = computeDwordIndex(info.getOldInnerType(), addressOp.getDef(), info.oldOuterArrayDims);

  if (!dwordIndex || (*dwordIndex) >= info.elements.size()) {
    /* Index is either out of bounds or dynamic */
    info.isDynamicallyIndexed = true;

    if (!info.isAtomicallyAccessed)
      info.fallbackType = combineTypes(info.fallbackType, accessType.getBaseType());
  } else {
    auto& element = info.elements.at(*dwordIndex);

    if (isAtomic) {
      /* Force element type to be the atomic type */
      element.resolvedType = accessType.getBaseType();
      element.isAtomicallyAccessed = true;
    } else if (!element.isAtomicallyAccessed) {
      /* If we get an unknown type as the first type, discard it so
       * we can further resolve the type with more info. However,
       * this means we have to handle void with a non-zero access
       * size, and treat it as an unknown incoming type. */
      if (element.resolvedType == ScalarType::eVoid) {
        if (accessType != ScalarType::eUnknown) {
          element.resolvedType = element.accessSize
            ? promoteTo32BitType(accessType.getBaseType())
            : accessType.getBaseType();
        }
      } else {
        element.resolvedType = combineTypes(element.resolvedType, accessType.getBaseType());
      }
    }

    element.accessSize = std::max<uint32_t>(element.accessSize, accessType.getVectorSize());
  }
}


void PropagateResourceTypesPass::determineElementTypeForUses(const Op& op, PropagateResourceTypeRewriteInfo& info) {
  auto [a, b] = m_builder.getUses(op.getDef());

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eLdsLoad:
      case OpCode::eBufferLoad:
      case OpCode::eScratchLoad:
      case OpCode::eConstantLoad:
        determineElementTypeForAccess(*iter, info,
          determineAccessTypeForLoad(*iter));
        break;

      case OpCode::eLdsStore:
      case OpCode::eBufferStore:
      case OpCode::eScratchStore:
        determineElementTypeForAccess(*iter, info,
          determineAccessTypeForStore(*iter));
        break;

      case OpCode::eLdsAtomic:
      case OpCode::eBufferAtomic:
        determineElementTypeForAccess(*iter, info,
          determineAccessTypeForAtomic(*iter));
        break;

      case OpCode::eDescriptorLoad:
        determineElementTypeForUses(*iter, info);
        break;

      default:
        break;
    }
  }
}


void PropagateResourceTypesPass::determineDeclarationType(const Op& op, PropagateResourceTypeRewriteInfo& info) {
  /* Determine the number of outer array dimensions to ignore. For constant
   * buffers, treat the entire buffer as a single structure, otherwise assume
   * that the first array dimension, if any is present, can be ignored. */
  info.oldType = op.getType();

  if (info.oldType.isArrayType() && op.getOpCode() != OpCode::eDclCbv)
    info.oldOuterArrayDims = 1u;

  uint32_t dwordCount = info.getOldInnerType().byteSize() / sizeof(uint32_t);
  info.elements.resize(dwordCount);

  /* Iterate over uses to work out accessed element types */
  determineElementTypeForUses(op, info);

  switch (op.getOpCode()) {
    case OpCode::eDclLds: {
      info.processLocalLayout(m_options.flattenLds, m_options.allowSubDwordScratchAndLds,
        !m_options.flattenLds && canRemoveTrivialArrays(op));
    } break;

    case OpCode::eConstant: {
      info.processLocalLayout(false, true, false);
    } break;

    case OpCode::eDclScratch: {
      info.processLocalLayout(m_options.flattenScratch, m_options.allowSubDwordScratchAndLds, false);
    } break;

    case OpCode::eDclCbv: {
      info.processConstantBufferLayout(m_options.structuredCbv);
    } break;

    case OpCode::eDclSrv:
    case OpCode::eDclUav: {
      auto kind = ResourceKind(op.getOperand(op.getFirstLiteralOperandIndex() + 3u));
      info.processResourceBufferLayout(m_options.structuredSrvUav && kind == ResourceKind::eBufferStructured);
    } break;

    default:
      dxbc_spv_unreachable();
  }
}


void PropagateResourceTypesPass::rewriteAccessOps(SsaDef def, const PropagateResourceTypeRewriteInfo& info) {
  util::small_vector<SsaDef, 256u> uses;
  m_builder.getUses(def, uses);

  for (auto use : uses) {
    switch (m_builder.getOp(use).getOpCode()) {
      case OpCode::eLdsLoad:
      case OpCode::eBufferLoad:
      case OpCode::eScratchLoad:
      case OpCode::eConstantLoad:
        rewriteLoad(use, info);
        break;

      case OpCode::eLdsStore:
      case OpCode::eBufferStore:
      case OpCode::eScratchStore:
        rewriteStore(use, info);
        break;

      case OpCode::eLdsAtomic:
      case OpCode::eBufferAtomic:
        rewriteAtomic(use, info);
        break;

      case OpCode::eDescriptorLoad:
        rewriteAccessOps(use, info);
        break;

      default:
        break;
    }
  }
}


void PropagateResourceTypesPass::rewriteDeclaration(SsaDef def, const PropagateResourceTypeRewriteInfo& info) {
  rewriteAccessOps(def, info);

  const auto& op = m_builder.getOp(def);

  if (op.isConstant()) {
    /* For constants, each incoming dword corresponds to one constant
     * operand. Simply copy used operands over and replace the op. */
    Op constant(OpCode::eConstant, info.newType);

    uint32_t scalarCount = info.elements.size();
    uint32_t flattenedArraySize = 1u;

    for (uint32_t i = 0u; i < info.newOuterArrayDims; i++)
      flattenedArraySize *= info.newType.getArraySize(i);

    for (uint32_t i = 0u; i < flattenedArraySize; i++) {
      for (uint32_t j = 0u; j < scalarCount; j++) {
        if (info.elements[j].isUsed())
          constant.addOperand(op.getOperand(scalarCount * i + j));
      }
    }

    m_builder.rewriteDef(def, m_builder.add(std::move(constant)));
  } else {
    /* Simply change the type */
    m_builder.setOpType(def, info.newType);
  }
}


void PropagateResourceTypesPass::rewritePartialVectorLoad(const Type& type, SsaDef def) {
  auto loadOp = m_builder.getOp(def);
  auto loadType = loadOp.getType();

  /* Sparse feedback is being obnoxious *again* so figure out the
   * actual type of the load first. */
  bool hasSparseFeedback = bool(loadOp.getFlags() & OpFlag::eSparseFeedback);

  if (hasSparseFeedback)
    loadType = loadType.getSubType(1u);

  /* Scalar loads are unconditionally fine */
  dxbc_spv_assert(loadType.isBasicType());

  if (loadType.isScalarType())
    return;

  /* Traverse type to see what we're actually loading from. Explicitly
   * allow the case where we load a vector from a scalar array. */
  const auto& addressOp = m_builder.getOpForOperand(loadOp, 1u);
  dxbc_spv_assert(addressOp.getType().isBasicType());

  auto addressComponents = addressOp.getType().getBaseType(0u).getVectorSize();
  auto arrayDimensions = type.getArrayDimensions();

  if (addressComponents <= arrayDimensions)
    return;

  auto resourceType = type;

  for (uint32_t i = 0u; i < arrayDimensions; i++)
    resourceType = resourceType.getSubType(0u);

  if (resourceType.isScalarType()) {
    dxbc_spv_assert(addressComponents == arrayDimensions);
    return;
  }

  /* Traverse structure until the second to last level. If that is a
   * vector that's being indexed into, we need to rewrite the load. */
  for (uint32_t i = arrayDimensions; i + 1u < addressComponents; i++) {
    auto index = extractConstantFromAddress(addressOp.getDef(), i);
    dxbc_spv_assert(index);

    resourceType = resourceType.getSubType(*index);
  }

  if (!resourceType.isVectorType()) {
    dxbc_spv_assert(resourceType.isStructType());
    return;
  }

  /* Build new address op, omitting the index into the vector */
  Op newAddressOp(OpCode::eCompositeConstruct, Type(ScalarType::eU32, addressComponents - 1u));

  for (uint32_t i = 0u; i + 1u < addressComponents; i++)
    newAddressOp.addOperand(extractIndexFromAddress(addressOp.getDef(), i));

  ir::SsaDef addressDef = { };

  if (newAddressOp.getOperandCount() == 1u)
    addressDef = SsaDef(newAddressOp.getOperand(0u));
  else if (newAddressOp.getOperandCount() > 1u)
    addressDef = m_builder.addBefore(def, std::move(newAddressOp));

  loadOp.setOperand(1u, addressDef);

  /* Determine properties of the actual vector being loaded */
  auto scalarType = loadType.getBaseType(0u).getBaseType();

  auto desiredCount = loadType.getBaseType(0u).getVectorSize();
  auto desiredIndex = extractConstantFromAddress(addressOp.getDef(), addressComponents - 1u);

  auto baseVectorSize = resourceType.getBaseType(0u).getVectorSize();

  dxbc_spv_assert(desiredIndex);

  /* If necessary, wrap the whole thing in a sparse feedback struct */
  BasicType newLoadType(scalarType, baseVectorSize);

  loadOp.setType(hasSparseFeedback
    ? Type().addStructMember(ScalarType::eU32).addStructMember(newLoadType)
    : Type().addStructMember(newLoadType));

  /* TODO restore aligment info? */
  loadOp.setOperand(loadOp.getFirstLiteralOperandIndex(), Operand(byteSize(scalarType)));

  auto loadDef = m_builder.addBefore(def, std::move(loadOp));

  /* Extract requested components only */
  Op swizzleOp(OpCode::eCompositeConstruct, loadType);

  for (uint32_t i = 0u; i < desiredCount; i++) {
    swizzleOp.addOperand(m_builder.addBefore(def,
      Op::CompositeExtract(scalarType, loadDef, hasSparseFeedback
        ? m_builder.makeConstant(1u, *desiredIndex + i)
        : m_builder.makeConstant(*desiredIndex + i))));
  }

  /* If we have sparse feedback, rebuild the feedback struct */
  if (hasSparseFeedback) {
    auto feedback = m_builder.addBefore(def, Op::CompositeExtract(
      ScalarType::eU32, loadDef, m_builder.makeConstant(0u)));

    auto dataDef = m_builder.addBefore(def, std::move(swizzleOp));

    swizzleOp = Op::CompositeConstruct(loadOp.getType(), feedback, dataDef);
  }

  /* Replace load op with the composite op */
  m_builder.rewriteOp(def, std::move(swizzleOp));
}


void PropagateResourceTypesPass::rewritePartialVectorLoadsForDescriptor(const Type& type, SsaDef def) {
  dxbc_spv_assert(m_builder.getOp(def).getOpCode() == OpCode::eDescriptorLoad);

  util::small_vector<SsaDef, 256u> uses;
  m_builder.getUses(def, uses);

  for (auto use : uses) {
    if (m_builder.getOp(use).getOpCode() == OpCode::eBufferLoad)
      rewritePartialVectorLoad(type, use);
  }
}


bool PropagateResourceTypesPass::canRemoveTrivialArrays(const Op& op) {
  /* Check whether all indices into an array dimension with a size of
   * 1 are constant zero. In that case, we can remove the dimension. */
  uint32_t dims = op.getType().getArrayDimensions();
  uint32_t size1 = 0;

  while (size1 < dims && op.getType().getArraySize(dims - size1 - 1u) == 1u)
    size1++;

  if (!size1)
    return false;

  auto [a, b] = m_builder.getUses(op.getDef());

  for (auto iter = a; iter != b; iter++) {
    switch (iter->getOpCode()) {
      case OpCode::eLdsLoad:
      case OpCode::eLdsAtomic:
      case OpCode::eLdsStore:
      case OpCode::eScratchLoad:
      case OpCode::eScratchStore:
      case OpCode::eConstantLoad: {
        const auto& indexOp = m_builder.getOpForOperand(*iter, 1u);

        if (indexOp.isConstant()) {
          for (uint32_t i = 0u; i < size1; i++) {
            if (uint32_t(indexOp.getOperand(i)))
              return false;
          }
        } else if (indexOp.getOpCode() == OpCode::eCompositeConstruct) {
          for (uint32_t i = 0u; i < size1; i++) {
            const auto& arg = m_builder.getOpForOperand(indexOp, i);

            if (!arg.isConstant() || uint32_t(arg.getOperand(0u)))
              return false;
          }
        } else {
          return false;
        }
      } break;

      default:
        break;
    }
  }

  return true;
}


bool PropagateResourceTypesPass::isUntypedDeclaration(const Op& op) {
  auto type = op.getType();

  switch (op.getOpCode()) {
    case OpCode::eConstant: {
      /* If we're here, the constant must be dynamically indexed */
      return type.isArrayType() && type.getBaseType(0u).isUnknownType();
    }

    case OpCode::eDclSrv:
    case OpCode::eDclUav: {
      auto kind = ResourceKind(op.getOperand(op.getFirstLiteralOperandIndex() + 3u));

      if (kind != ResourceKind::eBufferRaw && kind != ResourceKind::eBufferStructured)
        return false;
    } [[fallthrough]];

    case OpCode::eDclCbv:
    case OpCode::eDclLds:
    case OpCode::eDclScratch: {
      while (type.isArrayType())
        type = type.getSubType(0u);

      return type.isBasicType() && type.getBaseType(0u).isUnknownType();
    }

    default:
      return false;
  }
}

}
