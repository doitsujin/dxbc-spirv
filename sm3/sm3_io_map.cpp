#include <algorithm>

#include "sm3_converter.h"
#include "sm3_io_map.h"

#include "../ir/ir_utils.h"

#include "../util/util_log.h"

namespace dxbc_spv::sm3 {

static std::array<Semantic, 12u> s_ffLocations = {{
  {SemanticUsage::eNormal,   0u},
  {SemanticUsage::eTexCoord, 0u},
  {SemanticUsage::eTexCoord, 1u},
  {SemanticUsage::eTexCoord, 2u},
  {SemanticUsage::eTexCoord, 3u},
  {SemanticUsage::eTexCoord, 4u},
  {SemanticUsage::eTexCoord, 5u},
  {SemanticUsage::eTexCoord, 6u},
  {SemanticUsage::eTexCoord, 7u},

  {SemanticUsage::eColor,    0u},
  {SemanticUsage::eColor,    1u},

  {SemanticUsage::eFog,      0u},
}};

IoMap::IoMap(Converter& converter)
: m_converter(converter) {}


IoMap::~IoMap() {

}


void IoMap::initialize(ir::Builder& builder) {
  const ShaderInfo& info = m_converter.getShaderInfo();

  /* Reserve the IO locations used by fixed function,
   * so programmable shaders can be used with fixed function without having to modify the shader. */
  if (info.getType() == ShaderType::eVertex) {
    m_nextOutputLocation = s_ffLocations.size();
  } else {
    m_nextInputLocation = s_ffLocations.size();
  }

  if (info.getVersion().first >= 3u) {
    /* Assume that shader model 3 vertex shaders hardly ever get mixed with fixed function pixel processing
     * If they do, the backend handles it. Color 0 is the exception because that needs a different value.
     * Shader model 3 pixel shaders don't support any kind of fog. */

    /* Emit functions that pick a register using
     * a switch statement to allow relative addressing */

    /* Emit placeholders */
    m_inputSwitchFunction = builder.add(ir::Op::Function(ir::Type(ir::ScalarType::eF32, 4)));

    if (info.getType() == ShaderType::eVertex) {
      /* Only VS outputs support relative addressing. */
      m_outputSwitchFunction = builder.add(ir::Op::Function(ir::Type()));
    }
  } else if (info.getVersion().first < 2u || info.getType() == ShaderType::eVertex) {
    /* VS 1 & 2 have fixed output registers that do not get explicitly declared.
     * PS 1 has fixed input registers that do not get explicitly declared.
     * PS 2 has input registers that get explicitly declared but unlike PS 3,
     * it uses distinct register types instead of generic input registers + semantics.
     * Declare the registers that the fixed function pipeline uses so their IO locations get reserved. */

    bool isPS = info.getType() == ShaderType::ePixel;

    ir::Type type(ir::ScalarType::eF32, 4u);

    /* Normal */
    if (!isPS) {
      /* There is no register for the normal, we emit it in case the VS is used with fixed function.
       * So we get a little tricky and use an imaginary 13th output register.
       * Register type & index are only used for emitting debug naming and we handle that edge case there. */
      dclIoVar(
        builder,
        RegisterType::eOutput,
        SM3VSOutputArraySize,
        { SemanticUsage::eNormal, 0u }
      );
    }

    /* Texture coords */
    for (uint32_t i = 0u; i < SM2TexCoordCount; i++) {
      dclIoVar(
        builder,
        isPS ? RegisterType::ePixelTexCoord : RegisterType::eTexCoordOut,
        i,
        { SemanticUsage::eTexCoord, i }
      );
    }

    /* Colors */
    for (uint32_t i = 0u; i < SM2ColorCount; i++) {
      dclIoVar(
        builder,
        isPS ? RegisterType::eInput : RegisterType::eAttributeOut,
        i,
        { SemanticUsage::eColor, i }
      );
    }

    /* Fog
     * There is no fog input register in the pixel shader that is accessible
     * to the shader. We do however need to pass the fog value calculated by the vertex shader
     * the fragment shader. Use an imaginary 11th input register. */
    dclIoVar(
      builder,
      isPS ? RegisterType::eInput : RegisterType::eRasterizerOut,
      isPS ? FogRegisterIndex : uint32_t(RasterizerOutIndex::eRasterOutFog),
      { SemanticUsage::eFog, 0u }
    );

    if (isPS) {
      /* Declare a output register for PS 1 shaders. */
      dclIoVar(
        builder,
        RegisterType::eColorOut,
        0u,
        { SemanticUsage::eColor, 0u }
      );
    }
  }

  if (m_converter.getShaderInfo().getType() == ShaderType::ePixel) {
    /* Declare the point coord built-in because we might need that for the texcoord register. */
    dclPointCoord(builder);
  }
}


void IoMap::finalize(ir::Builder& builder) {
  /* Now that all dcl instructions are processed, we can emit the functions containing the switch statements. */
  if (m_inputSwitchFunction) {
    ir::SsaDef cursor = builder.setCursor(m_inputSwitchFunction);
    auto inputSwitchFunction = emitDynamicLoadFunction(builder);
    builder.rewriteDef(m_inputSwitchFunction, inputSwitchFunction);
    m_inputSwitchFunction = inputSwitchFunction;
    builder.setCursor(cursor);
  }

  if (m_outputSwitchFunction) {
    ir::SsaDef cursor = builder.setCursor(m_outputSwitchFunction);
    auto outputSwitchFunction = emitDynamicStoreFunction(builder);
    builder.rewriteDef(m_outputSwitchFunction, outputSwitchFunction);
    m_outputSwitchFunction = outputSwitchFunction;
    builder.setCursor(cursor);
  }

  if (m_converter.getShaderInfo().getType() == ShaderType::eVertex)
    emitVSClipping(builder);

  flushOutputs(builder);
}


bool IoMap::handleDclIoVar(ir::Builder& builder, const Instruction& op) {
  const auto& dst = op.getDst();
  const auto& dcl = op.getDcl();

  auto info = m_converter.getShaderInfo();

  Semantic semantic;

  bool isPixelShader = info.getType() == ShaderType::ePixel;
  bool isVarying = registerTypeIsInput(dst.getRegisterType(), info.getType()) == isPixelShader;

  if (!isVarying || info.getVersion().first >= 3u ) {
    /* DCL instructions for VS outputs and PS inputs that associate generic input/output registers
     * to semantics only exist in SM3.
     * Instructions that associate VS input registers to semantics do exist in earlier shader models though. */
    semantic = { dcl.getSemanticUsage(), dcl.getSemanticIndex() };
  } else {
    /* SM2 doesn't have semantics for VS outputs or PS inputs.
     * Generate a matching semantic so we can use the same code. */
    auto semanticOpt = determineSemanticForRegister(dst.getRegisterType(), dst.getIndex());
    dxbc_spv_assert(semanticOpt.has_value());
    semantic = semanticOpt.value();
  }

  dclIoVar(builder, dst.getRegisterType(), dst.getIndex(), semantic);
  emitIoVarDefault(builder, m_variables.back());
  return true;
}


std::optional<ir::BuiltIn> IoMap::determineBuiltinForRegister(RegisterType regType, uint32_t regIndex, Semantic semantic) {
  auto shaderInfo = m_converter.getShaderInfo();

  if (!registerTypeIsInput(regType, shaderInfo.getType())) {

    if (regType == RegisterType::eDepthOut) {
      return std::make_optional(ir::BuiltIn::eDepth);
    }

    if (regType == RegisterType::eRasterizerOut) {
      if (regIndex == uint32_t(RasterizerOutIndex::eRasterOutPointSize)) {
        dxbc_spv_assert(semantic.usage == SemanticUsage::ePointSize && semantic.index == 0u);
        return std::make_optional(ir::BuiltIn::ePointSize);
      }

      if (regIndex == uint32_t(RasterizerOutIndex::eRasterOutPosition)) {
        dxbc_spv_assert(semantic.usage == SemanticUsage::ePosition && semantic.index == 0u);
        return std::make_optional(ir::BuiltIn::ePosition);
      }

      /* The only other register index we accept for RasterizerOut registers is
       * the fog register. */
      dxbc_spv_assert(regIndex == uint32_t(RasterizerOutIndex::eRasterOutFog));
      dxbc_spv_assert(semantic.usage == SemanticUsage::eFog && semantic.index == 0u);
      /* Fog is a builtin for D3D9 but not for Vulkan. */

      return std::nullopt;
    }

    /* The dcl instructions with a semantic only exist in SM3
     * and SM3 uses generic output registers. */

    if (semantic.usage == SemanticUsage::ePosition && semantic.index == 0u) {
      dxbc_spv_assert(regType == RegisterType::eOutput);
      return std::make_optional(ir::BuiltIn::ePosition);
    }

    if (semantic.usage == SemanticUsage::ePointSize && semantic.index == 0u) {
      dxbc_spv_assert(regType == RegisterType::eOutput);
      return std::make_optional(ir::BuiltIn::ePointSize);
    }

    return std::nullopt;

  } else {

    /* Position must not be mapped to a regular input. SM3 still has a separate register for that. */
    dxbc_spv_assert(shaderInfo.getType() == ShaderType::eVertex
      || semantic.usage != SemanticUsage::ePosition
      || regType != RegisterType::eInput);

    if (regType == RegisterType::eMiscType) {
      if (regIndex == uint32_t(MiscTypeIndex(MiscTypeIndex::eMiscTypeFace))) {
        return std::make_optional(ir::BuiltIn::eIsFrontFace);
      }

      if (regIndex == uint32_t(MiscTypeIndex::eMiscTypePosition)) {
        return std::make_optional(ir::BuiltIn::ePosition);
      }

      /* Invalid MiscType */
      dxbc_spv_assert(false);
    }

  }

  return std::nullopt;
}


void IoMap::dclIoVar(
   ir::Builder& builder,
   RegisterType registerType,
   uint32_t     registerIndex,
   Semantic     semantic) {

  auto shaderType = m_converter.getShaderInfo().getType();
  bool isInput = registerTypeIsInput(registerType, shaderType);

  /* Semantics only apply to specific register types.
   * Multiple RegisterType::eMiscType registers may have the same semantic. */
  bool isRegularRegister = registerType == RegisterType::eInput
    || registerType == RegisterType::eOutput;

  bool foundExisting = false;

  for (auto& entry : m_variables) {
    if (isInput != registerTypeIsInput(entry.registerType, shaderType)) {
      continue;
    }

    if ((isRegularRegister && entry.semantic == semantic)
      || (registerType == entry.registerType && registerIndex == entry.registerIndex)) {
      foundExisting = true;
      break;
    }
  }

  dxbc_spv_assert(!foundExisting);

  auto builtIn = determineBuiltinForRegister(registerType, registerIndex, semantic);

  bool isScalar = registerType == RegisterType::eRasterizerOut
    && (registerIndex == uint32_t(RasterizerOutIndex::eRasterOutFog)
    || registerIndex == uint32_t(RasterizerOutIndex::eRasterOutPointSize));
  isScalar |= registerType == RegisterType::eInput && registerIndex == FogRegisterIndex;
  isScalar |= builtIn == ir::BuiltIn::eIsFrontFace;
  isScalar |= builtIn == ir::BuiltIn::eDepth;
  isScalar |= builtIn == ir::BuiltIn::ePointSize;

  uint32_t typeVectorSize = isScalar ? 1u : 4u;
  ir::Type type(
    builtIn == ir::BuiltIn::eIsFrontFace ? ir::ScalarType::eBool : ir::ScalarType::eF32,
    typeVectorSize
  );

  ir::SsaDef cursor;
  ir::SsaDef declarationDef;
  uint32_t location = 0u;

  if (!builtIn) {
    if (shaderType == ShaderType::eVertex || isInput) {
      bool foundFFLocation = false;
      if ((shaderType == ShaderType::ePixel) == isInput) {
        /* Pick FF-compatible locations for VS outputs and PS inputs. */
        for (uint32_t i = 0u; i < s_ffLocations.size() && !foundFFLocation; i++) {
          if (s_ffLocations[i] == semantic) {
            location = i;
            foundFFLocation = true;
          }
        }
      }

      if (!foundFFLocation) {
        location = isInput ? m_nextInputLocation++ : m_nextOutputLocation++;
      }
    } else {
      /* PS outputs need to write to the location that the shader specifies so values end up in the correct
       * render target */
      location = registerIndex;
    }

    ir::OpCode opCode = isInput
      ? ir::OpCode::eDclInput
      : ir::OpCode::eDclOutput;

    auto declaration = ir::Op(opCode, type)
      .addOperand(m_converter.getEntryPoint())
      .addOperand(location)
      .addOperand(0u);

    if (isInput && shaderType == ShaderType::ePixel && semantic.usage == SemanticUsage::eColor) {
      declaration.addOperand(ir::InterpolationModes(ir::InterpolationMode::eCentroid));
    }

    if (!isInput && shaderType == ShaderType::eVertex && semantic.usage == SemanticUsage::ePosition)
      declaration.setFlags(ir::OpFlag::eInvariant);

    declarationDef = builder.addBefore(builder.getCode().first->getDef(), std::move(declaration));
    cursor = builder.setCursor(declarationDef);

    std::stringstream semanticNameStream;
    semanticNameStream << semantic.usage;
    std::string semanticNameString = semanticNameStream.str();
    builder.add(ir::Op::Semantic(declarationDef, semantic.index, semanticNameString.c_str()));
  } else {
    ir::OpCode opCode = isInput
      ? ir::OpCode::eDclInputBuiltIn
      : ir::OpCode::eDclOutputBuiltIn;

    auto declaration = ir::Op(opCode, type)
      .addOperand(m_converter.getEntryPoint())
      .addOperand(*builtIn);

    declarationDef = builder.addBefore(builder.getCode().first->getDef(), std::move(declaration));
    cursor = builder.setCursor(declarationDef);
  }

  auto& mapping = m_variables.emplace_back();
  mapping.semantic = semantic;
  mapping.registerType = registerType;
  mapping.registerIndex = registerIndex;
  mapping.location = location;
  mapping.baseType = type;
  mapping.baseDef = declarationDef;
  mapping.tempDefs = { };

  uint32_t tempVectorSize = typeVectorSize;

  /* Point Size always has a full 4 component vector but only one component is used for the builtin. */
  if (builtIn == ir::BuiltIn::ePointSize)
    tempVectorSize = 4u;

  if (!isInput) {
    /* SM 1 texture ops write the texture data into the texture register which used to hold the texcoord.
     * So we need writable temps for this input register. */
    for (uint32_t i = 0u; i < tempVectorSize; i++) {
      mapping.tempDefs[i] = builder.add(ir::Op::DclTmp(ir::ScalarType::eF32, m_converter.getEntryPoint()));
    }
  }

  emitDebugName(
    builder,
    mapping.baseDef,
    registerType,
    registerIndex,
    WriteMask(ComponentBit::eAll),
    mapping.semantic,
    false
  );

  for (uint32_t i = 0u; i < typeVectorSize && mapping.tempDefs[0u]; i++) {
    emitDebugName(
      builder,
      mapping.tempDefs[i],
      registerType,
      registerIndex,
      util::componentBit(Component(i)),
      mapping.semantic,
      true
    );
  }

  builder.setCursor(cursor);
}


void IoMap::dclPointCoord(ir::Builder& builder) {
  auto type = ir::BasicType(ir::ScalarType::eF32, 2u);
  m_pointCoord = builder.add(ir::Op::DclInputBuiltIn(type, m_converter.getEntryPoint(), ir::BuiltIn::ePointCoord));
}


ir::SsaDef IoMap::emitPointCoordLoad(ir::Builder& builder) {
  auto pointCoordType = ir::BasicType(ir::ScalarType::eF32, 2u);
  auto pointCoord = builder.add(ir::Op::InputLoad(pointCoordType, m_pointCoord, ir::SsaDef()));
  /* Turn it into a vec4 so it can easily be used as the texcoord register value. */
  std::array<ir::SsaDef, 4u> components = { pointCoord, pointCoord, builder.makeConstant(0.0f), builder.makeConstant(0.0f) };
  return ir::buildVector(builder, ir::ScalarType::eF32, 4u, components.data());
}


void IoMap::emitIoVarDefaults(ir::Builder& builder) {
  for (const IoVarInfo& ioVar : m_variables) {
    emitIoVarDefault(builder, ioVar);
  }
}


void IoMap::emitIoVarDefault(
        ir::Builder& builder,
  const IoVarInfo&   ioVar) {

  const ShaderInfo& shaderInfo = m_converter.getShaderInfo();

  bool isInput = registerTypeIsInput(ioVar.registerType, shaderInfo.getType());
  ir::BasicType ioVarType = builder.getOp(ioVar.baseDef).getType().getBaseType(0u);

  if (!isInput) {

    if (ioVar.semantic == Semantic { SemanticUsage::eColor, 0u }) {
      /* The default for color 0 is 1.0, 1.0, 1.0, 1.0 */
      for (uint32_t i = 0u; i < ioVarType.getVectorSize(); i++) {
        builder.add(ir::Op::TmpStore(ioVar.tempDefs[i], builder.makeConstant(1.0f)));
      }
    } else if (ioVar.semantic.usage == SemanticUsage::eColor) {
      /* The default for other color registers is 0.0, 0.0, 0.0, 1.0.
       * TODO: If it's used with a SM3 PS, we need to export 0,0,0,0 as the default for color1.
       *       Implement that using a spec constant. */
      for (uint32_t i = 0u; i < ioVarType.getVectorSize(); i++) {
        builder.add(ir::Op::TmpStore(ioVar.tempDefs[i], builder.makeConstant(i == 3u ? 1.0f : 0.0f)));
      }
    } else if (ioVar.semantic.usage == SemanticUsage::ePointSize) {
      auto pointSize = builder.add(ir::Op::PushDataLoad(ir::ScalarType::eF32, m_converter.m_renderState,
        builder.makeConstant(uint32_t(RenderStateItem::ePointSize))));
      auto pointSizeMin = builder.add(ir::Op::PushDataLoad(ir::ScalarType::eF32, m_converter.m_renderState,
        builder.makeConstant(uint32_t(RenderStateItem::ePointSizeMin))));
      auto pointSizeMax = builder.add(ir::Op::PushDataLoad(ir::ScalarType::eF32, m_converter.m_renderState,
        builder.makeConstant(uint32_t(RenderStateItem::ePointSizeMax))));

      auto finalSize = builder.add(ir::Op::FClamp(ir::ScalarType::eF32, pointSize, pointSizeMin, pointSizeMax));
      builder.add(ir::Op::TmpStore(ioVar.tempDefs[0], finalSize));
    } else if (ioVar.semantic == Semantic { SemanticUsage::eFog, 0u }) {
      /* The default for fog is 1.0 */
      builder.add(ir::Op::TmpStore(ioVar.tempDefs[0u], builder.makeConstant(1.0f)));
    } else {
      /* The default for other registers is 0.0, 0.0, 0.0, 0.0 */
      for (uint32_t i = 0u; i < ioVarType.getVectorSize(); i++) {
        builder.add(ir::Op::TmpStore(ioVar.tempDefs[i], builder.makeConstant(0.0f)));
      }
    }

  } else if (ioVar.tempDefs[0u]
    && ioVar.registerType == RegisterType::eTexture
    && shaderInfo.getType() == ShaderType::ePixel
    && shaderInfo.getVersion().first < 2u
    && shaderInfo.getVersion().second < 4u) {

    /* Load the initial input tex coords. */
    for (uint32_t i = 0u; i < ioVarType.getVectorSize(); i++) {
      builder.add(ir::Op::TmpStore(
        ioVar.tempDefs[i],
        builder.add(ir::Op::InputLoad(ir::ScalarType::eF32, ioVar.baseDef, builder.makeConstant(i)))
      ));
    }

  }
}


std::optional<Semantic> IoMap::determineSemanticForRegister(RegisterType regType, uint32_t regIndex) {
  switch (regType) {
    case RegisterType::eColorOut:
      return std::make_optional(Semantic { SemanticUsage::eColor, regIndex });

    case RegisterType::eInput:
      if (regIndex == FogRegisterIndex)
        return std::make_optional(Semantic { SemanticUsage::eFog, 0u });
      else
        return std::make_optional(Semantic { SemanticUsage::eColor, regIndex });

    case RegisterType::eTexCoordOut:
      return std::make_optional(Semantic { SemanticUsage::eTexCoord, regIndex });

    case RegisterType::ePixelTexCoord:
      return std::make_optional(Semantic { SemanticUsage::eTexCoord, regIndex });

    case RegisterType::eDepthOut:
      return std::make_optional(Semantic { SemanticUsage::eDepth, regIndex });

    case RegisterType::eTexture:
      return std::make_optional(Semantic { SemanticUsage::eTexCoord, regIndex });

    case RegisterType::eAttributeOut:
      return std::make_optional(Semantic { SemanticUsage::eColor, regIndex });

    case RegisterType::eRasterizerOut:
      switch (regIndex) {
        case uint32_t(RasterizerOutIndex::eRasterOutFog):
            return std::make_optional(Semantic { SemanticUsage::eFog, 0u });

        case uint32_t(RasterizerOutIndex::eRasterOutPointSize):
            return std::make_optional(Semantic { SemanticUsage::ePointSize, 0u });

        case uint32_t(RasterizerOutIndex::eRasterOutPosition):
            return std::make_optional(Semantic { SemanticUsage::ePosition, 0u });
      }
      break;

    case RegisterType::eMiscType:
      switch (regIndex) {
        case uint32_t(MiscTypeIndex::eMiscTypePosition):
            return std::make_optional(Semantic { SemanticUsage::ePosition, 0u });

        case uint32_t(MiscTypeIndex::eMiscTypeFace):
            /* There is no semantic usage for the front face. */
            break;
      }
      break;

    default: break;
  }
  return std::nullopt;
}


ir::SsaDef IoMap::emitLoad(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        WriteMask               componentMask,
        Swizzle                 swizzle,
        ir::ScalarType          type) {
  std::array<ir::SsaDef, 4u> components = { };

  if (!operand.hasRelativeAddressing()) {
    const IoVarInfo* ioVar = findIoVar(m_variables, operand.getRegisterType(), operand.getIndex());

    if (ioVar == nullptr) {
      std::optional<Semantic> semantic = determineSemanticForRegister(operand.getRegisterType(), operand.getIndex());

      if (!semantic.has_value()) {
        m_converter.logOpError(op, "Failed to process I/O load.");
      } else {
        dclIoVar(builder, operand.getRegisterType(), operand.getIndex(), semantic.value());
        ioVar = &m_variables.back();
        emitIoVarDefault(builder, *ioVar);
      }
    }

    for (auto c : swizzle.getReadMask(componentMask)) {
      auto componentIndex = uint8_t(util::componentFromBit(c));

      if (!ioVar) {
        components[componentIndex] = builder.add(ir::Op::Undef(type));
        continue;
      }

      bool isFrontFaceBuiltin = ioVar->registerType == RegisterType::eMiscType && ioVar->registerIndex == uint32_t(MiscTypeIndex::eMiscTypeFace);
      ir::SsaDef value;

      if (!isFrontFaceBuiltin) {
        auto baseType = ioVar->baseType.getBaseType(0u);
        ir::ScalarType varScalarType = ioVar->baseType.getBaseType(0u).getBaseType();

        if (!ioVar->tempDefs[0u]) {
          ir::SsaDef addressConstant = ir::SsaDef();

          if (!baseType.isScalar())
            addressConstant = builder.makeConstant(uint32_t(componentIndex));

          value = builder.add(ir::Op::InputLoad(varScalarType, ioVar->baseDef, addressConstant));

          if (m_converter.getShaderInfo().getType() == ShaderType::ePixel
            && ioVar->registerType == RegisterType::eInput
            && ioVar->semantic.usage == SemanticUsage::eTexCoord) {
            /* We need to replace TEXCOORD inputs with gl_PointCoord
             * if D3DRS_POINTSPRITEENABLE is set. */
            auto pointCoord = emitPointCoordLoad(builder);

            auto specConstBit = m_converter.m_specConstants.get(builder, SpecConstantId::eSpecPointMode,
              builder.makeConstant(1u), builder.makeConstant(1u));
            auto pointSpriteEnabled = builder.add(ir::Op::IEq(ir::ScalarType::eU32, specConstBit, builder.makeConstant(1u)));

            value = builder.add(ir::Op::Select(
              ir::BasicType(ir::ScalarType::eF32, 4u),
              pointSpriteEnabled,
              pointCoord,
              value
            ));
          }

        } else {
          /* The input register is writable. (SM 1 Texture register) */
          value = builder.add(ir::Op::TmpLoad(varScalarType, ioVar->tempDefs[uint32_t(componentIndex)]));
        }
      } else {
        /* The front face needs to be transformed from a bool to 1.0/-1.0.
         * It can only be loaded using a separate register, even on SM3.
         * So we don't need to handle it in the relative addressing function. */
        dxbc_spv_assert(ioVar->baseType.isScalarType());
        value = builder.add(ir::Op::InputLoad(ioVar->baseType, ioVar->baseDef, ir::SsaDef()));
        value = emitFrontFaceFloat(builder, value);
      }

      components[componentIndex] = convertScalar(builder, type, value);
    }
  } else {
    dxbc_spv_assert(operand.getRegisterType() == RegisterType::eInput);
    dxbc_spv_assert(m_converter.getShaderInfo().getVersion().first >= 3);

    auto index = m_converter.calculateAddress(builder,
      operand.getRelativeAddressingRegisterType(),
      operand.getRelativeAddressingSwizzle(),
      operand.getIndex(),
      ir::ScalarType::eU32);

    dxbc_spv_assert(m_inputSwitchFunction);

    auto vec4Value = builder.add(ir::Op::FunctionCall(ir::Type(ir::ScalarType::eF32, 4u), m_inputSwitchFunction)
        .addOperand(index));

    for (auto c : swizzle.getReadMask(componentMask)) {
      auto componentIndex = uint8_t(util::componentFromBit(c));

      components[componentIndex] = convertScalar(
        builder,
        type,
        builder.add(ir::Op::CompositeExtract(type, vec4Value, builder.makeConstant(componentIndex)))
      );
    }
  }

  ir::SsaDef value = composite(builder, ir::BasicType(type, util::popcnt(uint8_t(componentMask))), components.data(), swizzle, componentMask);

  return value;
}


ir::SsaDef IoMap::emitTexCoordLoad(
         ir::Builder&            builder,
   const Instruction&            op,
         uint32_t                regIdx,
         WriteMask               componentMask,
         Swizzle                 swizzle,
         ir::ScalarType          type) {
  std::array<ir::SsaDef, 4u> components = { };

  const IoVarInfo* ioVar = findIoVar(m_variables, RegisterType::ePixelTexCoord, regIdx);

  if (ioVar == nullptr) {
    std::optional<Semantic> semantic = determineSemanticForRegister(RegisterType::ePixelTexCoord, regIdx);

    if (!semantic.has_value()) {
      m_converter.logOpError(op, "Failed to process I/O load.");
    } else {
      dclIoVar(builder, RegisterType::ePixelTexCoord, regIdx, semantic.value());
      ioVar = &m_variables.back();
    }
  }

  for (auto c : swizzle.getReadMask(componentMask)) {
    auto componentIndex = uint8_t(util::componentFromBit(c));

    if (!ioVar) {
      components[componentIndex] = builder.add(ir::Op::Undef(type));
      continue;
    }

    auto varScalarType = ioVar->baseType.getBaseType(0u).getBaseType();

    ir::SsaDef addressConstant = builder.makeConstant(componentIndex);
    auto value = builder.add(ir::Op::InputLoad(varScalarType, ioVar->baseDef, addressConstant));

    components[componentIndex] = convertScalar(builder, type, value);
  }

  return composite(builder, ir::BasicType(type, util::popcnt(uint8_t(componentMask))), components.data(), swizzle, componentMask);
}


bool IoMap::emitStore(
        ir::Builder&            builder,
  const Instruction&            op,
  const Operand&                operand,
        WriteMask               writeMask,
        ir::SsaDef              predicateVec,
        ir::SsaDef              value) {
  auto srcType = builder.getOp(value).getType();
  auto srcBaseType = srcType.getBaseType(0);
  auto srcScalarType = srcBaseType.getBaseType();

  if (!operand.hasRelativeAddressing()) {
    const IoVarInfo* ioVar = findIoVar(m_variables, operand.getRegisterType(), operand.getIndex());

    if (ioVar == nullptr) {
      std::optional<Semantic> semantic;
      semantic = determineSemanticForRegister(operand.getRegisterType(), operand.getIndex());

      if (!semantic.has_value()) {
        m_converter.logOpError(op, "Failed to process I/O store.");
        return false;
      }

      dclIoVar(builder, operand.getRegisterType(), operand.getIndex(), semantic.value());
      ioVar = &m_variables.back();
      emitIoVarDefault(builder, *ioVar);
    }

    dxbc_spv_assert(!registerTypeIsInput(ioVar->registerType, m_converter.getShaderInfo().getType()));

    auto ioVarBaseType = ioVar->baseType.getBaseType(0u);
    ir::ScalarType ioVarScalarType = ioVarBaseType.getBaseType();

    uint32_t componentIndex = 0u;

    for (auto c : writeMask) {
      ir::SsaDef valueScalar = value;

      if (srcType.isVectorType()) {
        auto componentIndexConst = builder.makeConstant(componentIndex);
        valueScalar = builder.add(ir::Op::CompositeExtract(srcScalarType, value, componentIndexConst));
      }

      valueScalar = convertScalar(builder, ioVarScalarType, valueScalar);

      if (ioVar->semantic.usage == SemanticUsage::eColor && ioVar->semantic.index < 2u && m_converter.getShaderInfo().getVersion().first < 3u) {
        /* The color register cannot be dynamically indexed, so there's no need to do this in the dynamic store function. */
        valueScalar = builder.add(ir::Op::FClamp(ioVarScalarType, valueScalar,
          builder.makeConstant(0.0f), builder.makeConstant(1.0f)));
      } else if (ioVar->semantic.usage == SemanticUsage::ePointSize) {
        /* Clamp value between D3DRS_POINTSIZE_MIN and D3DRS_POINTSIZE_MAX. */
        auto pointSizeMin = builder.add(ir::Op::PushDataLoad(ir::ScalarType::eF32, m_converter.m_renderState,
          builder.makeConstant(uint32_t(RenderStateItem::ePointSizeMin))));
        auto pointSizeMax = builder.add(ir::Op::PushDataLoad(ir::ScalarType::eF32, m_converter.m_renderState,
          builder.makeConstant(uint32_t(RenderStateItem::ePointSizeMax))));
        valueScalar = builder.add(ir::Op::FClamp(ioVarScalarType, valueScalar, pointSizeMin, pointSizeMax));
      }

      if (predicateVec) {
        /* Check if the matching component of the predicate register vector is true first.
         * Pick the old value if not. */
        auto condComponent = extractFromVector(builder, predicateVec, componentIndex);
        auto oldValue = builder.add(ir::Op::TmpLoad(ioVarScalarType, ioVar->tempDefs[uint32_t(util::componentFromBit(c))]));
        valueScalar = builder.add(ir::Op::Select(ioVarScalarType, condComponent, valueScalar, oldValue));
      }

      builder.add(ir::Op::TmpStore(ioVar->tempDefs[uint32_t(util::componentFromBit(c))], valueScalar));

      componentIndex++;
    }
  } else {
    dxbc_spv_assert(operand.getRegisterType() == RegisterType::eOutput);
    dxbc_spv_assert(m_converter.getShaderInfo().getVersion().first >= 3);

    auto index = m_converter.calculateAddress(builder,
      operand.getRelativeAddressingRegisterType(),
      operand.getRelativeAddressingSwizzle(),
      operand.getIndex(),
      ir::ScalarType::eU32);

    dxbc_spv_assert(m_outputSwitchFunction);

    uint32_t componentIndex = 0u;

    for (auto c : writeMask) {
      ir::SsaDef valueScalar = value;

      if (srcType.isVectorType()) {
        auto componentIndexConst = builder.makeConstant(componentIndex);

        valueScalar = builder.add(ir::Op::CompositeExtract(srcScalarType, value, componentIndexConst));
      }

      valueScalar = convertScalar(builder, ir::ScalarType::eF32, valueScalar);
      ir::SsaDef predicateIf = ir::SsaDef();

      if (predicateVec) {
        /* Check if the matching component of the predicate register vector is true first. */
        auto condComponent = extractFromVector(builder, predicateVec, componentIndex);
        predicateIf = builder.add(ir::Op::ScopedIf(ir::SsaDef(), condComponent));
      }

      auto dstComponentIndexConst = builder.makeConstant(uint32_t(util::componentFromBit(c)));
      auto flattenedIndex = builder.add(ir::Op::IAdd(ir::ScalarType::eU32,
        builder.add(ir::Op::IMul(ir::ScalarType::eU32, index, builder.makeConstant(4u))),
        dstComponentIndexConst
      ));

      builder.add(ir::Op::FunctionCall(ir::Type(), m_outputSwitchFunction)
        .addOperand(flattenedIndex)
        .addOperand(valueScalar));

      if (predicateIf) {
        auto predicateIfEnd = builder.add(ir::Op::ScopedEndIf(predicateIf));
        builder.rewriteOp(predicateIf, ir::Op(builder.getOp(predicateIf)).setOperand(0u, predicateIfEnd));
      }

      componentIndex++;
    }
  }

  return true;
}


bool IoMap::emitDepthStore(ir::Builder &builder, const Instruction &op, ir::SsaDef value) {
  const IoVarInfo* ioVar = findIoVar(m_variables, RegisterType::eDepthOut, 0u);

  if (ioVar == nullptr) {
    std::optional<Semantic> semantic = determineSemanticForRegister(RegisterType::eDepthOut, 0u);

    if (!semantic.has_value()) {
      m_converter.logOpError(op, "Failed to process I/O depth store.");
      return false;
    }

    dclIoVar(builder, RegisterType::eDepthOut, 0u, semantic.value());
    ioVar = &m_variables.back();
  }

  dxbc_spv_assert(builder.getOp(ioVar->tempDefs[0u]).getType() == builder.getOp(value).getType());
  builder.add(ir::Op::TmpStore(ioVar->tempDefs[0u], value));

  return true;
}


bool IoMap::emitColorStore(ir::Builder& builder, ir::SsaDef value) {
  const IoVarInfo* ioVar = findIoVar(m_variables, RegisterType::eColorOut, 0u);

  if (ioVar == nullptr) {
    std::optional<Semantic> semantic = determineSemanticForRegister(RegisterType::eColorOut, 0u);

    if (!semantic.has_value()) {
      Logger::err("Failed to process I/O color store.");
      return false;
    }

    dclIoVar(builder, RegisterType::eColorOut, 0u, semantic.value());
    ioVar = &m_variables.back();
  }

  for (uint32_t i = 0u; i < 4u; i++) {
    auto valueScalar = ir::extractFromVector(builder, value, i);
    dxbc_spv_assert(builder.getOp(ioVar->tempDefs[i]).getType() == builder.getOp(valueScalar).getType());
    builder.add(ir::Op::TmpStore(ioVar->tempDefs[i], valueScalar));
  }
  return true;
}


ir::SsaDef IoMap::getColorValue(ir::Builder& builder) {
  const IoVarInfo* ioVar = findIoVar(m_variables, RegisterType::eColorOut, 0u);
  dxbc_spv_assert(ioVar != nullptr);

  auto componentType = ioVar->baseType.getBaseType(0u).getBaseType();

  std::array<ir::SsaDef, 4u> components;
  for (uint32_t i = 0u; i < components.size(); i++) {
    components[i] = builder.add(ir::Op::TmpLoad(componentType, ioVar->tempDefs[i]));
  }
  return ir::buildVector(builder, componentType, components.size(), components.data());
}


ir::SsaDef IoMap::emitDynamicLoadFunction(ir::Builder& builder) const {
  auto indexParameter = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));

  if (m_converter.m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(indexParameter, "reg"));

  auto function = builder.add(
    ir::Op::Function(ir::Type(ir::ScalarType::eF32, 4u))
    .addOperand(indexParameter)
  );

  if (m_converter.m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(function, "loadInputDynamic"));

  auto indexArg = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, function, indexParameter));
  auto switchDef = builder.add(ir::Op::ScopedSwitch(ir::SsaDef(), indexArg));

  for (uint32_t i = 0u; i < SM3VSInputArraySize; i++) {
    const IoVarInfo* ioVar = nullptr;

    for (const auto& variable : m_variables) {
      if (variable.registerType == RegisterType::eInput && variable.registerIndex == i) {
        ioVar = &variable;
        break;
      }
    }

    if (ioVar == nullptr)
      continue;

    dxbc_spv_assert(ioVar != nullptr);

    builder.add(ir::Op::ScopedSwitchCase(switchDef, i));

    auto input = builder.add(ir::Op::InputLoad(ioVar->baseType, ioVar->baseDef, ir::SsaDef()));
    auto baseType = ioVar->baseType.getBaseType(0u);
    ir::SsaDef vec4 = input;

    if (baseType.getVectorSize() != 4u) {
      std::array<ir::SsaDef, 4u> components;

      for (uint32_t j = 0u; j < 4u; j++) {
        if ((baseType.isScalar() && j == 0) || j < baseType.getVectorSize()) {
          components[j] = builder.add(ir::Op::CompositeExtract(ir::ScalarType::eF32, input, builder.makeConstant(i)));;
        } else {
          components[j] = builder.makeConstant(0.0f);
        }
      }

      vec4 = buildVector(builder, ir::ScalarType::eF32, components.size(), components.data());
    }

    builder.add(ir::Op::Return(ir::Type(ir::ScalarType::eF32, 4u), vec4));
    builder.add(ir::Op::ScopedSwitchBreak(switchDef));
  }

  /* Default case */
  builder.add(ir::Op::ScopedSwitchDefault(switchDef));
  builder.add(ir::Op::Return(ir::Type(ir::ScalarType::eF32, 4u),
    builder.makeConstant(0.0f, 0.0f, 0.0f, 0.0f)));
  builder.add(ir::Op::ScopedSwitchBreak(switchDef));

  auto switchEnd = builder.add(ir::Op::ScopedEndSwitch(switchDef));
  builder.rewriteOp(switchDef, ir::Op::ScopedSwitch(switchEnd, indexArg));

  builder.add(ir::Op::FunctionEnd());

  return function;
}


