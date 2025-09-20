#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>

#include "../ir/ir.h"

#include "../util/util_bit.h"
#include "../util/util_flags.h"
#include "../util/util_swizzle.h"

namespace dxbc_spv::dxbc {

using util::Component;
using util::ComponentBit;
using util::WriteMask;
using util::Swizzle;

/** Opcode */
enum class OpCode : uint32_t {
  eAdd                                   = 0u,
  eAnd                                   = 1u,
  eBreak                                 = 2u,
  eBreakc                                = 3u,
  eCall                                  = 4u,
  eCallc                                 = 5u,
  eCase                                  = 6u,
  eContinue                              = 7u,
  eContinuec                             = 8u,
  eCut                                   = 9u,
  eDefault                               = 10u,
  eDerivRtx                              = 11u,
  eDerivRty                              = 12u,
  eDiscard                               = 13u,
  eDiv                                   = 14u,
  eDp2                                   = 15u,
  eDp3                                   = 16u,
  eDp4                                   = 17u,
  eElse                                  = 18u,
  eEmit                                  = 19u,
  eEmitThenCut                           = 20u,
  eEndIf                                 = 21u,
  eEndLoop                               = 22u,
  eEndSwitch                             = 23u,
  eEq                                    = 24u,
  eExp                                   = 25u,
  eFrc                                   = 26u,
  eFtoI                                  = 27u,
  eFtoU                                  = 28u,
  eGe                                    = 29u,
  eIAdd                                  = 30u,
  eIf                                    = 31u,
  eIEq                                   = 32u,
  eIGe                                   = 33u,
  eILt                                   = 34u,
  eIMad                                  = 35u,
  eIMax                                  = 36u,
  eIMin                                  = 37u,
  eIMul                                  = 38u,
  eINe                                   = 39u,
  eINeg                                  = 40u,
  eIShl                                  = 41u,
  eIShr                                  = 42u,
  eItoF                                  = 43u,
  eLabel                                 = 44u,
  eLd                                    = 45u,
  eLdMs                                  = 46u,
  eLog                                   = 47u,
  eLoop                                  = 48u,
  eLt                                    = 49u,
  eMad                                   = 50u,
  eMin                                   = 51u,
  eMax                                   = 52u,
  eCustomData                            = 53u,
  eMov                                   = 54u,
  eMovc                                  = 55u,
  eMul                                   = 56u,
  eNe                                    = 57u,
  eNop                                   = 58u,
  eNot                                   = 59u,
  eOr                                    = 60u,
  eResInfo                               = 61u,
  eRet                                   = 62u,
  eRetc                                  = 63u,
  eRoundNe                               = 64u,
  eRoundNi                               = 65u,
  eRoundPi                               = 66u,
  eRoundZ                                = 67u,
  eRsq                                   = 68u,
  eSample                                = 69u,
  eSampleC                               = 70u,
  eSampleClz                             = 71u,
  eSampleL                               = 72u,
  eSampleD                               = 73u,
  eSampleB                               = 74u,
  eSqrt                                  = 75u,
  eSwitch                                = 76u,
  eSinCos                                = 77u,
  eUDiv                                  = 78u,
  eULt                                   = 79u,
  eUGe                                   = 80u,
  eUMul                                  = 81u,
  eUMad                                  = 82u,
  eUMax                                  = 83u,
  eUMin                                  = 84u,
  eUShr                                  = 85u,
  eUtoF                                  = 86u,
  eXor                                   = 87u,
  eDclResource                           = 88u,
  eDclConstantBuffer                     = 89u,
  eDclSampler                            = 90u,
  eDclIndexRange                         = 91u,
  eDclGsOutputPrimitiveTopology          = 92u,
  eDclGsInputPrimitive                   = 93u,
  eDclMaxOutputVertexCount               = 94u,
  eDclInput                              = 95u,
  eDclInputSgv                           = 96u,
  eDclInputSiv                           = 97u,
  eDclInputPs                            = 98u,
  eDclInputPsSgv                         = 99u,
  eDclInputPsSiv                         = 100u,
  eDclOutput                             = 101u,
  eDclOutputSgv                          = 102u,
  eDclOutputSiv                          = 103u,
  eDclTemps                              = 104u,
  eDclIndexableTemp                      = 105u,
  eDclGlobalFlags                        = 106u,
  /* eReserved0                             = 107u, */
  eLod                                   = 108u,
  eGather4                               = 109u,
  eSamplePos                             = 110u,
  eSampleInfo                            = 111u,
  /* eReserved1                             = 112u, */
  eHsDecls                               = 113u,
  eHsControlPointPhase                   = 114u,
  eHsForkPhase                           = 115u,
  eHsJoinPhase                           = 116u,
  eEmitStream                            = 117u,
  eCutStream                             = 118u,
  eEmitThenCutStream                     = 119u,
  eInterfaceCall                         = 120u,
  eBufInfo                               = 121u,
  eDerivRtxCoarse                        = 122u,
  eDerivRtxFine                          = 123u,
  eDerivRtyCoarse                        = 124u,
  eDerivRtyFine                          = 125u,
  eGather4C                              = 126u,
  eGather4Po                             = 127u,
  eGather4PoC                            = 128u,
  eRcp                                   = 129u,
  eF32toF16                              = 130u,
  eF16toF32                              = 131u,
  eUAddc                                 = 132u,
  eUSubb                                 = 133u,
  eCountBits                             = 134u,
  eFirstBitHi                            = 135u,
  eFirstBitLo                            = 136u,
  eFirstBitShi                           = 137u,
  eUBfe                                  = 138u,
  eIBfe                                  = 139u,
  eBfi                                   = 140u,
  eBfRev                                 = 141u,
  eSwapc                                 = 142u,
  eDclStream                             = 143u,
  eDclFunctionBody                       = 144u,
  eDclFunctionTable                      = 145u,
  eDclInterface                          = 146u,
  eDclInputControlPointCount             = 147u,
  eDclOutputControlPointCount            = 148u,
  eDclTessDomain                         = 149u,
  eDclTessPartitioning                   = 150u,
  eDclTessOutputPrimitive                = 151u,
  eDclHsMaxTessFactor                    = 152u,
  eDclHsForkPhaseInstanceCount           = 153u,
  eDclHsJoinPhaseInstanceCount           = 154u,
  eDclThreadGroup                        = 155u,
  eDclUavTyped                           = 156u,
  eDclUavRaw                             = 157u,
  eDclUavStructured                      = 158u,
  eDclThreadGroupSharedMemoryRaw         = 159u,
  eDclThreadGroupSharedMemoryStructured  = 160u,
  eDclResourceRaw                        = 161u,
  eDclResourceStructured                 = 162u,
  eLdUavTyped                            = 163u,
  eStoreUavTyped                         = 164u,
  eLdRaw                                 = 165u,
  eStoreRaw                              = 166u,
  eLdStructured                          = 167u,
  eStoreStructured                       = 168u,
  eAtomicAnd                             = 169u,
  eAtomicOr                              = 170u,
  eAtomicXor                             = 171u,
  eAtomicCmpStore                        = 172u,
  eAtomicIAdd                            = 173u,
  eAtomicIMax                            = 174u,
  eAtomicIMin                            = 175u,
  eAtomicUMax                            = 176u,
  eAtomicUMin                            = 177u,
  eImmAtomicAlloc                        = 178u,
  eImmAtomicConsume                      = 179u,
  eImmAtomicIAdd                         = 180u,
  eImmAtomicAnd                          = 181u,
  eImmAtomicOr                           = 182u,
  eImmAtomicXor                          = 183u,
  eImmAtomicExch                         = 184u,
  eImmAtomicCmpExch                      = 185u,
  eImmAtomicIMax                         = 186u,
  eImmAtomicIMin                         = 187u,
  eImmAtomicUMax                         = 188u,
  eImmAtomicUMin                         = 189u,
  eSync                                  = 190u,
  eDAdd                                  = 191u,
  eDMax                                  = 192u,
  eDMin                                  = 193u,
  eDMul                                  = 194u,
  eDEq                                   = 195u,
  eDGe                                   = 196u,
  eDLt                                   = 197u,
  eDNe                                   = 198u,
  eDMov                                  = 199u,
  eDMovc                                 = 200u,
  eDtoF                                  = 201u,
  eFtoD                                  = 202u,
  eEvalSnapped                           = 203u,
  eEvalSampleIndex                       = 204u,
  eEvalCentroid                          = 205u,
  eDclGsInstanceCount                    = 206u,
  eAbort                                 = 207u,
  eDebugBreak                            = 208u,
  /* eReservedBegin11_1                     = 209u, */
  eDDiv                                  = 210u,
  eDFma                                  = 211u,
  eDRcp                                  = 212u,
  eMsad                                  = 213u,
  eDtoI                                  = 214u,
  eDtoU                                  = 215u,
  eItoD                                  = 216u,
  eUtoD                                  = 217u,
  /* ReservedBegin11_2                    = 218u, */
  eGather4S                              = 219u,
  eGather4CS                             = 220u,
  eGather4PoS                            = 221u,
  eGather4PoCS                           = 222u,
  eLdS                                   = 223u,
  eLdMsS                                 = 224u,
  eLdUavTypedS                           = 225u,
  eLdRawS                                = 226u,
  eLdStructuredS                         = 227u,
  eSampleLS                              = 228u,
  eSampleClzS                            = 229u,
  eSampleClampS                          = 230u,
  eSampleBClampS                         = 231u,
  eSampleDClampS                         = 232u,
  eSampleCClampS                         = 233u,
  eCheckAccessFullyMapped                = 234u,
};


/** Operand type */
enum class RegisterType : uint32_t {
  eTemp                     = 0u,
  eInput                    = 1u,
  eOutput                   = 2u,
  eIndexableTemp            = 3u,
  eImm32                    = 4u,
  eImm64                    = 5u,
  eSampler                  = 6u,
  eResource                 = 7u,
  eCbv                      = 8u,
  eIcb                      = 9u,
  eLabel                    = 10u,
  ePrimitiveId              = 11u,
  eDepth                    = 12u,
  eNull                     = 13u,
  eRasterizer               = 14u,
  eCoverageOut              = 15u,
  eStream                   = 16u,
  eFunctionBody             = 17u,
  eFunctionTable            = 18u,
  eInterface                = 19u,
  eFunctionInput            = 20u,
  eFunctionOutput           = 21u,
  eControlPointId           = 22u,
  eForkInstanceId           = 23u,
  eJoinInstanceId           = 24u,
  eControlPointIn           = 25u,
  eControlPointOut          = 26u,
  ePatchConstant            = 27u,
  eTessCoord                = 28u,
  eThis                     = 29u,
  eUav                      = 30u,
  eTgsm                     = 31u,
  eThreadId                 = 32u,
  eThreadGroupId            = 33u,
  eThreadIdInGroup          = 34u,
  eCoverageIn               = 35u,
  eThreadIndexInGroup       = 36u,
  eGsInstanceId             = 37u,
  eDepthGe                  = 38u,
  eDepthLe                  = 39u,
  eCycleCounter             = 40u,
  eStencilRef               = 41u,
  eInnerCoverage            = 42u,
};


/** System value enum for system values included in signatures.
 *  Any system values not included here use dedicated registers.
 *  Some of these system values may use dedicated registers and
 *  will thus have a register index of -1 in the signature. */
enum class Sysval : uint32_t {
  eNone                   = 0u,
  ePosition               = 1u,
  eClipDistance           = 2u,
  eCullDistance           = 3u,
  eRenderTargetId         = 4u,
  eViewportId             = 5u,
  eVertexId               = 6u,
  ePrimitiveId            = 7u,
  eInstanceId             = 8u,
  eIsFrontFace            = 9u,
  eSampleIndex            = 10u,
  eQuadU0EdgeTessFactor   = 11u,
  eQuadV0EdgeTessFactor   = 12u,
  eQuadU1EdgeTessFactor   = 13u,
  eQuadV1EdgeTessFactor   = 14u,
  eQuadUInsideTessFactor  = 15u,
  eQuadVInsideTessFactor  = 16u,
  eTriUEdgeTessFactor     = 17u,
  eTriVEdgeTessFactor     = 18u,
  eTriWEdgeTessFactor     = 19u,
  eTriInsideTessFactor    = 20u,
  eLineDetailTessFactor   = 21u,
  eLineDensityTessFactor  = 22u,
};


/** Scalar component type */
enum class ComponentType : uint32_t {
  eVoid                   = 0u,
  eUint                   = 1u,
  eSint                   = 2u,
  eFloat                  = 3u,
  eBool                   = 4u,
  eDouble                 = 5u,
};


/** Min precision type */
enum class MinPrecision : uint32_t {
  eNone                   = 0u,
  eMin16Float             = 1u,
  eMin10Float             = 2u,
  eMin16Sint              = 4u,
  eMin16Uint              = 5u,
};


/** Determines the scalar type to use for a given combination
 *  of DXBC component types and min precision flags. */
ir::ScalarType resolveType(ComponentType type, MinPrecision precision);


/** Determines the DXBC component type for a given scalar type */
std::pair<ComponentType, MinPrecision> determineComponentType(ir::ScalarType type);


/** Boolean test */
enum class TestBoolean : uint32_t {
  eZero                   = 0u,
  eNonZero                = 1u,
};


/** Instruction return type */
enum class ReturnType : uint32_t {
  eFloat                  = 0u,
  eUint                   = 1u,
};


/** Resinfo return type */
enum class ResInfoType : uint32_t {
  eFloat                  = 0u,
  eRcpFloat               = 1u,
  eUint                   = 2u,
};


/** UAV declaration flags */
enum class UavFlag : uint32_t {
  eGloballyCoherent       = (1u << 0),
  eRasterizerOrdered      = (1u << 1),

