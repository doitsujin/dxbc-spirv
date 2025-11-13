#include "ir.h"

#include <algorithm>
#include <iomanip>

namespace dxbc_spv::ir {

Type& Type::addStructMember(BasicType type) {
  dxbc_spv_assert(!type.isVoidType());

  m_members.push_back(type);
  return *this;
}


Type& Type::addArrayDimension(uint32_t size) {
  m_sizes.at(m_dimensions++) = size;
  return *this;
}


Type Type::getSubType(uint32_t index) const {
  if (isArrayType()) {
    Type result = *this;
    result.m_dimensions -= 1u;
    return result;
  }

  if (isStructType())
    return Type(getBaseType(index));

  BasicType base = getBaseType(0u);

  if (base.isVector())
    return Type(base.getBaseType());

  return Type();
}


ScalarType Type::resolveFlattenedType(uint32_t index) const {
  uint32_t componentCount = 0u;

  for (uint32_t i = 0u; i < m_members.size(); i++)
    componentCount += getBaseType(i).getVectorSize();

  index %= componentCount;

  uint32_t start = 0u;

  for (uint32_t i = 0u; i < m_members.size(); i++) {
    auto type = getBaseType(i);

    if (start + type.getVectorSize() > index)
      return type.getBaseType();

    start += type.getVectorSize();
  }

  return ScalarType::eVoid;
}


uint32_t Type::computeFlattenedScalarCount() const {
  uint32_t componentCount = 0u;

  for (uint32_t i = 0u; i < m_members.size(); i++)
    componentCount += getBaseType(i).getVectorSize();

  for (uint32_t i = 0u; i < m_dimensions; i++)
    componentCount *= getArraySize(i);

  return componentCount;
}


uint32_t Type::computeTopLevelMemberCount() const {
  if (isArrayType())
    return getArraySize(m_dimensions - 1u);

  if (isStructType())
    return getStructMemberCount();

  if (isVoidType())
    return 0u;

  return getBaseType(0u).getVectorSize();
}


uint32_t Type::computeScalarIndex(uint32_t member) const {
  if (isArrayType())
    return member * getSubType(0u).computeFlattenedScalarCount();

  if (isStructType()) {
    uint32_t base = 0u;

    for (uint32_t i = 0u; i < member; i++)
      base += getBaseType(i).getVectorSize();

    return base;
  }

  if (isVectorType())
    return member;

  if (isScalarType()) {
    dxbc_spv_assert(!member);
    return 0u;
  }

  dxbc_spv_unreachable();
  return 0u;
}


uint32_t Type::byteSize() const {
  uint32_t alignment = 0u;
  uint32_t size = 0u;

  for (uint32_t i = 0u; i < m_members.size(); i++) {
    uint32_t memberAlignment = getBaseType(i).byteAlignment();

    size = util::align(size, memberAlignment);
    size += getBaseType(i).byteSize();

    alignment = std::max(alignment, memberAlignment);
  }

  size = util::align(size, alignment);

  for (uint32_t i = 0u; i < m_dimensions; i++) {
    if (getArraySize(i) || i + 1u < m_dimensions)
      size *= getArraySize(i);
  }

  return size;
}


uint32_t Type::byteAlignment() const {
  uint32_t alignment = 0u;

  for (uint32_t i = 0u; i < m_members.size(); i++)
    alignment = std::max(alignment, getBaseType(i).byteAlignment());

  return alignment;
}


uint32_t Type::byteOffset(uint32_t member) const {
  if (isArrayType())
    return member * getSubType(0u).byteSize();

  if (isStructType()) {
    dxbc_spv_assert(member < m_members.size());

    uint32_t offset = 0u;

    for (uint32_t i = 0u; i < member; i++) {
      uint32_t memberAlignment = getBaseType(i).byteAlignment();

      offset = util::align(offset, memberAlignment);
      offset += getBaseType(i).byteSize();
    }

    uint32_t memberAlignment = getBaseType(member).byteAlignment();
    return util::align(offset, memberAlignment);
  }

  if (isVectorType()) {
    dxbc_spv_assert(member < getBaseType(0u).getVectorSize());
    return member * ir::byteSize(getBaseType(0u).getBaseType());
  }

  if (isScalarType()) {
    dxbc_spv_assert(!member);
    return 0u;
  }

  dxbc_spv_unreachable(0u);
  return 0u;
}


bool Type::operator == (const Type& other) const {
  bool eq = m_dimensions == other.m_dimensions
         && m_members.size() == other.m_members.size();

  for (uint32_t i = 0; i < m_dimensions && eq; i++)
    eq = m_sizes[i] == other.m_sizes[i];

  for (uint32_t i = 0; i < m_members.size() && eq; i++)
    eq = m_members[i] == other.m_members[i];

  return eq;
}


bool Type::operator != (const Type& other) const {
  return !(this->operator == (other));
}


bool Operand::getToString(std::string& str) const {
  uint64_t lit = m_data;

  for (uint32_t i = 0u; i < sizeof(lit); i++) {
    char ch = char((lit >> (8u * i)) & 0xffu);

    if (!ch)
      return false;

    str += ch;
  }

  return true;
}


Op& Op::addLiteralString(const char* string) {
  uint64_t lit = uint8_t(string[0u]);

  for (size_t i = 1u; string[i]; i++) {
    if (!(i % 8u)) {
      addOperand(Operand(lit));
      lit = uint8_t(string[i]);
    } else {
      lit |= uint64_t(uint8_t(string[i])) << (8u * (i % 8u));
    }
  }

  addOperand(Operand(lit));
  return *this;
}


std::string Op::getLiteralString(uint32_t index) const {
  std::string str;

  for (uint32_t i = index; i < getOperandCount(); i++)
    getOperand(i).getToString(str);

  return str;
}


bool Op::isEquivalent(const Op& other) const {
  bool eq = m_opCode == other.m_opCode && m_flags == other.m_flags &&
    m_resultType == other.m_resultType &&
    m_operands.size() == other.m_operands.size();

  for (size_t i = 0u; i < m_operands.size() && eq; i++)
    eq = m_operands[i] == other.m_operands[i];

  return eq;
}


uint32_t Op::getFirstLiteralOperandIndex() const {
  switch (m_opCode) {
    case OpCode::eConstant:
    case OpCode::eBarrier:
    case OpCode::eEmitVertex:
    case OpCode::eEmitPrimitive:
    case OpCode::eRovScopedLockBegin:
    case OpCode::eRovScopedLockEnd:
      return 0u;

    case OpCode::eSemantic:
    case OpCode::eDebugName:
    case OpCode::eDebugMemberName:
    case OpCode::eSetCsWorkgroupSize:
    case OpCode::eSetGsInstances:
    case OpCode::eSetGsInputPrimitive:
    case OpCode::eSetGsOutputVertices:
    case OpCode::eSetGsOutputPrimitive:
    case OpCode::eSetTessPrimitive:
    case OpCode::eSetTessDomain:
    case OpCode::eSetTessControlPoints:
    case OpCode::eSetFpMode:
    case OpCode::eDclInput:
    case OpCode::eDclInputBuiltIn:
    case OpCode::eDclOutput:
    case OpCode::eDclOutputBuiltIn:
    case OpCode::eDclSpecConstant:
    case OpCode::eDclPushData:
    case OpCode::eDclSampler:
    case OpCode::eDclCbv:
    case OpCode::eDclSrv:
    case OpCode::eDclUav:
    case OpCode::eDclXfb:
    case OpCode::eDerivX:
    case OpCode::eDerivY:
    case OpCode::eFRound:
    case OpCode::ePointer:
    case OpCode::eScopedSwitchCase:
      return 1u;

    case OpCode::eBufferLoad:
    case OpCode::eMemoryLoad:
      return 2u;

    case OpCode::eBufferStore:
    case OpCode::eMemoryStore:
      return 3u;

    case OpCode::eEntryPoint:
    case OpCode::eLabel:
    case OpCode::eLdsAtomic:
    case OpCode::eBufferAtomic:
    case OpCode::eImageAtomic:
    case OpCode::eCounterAtomic:
    case OpCode::eMemoryAtomic:
      return getOperandCount() ? getOperandCount() - 1u : 0u;

    case OpCode::eImageGather:
      return 6u;

    default:
      return getOperandCount();
  }
}


std::ostream& operator << (std::ostream& os, const ScalarType& ty) {
  switch (ty) {
    case ScalarType::eVoid:       return os << "void";
    case ScalarType::eUnknown:    return os << "?";
    case ScalarType::eBool:       return os << "bool";

    case ScalarType::eI8:         return os << "i8";
    case ScalarType::eI16:        return os << "i16";
    case ScalarType::eI32:        return os << "i32";
    case ScalarType::eI64:        return os << "i64";

    case ScalarType::eU8:         return os << "u8";
    case ScalarType::eU16:        return os << "u16";
    case ScalarType::eU32:        return os << "u32";
    case ScalarType::eU64:        return os << "u64";

    case ScalarType::eF16:        return os << "f16";
    case ScalarType::eF32:        return os << "f32";
    case ScalarType::eF64:        return os << "f64";

    case ScalarType::eMinI16:     return os << "mini16";
    case ScalarType::eMinU16:     return os << "minu16";
    case ScalarType::eMinF16:     return os << "minf16";
    case ScalarType::eMinF10:     return os << "minf10";

    case ScalarType::eSampler:    return os << "sampler";
    case ScalarType::eCbv:        return os << "cbv";
    case ScalarType::eSrv:        return os << "srv";
    case ScalarType::eUav:        return os << "uav";
    case ScalarType::eUavCounter: return os << "uav_ctr";

    case ScalarType::eCount: break;
  }

  return os << "ScalarType(" << std::dec << uint32_t(ty) << ")";
}


std::ostream& operator << (std::ostream& os, const BasicType& ty) {
  if (ty.isVector())
    os << "vec" << ty.getVectorSize() << '<';

  os << ty.getBaseType();

  if (ty.isVector())
    os << '>';

  return os;
}


std::ostream& operator << (std::ostream& os, const Type& ty) {
  if (ty.getStructMemberCount() > 1u) {
    os << '{' << ty.getBaseType(0u);

    for (uint32_t i = 1u; i < ty.getStructMemberCount(); i++)
      os << ',' << ty.getBaseType(i);

    os << '}';
  } else {
    os << ty.getBaseType(0u);
  }

  for (uint32_t i = 0u; i < ty.getArrayDimensions(); i++) {
    uint32_t size = ty.getArraySize(i);

    if (size || i + 1u < ty.getArrayDimensions())
      os << '[' << size << ']';
    else
      os << "[]";
  }

  return os;
}


std::ostream& operator << (std::ostream& os, const Construct& construct) {
  switch (construct) {
    case Construct::eNone:                return os << "None";
    case Construct::eStructuredSelection: return os << "StructuredSelection";
    case Construct::eStructuredLoop:      return os << "StructuredLoop";
  }

  return os << "Construct(" << std::dec << uint32_t(construct) << ")";
}


std::ostream& operator << (std::ostream& os, const ResourceKind& kind) {
  switch (kind) {
    case ResourceKind::eBufferTyped:      return os << "BufferTyped";
    case ResourceKind::eBufferStructured: return os << "BufferStructured";
    case ResourceKind::eBufferRaw:        return os << "BufferRaw";
    case ResourceKind::eImage1D:          return os << "Image1D";
    case ResourceKind::eImage1DArray:     return os << "Image1DArray";
    case ResourceKind::eImage2D:          return os << "Image2D";
    case ResourceKind::eImage2DArray:     return os << "Image2DArray";
    case ResourceKind::eImage2DMS:        return os << "Image2DMS";
    case ResourceKind::eImage2DMSArray:   return os << "Image2DMSArray";
    case ResourceKind::eImageCube:        return os << "ImageCube";
    case ResourceKind::eImageCubeArray:   return os << "ImageCubeArray";
    case ResourceKind::eImage3D:          return os << "Image3D";
  }

  return os << "ResourceKind(" << std::dec << uint32_t(kind) << ")";
}


std::ostream& operator << (std::ostream& os, const PrimitiveType& primitive) {
  switch (primitive) {
    case PrimitiveType::ePoints:        return os << "Points";
    case PrimitiveType::eLines:         return os << "Lines";
    case PrimitiveType::eLinesAdj:      return os << "LinesAdj";
    case PrimitiveType::eTriangles:     return os << "Triangles";
    case PrimitiveType::eTrianglesAdj:  return os << "TrianglesAdj";
    case PrimitiveType::eQuads:         return os << "Quads";

    case PrimitiveType::ePatch: break;
  }

  uint32_t patchVertexCount = uint32_t(primitive) - uint32_t(PrimitiveType::ePatch);

  if (patchVertexCount >= 1u && patchVertexCount <= 32u)
    return os << "Patch<" << patchVertexCount << ">";

  return os << "PrimitiveType(" << std::dec << uint32_t(primitive) << ")";
}


std::ostream& operator << (std::ostream& os, const TessWindingOrder& winding) {
  switch (winding) {
    case TessWindingOrder::eCcw:  return os << "Ccw";
    case TessWindingOrder::eCw:   return os << "Cw";
  }

  return os << "TessWindingOrder(" << std::dec << uint32_t(winding) << ")";
}


std::ostream& operator << (std::ostream& os, const TessPartitioning& partitioning) {
  switch (partitioning) {
    case TessPartitioning::eInteger:    return os << "Integer";
    case TessPartitioning::eFractOdd:   return os << "FractOdd";
    case TessPartitioning::eFractEven:  return os << "FractEven";
  }

  return os << "TessPartitioning(" << uint32_t(partitioning) << ")";
}


std::ostream& operator << (std::ostream& os, const BuiltIn& builtIn) {
  switch (builtIn) {
    case BuiltIn::ePosition:            return os << "Position";
    case BuiltIn::eClipDistance:        return os << "ClipDistance";
    case BuiltIn::eCullDistance:        return os << "CullDistance";
    case BuiltIn::eVertexId:            return os << "VertexId";
    case BuiltIn::eInstanceId:          return os << "InstanceId";
    case BuiltIn::ePrimitiveId:         return os << "PrimitiveId";
    case BuiltIn::eLayerIndex:          return os << "LayerIndex";
    case BuiltIn::eViewportIndex:       return os << "ViewportIndex";
    case BuiltIn::eGsVertexCountIn:     return os << "eGsVertexCountIn";
    case BuiltIn::eGsInstanceId:        return os << "GsInstanceId";
    case BuiltIn::eTessControlPointCountIn: return os << "TessControlPointCountIn";
    case BuiltIn::eTessControlPointId:  return os << "TessControlPointId";
    case BuiltIn::eTessCoord:           return os << "TessCoord";
    case BuiltIn::eTessFactorInner:     return os << "TessFactorInner";
    case BuiltIn::eTessFactorOuter:     return os << "TessFactorOuter";
    case BuiltIn::eSampleCount:         return os << "SampleCount";
    case BuiltIn::eSampleId:            return os << "SampleId";
    case BuiltIn::eSamplePosition:      return os << "SamplePosition";
    case BuiltIn::eSampleMask:          return os << "SampleMask";
    case BuiltIn::eIsFrontFace:         return os << "IsFrontFace";
    case BuiltIn::eDepth:               return os << "Depth";
    case BuiltIn::eStencilRef:          return os << "StencilRef";
    case BuiltIn::eIsFullyCovered:      return os << "IsFullyCovered";
    case BuiltIn::eWorkgroupId:         return os << "WorkgroupId";
    case BuiltIn::eGlobalThreadId:      return os << "GlobalThreadId";
    case BuiltIn::eLocalThreadId:       return os << "LocalThreadId";
    case BuiltIn::eLocalThreadIndex:    return os << "LocalThreadIndex";
    case BuiltIn::ePointSize:           return os << "PointSize";
  }

  return os << "BuiltIn(" << uint32_t(builtIn) << ")";
}


std::ostream& operator << (std::ostream& os, const AtomicOp& atomicOp) {
  switch (atomicOp) {
    case AtomicOp::eLoad:               return os << "Load";
    case AtomicOp::eStore:              return os << "Store";
    case AtomicOp::eExchange:           return os << "Exchange";
    case AtomicOp::eCompareExchange:    return os << "CompareExchange";
    case AtomicOp::eAdd:                return os << "Add";
    case AtomicOp::eSub:                return os << "Sub";
    case AtomicOp::eSMin:               return os << "SMin";
    case AtomicOp::eSMax:               return os << "SMax";
    case AtomicOp::eUMin:               return os << "UMin";
    case AtomicOp::eUMax:               return os << "UMax";
    case AtomicOp::eAnd:                return os << "And";
    case AtomicOp::eOr:                 return os << "Or";
    case AtomicOp::eXor:                return os << "Xor";
    case AtomicOp::eInc:                return os << "Inc";
    case AtomicOp::eDec:                return os << "dec";
  }

  return os << "AtomicOp(" << std::dec << uint32_t(atomicOp) << ")";
}


std::ostream& operator << (std::ostream& os, const InterpolationMode& mode) {
  switch (mode) {
    case InterpolationMode::eFlat:          return os << "Flat";
    case InterpolationMode::eCentroid:      return os << "Centroid";
    case InterpolationMode::eSample:        return os << "Sample";
    case InterpolationMode::eNoPerspective: return os << "NoPerspective";

    case InterpolationMode::eFlagEnum: break;
  }

  return os << "InterpolationMode(" << std::dec << uint32_t(mode) << ")";
}


std::ostream& operator << (std::ostream& os, const UavFlag& flag) {
  switch (flag) {
    case UavFlag::eCoherent:          return os << "Coherent";
    case UavFlag::eReadOnly:          return os << "ReadOnly";
    case UavFlag::eWriteOnly:         return os << "WriteOnly";
    case UavFlag::eRasterizerOrdered: return os << "RasterizerOrdered";
    case UavFlag::eFixedFormat:       return os << "FixedFormat";

    case UavFlag::eFlagEnum: break;
  }

  return os << "UavFlag(" << std::dec << uint32_t(flag) << ")";
}


std::ostream& operator << (std::ostream& os, const ShaderStage& stage) {
  switch (stage) {
    case ShaderStage::ePixel:     return os << "Pixel";
    case ShaderStage::eVertex:    return os << "Vertex";
    case ShaderStage::eGeometry:  return os << "Geometry";
    case ShaderStage::eHull:      return os << "Hull";
    case ShaderStage::eDomain:    return os << "Domain";
    case ShaderStage::eCompute:   return os << "Compute";

    case ShaderStage::eFlagEnum: break;
  }

  return os << "ShaderStage(" << std::dec << uint32_t(stage) << ")";
}


std::ostream& operator << (std::ostream& os, const Scope& scope) {
  switch (scope) {
    case Scope::eThread:    return os << "Thread";
    case Scope::eQuad:      return os << "Quad";
    case Scope::eSubgroup:  return os << "Subgroup";
    case Scope::eWorkgroup: return os << "Workgroup";
    case Scope::eGlobal:    return os << "Global";
  }

  return os << "Scope(" << std::dec << uint32_t(scope) << ")";
}


std::ostream& operator << (std::ostream& os, const MemoryType& type) {
  switch (type) {
    case MemoryType::eLds:        return os << "Lds";
    case MemoryType::eUav:        return os << "Uav";

    case MemoryType::eFlagEnum: break;
  }

  return os << "MemoryType(" << std::dec << uint32_t(type) << ")";
}


std::ostream& operator << (std::ostream& os, const DerivativeMode& mode) {
  switch (mode) {
    case DerivativeMode::eDefault:  return os << "Default";
    case DerivativeMode::eCoarse:   return os << "Coarse";
    case DerivativeMode::eFine:     return os << "Fine";
  }

  return os << "DerivativeMode(" << std::dec << uint32_t(mode) << ")";
}


std::ostream& operator << (std::ostream& os, const RovScope& scope) {
  switch (scope) {
    case RovScope::eSample:   return os << "Sample";
    case RovScope::ePixel:    return os << "Pixel";
    case RovScope::eVrsBlock: return os << "VrsBlock";
    case RovScope::eFlagEnum: break;
  }

  return os << "RovScope(" << std::dec << uint32_t(scope) << ")";
}


std::ostream& operator << (std::ostream& os, const RoundMode& mode) {
  switch (mode) {
    case RoundMode::eZero:        return os << "Zero";
    case RoundMode::eNearestEven: return os << "NearestEven";
    case RoundMode::eNegativeInf: return os << "NegativeInf";
    case RoundMode::ePositiveInf: return os << "PositiveInf";

    case RoundMode::eFlagEnum: break;
  }

  return os << "RoundMode(" << std::dec << uint32_t(mode) << ")";
}


std::ostream& operator << (std::ostream& os, const DenormMode& mode) {
  switch (mode) {
    case DenormMode::eFlush:    return os << "Flush";
    case DenormMode::ePreserve: return os << "Preserve";

    case DenormMode::eFlagEnum: break;
  }

  return os << "DenormMode(" << std::dec << uint32_t(mode) << ")";
}


std::ostream& operator << (std::ostream& os, const SsaDef& def) {
  if (!def)
    return os << "null";

  return os << '%' << def.getId();
}


std::ostream& operator << (std::ostream& os, const OpFlag& flag) {
  switch (flag) {
    case OpFlag::ePrecise:        return os << "precise";
    case OpFlag::eInvariant:      return os << "invariant";
    case OpFlag::eNonUniform:     return os << "nonuniform";
    case OpFlag::eSparseFeedback: return os << "sparsefeedback";
    case OpFlag::eNoNan:          return os << "nonan";
    case OpFlag::eNoInf:          return os << "noinf";
    case OpFlag::eNoSz:           return os << "nosz";
    case OpFlag::eInBounds:       return os << "inbounds";

    case OpFlag::eFlagEnum: break;
  }

  return os << "OpFlag(" << std::dec << uint32_t(flag) << ")";
}


std::ostream& operator << (std::ostream& os, const OpCode& opCode) {
  switch (opCode) {
    case OpCode::eUnknown:
    case OpCode::eLastDeclarative:
    case OpCode::Count:
      break;

    case OpCode::eEntryPoint: return os << "EntryPoint";
    case OpCode::eSemantic: return os << "Semantic";
    case OpCode::eDebugName: return os << "DebugName";
    case OpCode::eDebugMemberName: return os << "DebugMemberName";
    case OpCode::eConstant: return os << "Constant";
    case OpCode::eUndef: return os << "Undef";
    case OpCode::eSetCsWorkgroupSize: return os << "SetCsWorkgroupSize";
    case OpCode::eSetGsInstances: return os << "SetGsInstances";
    case OpCode::eSetGsInputPrimitive: return os << "SetGsInputPrimitive";
    case OpCode::eSetGsOutputVertices: return os << "SetGsOutputVertices";
    case OpCode::eSetGsOutputPrimitive: return os << "SetGsOutputPrimitive";
    case OpCode::eSetPsEarlyFragmentTest: return os << "SetPsEarlyFragmentTest";
    case OpCode::eSetPsDepthGreaterEqual: return os << "SetPsDepthGreaterEqual";
    case OpCode::eSetPsDepthLessEqual: return os << "SetPsDepthLessEqual";
    case OpCode::eSetTessPrimitive: return os << "SetTessPrimitive";
    case OpCode::eSetTessDomain: return os << "SetTessDomain";
    case OpCode::eSetTessControlPoints: return os << "SetTessControlPoints";
    case OpCode::eSetFpMode: return os << "SetFpMode";
    case OpCode::eDclInput: return os << "DclInput";
    case OpCode::eDclInputBuiltIn: return os << "DclInputBuiltIn";
    case OpCode::eDclOutput: return os << "DclOutput";
    case OpCode::eDclOutputBuiltIn: return os << "DclOutputBuiltIn";
    case OpCode::eDclSpecConstant: return os << "DclSpecConstant";
    case OpCode::eDclPushData: return os << "DclPushData";
    case OpCode::eDclSampler: return os << "DclSampler";
    case OpCode::eDclCbv: return os << "DclCbv";
    case OpCode::eDclSrv: return os << "DclSrv";
    case OpCode::eDclUav: return os << "DclUav";
    case OpCode::eDclUavCounter: return os << "DclUavCounter";
    case OpCode::eDclLds: return os << "DclLds";
    case OpCode::eDclScratch: return os << "DclScratch";
    case OpCode::eDclTmp: return os << "DclTmp";
    case OpCode::eDclParam: return os << "DclParam";
    case OpCode::eDclXfb: return os << "DclXfb";
    case OpCode::eFunction: return os << "Function";
    case OpCode::eFunctionEnd: return os << "FunctionEnd";
    case OpCode::eFunctionCall: return os << "FunctionCall";
    case OpCode::eLabel: return os << "Label";
    case OpCode::eBranch: return os << "Branch";
    case OpCode::eBranchConditional: return os << "BranchConditional";
    case OpCode::eSwitch: return os << "Switch";
    case OpCode::eUnreachable: return os << "Unreachable";
    case OpCode::eReturn: return os << "Return";
    case OpCode::ePhi: return os << "Phi";
    case OpCode::eScopedIf: return os << "ScopedIf";
    case OpCode::eScopedElse: return os << "ScopedElse";
    case OpCode::eScopedEndIf: return os << "ScopedEndIf";
    case OpCode::eScopedLoop: return os << "ScopedLoop";
    case OpCode::eScopedLoopBreak: return os << "ScopedLoopBreak";
    case OpCode::eScopedLoopContinue: return os << "ScopedLoopContinue";
    case OpCode::eScopedEndLoop: return os << "ScopedEndLoop";
    case OpCode::eScopedSwitch: return os << "ScopedSwitch";
    case OpCode::eScopedSwitchCase: return os << "ScopedSwitchCase";
    case OpCode::eScopedSwitchDefault: return os << "ScopedSwitchDefault";
    case OpCode::eScopedSwitchBreak: return os << "ScopedSwitchBreak";
    case OpCode::eScopedEndSwitch: return os << "ScopedEndSwitch";
    case OpCode::eBarrier: return os << "Barrier";
    case OpCode::eConvertFtoF: return os << "ConvertFtoF";
    case OpCode::eConvertFtoI: return os << "ConvertFtoI";
    case OpCode::eConvertItoF: return os << "ConvertItoF";
    case OpCode::eConvertItoI: return os << "ConvertItoI";
    case OpCode::eConvertF32toPackedF16: return os << "ConvertF32toPackedF16";
    case OpCode::eConvertPackedF16toF32: return os << "ConvertPackedF16toF32";
    case OpCode::eCast: return os << "Cast";
    case OpCode::eConsumeAs: return os << "ConsumeAs";
    case OpCode::eCompositeInsert: return os << "CompositeInsert";
    case OpCode::eCompositeExtract: return os << "CompositeExtract";
    case OpCode::eCompositeConstruct: return os << "CompositeConstruct";
    case OpCode::eCheckSparseAccess: return os << "CheckSparseAccess";
    case OpCode::eParamLoad: return os << "ParamLoad";
    case OpCode::eTmpLoad: return os << "TmpLoad";
    case OpCode::eTmpStore: return os << "TmpStore";
    case OpCode::eScratchLoad: return os << "ScratchLoad";
    case OpCode::eScratchStore: return os << "ScratchStore";
    case OpCode::eLdsLoad: return os << "LdsLoad";
    case OpCode::eLdsStore: return os << "LdsStore";
    case OpCode::ePushDataLoad: return os << "PushDataLoad";
    case OpCode::eInputLoad: return os << "InputLoad";
    case OpCode::eOutputLoad: return os << "OutputLoad";
    case OpCode::eOutputStore: return os << "OutputStore";
    case OpCode::eDescriptorLoad: return os << "DescriptorLoad";
    case OpCode::eBufferLoad: return os << "BufferLoad";
    case OpCode::eBufferStore: return os << "BufferStore";
    case OpCode::eBufferQuerySize: return os << "BufferQuerySize";
    case OpCode::eMemoryLoad: return os << "MemoryLoad";
    case OpCode::eMemoryStore: return os << "MemoryStore";
    case OpCode::eConstantLoad: return os << "ConstantLoad";
    case OpCode::eLdsAtomic: return os << "LdsAtomic";
    case OpCode::eBufferAtomic: return os << "BufferAtomic";
    case OpCode::eImageAtomic: return os << "ImageAtomic";
    case OpCode::eCounterAtomic: return os << "CounterAtomic";
    case OpCode::eMemoryAtomic: return os << "MemoryAtomic";
    case OpCode::eImageLoad: return os << "ImageLoad";
    case OpCode::eImageStore: return os << "ImageStore";
    case OpCode::eImageQuerySize: return os << "ImageQuerySize";
    case OpCode::eImageQueryMips: return os << "ImageQueryMips";
    case OpCode::eImageQuerySamples: return os << "ImageQuerySamples";
    case OpCode::eImageSample: return os << "ImageSample";
    case OpCode::eImageGather: return os << "ImageGather";
    case OpCode::eImageComputeLod: return os << "ImageComputeLod";
    case OpCode::ePointer: return os << "Pointer";
    case OpCode::eEmitVertex: return os << "EmitVertex";
    case OpCode::eEmitPrimitive: return os << "EmitPrimitive";
    case OpCode::eDemote: return os << "Demote";
    case OpCode::eInterpolateAtCentroid: return os << "InterpolateAtCentroid";
    case OpCode::eInterpolateAtSample: return os << "InterpolateAtSample";
    case OpCode::eInterpolateAtOffset: return os << "InterpolateAtOffset";
    case OpCode::eDerivX: return os << "DerivX";
    case OpCode::eDerivY: return os << "DerivY";
    case OpCode::eRovScopedLockBegin: return os << "RovScopedLockBegin";
    case OpCode::eRovScopedLockEnd: return os << "RovScopedLockEnd";
    case OpCode::eFEq: return os << "FEq";
    case OpCode::eFNe: return os << "FNe";
    case OpCode::eFLt: return os << "FLt";
    case OpCode::eFLe: return os << "FLe";
    case OpCode::eFGt: return os << "FGt";
    case OpCode::eFGe: return os << "FGe";
    case OpCode::eFIsNan: return os << "FIsNan";
    case OpCode::eIEq: return os << "IEq";
    case OpCode::eINe: return os << "INe";
    case OpCode::eSLt: return os << "SLt";
    case OpCode::eSLe: return os << "SLe";
    case OpCode::eSGt: return os << "SGt";
    case OpCode::eSGe: return os << "SGe";
    case OpCode::eULt: return os << "ULt";
    case OpCode::eULe: return os << "ULe";
    case OpCode::eUGt: return os << "UGt";
    case OpCode::eUGe: return os << "UGe";
    case OpCode::eBAnd: return os << "BAnd";
    case OpCode::eBOr: return os << "BOr";
    case OpCode::eBEq: return os << "BEq";
    case OpCode::eBNe: return os << "BNe";
    case OpCode::eBNot: return os << "BNot";
    case OpCode::eSelect: return os << "Select";
    case OpCode::eFAbs: return os << "FAbs";
    case OpCode::eFNeg: return os << "FNeg";
    case OpCode::eFAdd: return os << "FAdd";
    case OpCode::eFSub: return os << "FSub";
    case OpCode::eFMul: return os << "FMul";
    case OpCode::eFMulLegacy: return os << "FMulLegacy";
    case OpCode::eFMad: return os << "FMad";
    case OpCode::eFMadLegacy: return os << "FMadLegacy";
    case OpCode::eFDiv: return os << "FDiv";
    case OpCode::eFRcp: return os << "FRcp";
    case OpCode::eFSqrt: return os << "FSqrt";
    case OpCode::eFRsq: return os << "FRsq";
    case OpCode::eFExp2: return os << "FExp2";
    case OpCode::eFLog2: return os << "FLog2";
    case OpCode::eFFract: return os << "FFract";
    case OpCode::eFRound: return os << "FRound";
    case OpCode::eFMin: return os << "FMin";
    case OpCode::eFMax: return os << "FMax";
    case OpCode::eFDot: return os << "FDot";
    case OpCode::eFDotLegacy: return os << "FDotLegacy";
    case OpCode::eFClamp: return os << "FClamp";
    case OpCode::eFSin: return os << "FSin";
    case OpCode::eFCos: return os << "FCos";
    case OpCode::eFPow: return os << "FPow";
    case OpCode::eFPowLegacy: return os << "FPowLegacy";
    case OpCode::eIAnd: return os << "IAnd";
    case OpCode::eIOr: return os << "IOr";
    case OpCode::eIXor: return os << "IXor";
    case OpCode::eINot: return os << "INot";
    case OpCode::eIBitInsert: return os << "IBitInsert";
    case OpCode::eUBitExtract: return os << "UBitExtract";
    case OpCode::eSBitExtract: return os << "SBitExtract";
    case OpCode::eIShl: return os << "IShl";
    case OpCode::eSShr: return os << "SShr";
    case OpCode::eUShr: return os << "UShr";
    case OpCode::eIBitCount: return os << "IBitCount";
    case OpCode::eIBitReverse: return os << "IBitReverse";
    case OpCode::eIFindLsb: return os << "IFindLsb";
    case OpCode::eSFindMsb: return os << "SFindMsb";
    case OpCode::eUFindMsb: return os << "UFindMsb";
    case OpCode::eIAdd: return os << "IAdd";
    case OpCode::eIAddCarry: return os << "IAddCarry";
    case OpCode::eISub: return os << "ISub";
    case OpCode::eISubBorrow: return os << "ISubBorrow";
    case OpCode::eINeg: return os << "INeg";
    case OpCode::eIAbs: return os << "IAbs";
    case OpCode::eIMul: return os << "IMul";
    case OpCode::eUDiv: return os << "UDiv";
    case OpCode::eUMod: return os << "UMod";
    case OpCode::eSMin: return os << "SMin";
    case OpCode::eSMax: return os << "SMax";
    case OpCode::eSClamp: return os << "SClamp";
    case OpCode::eUMin: return os << "UMin";
    case OpCode::eUMax: return os << "UMax";
    case OpCode::eUClamp: return os << "UClamp";
    case OpCode::eUMSad: return os << "UMSad";
    case OpCode::eSMulExtended: return os << "SMulExtended";
    case OpCode::eUMulExtended: return os << "UMulExtended";
    case OpCode::eDrain: return os << "Drain";
  }

  return os << "OpCode(" << std::dec << uint32_t(opCode) << ")";
}

}
