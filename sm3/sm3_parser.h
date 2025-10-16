#pragma once

#include <optional>

#include "../util/util_byte_stream.h"
#include "../util/util_bit.h"

#include "../ir/ir.h"

#include "sm3_types.h"

namespace dxbc_spv::sm3 {

class Instruction;

/** Shader code header */
class ShaderInfo {

public:

  ShaderInfo() = default;

  /** Constructs shader info. Note that the dword count must include
   *  the two dwords consumed by the info structure itself. */
  ShaderInfo(ShaderType type, uint32_t major, uint32_t minor)
  : m_token(((~0u & uint32_t(type)) << 16) | ((major & 0xff) << 8u) | (minor & 0xff)) { }

  /** Parses shader info */
  explicit ShaderInfo(util::ByteReader& reader);

  /** Extracts shader type from version token */
  ShaderType getType() const {
    return ShaderType(util::bextract(m_token, 16u, 1));
  }

  /** Extracts shader model version from version token */
  std::pair<uint32_t, uint32_t> getVersion() const {
    return std::make_pair(
      util::bextract(m_token, 8u, 8u),
      util::bextract(m_token, 0u, 8u));
  }

  /** Checks whether shader header is valid */
  explicit operator bool () const {
    return util::bextract(m_token, 17u, 15u) == 0x7fff;
  }

  /** Writes code header to binary blob. */
  bool write(util::ByteWriter& writer) const;

  uint32_t getToken() const {
    return m_token;
  }

private:

  uint32_t m_token = 0u;

  void resetOnError();

};


/** The constant table data structure embedded into comments in the shader */
struct CommentConstantTable {
  /** Size of the struct, has to be `sizeof(CommentConstantTable)` */
  uint32_t size;
  /** Offset of the null-terminated creator string in bytes from the start of this struct */
  uint32_t creatorOffset;
  /** Version of the shader */
  uint32_t shaderVersion;
  /** The number of constants in the following array of CommentConstantInfo structs */
  uint32_t constantsCount;
  /** Offset of the array of CommentConstantInfo structs in bytes from the start of this struct */
  uint32_t constantInfoOffset;
  /** Flags that were used for compiling the HLSL code of this shader */
  uint32_t flags;
  /** Offset of the null-terminated target shader model string in bytes from the start of this struct */
  uint32_t targetOffset;
};

enum class ConstantType : uint16_t {
  eBool    = 0u,
  eInt4    = 1u,
  eFloat4  = 2u,
  eSampler = 3u
};

/** A single constant entry embedded into comments in the shader */
struct CommentConstantInfo {
  /** Offset of the null-terminated name string in bytes from the start of this struct */
  uint32_t nameOffset;
  /** Type of the constants */
  ConstantType registerSet;
  /** Index of the constants in the register array */
  uint16_t registerIndex;
  /** The number of constants associated with this entry */
  uint16_t registerCount;
  uint16_t padding;
  /** Offset of the type info in bytes from the start of this struct */
  uint32_t typeInfoOffset;
  /** Offset of the default value in bytes from the start of this struct */
  uint32_t defaultValueOffset;
};

/** A constant entry */
struct ConstantInfo {
  /** Name of the constants */
  std::string name;
  /** Type of the constants */
  ConstantType registerSet;
  /** Index of the constants in the register array */
  uint32_t index;
  /** The number of constants */
  uint32_t count;
};

/** Stores debug information about constant registers */
class ConstantTable {

public:

  ConstantTable() = default;

  explicit ConstantTable(util::ByteReader reader);

  /** Looks up debug information for the given constant register.
   *  Will return null if no entry is found for the given constant register.
   *  Passing other non-constant register types is fine, it will just return null. */
  const ConstantInfo* findConstantInfo(RegisterType registerType, uint32_t index) const;

  /** Whether it contains debug information for any constant */
  explicit operator bool () const {
    for (uint32_t i = 0; i < m_constants.size(); i++) {
      if (!m_constants[i].empty()) {
        return true;
      }
    }
    return false;
  }

private:

  std::array<std::vector<ConstantInfo>, uint32_t(ConstantType::eSampler) + 1u> m_constants = { };

};


/** Operand type. Used internally in order to set up the instruction
 *  layout and to distinguish operand tokens from immediate values
 *  when parsing instructions. */
enum class OperandKind : uint8_t {
  eNone    = 0u,
  eDstReg  = 1u,
  eSrcReg  = 2u,
  eDcl     = 3u,
  eImm32   = 4u,
};


/** Component selection */
enum class SelectionMode : uint32_t {
  eMask       = 0u,
  eSwizzle    = 1u,
  eSelect1    = 2u,
};


/**
 * Source operand modifiers
 *
 * These are applied after loading
 * an operand register.
 */
enum class OperandModifier : uint32_t {
  eNone    = 0u,  // r
  eNeg     = 1u,  // -r
  eBias    = 2u,  // r - 0.5
  eBiasNeg = 3u,  // -(r - 0.5)
  eSign    = 4u,  // fma(r, 2.0f, -1.0f)
  eSignNeg = 5u,  // -fma(r, 2.0f, -1.0f)
  eComp    = 6u,  // 1 - r
  eX2      = 7u,  // r * 2
  eX2Neg   = 8u,  // -r * 2
  eDz      = 9u,  // r / r.z
  eDw      = 10u, // r / r.w
  eAbs     = 11u, // abs(r)
  eAbsNeg  = 12u, // -abs(r)
  eNot     = 13u, // !r
};


/**
 * Determines whether the shader uses a separate token to configure relative addressing
 */
inline bool hasExtraRelativeAddressingToken(OperandKind kind, const ShaderInfo& info) {
  return (info.getVersion().first >= 2 && kind == OperandKind::eSrcReg)
        || (info.getVersion().first >= 3 && kind == OperandKind::eDstReg);
}


/** Operand info */
struct OperandInfo {
  OperandKind     kind = { };
  ir::ScalarType  type = ir::ScalarType::eUnknown;
};


/** Operand info. Stores the base and extended operand tokens if
 *  applicable, as well as immediates or index values, depending
 *  on the operand type. */
class Operand {

public:

  Operand() = default;

  /** Creates operand and sets up the operand type.
   * Other properties need to be set manually. */
  Operand(OperandInfo info, RegisterType type)
    : m_token(((uint32_t(type) & 0b11000) << 11u) | (uint32_t(type) & 0b111) << 28u),
      m_info(info) { }

  /** Recursively parses operand in byte stream, index operands. */
  Operand(util::ByteReader& reader, const OperandInfo& info, Instruction& op, const ShaderInfo& shaderInfo);

  /** Operand metadata. Not part of the operand tokens, but useful
   *  when processing operands depending on the instruction layout. */
  OperandInfo getInfo() const {
    return m_info;
  }

  /** Queries whether the operand is scalar */
  bool isScalar(const ShaderInfo& shaderInfo) const {
    return getRegisterType() == RegisterType::eLoop
       || getRegisterType() == RegisterType::eConstBool
       || (getRegisterType() == RegisterType::eRasterizerOut && getIndex() == uint32_t(RasterizerOutIndex::eRasterOutFog))
       || (getRegisterType() == RegisterType::eRasterizerOut && getIndex() == uint32_t(RasterizerOutIndex::eRasterOutPointSize))
       || (getRegisterType() == RegisterType::eMiscType && getIndex() == uint32_t(MiscTypeIndex::eMiscTypeFace))
       || (getRegisterType() == RegisterType::eAddr && shaderInfo.getVersion().first == 1)
       || getRegisterType() == RegisterType::eDepthOut;
  }

  /** Queries component selection mode. Determines the existence
   *  of a swizzle or write mask in the operand token. */
  SelectionMode getSelectionMode(const ShaderInfo& shaderInfo) const {
    if (isScalar(shaderInfo)) {
      return SelectionMode::eSelect1;
    }
    return m_info.kind == OperandKind::eDstReg ? SelectionMode::eMask : SelectionMode::eSwizzle;
  }