ir::SsaDef IoMap::emitDynamicStoreFunction(ir::Builder& builder) const {
  auto indexParameter = builder.add(ir::Op::DclParam(ir::ScalarType::eU32));

  if (m_converter.m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(indexParameter, "reg"));

  auto valueParameter = builder.add(ir::Op::DclParam(ir::ScalarType::eF32));

  if (m_converter.m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(valueParameter, "value"));

  auto function = builder.add(
    ir::Op::Function(ir::Type())
    .addOperand(indexParameter)
    .addOperand(valueParameter)
  );

  if (m_converter.m_options.includeDebugNames)
    builder.add(ir::Op::DebugName(function, "storeOutputDynamic"));

  /* The index is: register index * 4 + component index */
  auto indexArg = builder.add(ir::Op::ParamLoad(ir::ScalarType::eU32, function, indexParameter));

  auto valueArg = builder.add(ir::Op::ParamLoad(ir::ScalarType::eF32, function, valueParameter));
  auto switchDef = builder.add(ir::Op::ScopedSwitch(ir::SsaDef(), indexArg));

  for (uint32_t i = 0u; i < SM3VSOutputArraySize; i++) {
    const IoVarInfo* ioVar = nullptr;

    for (const auto& variable : m_variables) {
      if (variable.registerType == RegisterType::eOutput && variable.registerIndex == i) {
        ioVar = &variable;
        break;
      }
    }

    if (ioVar == nullptr)
      continue;

    dxbc_spv_assert(ioVar != nullptr);

    auto baseType = ioVar->baseType.getBaseType(0u);

    for (uint32_t j = 0u; j < baseType.getVectorSize(); j++) {
      builder.add(ir::Op::ScopedSwitchCase(switchDef, i * 4u + j));
      builder.add(ir::Op::TmpStore(ioVar->tempDefs[j], valueArg));
      builder.add(ir::Op::ScopedSwitchBreak(switchDef));
    }
  }

  /* Default case */
  builder.add(ir::Op::ScopedSwitchDefault(switchDef));
  builder.add(ir::Op::ScopedSwitchBreak(switchDef));

  auto switchEnd = builder.add(ir::Op::ScopedEndSwitch(switchDef));
  builder.rewriteOp(switchDef, ir::Op::ScopedSwitch(switchEnd, indexArg));

  builder.add(ir::Op::FunctionEnd());

  return function;
}


