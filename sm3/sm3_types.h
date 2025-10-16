#pragma once

#include <cstdint>
#include <iostream>

#include "../util/util_swizzle.h"

namespace dxbc_spv::sm3 {

using util::Component;
using util::ComponentBit;
using util::WriteMask;
using util::Swizzle;

/** Opcode */
enum class OpCode : uint32_t {
  eNop          = 0u,
  eMov          = 1u,
  eAdd          = 2u,
  eSub          = 3u,
  eMad          = 4u,
  eMul          = 5u,
  eRcp          = 6u,
  eRsq          = 7u,
  eDp3          = 8u,
  eDp4          = 9u,
  eMin          = 10u,
  eMax          = 11u,
  eSlt          = 12u,
  eSge          = 13u,
  eExp          = 14u,
  eLog          = 15u,
  eLit          = 16u,
  eDst          = 17u,
  eLrp          = 18u,
  eFrc          = 19u,
  eM4x4         = 20u,
  eM4x3         = 21u,
  eM3x4         = 22u,
  eM3x3         = 23u,
  eM3x2         = 24u,
  eCall         = 25u,
  eCallNz       = 26u,
  eLoop         = 27u,
  eRet          = 28u,
  eEndLoop      = 29u,
  eLabel        = 30u,
  eDcl          = 31u,
  ePow          = 32u,
  eCrs          = 33u,
  eSgn          = 34u,
  eAbs          = 35u,
  eNrm          = 36u,
  eSinCos       = 37u,
  eRep          = 38u,
  eEndRep       = 39u,
  eIf           = 40u,
  eIfc          = 41u,
  eElse         = 42u,
  eEndIf        = 43u,
  eBreak        = 44u,
  eBreakC       = 45u,
  eMova         = 46u,
  eDefB         = 47u,
  eDefI         = 48u,

  eTexCrd       = 64u,
  eTexKill      = 65u,
  eTexLd        = 66u,
  eTexBem       = 67u,
  eTexBemL      = 68u,
  eTexReg2Ar    = 69u,
  eTexReg2Gb    = 70u,
  eTexM3x2Pad   = 71u,
  eTexM3x2Tex   = 72u,
  eTexM3x3Pad   = 73u,
  eTexM3x3Tex   = 74u,
  eReserved0    = 75u,
  eTexM3x3Spec  = 76u,
  eTexM3x3VSpec = 77u,
  eExpP         = 78u,
  eLogP         = 79u,
  eCnd          = 80u,
  eDef          = 81u,
  eTexReg2Rgb   = 82u,
  eTexDp3Tex    = 83u,
  eTexM3x2Depth = 84u,
  eTexDp3       = 85u,
  eTexM3x3      = 86u,
  eTexDepth     = 87u,
  eCmp          = 88u,
  eBem          = 89u,
  eDp2Add       = 90u,
  eDsX          = 91u,
  eDsY          = 92u,
  eTexLdd       = 93u,
  eSetP         = 94u,
  eTexLdl       = 95u,
  eBreakP       = 96u,

  ePhase        = 0xfffdu,
  eComment      = 0xfffeu,
  eEnd          = 0xffffu,
};

/** Operand type */
enum class RegisterType : uint32_t {
  eTemp           =  0u, // Temporary Register File
  eInput          =  1u, // Input Register File
  eConst          =  2u, // Constant Register File
  eAddr           =  3u, // Address Register (VS)
  eTexture        =  3u, // Texture Register File (PS). Same value as eAddr.
  eRasterizerOut  =  4u, // Rasterizer Register File
  eAttributeOut   =  5u, // Attribute Output Register File
  eTexCoordOut    =  6u, // Texture Coordinate Output Register File
  eOutput         =  6u, // Output register file for VS3.0+. Same value as eTexCoordOut.
  eConstInt       =  7u, // Constant Integer Vector Register File
  eColorOut       =  8u, // Color Output Register File
  eDepthOut       =  9u, // Depth Output Register File
  eSampler        = 10u, // Sampler State Register File
  eConst2         = 11u, // Constant Register File  2048 - 4095
  eConst3         = 12u, // Constant Register File  4096 - 6143
  eConst4         = 13u, // Constant Register File  6144 - 8191
  eConstBool      = 14u, // Constant Boolean register file
  eLoop           = 15u, // Loop counter register file
  eTempFloat16    = 16u, // 16-bit float temp register file
  eMiscType       = 17u, // Miscellaneous (single) registers.
  eLabel          = 18u, // Label
  ePredicate      = 19u, // Predicate register
  ePixelTexCoord  = 20u,
};

/** Usage used in the semantics used for shader IO */
enum class SemanticUsage : uint32_t {
  ePosition        = 0u,
  eBlendWeight     = 1u,
  eBlendIndices    = 2u,
  eNormal          = 3u,
  ePointSize       = 4u,
  eTexCoord        = 5u,
  eTangent         = 6u,
  eBinormal        = 7u,
  eTessFactor      = 8u,
  ePositionT       = 9u,
  eColor           = 10u,
  eFog             = 11u,
  eDepth           = 12u,
  eSample          = 13u,
};

struct Semantic {
  SemanticUsage usage;
  uint32_t      index;

  bool operator==(const Semantic& other) const {
    return usage == other.usage && index == other.index;
  }

  bool operator!=(const Semantic& other) const {
    return !(*this == other);
  }
};

/** Texture type */
enum class TextureType : uint32_t {
  eTexture2D   = 2u,
  eTextureCube = 3u,
  eTexture3D   = 4u
};

/** Valid indices for RasterizerOut registers */
enum class RasterizerOutIndex : uint32_t {
  eRasterOutPosition  = 0,
  eRasterOutFog       = 1,
  eRasterOutPointSize = 2
};

/** Valid indices for MiscType registers */
enum class MiscTypeIndex : uint32_t {
  eMiscTypePosition,
  eMiscTypeFace,
};

/* Comparison modes for usage with the ifc instruction */
enum class ComparisonMode : uint32_t {
  eNever        = 0u,
  eGreaterThan  = 1u,
  eEqual        = 2u,
  eGreaterEqual = 3u,
  eLessThan     = 4u,
  eNotEqual     = 5u,
  eLessEqual    = 6u,
  eAlways       = 7u,
};

enum class TexLdMode : uint32_t {
  eRegular = 0u,
  eProject = 1u,
  eBias    = 2u,
};

enum class ShaderType : uint32_t {
  eVertex   = 0u,
  ePixel    = 1u,
};

struct UnambiguousRegisterType {
  RegisterType registerType;
  ShaderType shaderType;
  uint32_t shaderVersionMajor;
};

std::ostream& operator << (std::ostream& os, ShaderType type);
std::ostream& operator << (std::ostream& os, UnambiguousRegisterType registerType);
std::ostream& operator << (std::ostream& os, OpCode op);
std::ostream& operator << (std::ostream& os, SemanticUsage usage);
std::ostream& operator << (std::ostream& os, TextureType textureType);
std::ostream& operator << (std::ostream& os, RasterizerOutIndex outIndex);
std::ostream& operator << (std::ostream& os, MiscTypeIndex miscTypeIndex);

}