  /** Queries component swizzle. Only useful for source operands. */
  Swizzle getSwizzle(const ShaderInfo& shaderInfo) const {
    dxbc_spv_assert(m_info.kind == OperandKind::eSrcReg);
    if (isScalar(shaderInfo)) {
      return Swizzle(Component::eX, Component::eX, Component::eX, Component::eX);
    }
    return Swizzle(util::bextract(m_token, 16u, 8u));
  }

  /** Queries component write mask. Only useful for destination
   *  operands. */
  WriteMask getWriteMask(const ShaderInfo& shaderInfo) const {
    dxbc_spv_assert(m_info.kind == OperandKind::eDstReg);
    if (isScalar(shaderInfo)) {
      return WriteMask(ComponentBit::eX);
    }
    return WriteMask(util::bextract(m_token, 16u, 4u));
  }

  /** Queries whether the results of the instruction get saturated (clamped to 0..1) before
    * writing them to the destination register. Only relevant for destination register operands. */
  bool isSaturated() const {
    dxbc_spv_assert(m_info.kind == OperandKind::eDstReg);
    return util::bextract(m_token, 20, 1);
  }

  /** Queries the number of bits by which the results of the instructions get shifted before
    * writing them to the destination register. Only relevant for destination register operands */
  int8_t getShift() const {
    dxbc_spv_assert(m_info.kind == OperandKind::eDstReg);
    return util::bextractSigned<int8_t, uint32_t>(m_token, 24u, 4u);
  }

  /** Queries whether the pixel shader input should be interpolated at the centroid.
    * Only relevant for shader inputs in destination register operands. */
  bool isCentroid() const {
    dxbc_spv_assert(m_info.kind == OperandKind::eDstReg);
    return util::bextract(m_token, 22u, 1u);
  }

  /** Queries whether the operation can be performed using reduced precision.
    * Only relevant for destination operands */
  bool isPartialPrecision() const {
    dxbc_spv_assert(m_info.kind == OperandKind::eDstReg);
    return util::bextract(m_token, 21, 1);
  }

  /** Queries register type */
  RegisterType getRegisterType() const {
    return RegisterType((util::bextract(m_token, 11u, 2u) << 3u)
      | util::bextract(m_token, 28u, 3u));
  }

  /** Queries register index. */
  uint32_t getIndex() const {
    return util::bextract(m_token, 0u, 11u);
  }

  /** Queries the relative addressing register. */
  RegisterType getRelativeAddressingRegisterType() const {
    dxbc_spv_assert(hasRelativeAddressing());
    return RegisterType((util::bextract(m_addressToken.value(), 11u, 2u) << 3u)
      | util::bextract(m_addressToken.value(), 28u, 3u));
  }

  /** Queries the relative addressing swizzle. */
  Swizzle getRelativeAddressingSwizzle() const {
    dxbc_spv_assert(hasRelativeAddressing());
    return Swizzle(util::bextract(m_addressToken.value(), 16u, 8u));
  }

  /** Queries whether the operand uses relative addressing. */
  bool hasRelativeAddressing() const {
    dxbc_spv_assert(m_info.kind == OperandKind::eSrcReg || m_info.kind == OperandKind::eDstReg);
    return util::bextract(m_token, 13u, 1u);
  }

  bool isPredicated() const {
    return m_predicateToken.has_value();
  }

  /** Queries the predicate swizzle. */
  Swizzle getPredicateSwizzle() const {
    dxbc_spv_assert(isPredicated());
    return Swizzle(util::bextract(m_predicateToken.value(), 16u, 8u));
  }

  /** Queries predicate modifier. */
  OperandModifier getPredicateModifier() const {
    dxbc_spv_assert(isPredicated());
    return OperandModifier(util::bextract(m_predicateToken.value(), 24u, 4u));
  }

  /** Queries operand modifier. */
  OperandModifier getModifier() const {
    dxbc_spv_assert(m_info.kind == OperandKind::eSrcReg);
    return OperandModifier(util::bextract(m_token, 24u, 4u));
  }