void IoMap::flushOutputs(ir::Builder& builder) {
  for (const auto& variable : m_variables) {
    if (!variable.tempDefs[0u])
      continue;

    auto op = builder.getOp(variable.baseDef);

    if (op.getOpCode() != ir::OpCode::eDclOutput && op.getOpCode() != ir::OpCode::eDclOutputBuiltIn)
      continue;

    auto baseType = variable.baseType.getBaseType(0u);

    for (uint32_t i = 0u; i < baseType.getVectorSize(); i++) {
      auto temp = builder.add(ir::Op::TmpLoad(variable.baseType, variable.tempDefs[i]));
      builder.add(ir::Op::OutputStore(variable.baseDef, baseType.getVectorSize() > 1u ? builder.makeConstant(i) : ir::SsaDef(), temp));
    }
  }
}


ir::SsaDef IoMap::emitFrontFaceFloat(ir::Builder &builder, ir::SsaDef isFrontFaceDef) const {
  auto frontFaceValue = builder.makeConstant(1.0f);
  auto backFaceValue = builder.makeConstant(-1.0f);
  return builder.add(ir::Op::Select(ir::ScalarType::eF32, isFrontFaceDef, frontFaceValue, backFaceValue));
}


void IoMap::emitVSClipping(ir::Builder& builder) {
  auto vec4Type = ir::BasicType(ir::ScalarType::eF32, 4u);

  /* Declare Cbv containing clip planes */
  auto clipPlaneArrayType = ir::Type(vec4Type);
  clipPlaneArrayType.addArrayDimension(MaxClipPlanes);
  auto clipPlaneBlock = builder.add(ir::Op::DclCbv(clipPlaneArrayType, m_converter.getEntryPoint(),
    0u, VSClipPlanesCbvRegIdx, 1u));

  if (m_converter.getOptions().includeDebugNames)
    builder.add(ir::Op::DebugName(clipPlaneBlock, "ClipPlanes"));

  /* Declare output array for clip distances */
  auto clipDistanceArrayType = ir::Type(ir::ScalarType::eF32);
  clipDistanceArrayType.addArrayDimension(MaxClipPlanes);
  auto clipDistancesArrayOp = ir::Op::DclOutputBuiltIn(clipDistanceArrayType,
    m_converter.getEntryPoint(), ir::BuiltIn::eClipDistance);
  clipDistancesArrayOp.setFlags(ir::OpFlag::eInvariant);
  auto clipDistanceArray = builder.add(clipDistancesArrayOp);

  const IoVarInfo* positionVar = nullptr;
  for (const auto& variable : m_variables) {
    if (variable.semantic.usage == SemanticUsage::ePosition && variable.semantic.index == 0u
      && !registerTypeIsInput(variable.registerType, ShaderType::eVertex)) {
      positionVar = &variable;
      break;
    }
  }

  dxbc_spv_assert(positionVar != nullptr);
  dxbc_spv_assert(positionVar->baseType == ir::BasicType(ir::ScalarType::eF32, 4u));

  std::array<ir::SsaDef, 4u> components;
  for (uint32_t i = 0u; i < 4u; i++) {
    dxbc_spv_assert(positionVar->tempDefs[i]);
    components[i] = builder.add(ir::Op::TmpLoad(positionVar->baseType, positionVar->tempDefs[i]));
  }

  // Always consider clip planes enabled when doing GPL by forcing 6 for the quick value.
  auto clipPlaneCount = m_converter.m_specConstants.get(builder, SpecConstantId::eSpecClipPlaneCount);

  auto position = ir::buildVector(builder, ir::ScalarType::eF32, components.size(), components.data());

  for (uint32_t i = 0u; i < MaxClipPlanes; i++) {
    auto descriptor = builder.add(ir::Op::DescriptorLoad(ir::ScalarType::eCbv, clipPlaneBlock, builder.makeConstant(0u)));
    auto clipPlane = builder.add(ir::Op::BufferLoad(vec4Type, descriptor, builder.makeConstant(i), 16u));
    auto dist = builder.add(m_converter.emitFDot(ir::ScalarType::eF32, position, clipPlane));

    auto clipPlaneEnabled = builder.add(ir::Op::ULe(ir::ScalarType::eBool, builder.makeConstant(i), clipPlaneCount));
    auto value = builder.add(ir::Op::Select(ir::ScalarType::eF32, clipPlaneEnabled, dist, builder.makeConstant(0.0f)));
    builder.add(ir::Op::OutputStore(clipDistanceArray, builder.makeConstant(i), value));
  }
}


