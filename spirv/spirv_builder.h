#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../ir/ir.h"
#include "../ir/ir_builder.h"

#include "../util/util_small_vector.h"

#include "spirv_mapping.h"
#include "spirv_types.h"

namespace dxbc_spv::spirv {

/** SPIR-V lowering pass.
 *
 * This iteratively consumes IR instructions and translates
 * them to SPIR-V. This will target SPIR-V 1.6 with optional
 * extensions as defined in the options struct.
 */
class SpirvBuilder {

public:

  struct Options {
    /** Whether to include debug names. */
    bool includeDebugNames = true;
    /** Whether to use NV raw access chains for raw
     *  and structured buffer access. */
    bool nvRawAccessChains = false;
    /** Whether dual-source blending is enabled. This affects
     *  output location mapping in fragment shaders only. */
    bool dualSourceBlending = false;
    /** Whether FloatControls2 is fully supported */
    bool floatControls2 = false;
    /** Supported rounding modes for the given float types */
    ir::RoundModes supportedRoundModesF16 = 0u;
    ir::RoundModes supportedRoundModesF32 = 0u;
    ir::RoundModes supportedRoundModesF64 = 0u;
    /** Supported denorm modes for the given float tyoes */
    ir::DenormModes supportedDenormModesF16 = 0u;
    ir::DenormModes supportedDenormModesF32 = 0u;
    ir::DenormModes supportedDenormModesF64 = 0u;
    /** Supported signed zero / inf / nan preserve modes for
     *  legacy float control extensions. */
    bool supportsZeroInfNanPreserveF16 = false;
    bool supportsZeroInfNanPreserveF32 = false;
    bool supportsZeroInfNanPreserveF64 = false;
    /** Maximum size for constant buffers. Larger buffers will be emitted
     *  as storage buffers instead. If 0, constant buffers can have any size. */
    uint32_t maxCbvSize = 0u;
    /** Maximum number of constant buffers. Excess buffers will be emitted as
     *  storage buffers, ordered by space and register numbers. If negative,
     *  the number of constant buffers is effectively unlimited. */
    int32_t maxCbvCount = -1;
  };

  explicit SpirvBuilder(const ir::Builder& builder, ResourceMapping& mapping, const Options& options);

  ~SpirvBuilder();

  SpirvBuilder             (const SpirvBuilder&) = delete;
  SpirvBuilder& operator = (const SpirvBuilder&) = delete;

  /** Processes shader module. */
  void buildSpirvBinary();

  /** Retrieves SPIR-V binary as a pain dword array. */
  std::vector<uint32_t> getSpirvBinary() const;

  /** Retrieves SPIR-V binary. If \c data is \c nullptr,
   *  the total binary size will be written to \c size. */
  void getSpirvBinary(size_t& size, void* data) const;

private:

  const ir::Builder& m_builder;
  ResourceMapping& m_mapping;

  ir::ShaderStage m_stage = ir::ShaderStage();

  Options m_options;

  std::unordered_map<ir::Type, uint32_t> m_types;
  std::unordered_map<ir::SsaDef, ir::SsaDef> m_debugNames;
  std::unordered_map<SpirvPointerTypeKey, uint32_t> m_ptrTypes;
  std::unordered_map<SpirvFunctionTypeKey, uint32_t> m_funcTypes;
  std::unordered_map<SpirvImageTypeKey, uint32_t> m_imageTypes;
  std::unordered_map<SpirvConstant, uint32_t> m_constants;
  std::unordered_set<spv::Capability> m_enabledCaps;
  std::unordered_set<std::string> m_enabledExt;
  std::unordered_map<SpirvFunctionParameterKey, uint32_t> m_funcParamIds;
  std::unordered_map<uint32_t, uint32_t> m_sampledImageTypeIds;
  std::unordered_map<ir::SsaDef, uint32_t> m_descriptorTypes;
  std::unordered_map<ir::SsaDef, uint32_t> m_constantVars;
  std::unordered_map<SpirvBdaTypeKey, uint32_t> m_bdaTypeIds;
  std::unordered_set<ir::SsaDef> m_storageBufferCbv;