  /** Queries the semantic usage.
   *  Only useful for declaration operands of shader inputs and outputs. */
  SemanticUsage getSemanticUsage() const {
    dxbc_spv_assert(m_info.kind == OperandKind::eDcl);
    return SemanticUsage(util::bextract(m_token, 0u, 4u));
  }

  /** Queries the semantic index.
   *  Only useful for declaration operands of shader inputs and outputs. */
  uint32_t getSemanticIndex() const {
    dxbc_spv_assert(m_info.kind == OperandKind::eDcl);
    return util::bextract(m_token, 16u, 4u);
  }

  /** Queries the texture type. Only useful for declaration operands. */
  TextureType getTextureType() const {
    dxbc_spv_assert(m_info.kind == OperandKind::eDcl);
    return TextureType(util::bextract(m_token, 27u, 4u));
  }

  /** Queries immediate value for a given component. */
  template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, bool> = true>
  T getImmediate(uint32_t idx) const {
    util::uint_type_t<T> data;
    data = m_imm[idx];
    T result;
    std::memcpy(&result, &data, sizeof(result));
    return result;
  }

  /** Writes code header to binary blob. */
  bool write(util::ByteWriter& writer, const ShaderInfo& info) const;

  /** Checks operand info is valid. */
  explicit operator bool () const {
    return m_isValid;
  }

private:

  uint32_t                  m_token   = { };

  bool                      m_isValid = false;

  OperandInfo               m_info = { };

  std::array<uint32_t, 4u>  m_imm            = { };
  std::optional<uint32_t>   m_addressToken   = { };
  std::optional<uint32_t>   m_predicateToken = { };

  void resetOnError();

};

/** Instruction layout. Stores the number of operands, and
 *  whether each operand is a source or destination register
 *  or an immediate. */
struct InstructionLayout {
  util::small_vector<OperandInfo, 8u> operands = { };
};


class Instruction {

public:

  Instruction() = default;

  /** Initializes instruction with opcode and no operands. This will
   *  not interact with the instruction layout in any way. */
  explicit Instruction(OpCode opCode)
  : m_token(uint32_t(opCode)) { }

  /** Initializes instruction with opcode token. */
  explicit Instruction(uint32_t token)
  : m_token(token) { }

  /** Parses an instruction in a code chunk. */
  explicit Instruction(util::ByteReader& reader, const ShaderInfo& info);

  /** Extracts opcode from token */
  OpCode getOpCode() const {
    return OpCode(util::bextract(m_token, 0u, 16u));
  }

  bool isPredicated() const {
    return util::bextract(m_token, 28u, 1u);
  }

  bool isCoissued() const {
    return util::bextract(m_token, 30u, 1u);
  }

  uint32_t getOperandTokenCount(const ShaderInfo& info, const InstructionLayout& layout) const;

  /** Retrieves whether the instruction has a destination operand */
  bool hasDst() const {
    // Dst is always the first operand except for dcl instructions where it's the second.
    return (!m_operands.empty() && m_operands[0u].getInfo().kind == OperandKind::eDstReg)
      || (m_operands.size() > 1u && m_operands[1u].getInfo().kind == OperandKind::eDstReg);
  }

  /** Retrieves whether the instruction has a declaration operand */
  bool hasDcl() const {
    // Dcl operands only exist in dcl instructions and they are always the first operand.
    return !m_operands.empty() && m_operands[0u].getInfo().kind == OperandKind::eDcl;
  }

  /** Retrieves number of source operands */
  uint32_t getSrcCount() const {
    if (m_operands.empty())
      return 0u;

    // Src operands can come with a dst operand (but never with anything else)
    OperandKind firstArgKind = m_operands[0u].getInfo().kind;
    if (firstArgKind == OperandKind::eDstReg)
      return uint32_t(m_operands.size() - 1u);

    if (firstArgKind == OperandKind::eSrcReg)
      return uint32_t(m_operands.size());

    return 0u;
  }