IoVarInfo* IoMap::findIoVar(IoVarList& list, RegisterType regType, uint32_t regIndex) {
  for (auto& e : list) {
    if (e.registerType == regType && e.registerIndex == regIndex) {
      return &e;
      break;
    }
  }

  return nullptr;
}


ir::SsaDef IoMap::convertScalar(ir::Builder& builder, ir::ScalarType dstType, ir::SsaDef value) {
  const auto& srcType = builder.getOp(value).getType();
  dxbc_spv_assert(srcType.isScalarType());

  auto scalarType = srcType.getBaseType(0u).getBaseType();

  if (scalarType == dstType)
    return value;

  return builder.add(ir::Op::ConsumeAs(dstType, value));
}


void IoMap::emitDebugName(
  ir::Builder& builder,
  ir::SsaDef def,
  RegisterType registerType,
  uint32_t registerIndex,
  WriteMask writeMask,
  Semantic semantic,
  bool isTemp) const {

  if (!m_converter.getOptions().includeDebugNames)
    return;

  std::stringstream nameStream;

  nameStream << m_converter.makeRegisterDebugName(registerType, registerIndex, writeMask);
  nameStream << "_";

  if (semantic.usage == SemanticUsage::eColor) {
    if (semantic.index == 0) {
      nameStream << "color";
    } else {
      nameStream << "specular" << std::to_string(semantic.index - 1u);
    }
  } else {
    nameStream << semantic.usage;

    if (semantic.usage == SemanticUsage::ePosition
      || semantic.usage == SemanticUsage::eNormal
      || semantic.usage == SemanticUsage::eTexCoord) {
      nameStream << semantic.index;
    }
  }

  if (isTemp)
    nameStream << "_temp";

  std::string name = nameStream.str();
  builder.add(ir::Op::DebugName(def, name.c_str()));
}

}