  eFlagEnum = 0u
};

using UavFlags = util::Flags<UavFlag>;


/** Barrier sync flags */
enum class SyncFlag : uint32_t {
  eWorkgroupThreads       = (1u << 0),
  eWorkgroupMemory        = (1u << 1),
  eUavMemoryGlobal        = (1u << 2),
  eUavMemoryLocal         = (1u << 3),

  eFlagEnum = 0u
};

using SyncFlags = util::Flags<SyncFlag>;


/** Global flags */
enum class GlobalFlag : uint32_t {
  eRefactoringAllowed       = (1u << 0),
  eEnableFp64               = (1u << 1),
  eEarlyZ                   = (1u << 2),
  eEnableRawStructuredCs4x  = (1u << 3),
  eSkipOptimization         = (1u << 4),
  eEnableMinPrecision       = (1u << 5),
  eEnableExtFp64            = (1u << 6),
  eEnableExtNonFp64         = (1u << 7),

  eFlagEnum = 0u
};

using GlobalFlags = util::Flags<GlobalFlag>;


/** Resource return type */
enum class SampledType : uint32_t {
  eUnorm  = 1u,
  eSnorm  = 2u,
  eSint   = 3u,
  eUint   = 4u,
  eFloat  = 5u,
  eMixed  = 6u,
  eDouble = 7u,
};

/** Determines scalar type for a resource declaration */
ir::ScalarType resolveSampledType(SampledType type);

/** Retrieves resource type from scalar type */
SampledType determineResourceType(ir::ScalarType type);


/** Resource dimension */
enum class ResourceDim : uint32_t {
  eUnknown           =  0u,
  eBuffer            =  1u,
  eTexture1D         =  2u,
  eTexture2D         =  3u,
  eTexture2DMS       =  4u,
  eTexture3D         =  5u,
  eTextureCube       =  6u,
  eTexture1DArray    =  7u,
  eTexture2DArray    =  8u,
  eTexture2DMSArray  =  9u,
  eTextureCubeArray  = 10u,
  eRawBuffer         = 11u,
  eStructuredBuffer  = 12u,
};

std::optional<ir::ResourceKind> resolveResourceDim(ResourceDim dim);

ResourceDim determineResourceDim(ir::ResourceKind kind);


/** Pixel shader input interpolation mode */
enum class InterpolationMode : uint32_t {
  eUndefined                    = 0u,
  eConstant                     = 1u,
  eLinear                       = 2u,
  eLinearCentroid               = 3u,
  eLinearNoPerspective          = 4u,
  eLinearNoPerspectiveCentroid  = 5u,
  eLinearSample                 = 6u,
  eLinearNoPerspectiveSample    = 7u,
};

std::optional<ir::InterpolationModes> resolveInterpolationMode(InterpolationMode mode);

InterpolationMode determineInterpolationMode(ir::InterpolationModes mode);


/** Primitive topology */
enum class PrimitiveTopology : uint32_t {
  eUndefined                    = 0u,
  ePointList                    = 1u,
  eLineList                     = 2u,
  eLineStrip                    = 3u,
  eTriangleList                 = 4u,
  eTriangleStrip                = 5u,
  eLineListAdj                  = 10u,
  eLineStripAdj                 = 11u,
  eTriangleListAdj              = 12u,
  eTriangleStripAdj             = 13u,
};

std::optional<ir::PrimitiveType> resolvePrimitiveTopology(PrimitiveTopology type);

PrimitiveTopology determinePrimitiveTopology(ir::PrimitiveType type, bool strip);

/** Primitive type */
enum class PrimitiveType : uint32_t {
  eUndefined                    = 0u,
  ePoint                        = 1u,
  eLine                         = 2u,
  eTriangle                     = 3u,
  eLineAdj                      = 6u,
  eTriangleAdj                  = 7u,
  ePatch1                       = 8u,
  ePatch32                      = 39u,
};

std::optional<ir::PrimitiveType> resolvePrimitiveType(PrimitiveType type);

PrimitiveType determinePrimitiveType(ir::PrimitiveType type);


/** Tessellator domain */
enum class TessDomain : uint32_t {
  eUndefined                    = 0u,
  eIsoline                      = 1u,
  eTriangle                     = 2u,
  eQuad                         = 3u,
};

std::optional<ir::PrimitiveType> resolveTessDomain(TessDomain domain);

TessDomain determineTessDomain(ir::PrimitiveType domain);


/** Tessellator partitioning */
enum class TessPartitioning : uint32_t {
  eUndefined                    = 0u,
  eInteger                      = 1u,
  ePow2                         = 2u,
  eFractionalOdd                = 3u,
  eFractionalEven               = 4u,
};

std::optional<ir::TessPartitioning> resolveTessPartitioning(TessPartitioning partitioning);

TessPartitioning determineTessPartitioning(ir::TessPartitioning partitioning);


/** Tessellator output */
enum class TessOutput : uint32_t {
  eUndefined                    = 0u,
  ePoint                        = 1u,
  eLine                         = 2u,
  eTriangleCw                   = 3u,
  eTriangleCcw                  = 4u,
};

std::optional<std::pair<ir::PrimitiveType, ir::TessWindingOrder>> resolveTessOutput(TessOutput output);

TessOutput determineTessOutput(ir::PrimitiveType type, ir::TessWindingOrder winding);


/** Hull shader phase for internal use */
enum class HullShaderPhase : uint32_t {
  eNone                         = 0u,
  eDcl                          = 1u,
  eControlPoint                 = 2u,
  eFork                         = 3u,
  eJoin                         = 4u,
};


/** Sampler mode */
enum class SamplerMode : uint32_t {
  eDefault                      = 0u,
  eComparison                   = 1u,
  eMono                         = 2u,
};

std::ostream& operator << (std::ostream& os, OpCode op);
std::ostream& operator << (std::ostream& os, Sysval sv);
std::ostream& operator << (std::ostream& os, TestBoolean mode);
std::ostream& operator << (std::ostream& os, ReturnType type);
std::ostream& operator << (std::ostream& os, ResInfoType type);
std::ostream& operator << (std::ostream& os, UavFlag flag);
std::ostream& operator << (std::ostream& os, SyncFlag flag);
std::ostream& operator << (std::ostream& os, GlobalFlag flag);
std::ostream& operator << (std::ostream& os, SampledType type);
std::ostream& operator << (std::ostream& os, ResourceDim dim);
std::ostream& operator << (std::ostream& os, InterpolationMode mode);
std::ostream& operator << (std::ostream& os, PrimitiveTopology prim);
std::ostream& operator << (std::ostream& os, PrimitiveType prim);
std::ostream& operator << (std::ostream& os, TessDomain domain);
std::ostream& operator << (std::ostream& os, TessPartitioning partitioning);
std::ostream& operator << (std::ostream& os, TessOutput output);
std::ostream& operator << (std::ostream& os, SamplerMode mode);
std::ostream& operator << (std::ostream& os, ComponentType type);
std::ostream& operator << (std::ostream& os, MinPrecision precision);

}
