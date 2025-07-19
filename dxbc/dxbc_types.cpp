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
    case ir::ScalarType::eVoid:
      return std::make_pair(ComponentType::eVoid, MinPrecision::eNone);

    case ir::ScalarType::eBool:
      return std::make_pair(ComponentType::eBool, MinPrecision::eNone);

    case ir::ScalarType::eMinU16:
      return std::make_pair(ComponentType::eUint, MinPrecision::eMin16Uint);

    case ir::ScalarType::eMinI16:
      return std::make_pair(ComponentType::eSint, MinPrecision::eMin16Sint);

    case ir::ScalarType::eMinF16:
      return std::make_pair(ComponentType::eFloat, MinPrecision::eMin16Float);

    case ir::ScalarType::eMinF10:
      return std::make_pair(ComponentType::eFloat, MinPrecision::eMin10Float);

    case ir::ScalarType::eU32:
      return std::make_pair(ComponentType::eUint, MinPrecision::eNone);

    case ir::ScalarType::eI32:
      return std::make_pair(ComponentType::eSint, MinPrecision::eNone);

    case ir::ScalarType::eF32:
      return std::make_pair(ComponentType::eFloat, MinPrecision::eNone);

    case ir::ScalarType::eF64:
      return std::make_pair(ComponentType::eDouble, MinPrecision::eNone);

    default:
      dxbc_spv_unreachable();
      return std::make_pair(ComponentType::eVoid, MinPrecision::eNone);
  }
}


WriteMask Swizzle::getReadMask(WriteMask accessMask) const {
  WriteMask readMask = 0u;

  for (uint32_t i = 0u; i < 4u; i++) {
    auto component = Component(i);

    if (accessMask & componentBit(component))
      readMask |= componentBit(map(component));
  }

  return readMask;
}


Swizzle Swizzle::compact(WriteMask accessMask) const {
  uint8_t swizzle = 0u;
  uint8_t shift = 0u;

  for (uint32_t i = 0u; i < 4u; i++) {
    auto component = Component(i);

    if (accessMask & componentBit(component)) {
      swizzle |= util::bextract(m_raw, 2u * i, 2u) << shift;
      shift += 2u;
    }
  }

  return Swizzle(swizzle);
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
    case OpCode::eEmitThenCut:                          return os << "emitThenCut";
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
    case OpCode::eCustomData:                           return os << "custom_data";
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
    case OpCode::eDclConstantBuffer:                    return os << "dcl_constantBuffer";
    case OpCode::eDclSampler:                           return os << "dcl_sampler";
    case OpCode::eDclIndexRange:                        return os << "dcl_indexRange";
    case OpCode::eDclGsOutputPrimitiveTopology:         return os << "dcl_outputTopology";
    case OpCode::eDclGsInputPrimitive:                  return os << "dcl_inputPrimitive";
    case OpCode::eDclMaxOutputVertexCount:              return os << "dcl_maxOutputVertexCount";
    case OpCode::eDclInput:                             return os << "dcl_input";
    case OpCode::eDclInputSgv:                          return os << "dcl_input_sgv";
    case OpCode::eDclInputSiv:                          return os << "dcl_input_siv";
    case OpCode::eDclInputPs:                           return os << "dcl_input_ps";
    case OpCode::eDclInputPsSgv:                        return os << "dcl_input_ps_sgv";
    case OpCode::eDclInputPsSiv:                        return os << "dcl_input_ps_siv";
    case OpCode::eDclOutput:                            return os << "dcl_output";
    case OpCode::eDclOutputSgv:                         return os << "dcl_output_sgv";
    case OpCode::eDclOutputSiv:                         return os << "dcl_output_siv";
    case OpCode::eDclTemps:                             return os << "dcl_temps";
    case OpCode::eDclIndexableTemp:                     return os << "dcl_indexableTemp";
    case OpCode::eDclGlobalFlags:                       return os << "dcl_globalFlags";
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
    case OpCode::eEmitThenCutStream:                    return os << "emitThenCut_stream";
    case OpCode::eInterfaceCall:                        return os << "interface_call";
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
    case OpCode::eDclThreadGroup:                       return os << "dcl_thread:group";
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
    case Sysval::eRenderTargetId:         return os << "rt_array_index";
    case Sysval::eViewportId:             return os << "viewport_array_index";
    case Sysval::eVertexId:               return os << "vertex_id";
    case Sysval::ePrimitiveId:            return os << "instance_id";
    case Sysval::eInstanceId:             return os << "primitive_id";
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
    case MinPrecision::eMin16Sint:  return os << "min10i";
  }

  return os << "MinPrecision(" << uint32_t(precision) << ")";
}

std::ostream& operator << (std::ostream& os, Component component) {
  switch (component) {
    case Component::eX: return os << 'x';
    case Component::eY: return os << 'y';
    case Component::eZ: return os << 'z';
    case Component::eW: return os << 'w';
  }

  return os << "Component(" << uint32_t(uint8_t(component)) << ")";
}

std::ostream& operator << (std::ostream& os, ComponentBit component) {
  switch (component) {
    case ComponentBit::eX: return os << 'x';
    case ComponentBit::eY: return os << 'y';
    case ComponentBit::eZ: return os << 'z';
    case ComponentBit::eW: return os << 'w';

    case ComponentBit::eAll:
    case ComponentBit::eFlagEnum: break;
  }

  return os << "ComponentBit(" << uint32_t(uint8_t(component)) << ")";
}

std::ostream& operator << (std::ostream& os, WriteMask mask) {
  for (uint32_t i = 0u; i < 4u; i++) {
    if (mask & componentBit(Component(i)))
      os << Component(i);
  }

  return os;
}

std::ostream& operator << (std::ostream& os, Swizzle swizzle) {
  for (uint32_t i = 0u; i < 4u; i++)
    os << swizzle.get(i);

  return os;
}

}
