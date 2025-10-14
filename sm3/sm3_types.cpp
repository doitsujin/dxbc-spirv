#include "sm3_types.h"

namespace dxbc_spv::sm3 {

std::ostream& operator << (std::ostream& os, ShaderType type) {
  switch (type) {
    case ShaderType::eVertex: return os << "vs";
    case ShaderType::ePixel:  return os << "ps";
  }

  return os << "ShaderType(" << uint32_t(type) << ")";
}

std::ostream& operator << (std::ostream& os, OpCode op) {
  switch (op) {
    case OpCode::eNop:          return os << "nop";
    case OpCode::eMov:          return os << "mov";
    case OpCode::eAdd:          return os << "add";
    case OpCode::eSub:          return os << "sub";
    case OpCode::eMad:          return os << "mad";
    case OpCode::eMul:          return os << "mul";
    case OpCode::eRcp:          return os << "rcp";
    case OpCode::eRsq:          return os << "rsq";
    case OpCode::eDp3:          return os << "dp3";
    case OpCode::eDp4:          return os << "dp4";
    case OpCode::eMin:          return os << "min";
    case OpCode::eMax:          return os << "max";
    case OpCode::eSlt:          return os << "slt";
    case OpCode::eSge:          return os << "sge";
    case OpCode::eExp:          return os << "exp";
    case OpCode::eLog:          return os << "log";
    case OpCode::eLit:          return os << "lit";
    case OpCode::eDst:          return os << "dst";
    case OpCode::eLrp:          return os << "lrp";
    case OpCode::eFrc:          return os << "frc";
    case OpCode::eM4x4:         return os << "m4x4";
    case OpCode::eM4x3:         return os << "m4x3";
    case OpCode::eM3x4:         return os << "m3x4";
    case OpCode::eM3x3:         return os << "m3x3";
    case OpCode::eM3x2:         return os << "m3x2";
    case OpCode::eCall:         return os << "call";
    case OpCode::eCallNz:       return os << "call_nz";
    case OpCode::eLoop:         return os << "loop";
    case OpCode::eRet:          return os << "ret";
    case OpCode::eEndLoop:      return os << "endLoop";
    case OpCode::eLabel:        return os << "label";
    case OpCode::eDcl:          return os << "dcl";
    case OpCode::ePow:          return os << "pow";
    case OpCode::eCrs:          return os << "crs";
    case OpCode::eSgn:          return os << "sgn";
    case OpCode::eAbs:          return os << "abs";
    case OpCode::eNrm:          return os << "nrm";
    case OpCode::eSinCos:       return os << "sincos";
    case OpCode::eRep:          return os << "rep";
    case OpCode::eEndRep:       return os << "endrep";
    case OpCode::eIf:           return os << "if";
    case OpCode::eIfc:          return os << "if";
    case OpCode::eElse:         return os << "else";
    case OpCode::eEndIf:        return os << "endif";
    case OpCode::eBreak:        return os << "break";
    case OpCode::eBreakC:       return os << "break";
    case OpCode::eMova:         return os << "mova";
    case OpCode::eDefB:         return os << "defb";
    case OpCode::eDefI:         return os << "defi";

    case OpCode::eTexCrd:     return os << "texcoord";
    case OpCode::eTexKill:      return os << "texkill";
    case OpCode::eTexLd:          return os << "texld";
    case OpCode::eTexBem:       return os << "texbem";
    case OpCode::eTexBemL:      return os << "texbeml";
    case OpCode::eTexReg2Ar:    return os << "texreg2ar";
    case OpCode::eTexReg2Gb:    return os << "texreg2gb";
    case OpCode::eTexM3x2Pad:   return os << "texm3x2pad";
    case OpCode::eTexM3x2Tex:   return os << "texm3x2tex";
    case OpCode::eTexM3x3Pad:   return os << "texm3x3pad";
    case OpCode::eTexM3x3Tex:   return os << "texm3x3tex";
    case OpCode::eReserved0:    return os << "reserved0";
    case OpCode::eTexM3x3Spec:  return os << "texm3x3spec";
    case OpCode::eTexM3x3VSpec: return os << "texm3x3vspec";
    case OpCode::eExpP:         return os << "ExpP";
    case OpCode::eLogP:         return os << "LogP";
    case OpCode::eCnd:          return os << "Cnd";
    case OpCode::eDef:          return os << "def";
    case OpCode::eTexReg2Rgb:   return os << "texreg2rgb";
    case OpCode::eTexDp3Tex:    return os << "texdp3tex";
    case OpCode::eTexM3x2Depth: return os << "texm3x2depth";
    case OpCode::eTexDp3:       return os << "texdp3";
    case OpCode::eTexM3x3:      return os << "texm3x3";
    case OpCode::eTexDepth:     return os << "texdepth";
    case OpCode::eCmp:          return os << "cmp";
    case OpCode::eBem:          return os << "bem";
    case OpCode::eDp2Add:       return os << "dp2add";
    case OpCode::eDsX:          return os << "dsX";
    case OpCode::eDsY:          return os << "dsY";
    case OpCode::eTexLdd:       return os << "texldd";
    case OpCode::eSetP:         return os << "setp";
    case OpCode::eTexLdl:       return os << "texldl";
    case OpCode::eBreakP:       return os << "break";

    case OpCode::ePhase:        return os << "phase";
    case OpCode::eComment:      return os << "comment";
    case OpCode::eEnd:          return os << "end";
  }

  return os << "Opcode(" << uint32_t(op) << ")";
}

std::ostream& operator << (std::ostream& os, SemanticUsage usage) {
  switch (usage) {
    case SemanticUsage::ePosition:     return os << "position";
    case SemanticUsage::eBlendWeight:  return os << "weight";
    case SemanticUsage::eBlendIndices: return os << "blend";
    case SemanticUsage::eNormal:       return os << "normal";
    case SemanticUsage::ePointSize:    return os << "psize";
    case SemanticUsage::eTexCoord:     return os << "texcoord";
    case SemanticUsage::eTangent:      return os << "tangent";
    case SemanticUsage::eBinormal:     return os << "binormal";
    case SemanticUsage::eTessFactor:   return os << "tessfactor";
    case SemanticUsage::ePositionT:    return os << "position_t";
    case SemanticUsage::eColor:        return os << "color";
    case SemanticUsage::eFog:          return os << "fog";
    case SemanticUsage::eDepth:        return os << "depth";
    case SemanticUsage::eSample:       return os << "sample";
  }

  return os << "SemanticUsage(" << uint32_t(usage) << ")";
}

std::ostream& operator << (std::ostream& os, TextureType textureType) {
  switch (textureType) {
    case TextureType::eTexture2D:   return os << "2d";
    case TextureType::eTextureCube: return os << "cube";
    case TextureType::eTexture3D:   return os << "3d";
  }

  return os << "TextureType(" << uint32_t(textureType) << ")";
}

std::ostream& operator << (std::ostream& os, RasterizerOutIndex outIndex) {
  switch (outIndex) {
    case RasterizerOutIndex::eRasterOutPosition:  return os << "Pos";
    case RasterizerOutIndex::eRasterOutFog:       return os << "Fog";
    case RasterizerOutIndex::eRasterOutPointSize: return os << "PointSize";
  }

  return os << "RasterizerOutValue(" << uint32_t(outIndex) << ")";
}

std::ostream& operator << (std::ostream& os, MiscTypeIndex miscTypeIndex) {
  switch (miscTypeIndex) {
    case MiscTypeIndex::eMiscTypePosition: return os << "Pos";
    case MiscTypeIndex::eMiscTypeFace:     return os << "Face";
  }

  return os << "MiscTypeIndex(" << uint32_t(miscTypeIndex) << ")";
}


std::ostream& operator << (std::ostream& os, UnambiguousRegisterType registerType) {
  switch (registerType.registerType) {
    case RegisterType::eTemp:          return os << "r";
    case RegisterType::eMiscType:
    case RegisterType::eInput:         return os << "v";
    case RegisterType::eConst:
    case RegisterType::eConst2:
    case RegisterType::eConst3:
    case RegisterType::eConst4:        return os << "c";
    case RegisterType::eAddr:
    // case RegisterType::eTexture: Same value
      return os << (registerType.shaderType == ShaderType::eVertex ? "a" : "t");
    case RegisterType::eRasterizerOut:
    case RegisterType::eAttributeOut:  return os << "o";
    case RegisterType::eTexCoordOut:
    // case RegisterType::eOutput: Same value.
      return os << (registerType.shaderVersionMajor == 3 ? "o" : "oT");
    case RegisterType::eConstBool:     return os << "b";
    case RegisterType::eLoop:          return os << "aL";
    case RegisterType::ePredicate:     return os << "p";
    case RegisterType::ePixelTexCoord: return os << "t";
    case RegisterType::eConstInt:      return os << "i";
    case RegisterType::eColorOut:      return os << "oC";
    case RegisterType::eDepthOut:      return os << "oDepth";
    case RegisterType::eSampler:       return os << "s";
    case RegisterType::eTempFloat16:   return os << "half";
    case RegisterType::eLabel:         return os << "l";
  }

  return os << "Register(" << uint32_t(registerType.registerType) << ")";
}


}
