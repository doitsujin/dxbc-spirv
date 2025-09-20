#include "dxbc_types.h"

namespace dxbc_spv::dxbc {

ir::ScalarType resolveType(ComponentType type, MinPrecision precision) {
  switch (type) {
    case ComponentType::eVoid:
      return ir::ScalarType::eVoid;

    case ComponentType::eFloat:
      if (precision == MinPrecision::eMin16Float)
        return ir::ScalarType::eMinF16;
      if (precision == MinPrecision::eMin10Float)
        return ir::ScalarType::eMinF10;
      return ir::ScalarType::eF32;

    case ComponentType::eUint:
      return precision == MinPrecision::eMin16Uint
        ? ir::ScalarType::eMinU16
        : ir::ScalarType::eU32;

    case ComponentType::eSint:
      return precision == MinPrecision::eMin16Sint
        ? ir::ScalarType::eMinI16
        : ir::ScalarType::eI32;

    case ComponentType::eBool:
      return ir::ScalarType::eBool;

    case ComponentType::eDouble:
      return ir::ScalarType::eF64;
  }

  return ir::ScalarType::eUnknown;
}


std::pair<ComponentType, MinPrecision> determineComponentType(ir::ScalarType type) {
  switch (type) {
    case ir::ScalarType::eVoid:   return std::make_pair(ComponentType::eVoid,   MinPrecision::eNone);
    case ir::ScalarType::eBool:   return std::make_pair(ComponentType::eBool,   MinPrecision::eNone);
    case ir::ScalarType::eMinU16: return std::make_pair(ComponentType::eUint,   MinPrecision::eMin16Uint);
    case ir::ScalarType::eMinI16: return std::make_pair(ComponentType::eSint,   MinPrecision::eMin16Sint);
    case ir::ScalarType::eMinF16: return std::make_pair(ComponentType::eFloat,  MinPrecision::eMin16Float);
    case ir::ScalarType::eMinF10: return std::make_pair(ComponentType::eFloat,  MinPrecision::eMin10Float);
    case ir::ScalarType::eU32:    return std::make_pair(ComponentType::eUint,   MinPrecision::eNone);
    case ir::ScalarType::eI32:    return std::make_pair(ComponentType::eSint,   MinPrecision::eNone);
    case ir::ScalarType::eF32:    return std::make_pair(ComponentType::eFloat,  MinPrecision::eNone);
    case ir::ScalarType::eF64:    return std::make_pair(ComponentType::eDouble, MinPrecision::eNone);

    default:
      dxbc_spv_unreachable();
      return std::make_pair(ComponentType::eVoid, MinPrecision::eNone);
  }
}


ir::ScalarType resolveSampledType(SampledType type) {
  switch (type) {
    case SampledType::eUnorm:
    case SampledType::eSnorm:
    case SampledType::eFloat:
    case SampledType::eMixed:
      return ir::ScalarType::eF32;

    case SampledType::eSint:
      return ir::ScalarType::eI32;

    case SampledType::eUint:
      return ir::ScalarType::eU32;

    case SampledType::eDouble:
      return ir::ScalarType::eF64;
  }

  return ir::ScalarType::eUnknown;
}


SampledType determineSampledType(ir::ScalarType type) {
  switch (type) {
    case ir::ScalarType::eI32:  return SampledType::eSint;
    case ir::ScalarType::eU32:  return SampledType::eUint;
    case ir::ScalarType::eF32:  return SampledType::eFloat;
    case ir::ScalarType::eF64:  return SampledType::eDouble;

    default:
      dxbc_spv_unreachable();
      return SampledType::eMixed;
  }
}


std::optional<ir::ResourceKind> resolveResourceDim(ResourceDim dim) {
  switch (dim) {
    case ResourceDim::eUnknown:           return std::nullopt;
    case ResourceDim::eBuffer:            return ir::ResourceKind::eBufferTyped;
    case ResourceDim::eRawBuffer:         return ir::ResourceKind::eBufferRaw;
    case ResourceDim::eStructuredBuffer:  return ir::ResourceKind::eBufferStructured;
    case ResourceDim::eTexture1D:         return ir::ResourceKind::eImage1D;
    case ResourceDim::eTexture1DArray:    return ir::ResourceKind::eImage1DArray;
    case ResourceDim::eTexture2D:         return ir::ResourceKind::eImage2D;
    case ResourceDim::eTexture2DArray:    return ir::ResourceKind::eImage2DArray;
    case ResourceDim::eTexture2DMS:       return ir::ResourceKind::eImage2DMS;
    case ResourceDim::eTexture2DMSArray:  return ir::ResourceKind::eImage2DMSArray;
    case ResourceDim::eTextureCube:       return ir::ResourceKind::eImageCube;
    case ResourceDim::eTextureCubeArray:  return ir::ResourceKind::eImageCubeArray;
    case ResourceDim::eTexture3D:         return ir::ResourceKind::eImage3D;
  }

  return std::nullopt;
}


ResourceDim determineResourceDim(ir::ResourceKind kind) {
  switch (kind) {
    case ir::ResourceKind::eBufferTyped:      return ResourceDim::eBuffer;
    case ir::ResourceKind::eBufferRaw:        return ResourceDim::eRawBuffer;
    case ir::ResourceKind::eBufferStructured: return ResourceDim::eStructuredBuffer;
    case ir::ResourceKind::eImage1D:          return ResourceDim::eTexture1D;
    case ir::ResourceKind::eImage1DArray:     return ResourceDim::eTexture1DArray;
    case ir::ResourceKind::eImage2D:          return ResourceDim::eTexture2D;
    case ir::ResourceKind::eImage2DArray:     return ResourceDim::eTexture2DArray;
    case ir::ResourceKind::eImage2DMS:        return ResourceDim::eTexture2DMS;
    case ir::ResourceKind::eImage2DMSArray:   return ResourceDim::eTexture2DMSArray;
    case ir::ResourceKind::eImageCube:        return ResourceDim::eTextureCube;
    case ir::ResourceKind::eImageCubeArray:   return ResourceDim::eTextureCubeArray;
    case ir::ResourceKind::eImage3D:          return ResourceDim::eTexture3D;
  }

  dxbc_spv_unreachable();
  return ResourceDim::eUnknown;
}


std::optional<ir::InterpolationModes> resolveInterpolationMode(InterpolationMode mode) {
  switch (mode) {
    case InterpolationMode::eUndefined:
      return std::nullopt;

    case InterpolationMode::eConstant:
      return ir::InterpolationModes(ir::InterpolationMode::eFlat);

    case InterpolationMode::eLinear:
      return ir::InterpolationModes();

    case InterpolationMode::eLinearCentroid:
      return ir::InterpolationModes(ir::InterpolationMode::eCentroid);

    case InterpolationMode::eLinearNoPerspective:
      return ir::InterpolationModes(ir::InterpolationMode::eNoPerspective);

    case InterpolationMode::eLinearNoPerspectiveCentroid:
      return ir::InterpolationModes(ir::InterpolationMode::eCentroid |
                                    ir::InterpolationMode::eNoPerspective);

    case InterpolationMode::eLinearSample:
      return ir::InterpolationModes(ir::InterpolationMode::eSample);

    case InterpolationMode::eLinearNoPerspectiveSample:
      return ir::InterpolationModes(ir::InterpolationMode::eSample |
                                    ir::InterpolationMode::eNoPerspective);
  }

  return std::nullopt;
}


InterpolationMode determineInterpolationMode(ir::InterpolationModes mode) {
  if (!mode)
    return InterpolationMode::eLinear;

  if (mode == ir::InterpolationMode::eFlat)
    return InterpolationMode::eConstant;

  if (mode == ir::InterpolationMode::eCentroid)
    return InterpolationMode::eLinearCentroid;

  if (mode == ir::InterpolationMode::eNoPerspective)
    return InterpolationMode::eLinearCentroid;

  if (mode == (ir::InterpolationMode::eCentroid | ir::InterpolationMode::eNoPerspective))
    return InterpolationMode::eLinearNoPerspectiveCentroid;

  if (mode == ir::InterpolationMode::eSample)
    return InterpolationMode::eLinearSample;

  if (mode == (ir::InterpolationMode::eSample | ir::InterpolationMode::eNoPerspective))
    return InterpolationMode::eLinearNoPerspectiveSample;

  dxbc_spv_unreachable();
  return InterpolationMode::eUndefined;
}


std::optional<ir::PrimitiveType> resolvePrimitiveTopology(PrimitiveTopology type) {
  switch (type) {
    case PrimitiveTopology::eUndefined:
      return std::nullopt;

    case PrimitiveTopology::ePointList:
      return ir::PrimitiveType::ePoints;

    case PrimitiveTopology::eLineList:
    case PrimitiveTopology::eLineStrip:
      return ir::PrimitiveType::eLines;

    case PrimitiveTopology::eTriangleList:
    case PrimitiveTopology::eTriangleStrip:
      return ir::PrimitiveType::eTriangles;

    case PrimitiveTopology::eLineListAdj:
    case PrimitiveTopology::eLineStripAdj:
      return ir::PrimitiveType::eLinesAdj;

    case PrimitiveTopology::eTriangleListAdj:
    case PrimitiveTopology::eTriangleStripAdj:
      return ir::PrimitiveType::eTrianglesAdj;
  }

  return std::nullopt;
}


PrimitiveTopology determinePrimitiveTopology(ir::PrimitiveType type, bool strip) {
  switch (type) {
    case ir::PrimitiveType::ePoints:
      return PrimitiveTopology::ePointList;

    case ir::PrimitiveType::eLines:
      return strip
        ? PrimitiveTopology::eLineStrip
        : PrimitiveTopology::eLineList;

    case ir::PrimitiveType::eLinesAdj:
      return strip
        ? PrimitiveTopology::eLineStripAdj
        : PrimitiveTopology::eLineListAdj;

    case ir::PrimitiveType::eTriangles:
      return strip
        ? PrimitiveTopology::eTriangleStrip
        : PrimitiveTopology::eTriangleList;

    case ir::PrimitiveType::eTrianglesAdj:
      return strip
        ? PrimitiveTopology::eTriangleStripAdj
        : PrimitiveTopology::eTriangleListAdj;

    default:
      dxbc_spv_unreachable();
      return PrimitiveTopology::eUndefined;
  }
}


std::optional<ir::PrimitiveType> resolvePrimitiveType(PrimitiveType type) {
  switch (type) {
    case PrimitiveType::eUndefined:   return std::nullopt;
    case PrimitiveType::ePoint:       return ir::PrimitiveType::ePoints;
    case PrimitiveType::eLine:        return ir::PrimitiveType::eLines;
    case PrimitiveType::eTriangle:    return ir::PrimitiveType::eTriangles;
    case PrimitiveType::eLineAdj:     return ir::PrimitiveType::eLinesAdj;
    case PrimitiveType::eTriangleAdj: return ir::PrimitiveType::eTrianglesAdj;

    default:
      if (type >= PrimitiveType::ePatch1 && type <= PrimitiveType::ePatch32) {
        uint32_t vertexCount = uint32_t(type) - uint32_t(PrimitiveType::ePatch1) + 1u;
        return ir::makePatchPrimitive(vertexCount);
      }
  }

  return std::nullopt;
}


PrimitiveType determinePrimitiveType(ir::PrimitiveType type) {
  switch (type) {
    case ir::PrimitiveType::ePoints:        return PrimitiveType::ePoint;
    case ir::PrimitiveType::eLines:         return PrimitiveType::eLine;
    case ir::PrimitiveType::eTriangles:     return PrimitiveType::eTriangle;
    case ir::PrimitiveType::eLinesAdj:      return PrimitiveType::eLineAdj;
    case ir::PrimitiveType::eTrianglesAdj:  return PrimitiveType::eTriangleAdj;

    case ir::PrimitiveType::eQuads:
      dxbc_spv_unreachable();
      return PrimitiveType::eUndefined;

    default: {
      uint32_t vertexCount = ir::primitiveVertexCount(type);
      return PrimitiveType(uint32_t(PrimitiveType::ePatch1) + vertexCount - 1u);
    }
  }
}


std::optional<ir::PrimitiveType> resolveTessDomain(TessDomain domain) {
  switch (domain) {
    case TessDomain::eUndefined:  return std::nullopt;
    case TessDomain::eIsoline:    return ir::PrimitiveType::eLines;
    case TessDomain::eTriangle:   return ir::PrimitiveType::eTriangles;
    case TessDomain::eQuad:       return ir::PrimitiveType::eQuads;
  }

  return std::nullopt;
}


TessDomain determineTessDomain(ir::PrimitiveType domain) {
  switch (domain) {
    case ir::PrimitiveType::eLines:     return TessDomain::eIsoline;
    case ir::PrimitiveType::eTriangles: return TessDomain::eTriangle;
    case ir::PrimitiveType::eQuads:     return TessDomain::eQuad;

    default:
      dxbc_spv_unreachable();
      return TessDomain::eUndefined;
  }
}


std::optional<ir::TessPartitioning> resolveTessPartitioning(TessPartitioning partitioning) {
  switch (partitioning) {
    case TessPartitioning::eUndefined:      return std::nullopt;
    case TessPartitioning::eInteger:        return ir::TessPartitioning::eInteger;
    case TessPartitioning::ePow2:           return ir::TessPartitioning::eInteger;
    case TessPartitioning::eFractionalOdd:  return ir::TessPartitioning::eFractOdd;
    case TessPartitioning::eFractionalEven: return ir::TessPartitioning::eFractEven;
  }

  return std::nullopt;
}


TessPartitioning determineTessPartitioning(ir::TessPartitioning partitioning) {
  switch (partitioning) {
    case ir::TessPartitioning::eInteger:    return TessPartitioning::eInteger;
    case ir::TessPartitioning::eFractOdd:   return TessPartitioning::eFractionalOdd;
    case ir::TessPartitioning::eFractEven:  return TessPartitioning::eFractionalEven;
  }

  dxbc_spv_unreachable();
  return TessPartitioning::eUndefined;
}


std::optional<std::pair<ir::PrimitiveType, ir::TessWindingOrder>> resolveTessOutput(TessOutput output) {
  switch (output) {
    case TessOutput::eUndefined:      return std::nullopt;
    case TessOutput::ePoint:          return std::make_pair(ir::PrimitiveType::ePoints,    ir::TessWindingOrder::eCcw);
    case TessOutput::eLine:           return std::make_pair(ir::PrimitiveType::eLines,     ir::TessWindingOrder::eCcw);
    case TessOutput::eTriangleCw:     return std::make_pair(ir::PrimitiveType::eTriangles, ir::TessWindingOrder::eCw);
    case TessOutput::eTriangleCcw:    return std::make_pair(ir::PrimitiveType::eTriangles, ir::TessWindingOrder::eCcw);
  }

  return std::nullopt;
}


TessOutput determineTessOutput(ir::PrimitiveType type, ir::TessWindingOrder winding) {
  switch (type) {
    case ir::PrimitiveType::ePoints:
      return TessOutput::ePoint;

    case ir::PrimitiveType::eLines:
      return TessOutput::eLine;

    case ir::PrimitiveType::eTriangles:
      return winding == ir::TessWindingOrder::eCcw
        ? TessOutput::eTriangleCcw
        : TessOutput::eTriangleCw;

    default:
      dxbc_spv_unreachable();
      return TessOutput::eUndefined;
  }
}


std::ostream& operator << (std::ostream& os, OpCode op) {
  switch (op) {
    case OpCode::eAdd:                                  return os << "add";
    case OpCode::eAnd:                                  return os << "and";
    case OpCode::eBreak:                                return os << "break";
    case OpCode::eBreakc:                               return os << "breakc";
    case OpCode::eCall:                                 return os << "call";
    case OpCode::eCallc:                                return os << "callc";
    case OpCode::eCase:                                 return os << "case";
    case OpCode::eContinue:                             return os << "continue";
    case OpCode::eContinuec:                            return os << "continuec";
    case OpCode::eCut:                                  return os << "cut";
    case OpCode::eDefault:                              return os << "default";
    case OpCode::eDerivRtx:                             return os << "deriv_rtx";
    case OpCode::eDerivRty:                             return os << "deriv_rty";
    case OpCode::eDiscard:                              return os << "discard";
    case OpCode::eDiv:                                  return os << "div";
    case OpCode::eDp2:                                  return os << "dp2";
    case OpCode::eDp3:                                  return os << "dp3";
    case OpCode::eDp4:                                  return os << "dp4";
    case OpCode::eElse:                                 return os << "else";
    case OpCode::eEmit:                                 return os << "emit";
    case OpCode::eEmitThenCut:                          return os << "emit_then_cut";
    case OpCode::eEndIf:                                return os << "endif";
    case OpCode::eEndLoop:                              return os << "endloop";
    case OpCode::eEndSwitch:                            return os << "endswitch";
    case OpCode::eEq:                                   return os << "eq";
    case OpCode::eExp:                                  return os << "exp";
    case OpCode::eFrc:                                  return os << "frc";
    case OpCode::eFtoI:                                 return os << "ftoi";
    case OpCode::eFtoU:                                 return os << "ftou";
    case OpCode::eGe:                                   return os << "ge";
    case OpCode::eIAdd:                                 return os << "iadd";
    case OpCode::eIf:                                   return os << "if";
    case OpCode::eIEq:                                  return os << "ieq";
    case OpCode::eIGe:                                  return os << "ige";
    case OpCode::eILt:                                  return os << "ilt";
    case OpCode::eIMad:                                 return os << "imad";
    case OpCode::eIMax:                                 return os << "imax";
    case OpCode::eIMin:                                 return os << "imin";
    case OpCode::eIMul:                                 return os << "imul";
    case OpCode::eINe:                                  return os << "ine";
    case OpCode::eINeg:                                 return os << "ineg";
    case OpCode::eIShl:                                 return os << "ishl";
    case OpCode::eIShr:                                 return os << "ishr";
    case OpCode::eItoF:                                 return os << "itof";
    case OpCode::eLabel:                                return os << "label";
    case OpCode::eLd:                                   return os << "ld";
    case OpCode::eLdMs:                                 return os << "ld2dms";
    case OpCode::eLog:                                  return os << "log";
    case OpCode::eLoop:                                 return os << "loop";
    case OpCode::eLt:                                   return os << "lt";
    case OpCode::eMad:                                  return os << "mad";
    case OpCode::eMin:                                  return os << "min";
    case OpCode::eMax:                                  return os << "max";
    case OpCode::eCustomData:                           return os << "customdata";
    case OpCode::eMov:                                  return os << "mov";
    case OpCode::eMovc:                                 return os << "movc";
    case OpCode::eMul:                                  return os << "mul";
    case OpCode::eNe:                                   return os << "ne";
    case OpCode::eNop:                                  return os << "nop";
    case OpCode::eNot:                                  return os << "not";
    case OpCode::eOr:                                   return os << "or";
    case OpCode::eResInfo:                              return os << "resinfo";
    case OpCode::eRet:                                  return os << "ret";
    case OpCode::eRetc:                                 return os << "retc";
    case OpCode::eRoundNe:                              return os << "round_ne";
    case OpCode::eRoundNi:                              return os << "round_ni";
    case OpCode::eRoundPi:                              return os << "round_pi";
    case OpCode::eRoundZ:                               return os << "round_z";
    case OpCode::eRsq:                                  return os << "rsq";
    case OpCode::eSample:                               return os << "sample";
    case OpCode::eSampleC:                              return os << "sample_c";
    case OpCode::eSampleClz:                            return os << "sample_c_lz";
    case OpCode::eSampleL:                              return os << "sample_l";
    case OpCode::eSampleD:                              return os << "sample_d";
    case OpCode::eSampleB:                              return os << "sample_b";
    case OpCode::eSqrt:                                 return os << "sqrt";
    case OpCode::eSwitch:                               return os << "switch";
    case OpCode::eSinCos:                               return os << "sincos";
    case OpCode::eUDiv:                                 return os << "udiv";
    case OpCode::eULt:                                  return os << "ult";
    case OpCode::eUGe:                                  return os << "uge";
    case OpCode::eUMul:                                 return os << "umul";
    case OpCode::eUMad:                                 return os << "umad";
    case OpCode::eUMax:                                 return os << "umax";
    case OpCode::eUMin:                                 return os << "umin";
    case OpCode::eUShr:                                 return os << "ushr";
    case OpCode::eUtoF:                                 return os << "utof";
    case OpCode::eXor:                                  return os << "xor";
    case OpCode::eDclResource:                          return os << "dcl_resource";
    case OpCode::eDclConstantBuffer:                    return os << "dcl_constant_buffer";
    case OpCode::eDclSampler:                           return os << "dcl_sampler";
    case OpCode::eDclIndexRange:                        return os << "dcl_index_range";
    case OpCode::eDclGsOutputPrimitiveTopology:         return os << "dcl_output_topology";
    case OpCode::eDclGsInputPrimitive:                  return os << "dcl_input_primitive";
    case OpCode::eDclMaxOutputVertexCount:              return os << "dcl_maxout";
    case OpCode::eDclInput:                             return os << "dcl_input";
    case OpCode::eDclInputSgv:                          return os << "dcl_input_sv";
    case OpCode::eDclInputSiv:                          return os << "dcl_input_sv";
    case OpCode::eDclInputPs:                           return os << "dcl_input";
    case OpCode::eDclInputPsSgv:                        return os << "dcl_input_sv";
    case OpCode::eDclInputPsSiv:                        return os << "dcl_input_sv";
    case OpCode::eDclOutput:                            return os << "dcl_output";
    case OpCode::eDclOutputSgv:                         return os << "dcl_output_sgv";
    case OpCode::eDclOutputSiv:                         return os << "dcl_output_siv";
    case OpCode::eDclTemps:                             return os << "dcl_temps";
    case OpCode::eDclIndexableTemp:                     return os << "dcl_indexable_temp";
    case OpCode::eDclGlobalFlags:                       return os << "dcl_global_flags";
    case OpCode::eLod:                                  return os << "lod";
    case OpCode::eGather4:                              return os << "gather4";
    case OpCode::eSamplePos:                            return os << "samplepos";
    case OpCode::eSampleInfo:                           return os << "sampleinfo";
    case OpCode::eHsDecls:                              return os << "hs_decls";
    case OpCode::eHsControlPointPhase:                  return os << "hs_control_point_phase";
    case OpCode::eHsForkPhase:                          return os << "hs_fork_phase";
    case OpCode::eHsJoinPhase:                          return os << "hs_join_phase";
    case OpCode::eEmitStream:                           return os << "emit_stream";
    case OpCode::eCutStream:                            return os << "cut_stream";
    case OpCode::eEmitThenCutStream:                    return os << "emit_then_cut_stream";
    case OpCode::eInterfaceCall:                        return os << "fcall";
    case OpCode::eBufInfo:                              return os << "bufinfo";
    case OpCode::eDerivRtxCoarse:                       return os << "deriv_rtx_coarse";
    case OpCode::eDerivRtxFine:                         return os << "deriv_rtx_fine";
    case OpCode::eDerivRtyCoarse:                       return os << "deriv_rty_coarse";
    case OpCode::eDerivRtyFine:                         return os << "deriv_rty_fine";
    case OpCode::eGather4C:                             return os << "gather4_c";
    case OpCode::eGather4Po:                            return os << "gather4_po";
    case OpCode::eGather4PoC:                           return os << "gather4_po_c";
    case OpCode::eRcp:                                  return os << "rcp";
    case OpCode::eF32toF16:                             return os << "f32tof16";
    case OpCode::eF16toF32:                             return os << "f16tof32";
    case OpCode::eUAddc:                                return os << "uaddc";
    case OpCode::eUSubb:                                return os << "usubb";
    case OpCode::eCountBits:                            return os << "countbits";
    case OpCode::eFirstBitHi:                           return os << "firstbit_hi";
    case OpCode::eFirstBitLo:                           return os << "firstbit_lo";
    case OpCode::eFirstBitShi:                          return os << "firstbit_shi";
    case OpCode::eUBfe:                                 return os << "ubfe";
    case OpCode::eIBfe:                                 return os << "ibfe";
    case OpCode::eBfi:                                  return os << "bfi";
    case OpCode::eBfRev:                                return os << "bfrev";
    case OpCode::eSwapc:                                return os << "swapc";
    case OpCode::eDclStream:                            return os << "dcl_stream";
    case OpCode::eDclFunctionBody:                      return os << "dcl_functionBody";
    case OpCode::eDclFunctionTable:                     return os << "dcl_functionTable";
    case OpCode::eDclInterface:                         return os << "dcl_interface";
    case OpCode::eDclInputControlPointCount:            return os << "dcl_input_control_point_count";
    case OpCode::eDclOutputControlPointCount:           return os << "dcl_output_control_point_count";
    case OpCode::eDclTessDomain:                        return os << "dcl_tessellator_domain";
    case OpCode::eDclTessPartitioning:                  return os << "dcl_tessellator_partitioning";
    case OpCode::eDclTessOutputPrimitive:               return os << "dcl_tessellator_output_primitive";
    case OpCode::eDclHsMaxTessFactor:                   return os << "dcl_hs_max_tessfactor";
    case OpCode::eDclHsForkPhaseInstanceCount:          return os << "dcl_hs_fork_phase_instance_count";
    case OpCode::eDclHsJoinPhaseInstanceCount:          return os << "dcl_hs_join_phase_instance_count";
    case OpCode::eDclThreadGroup:                       return os << "dcl_thread_group";
    case OpCode::eDclUavTyped:                          return os << "dcl_uav_typed";
    case OpCode::eDclUavRaw:                            return os << "dcl_uav_raw";
    case OpCode::eDclUavStructured:                     return os << "dcl_uav_structured";
    case OpCode::eDclThreadGroupSharedMemoryRaw:        return os << "dcl_tgsm_raw";
    case OpCode::eDclThreadGroupSharedMemoryStructured: return os << "dcl_tgsm_structured";
    case OpCode::eDclResourceRaw:                       return os << "dcl_resource_raw";
    case OpCode::eDclResourceStructured:                return os << "dcl_resource_structured";
    case OpCode::eLdUavTyped:                           return os << "ld_uav_typed";
    case OpCode::eStoreUavTyped:                        return os << "store_uav_typed";
    case OpCode::eLdRaw:                                return os << "ld_raw";
    case OpCode::eStoreRaw:                             return os << "store_raw";
    case OpCode::eLdStructured:                         return os << "ld_structured";
    case OpCode::eStoreStructured:                      return os << "store_structured";
    case OpCode::eAtomicAnd:                            return os << "atomic_and";
    case OpCode::eAtomicOr:                             return os << "atomic_or";
    case OpCode::eAtomicXor:                            return os << "atomic_xor";
    case OpCode::eAtomicCmpStore:                       return os << "atomic_cmp_store";
    case OpCode::eAtomicIAdd:                           return os << "atomic_iadd";
    case OpCode::eAtomicIMax:                           return os << "atomic_imax";
    case OpCode::eAtomicIMin:                           return os << "atomic_imin";
    case OpCode::eAtomicUMax:                           return os << "atomic_umax";
    case OpCode::eAtomicUMin:                           return os << "atomic_umin";
    case OpCode::eImmAtomicAlloc:                       return os << "imm_atomic_alloc";
    case OpCode::eImmAtomicConsume:                     return os << "imm_atomic_consume";
    case OpCode::eImmAtomicIAdd:                        return os << "imm_atomic_iadd";
    case OpCode::eImmAtomicAnd:                         return os << "imm_atomic_and";
    case OpCode::eImmAtomicOr:                          return os << "imm_atomic_or";
    case OpCode::eImmAtomicXor:                         return os << "imm_atomic_xor";
    case OpCode::eImmAtomicExch:                        return os << "imm_atomic_exch";
    case OpCode::eImmAtomicCmpExch:                     return os << "imm_atomic_cmp_exch";
    case OpCode::eImmAtomicIMax:                        return os << "imm_atomic_imax";
    case OpCode::eImmAtomicIMin:                        return os << "imm_atomic_imin";
    case OpCode::eImmAtomicUMax:                        return os << "imm_atomic_umax";
    case OpCode::eImmAtomicUMin:                        return os << "imm_atomic_umin";
    case OpCode::eSync:                                 return os << "sync";
    case OpCode::eDAdd:                                 return os << "dadd";
    case OpCode::eDMax:                                 return os << "dmax";
    case OpCode::eDMin:                                 return os << "dmin";
    case OpCode::eDMul:                                 return os << "dmul";
    case OpCode::eDEq:                                  return os << "deq";
    case OpCode::eDGe:                                  return os << "dge";
    case OpCode::eDLt:                                  return os << "dlt";
    case OpCode::eDNe:                                  return os << "dne";
    case OpCode::eDMov:                                 return os << "dmov";
    case OpCode::eDMovc:                                return os << "dmovc";
    case OpCode::eDtoF:                                 return os << "dtof";
    case OpCode::eFtoD:                                 return os << "ftod";
    case OpCode::eEvalSnapped:                          return os << "eval_snapped";
    case OpCode::eEvalSampleIndex:                      return os << "eval_sample_index";
    case OpCode::eEvalCentroid:                         return os << "eval_centroid";
    case OpCode::eDclGsInstanceCount:                   return os << "dcl_gs_instance_count";
    case OpCode::eAbort:                                return os << "abort";
    case OpCode::eDebugBreak:                           return os << "debug_break";
    case OpCode::eDDiv:                                 return os << "ddiv";
    case OpCode::eDFma:                                 return os << "dfma";
    case OpCode::eDRcp:                                 return os << "drcp";
    case OpCode::eMsad:                                 return os << "msad";
    case OpCode::eDtoI:                                 return os << "dtoi";
    case OpCode::eDtoU:                                 return os << "dtou";
    case OpCode::eItoD:                                 return os << "itod";
    case OpCode::eUtoD:                                 return os << "utod";
    case OpCode::eGather4S:                             return os << "gather4_s";
    case OpCode::eGather4CS:                            return os << "gather4_c_s";
    case OpCode::eGather4PoS:                           return os << "gather4_po_s";
    case OpCode::eGather4PoCS:                          return os << "gather4_po_c_s";
    case OpCode::eLdS:                                  return os << "ld_s";
    case OpCode::eLdMsS:                                return os << "ld2dms_s";
    case OpCode::eLdUavTypedS:                          return os << "ld_uav_typed_s";
    case OpCode::eLdRawS:                               return os << "ld_raw_s";
    case OpCode::eLdStructuredS:                        return os << "ld_structured_s";
    case OpCode::eSampleLS:                             return os << "sample_l_s";
    case OpCode::eSampleClzS:                           return os << "sample_c_lz_s";
    case OpCode::eSampleClampS:                         return os << "sample_cl_s";
    case OpCode::eSampleBClampS:                        return os << "sample_b_cl_s";
    case OpCode::eSampleDClampS:                        return os << "sample_d_cl_s";
    case OpCode::eSampleCClampS:                        return os << "sample_c_cl_s";
    case OpCode::eCheckAccessFullyMapped:               return os << "check_access_mapped";
  }

  return os << "OpCode(" << uint32_t(op) << ")";
}


std::ostream& operator << (std::ostream& os, Sysval sv) {
  switch (sv) {
    case Sysval::eNone:                   return os << "none";
    case Sysval::ePosition:               return os << "position";
    case Sysval::eClipDistance:           return os << "clip_distance";
    case Sysval::eCullDistance:           return os << "cull_distance";
    case Sysval::eRenderTargetId:         return os << "rendertarget_array_index";
    case Sysval::eViewportId:             return os << "viewport_array_index";
    case Sysval::eVertexId:               return os << "vertex_id";
    case Sysval::ePrimitiveId:            return os << "primitive_id";
    case Sysval::eInstanceId:             return os << "instance_id";
    case Sysval::eIsFrontFace:            return os << "is_front_face";
    case Sysval::eSampleIndex:            return os << "sampleIndex";
    case Sysval::eQuadU0EdgeTessFactor:   return os << "finalQuadUeq0EdgeTessFactor";
    case Sysval::eQuadV0EdgeTessFactor:   return os << "finalQuadVeq0EdgeTessFactor";
    case Sysval::eQuadU1EdgeTessFactor:   return os << "finalQuadUeq1EdgeTessFactor";
    case Sysval::eQuadV1EdgeTessFactor:   return os << "finalQuadVeq1EdgeTessFactor";
    case Sysval::eQuadUInsideTessFactor:  return os << "finalQuadUInsideTessFactor";
    case Sysval::eQuadVInsideTessFactor:  return os << "finalQuadVInsideTessFactor";
    case Sysval::eTriUEdgeTessFactor:     return os << "finalTriUeq0EdgeTessFactor";
    case Sysval::eTriVEdgeTessFactor:     return os << "finalTriVeq0EdgeTessFactor";
    case Sysval::eTriWEdgeTessFactor:     return os << "finalTriUeqwEdgeTessFactor";
    case Sysval::eTriInsideTessFactor:    return os << "finalTriInsideTessFactor";
    case Sysval::eLineDetailTessFactor:   return os << "finalLineDetailTessFactor";
    case Sysval::eLineDensityTessFactor:  return os << "finalLineDensityTessFactor";
  }

  return os << "SystemValue(" << uint32_t(sv) << ")";
}


std::ostream& operator << (std::ostream& os, TestBoolean mode) {
  return os << (mode == TestBoolean::eZero ? "z" : "nz");
}


std::ostream& operator << (std::ostream& os, ReturnType type) {
  switch (type) {
    case ReturnType::eFloat:  return os << "float";
    case ReturnType::eUint:   return os << "uint";
  }

  return os << "ReturnType(" << uint32_t(type) << ")";
}


std::ostream& operator << (std::ostream& os, ResInfoType type) {
  switch (type) {
    case ResInfoType::eFloat:     return os << "float";
    case ResInfoType::eRcpFloat:  return os << "rcpfloat";
    case ResInfoType::eUint:      return os << "uint";
  }

  return os << "ResInfoType(" << uint32_t(type) << ")";
}


std::ostream& operator << (std::ostream& os, UavFlag flag) {
  switch (flag) {
    case UavFlag::eGloballyCoherent:  return os << "glc";
    case UavFlag::eRasterizerOrdered: return os << "rov";
    case UavFlag::eFlagEnum:          break;
  }

  return os << "UavFlag(" << uint32_t(flag) << ")";
}


std::ostream& operator << (std::ostream& os, SyncFlag flag) {
  switch (flag) {
    case SyncFlag::eWorkgroupThreads: return os << "t";
    case SyncFlag::eWorkgroupMemory:  return os << "g";
    case SyncFlag::eUavMemoryGlobal:  return os << "uglobal";
    case SyncFlag::eUavMemoryLocal:   return os << "ugroup";
    case SyncFlag::eFlagEnum:         break;
  }

  return os << "SyncFlag(" << uint32_t(flag) << ")";
}


std::ostream& operator << (std::ostream& os, GlobalFlag flag) {
  switch (flag) {
    case GlobalFlag::eRefactoringAllowed:       return os << "refactoringAllowed";
    case GlobalFlag::eEnableFp64:               return os << "enableDoublePrecisionFloatOps";
    case GlobalFlag::eEarlyZ:                   return os << "forceEarlyDepthStencil";
    case GlobalFlag::eEnableRawStructuredCs4x:  return os << "enableRawAndStructuredBuffers";
    case GlobalFlag::eSkipOptimization:         return os << "skipOptimization";
    case GlobalFlag::eEnableMinPrecision:       return os << "enableMinimumPrecision";
    case GlobalFlag::eEnableExtFp64:            return os << "enable11_1DoubleExtensions";
    case GlobalFlag::eEnableExtNonFp64:         return os << "enable11_1Extensions";
    case GlobalFlag::eFlagEnum:                 break;
  }

  return os << "GlobalFlag(" << uint32_t(flag) << ")";
}


std::ostream& operator << (std::ostream& os, SampledType type) {
  switch (type) {
    case SampledType::eUnorm:   return os << "unorm";
    case SampledType::eSnorm:   return os << "snorm";
    case SampledType::eSint:    return os << "int";
    case SampledType::eUint:    return os << "uint";
    case SampledType::eFloat:   return os << "float";
    case SampledType::eMixed:   return os << "mixed";
    case SampledType::eDouble:  return os << "double";
  }

  return os << "SampledType(" << uint32_t(type) << ")";
}


std::ostream& operator << (std::ostream& os, ResourceDim dim) {
  switch (dim) {
    case ResourceDim::eUnknown:           break;
    case ResourceDim::eBuffer:            return os << "buffer";
    case ResourceDim::eTexture1D:         return os << "texture1d";
    case ResourceDim::eTexture2D:         return os << "texture2d";
    case ResourceDim::eTexture2DMS:       return os << "texture2dms";
    case ResourceDim::eTexture3D:         return os << "texture3d";
    case ResourceDim::eTextureCube:       return os << "texturecube";
    case ResourceDim::eTexture1DArray:    return os << "texture1darray";
    case ResourceDim::eTexture2DArray:    return os << "texture2darray";
    case ResourceDim::eTexture2DMSArray:  return os << "texture2dmsarray";
    case ResourceDim::eTextureCubeArray:  return os << "texturecubearray";
    case ResourceDim::eRawBuffer:         return os << "raw_buffer";
    case ResourceDim::eStructuredBuffer:  return os << "structured_buffer";
  }

  return os << "ResourceDim(" << uint32_t(dim) << ")";
}


std::ostream& operator << (std::ostream& os, InterpolationMode mode) {
  switch (mode) {
    case InterpolationMode::eUndefined:                   break;
    case InterpolationMode::eConstant:                    return os << "constant";
    case InterpolationMode::eLinear:                      return os << "linear";
    case InterpolationMode::eLinearCentroid:              return os << "linear centroid";
    case InterpolationMode::eLinearNoPerspective:         return os << "linear noperspective";
    case InterpolationMode::eLinearNoPerspectiveCentroid: return os << "linear noperspective centroid";
    case InterpolationMode::eLinearSample:                return os << "linear sample";
    case InterpolationMode::eLinearNoPerspectiveSample:   return os << "linear noperspective sample";
  }

  return os << "InterpolationMode(" << uint32_t(mode) << ")";
}


std::ostream& operator << (std::ostream& os, PrimitiveTopology prim) {
  switch (prim) {
    case PrimitiveTopology::eUndefined:         break;
    case PrimitiveTopology::ePointList:         return os << "pointlist";
    case PrimitiveTopology::eLineList:          return os << "linelist";
    case PrimitiveTopology::eLineStrip:         return os << "linestrip";
    case PrimitiveTopology::eTriangleList:      return os << "trianglelist";
    case PrimitiveTopology::eTriangleStrip:     return os << "trianglestrip";
    case PrimitiveTopology::eLineListAdj:       return os << "linelistadj";
    case PrimitiveTopology::eLineStripAdj:      return os << "linestripadj";
    case PrimitiveTopology::eTriangleListAdj:   return os << "trianglelistadj";
    case PrimitiveTopology::eTriangleStripAdj:  return os << "trianglestripadj";
  }

  return os << "PrimitiveTopology(" << uint32_t(prim) << ")";
}


std::ostream& operator << (std::ostream& os, PrimitiveType prim) {
  switch (prim) {
    case PrimitiveType::eUndefined:             break;
    case PrimitiveType::ePoint:                 return os << "point";
    case PrimitiveType::eLine:                  return os << "line";
    case PrimitiveType::eTriangle:              return os << "triangle";
    case PrimitiveType::eLineAdj:               return os << "lineadj";
    case PrimitiveType::eTriangleAdj:           return os << "triangleadj";

    default: {
      if (prim >= PrimitiveType::ePatch1 && prim <= PrimitiveType::ePatch32)
        return os << "patch" << (uint32_t(prim) - uint32_t(PrimitiveType::ePatch1) + 1u);
    }
  }

  return os << "PrimitiveType(" << uint32_t(prim) << ")";
}


std::ostream& operator << (std::ostream& os, TessDomain domain) {
  switch (domain) {
    case TessDomain::eUndefined:    break;
    case TessDomain::eIsoline:      return os << "domain_isoline";
    case TessDomain::eTriangle:     return os << "domain_tri";
    case TessDomain::eQuad:         return os << "domain_quad";
  }

  return os << "TessDomain(" << uint32_t(domain) << ")";
}


std::ostream& operator << (std::ostream& os, TessPartitioning partitioning) {
  switch (partitioning) {
    case TessPartitioning::eUndefined:      break;
    case TessPartitioning::eInteger:        return os << "partitioning_integer";
    case TessPartitioning::ePow2:           return os << "partitioning_pow2";
    case TessPartitioning::eFractionalOdd:  return os << "partitioning_fractional_odd";
    case TessPartitioning::eFractionalEven: return os << "partitioning_fractional_even";
  }

  return os << "TessPartitioning(" << uint32_t(partitioning) << ")";
}


std::ostream& operator << (std::ostream& os, TessOutput output) {
  switch (output) {
    case TessOutput::eUndefined:    break;
    case TessOutput::ePoint:        return os << "output_point";
    case TessOutput::eLine:         return os << "output_line";
    case TessOutput::eTriangleCw:   return os << "output_triangle_cw";
    case TessOutput::eTriangleCcw:  return os << "output_triangle_ccw";
  }

  return os << "TessOutput(" << uint32_t(output) << ")";
}


std::ostream& operator << (std::ostream& os, SamplerMode mode) {
  switch (mode) {
    case SamplerMode::eDefault:     return os << "default";
    case SamplerMode::eComparison:  return os << "comparison";
    case SamplerMode::eMono:        return os << "mono";
  }

  return os << "SamplerMode(" << uint32_t(mode) << ")";
}


std::ostream& operator << (std::ostream& os, ComponentType type) {
  switch (type) {
    case ComponentType::eVoid:      return os << "void";
    case ComponentType::eUint:      return os << "uint";
    case ComponentType::eSint:      return os << "int";
    case ComponentType::eFloat:     return os << "float";
    case ComponentType::eBool:      return os << "bool";
    case ComponentType::eDouble:    return os << "double";
  }

  return os << "ComponentType(" << uint32_t(type) << ")";
}

std::ostream& operator << (std::ostream& os, MinPrecision precision) {
  switch (precision) {
    case MinPrecision::eNone:       return os << "none";
    case MinPrecision::eMin16Float: return os << "min16f";
    case MinPrecision::eMin10Float: return os << "min2_8f";
    case MinPrecision::eMin16Uint:  return os << "min16u";
    case MinPrecision::eMin16Sint:  return os << "min16i";
  }

  return os << "MinPrecision(" << uint32_t(precision) << ")";
}

}