  std::vector<uint32_t> m_ssaDefsToId;

  SpirvHeader m_header;

  util::small_vector<uint32_t, 64>    m_capabilities;
  util::small_vector<uint32_t, 64>    m_extensions;
  util::small_vector<uint32_t, 64>    m_imports;
  util::small_vector<uint32_t, 8>     m_memoryModel;
  util::small_vector<uint32_t, 256>   m_entryPoint;
  util::small_vector<uint32_t, 64>    m_executionModes;
  util::small_vector<uint32_t, 64>    m_source;
  util::small_vector<uint32_t, 1024>  m_debug;
  util::small_vector<uint32_t, 1024>  m_decorations;

  std::vector<uint32_t> m_declarations;
  std::vector<uint32_t> m_code;

  uint32_t m_glslExtId = 0u;

  uint32_t m_samplerTypeId = 0u;

  struct {
    ir::OpFlags f16 = 0u;
    ir::OpFlags f32 = 0u;
    ir::OpFlags f64 = 0u;
  } m_fpMode;

  struct {
    ir::Op blockLabel = { };
  } m_structure;

  struct {
    uint32_t streamMask = 0u;
    uint32_t inputVertices = 0u;
  } m_geometry;

  struct {
    bool needsIoBarrier = false;
    uint32_t invocationId = 0u;
    uint32_t controlPointFuncId = 0u;
    uint32_t patchConstantFuncId = 0u;
  } m_tessControl;

  struct {
    uint32_t baseVertex = 0u;
    uint32_t baseInstance = 0u;
  } m_drawParams;

  struct PushDataInfo {
    ir::SsaDef def;
    uint32_t member;
    uint32_t offset;
  };

  struct {
    uint32_t blockId = 0u;
    util::small_vector<PushDataInfo, 64u> members;
  } m_pushData;

  uint32_t m_entryPointId = 0u;

  void processDebugNames();

  void demoteCbv();

  bool cbvAsSsbo(const ir::Op& op) const;

  void finalize();

  void emitHsEntryPoint();

  void emitInstruction(const ir::Op& op);

  void emitEntryPoint(const ir::Op& op);

  void emitSourceName(const ir::Op& op);

  void emitConstant(const ir::Op& op);

  void emitUndef(const ir::Op& op);

  void emitInterpolationModes(uint32_t id, ir::InterpolationModes modes);

  void emitDclSpecConstant(const ir::Op& op);

  void emitDclPushData(const ir::Op& op);

  void emitDclLds(const ir::Op& op);

  void emitDclScratch(const ir::Op& op);

  void emitDclIoVar(const ir::Op& op);

  uint32_t emitBuiltInDrawParameter(spv::BuiltIn builtIn);

  void emitDclBuiltInIoVar(const ir::Op& op);

  void emitDclXfb(const ir::Op& op);

  void emitDclSampler(const ir::Op& op);

  void emitDclCbv(const ir::Op& op);

  void emitDclSrvUav(const ir::Op& op);

  void emitDclUavCounter(const ir::Op& op);

  uint32_t getDescriptorArrayIndex(const ir::Op& op);

  uint32_t getImageDescriptorPointer(const ir::Op& op);

  void emitPushDataLoad(const ir::Op& op);

  void emitConstantLoad(const ir::Op& op);

  void emitDescriptorLoad(const ir::Op& op);

  void emitBufferLoad(const ir::Op& op);

  void emitBufferStore(const ir::Op& op);

  void emitBufferQuery(const ir::Op& op);

  void emitBufferAtomic(const ir::Op& op);

  void emitMemoryLoad(const ir::Op& op);

  void emitMemoryStore(const ir::Op& op);

  void emitMemoryAtomic(const ir::Op& op);

  void emitCounterAtomic(const ir::Op& op);

  void emitLdsAtomic(const ir::Op& op);

  uint32_t emitSampledImage(const ir::SsaDef& imageDef, const ir::SsaDef& samplerDef);

  uint32_t emitMergeImageCoordLayer(const ir::SsaDef& coordDef, const ir::SsaDef& layerDef);

  void emitImageLoad(const ir::Op& op);