  /** Retrieves number of immediate operands */
  bool hasImm() const {
    // Imm operands only exist in def instructions and are always at index 1
    return m_operands.size() > 1u && m_operands[1u].getInfo().kind == OperandKind::eImm32;
  }

  /** Queries destination operand. */
  const Operand& getDst() const {
    if (m_operands.size() > 1u && m_operands[1u].getInfo().kind == OperandKind::eDstReg)
      return getRawOperand(1u);

    dxbc_spv_assert(!m_operands.empty() && m_operands[0u].getInfo().kind == OperandKind::eDstReg);
    return getRawOperand(0u);
  }

  /** Queries destination operand. */
  const Operand& getDcl() const {
    dxbc_spv_assert(hasDcl());
    return getRawOperand(0u);
  }

  /** Queries source operand. */
  const Operand& getSrc(uint32_t index) const {
    dxbc_spv_assert(!m_operands.empty());
    uint32_t operandIndex = m_operands[0u].getInfo().kind == OperandKind::eDstReg
      ? index + 1u
      : index;

    const Operand& operand = getRawOperand(operandIndex);
    dxbc_spv_assert(operand.getInfo().kind == OperandKind::eSrcReg);
    return operand;
  }

  /** Queries immediate operand. */
  const Operand& getImm() const {
    dxbc_spv_assert(hasImm());
    return getRawOperand(1u);
  }

  /** Queries raw operand index. Generally used
   *  when processing relative operand indices. */
  const Operand& getRawOperand(uint32_t index) const {
    dxbc_spv_assert(index < m_operands.size());
    return m_operands[index];
  }

  /** Queries instruction layout for the given instruction,
   *  with the given shader model in mind. The shader model
   *  changes the layout of resource declaration ops. */
  InstructionLayout getLayout(const ShaderInfo& info) const;

  /** Writes instruction and all its operands to a binary blob. */
  bool write(util::ByteWriter& writer, const ShaderInfo& info) const;

  /** Queries the comparison mode of the instruction.
    * Only relevant for a few control flow instructions. */
  ComparisonMode getComparisonMode() const {
    return ComparisonMode(util::bextract(m_token, 16u, 8u));
  }

  /** Queries the TexLd mode of the instruction. */
  TexLdMode getTexLdMode() const {
    return TexLdMode(util::bextract(m_token, 16u, 8u));
  }

  /** Get the comment data. Must only be called for comment instructions. */
  const unsigned char* getCommentData() const {
    return m_commentData.data();
  }

  /** Get the size of the comment in bytes. */
  uint32_t getCommentDataSize() const {
    return m_commentData.size();
  }

  /** Checks whether instruction is valid */
  explicit operator bool () const {
    return m_isValid;
  }

private:

  uint32_t m_token = { };

  bool m_isValid = false;

  util::small_vector<Operand, 16u> m_operands = { };

  std::vector<unsigned char> m_commentData = { };

  void resetOnError();

};


class Parser {

public:
  Parser() = default;

  explicit Parser(util::ByteReader reader);

  /** Queries shader info, including the shader type and version. This
   *  is always available since it is stored at the start of the chunk. */
  ShaderInfo getShaderInfo() const {
    return m_info;
  }

  Instruction parseInstruction();

  /** Checks whether any more instructions are
   *  available to be parsed. */
  explicit operator bool () const {
    return !m_isPastEnd;
  }

private:

  util::ByteReader m_reader;

  ShaderInfo m_info = { };

  bool m_isPastEnd = false;

};


/** Builder. Provides a simple way to create an instruction sequence
 *  and generate a valid shader out of it. */
class Builder {

public:

  /** Initializes builder with shader type info */
  Builder(ShaderType type, uint32_t major, uint32_t minor);

  ~Builder();

  /** Appends an instruction to the list */
  void add(Instruction ins);

  /** Writes the instructions. */
  bool write(util::ByteWriter& writer) const;

private:

  ShaderInfo m_info;

  std::vector<Instruction> m_instructions;

};


std::ostream& operator << (std::ostream& os, ShaderType type);
std::ostream& operator << (std::ostream& os, const ShaderInfo& shaderInfo);

}