  void emitImageStore(const ir::Op& op);

  void emitImageAtomic(const ir::Op& op);

  void emitImageSample(const ir::Op& op);

  void emitImageGather(const ir::Op& op);

  void emitImageComputeLod(const ir::Op& op);

  void emitImageQuerySize(const ir::Op& op);

  void emitImageQueryMips(const ir::Op& op);

  void emitImageQuerySamples(const ir::Op& op);

  void emitConvert(const ir::Op& op);

  void emitDerivative(const ir::Op& op);

  void emitAtomic(const ir::Op& op, const ir::Type& type, uint32_t id, ir::SsaDef operandDef,
      uint32_t ptrId, spv::Scope scope, spv::MemorySemanticsMask memoryTypes);

  void emitFunction(const ir::Op& op);

  void emitFunctionEnd();

  void emitFunctionCall(const ir::Op& op);

  void emitParamLoad(const ir::Op& op);

  void emitLabel(const ir::Op& op);

  void emitBranch(const ir::Op& op);

  void emitBranchConditional(const ir::Op& op);

  void emitSwitch(const ir::Op& op);

  void emitUnreachable();

  void emitReturn(const ir::Op& op);

  void emitPhi(const ir::Op& op);

  void emitStructuredInfo(const ir::Op& op);

  void emitBarrier(const ir::Op& op);

  void emitGsEmit(const ir::Op& op);

  void emitDemote();

  void emitRovLockBegin(const ir::Op& op);

  void emitRovLockEnd(const ir::Op& op);

  void emitPointer(const ir::Op& op);

  void emitDrain(const ir::Op& op);

  void emitMemoryModel();

  void emitFpMode(const ir::Op& op, uint32_t id, uint32_t mask = 0u);

  bool opSupportsFpMode(const ir::Op& op);

  void emitDebugName(ir::SsaDef def, uint32_t id);

  void emitDebugTypeName(ir::SsaDef def, uint32_t id, const char* suffix);

  void emitDebugMemberNames(ir::SsaDef def, uint32_t structId);

  void emitDebugPushDataName(const PushDataInfo& pushData, uint32_t structId, uint32_t memberIdx);

  uint32_t emitAddressOffset(ir::SsaDef def, uint32_t offset);

  uint32_t emitAccessChain(spv::Op op, spv::StorageClass storageClass, const ir::Type& baseType,
    uint32_t baseId, ir::SsaDef address, uint32_t offset, bool wrapperStruct);

  uint32_t emitAccessChain(spv::Op op, spv::StorageClass storageClass, ir::SsaDef base, ir::SsaDef address, bool wrapped);

  uint32_t emitRawStructuredElementAddress(const ir::Op& op, uint32_t stride);

  uint32_t emitStructuredByteOffset(const ir::Op& op, ir::Type type);

  uint32_t emitRawAccessChainNv(spv::StorageClass storageClass, const ir::Type& type, const ir::Op& resourceOp, uint32_t baseId, ir::SsaDef address);

  void emitCheckSparseAccess(const ir::Op& op);

  void emitLoadDrawParameterBuiltIn(const ir::Op& op, ir::BuiltIn builtIn);

  void emitLoadGsVertexCountBuiltIn(const ir::Op& op);

  void emitLoadSamplePositionBuiltIn(const ir::Op& op);

  void emitLoadVariable(const ir::Op& op);

  void emitStoreVariable(const ir::Op& op);

  void emitCompositeOp(const ir::Op& op);

  void emitCompositeConstruct(const ir::Op& op);

  void emitSimpleArithmetic(const ir::Op& op);

  void emitExtendedGlslArithmetic(const ir::Op& op);

  void emitExtendedIntArithmetic(const ir::Op& op);

  void emitFRcp(const ir::Op& op);

  void emitFRound(const ir::Op& op);

  void emitInterpolation(const ir::Op& op);

  void emitSetFpMode(const ir::Op& op);

  void emitSetCsWorkgroupSize(const ir::Op& op);

  void emitSetGsInstances(const ir::Op& op);

  void emitSetGsInputPrimitive(const ir::Op& op);

  void emitSetGsOutputVertices(const ir::Op& op);

  void emitSetGsOutputPrimitive(const ir::Op& op);

  void emitSetPsDepthMode(const ir::Op& op);

  void emitSetPsEarlyFragmentTest();

  void emitSetTessPrimitive(const ir::Op& op);

  void emitSetTessDomain(const ir::Op& op);

  void emitSetTessControlPoints(const ir::Op& op);

  uint32_t emitExtractComponent(ir::SsaDef vectorDef, uint32_t index);

  uint32_t importGlslExt();

  uint32_t allocId();

  void setIdForDef(ir::SsaDef def, uint32_t id);

  bool hasIdForDef(ir::SsaDef def) const;

  uint32_t getIdForDef(ir::SsaDef def);

  uint32_t getIdForType(const ir::Type& type);

  uint32_t defType(const ir::Type& type, bool explicitLayout, ir::SsaDef dclOp = ir::SsaDef());

  uint32_t defStructWrapper(uint32_t typeId);

  uint32_t defDescriptor(const ir::Op& op, uint32_t typeId, spv::StorageClass storageClass);

  uint32_t getIdForPtrType(uint32_t typeId, spv::StorageClass storageClass);

  uint32_t getIdForFuncType(const SpirvFunctionTypeKey& key);

  uint32_t getIdForImageType(const SpirvImageTypeKey& key);

  uint32_t getIdForSamplerType();

  uint32_t getIdForSampledImageType(uint32_t imageTypeId);

  uint32_t getIdForBdaType(const ir::Type& type, ir::UavFlags flags);

  uint32_t getIdForConstant(const SpirvConstant& constant, uint32_t memberCount);

  uint32_t getIdForConstantNull(const ir::Type& type);

  uint32_t getIdForPushDataBlock();

  spv::Scope getUavCoherentScope(ir::UavFlags flags);

  spv::Scope translateScope(ir::Scope scope);

  uint32_t translateMemoryTypes(ir::MemoryTypeFlags memoryFlags, spv::MemorySemanticsMask base);

  uint32_t makeScalarConst(ir::ScalarType type, const ir::Op& op, uint32_t& operandIndex);

  uint32_t makeBasicConst(ir::BasicType type, const ir::Op& op, uint32_t& operandIndex);

  uint32_t makeConstant(const ir::Type& type, const ir::Op& op, uint32_t& operandIndex);

  uint32_t makeConstBool(bool value);

  uint32_t makeConstU32(uint32_t value);

  uint32_t makeConstI32(int32_t value);

  uint32_t makeConstNull(uint32_t typeId);

  uint32_t makeUndef(uint32_t typeId);

  void setDebugName(uint32_t id, const char* name);

  void setDebugMemberName(uint32_t id, uint32_t member, const char* name);

  bool enableCapability(spv::Capability cap);

  void enableExtension(const char* name);

  void addEntryPointId(uint32_t id);

  bool declaresPlainBufferResource(const ir::Op& op);

  uint32_t getDescriptorArraySize(const ir::Op& op);

  void setUavImageReadOperands(SpirvImageOperands& operands, const ir::Op& uavOp);

  void setUavImageWriteOperands(SpirvImageOperands& operands, const ir::Op& uavOp);

  ir::Type traverseType(ir::Type type, ir::SsaDef address) const;

  bool isMultiStreamGs() const;

  bool isPatchConstant(const ir::Op& op) const;

  DescriptorBinding mapDescriptor(const ir::Op& op, const ir::Op& bindingOp) const;

  spv::Op getAccessChainOp(const ir::Op& op) const;

  static uint32_t getFpModeFlags(ir::OpFlags flags);

  static ir::UavFlags getUavFlags(const ir::Op& op);

  static ir::ResourceKind getResourceKind(const ir::Op& op);

  static spv::StorageClass getVariableStorageClass(const ir::Op& op);

  static uint32_t makeOpcodeToken(spv::Op op, uint32_t len);

  static uint32_t getStringDwordCount(const char* str);

  template<typename T, typename... Args>
  static void pushOp(T& container, spv::Op op, Args... args);

  template<typename T>
  static void pushString(T& container, const char* str);

};

}
