#include "test_api_pass_arithmetic.h"

#include "../../ir/passes/ir_pass_arithmetic.h"
#include "../../ir/passes/ir_pass_lower_consume.h"
#include "../../ir/passes/ir_pass_remove_unused.h"
#include "../../ir/passes/ir_pass_scalarize.h"

namespace dxbc_spv::test_api {

static Builder& run_passes(Builder& b) {
  ir::ArithmeticPass::Options arithmeticOptions = { };

  while (true) {
    bool progress = false;

    progress |= ir::ArithmeticPass::runPass(b, arithmeticOptions);
    progress |= ir::LowerConsumePass::runResolveCastChainsPass(b);
    progress |= ir::ScalarizePass::runResolveRedundantCompositesPass(b);

    if (!progress)
      break;

    ir::RemoveUnusedPass::runPass(b);
  }

  ir::ArithmeticPass::runLateLoweringPasses(b, arithmeticOptions);
  return b;
}


static std::tuple<Builder, SsaDef, SsaDef> setup_builder() {
  Builder builder;

  auto entryPoint = setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));
  builder.add(Op::Label());

  auto uav = builder.add(Op::DclUav(Type(ScalarType::eI32).addArrayDimension(0u), entryPoint, 0u, 0u, 1u, ResourceKind::eBufferRaw, UavFlag::eWriteOnly));
  auto uavDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));

  auto srv = builder.add(Op::DclSrv(Type(ScalarType::eI32).addArrayDimension(0u), entryPoint, 0u, 0u, 1u, ResourceKind::eBufferRaw));
  auto srvDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eSrv, srv, builder.makeConstant(0u)));

  builder.addAfter(srvDescriptor, Op::Return());
  return std::tuple(builder, uavDescriptor, srvDescriptor);
}


void emit_store(Builder& b, SsaDef uav, uint32_t index, SsaDef value) {
  const auto& uavDef = b.getOpForOperand(uav, 0);
  auto uavType = uavDef.getType().getBaseType(0u);

  const auto& valueDef = b.getOp(value);
  auto valueType = valueDef.getType().getBaseType(0);

  value = b.add(Op::Drain(valueType, value));

  for (uint32_t i = 0u; i < valueType.getVectorSize(); i++) {
    auto address = b.makeConstant(index * valueType.getVectorSize() + i);

    auto result = value;

    if (valueType.isVector())
      result = b.add(Op::CompositeExtract(valueType.getBaseType(), value, b.makeConstant(i)));

    if (valueType.isBoolType()) {
      result = b.add(Op::Select(uavType, result,
        b.add(Op(OpCode::eConstant, uavType).addOperand(1u)),
        b.add(Op(OpCode::eConstant, uavType).addOperand(0u))));
    } else if (valueType != uavType) {
      if (byteSize(valueType.getBaseType()) == uavType.byteSize())
        result = b.add(Op::Cast(uavType, result));
      else if (valueType.isIntType())
        result = b.add(Op::ConvertItoI(uavType, result));
      else
        result = b.add(Op::ConvertFtoI(uavType, result));
    }

    b.add(Op::BufferStore(uav, address, result, 4));
  }
}


SsaDef emit_load(Builder& b, SsaDef srv, uint32_t index, BasicType type) {
  const auto& srvDef = b.getOpForOperand(srv, 0);
  auto srvType = srvDef.getType().getBaseType(0u);

  auto address = b.makeConstant(index * type.getVectorSize());

  auto loadType = BasicType(srvType.getBaseType(), type.getVectorSize());
  auto load = b.add(Op::BufferLoad(loadType, srv, address, 4));

  if (type.isBoolType()) {
    Op constant(OpCode::eConstant, loadType);

    for (uint32_t i = 0u; i < type.getVectorSize(); i++)
      constant.addOperand(Operand());

    load = b.add(Op::INe(type, load, b.add(constant)));
  } else if (type != loadType) {
    if (type.isFloatType())
      load = b.add(Op::ConvertItoF(type, load));
    else if (type.byteSize() != loadType.byteSize())
      load = b.add(Op::ConvertItoI(type, load));
    else
      load = b.add(Op::Cast(type, load));
  }

  return b.add(Op::Drain(type, load));
}


Builder test_pass_arithmetic_constant_fold_int32() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* 0x43028041 */
  emit_store(b, uav, test++, b.add(Op::IAnd(ScalarType::eI32,
    b.makeConstant(int32_t(0xf302a04f)), b.makeConstant(0x432f8761))));

  /* -1u */
  emit_store(b, uav, test++, b.add(Op::IAnd(ScalarType::eU32,
    b.makeConstant(-1u), b.makeConstant(-1u))));

  /* 0u */
  emit_store(b, uav, test++, b.add(Op::IAnd(ScalarType::eU32,
    b.makeConstant(-1u), b.makeConstant(0u))));

  /* 15 */
  emit_store(b, uav, test++, b.add(Op::IOr(ScalarType::eI32,
    b.makeConstant(7), b.makeConstant(8))));

  /* 28 */
  emit_store(b, uav, test++, b.add(Op::IOr(ScalarType::eI32,
    b.makeConstant(0), b.makeConstant(28))));

  /* -1 */
  emit_store(b, uav, test++, b.add(Op::IOr(ScalarType::eU32,
    b.makeConstant(-1u), b.makeConstant(39u))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::IXor(ScalarType::eI32,
    b.makeConstant(47), b.makeConstant(47))));

  /* 33 */
  emit_store(b, uav, test++, b.add(Op::IXor(ScalarType::eI32,
    b.makeConstant(48), b.makeConstant(17))));

  /* -1u */
  emit_store(b, uav, test++, b.add(Op::IXor(ScalarType::eU32,
    b.makeConstant(0u), b.makeConstant(-1u))));

  /* -1 */
  emit_store(b, uav, test++, b.add(Op::INot(ScalarType::eI32, b.makeConstant(0))));

  /* 65535u */
  emit_store(b, uav, test++, b.add(Op::INot(ScalarType::eU32, b.makeConstant(0xffff0000u))));

  /* 0x35b */
  emit_store(b, uav, test++, b.add(Op::IBitInsert(ScalarType::eU32,
    b.makeConstant(0x37fu), b.makeConstant(6u), b.makeConstant(2u), b.makeConstant(4u))));

  /* 0x123 */
  emit_store(b, uav, test++, b.add(Op::UBitExtract(ScalarType::eU32,
    b.makeConstant(0xf2345678u), b.makeConstant(20u), b.makeConstant(9u))));

  /* 0xffffff23 */
  emit_store(b, uav, test++, b.add(Op::SBitExtract(ScalarType::eI32,
    b.makeConstant(int32_t(0xf2345678)), b.makeConstant(20u), b.makeConstant(9u))));

  /* 0x45 */
  emit_store(b, uav, test++, b.add(Op::SBitExtract(ScalarType::eI32,
    b.makeConstant(int32_t(0xf2345678)), b.makeConstant(12u), b.makeConstant(8u))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::SBitExtract(ScalarType::eI32,
    b.makeConstant(int32_t(0xf2345678)), b.makeConstant(12u), b.makeConstant(0u))));

  /* -1 */
  emit_store(b, uav, test++, b.add(Op::SBitExtract(ScalarType::eI32,
    b.makeConstant(int32_t(0xf2345678)), b.makeConstant(3u), b.makeConstant(1u))));

  /* 48 */
  emit_store(b, uav, test++, b.add(Op::IShl(ScalarType::eI32,
    b.makeConstant(3), b.makeConstant(4u))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::UShr(ScalarType::eU32,
    b.makeConstant(16), b.makeConstant(5u))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::UShr(ScalarType::eU32,
    b.makeConstant(32), b.makeConstant(37u))));

  /* 15 */
  emit_store(b, uav, test++, b.add(Op::UShr(ScalarType::eU32,
    b.makeConstant(-1u), b.makeConstant(28u))));

  /* -2 */
  emit_store(b, uav, test++, b.add(Op::SShr(ScalarType::eI32,
    b.makeConstant(int32_t(0x80000000)), b.makeConstant(30u))));

  /* 38 */
  emit_store(b, uav, test++, b.add(Op::IAdd(ScalarType::eI32,
    b.makeConstant(15), b.makeConstant(23))));

  /* 24 */
  emit_store(b, uav, test++, b.add(Op::IAdd(ScalarType::eI32,
    b.makeConstant(30), b.makeConstant(-6))));

  /* -2u */
  emit_store(b, uav, test++, b.add(Op::IAdd(ScalarType::eU32,
    b.makeConstant(6u), b.makeConstant(-8u))));

  /* -4u */
  emit_store(b, uav, test++, b.add(Op::IAdd(ScalarType::eI32,
    b.makeConstant(13), b.makeConstant(-17))));

  /* 0u */
  emit_store(b, uav, test++, b.add(Op::ISub(ScalarType::eI32,
    b.makeConstant(99), b.makeConstant(99))));

  /* 100 */
  emit_store(b, uav, test++, b.add(Op::ISub(ScalarType::eI32,
    b.makeConstant(50), b.makeConstant(-50))));

  /* 33 */
  emit_store(b, uav, test++, b.add(Op::IAbs(ScalarType::eI32, b.makeConstant(33))));

  /* 12 */
  emit_store(b, uav, test++, b.add(Op::IAbs(ScalarType::eI32, b.makeConstant(-12))));

  /* 17 */
  emit_store(b, uav, test++, b.add(Op::IAbs(ScalarType::eI32, b.makeConstant(17))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::IAbs(ScalarType::eI32, b.makeConstant(0))));

  /* 3198 */
  emit_store(b, uav, test++, b.add(Op::IAbs(ScalarType::eI32, b.makeConstant(-3198))));

  /* -6 */
  emit_store(b, uav, test++, b.add(Op::IMul(ScalarType::eI32,
    b.makeConstant(-2), b.makeConstant(3))));

  /* 45 */
  emit_store(b, uav, test++, b.add(Op::IMul(ScalarType::eI32,
    b.makeConstant(9), b.makeConstant(5))));

  /* 70u */
  emit_store(b, uav, test++, b.add(Op::IMul(ScalarType::eU32,
    b.makeConstant(-7u), b.makeConstant(-10u))));

  /* 7u */
  emit_store(b, uav, test++, b.add(Op::UDiv(ScalarType::eU32,
    b.makeConstant(65u), b.makeConstant(9u))));

  /* 2u */
  emit_store(b, uav, test++, b.add(Op::UMod(ScalarType::eU32,
    b.makeConstant(65u), b.makeConstant(9u))));

  /* 6 */
  emit_store(b, uav, test++, b.add(Op::SMin(ScalarType::eI32,
    b.makeConstant(39), b.makeConstant(6))));

  /* 39 */
  emit_store(b, uav, test++, b.add(Op::SMax(ScalarType::eI32,
    b.makeConstant(39), b.makeConstant(6))));

  /* -15 */
  emit_store(b, uav, test++, b.add(Op::SMin(ScalarType::eI32,
    b.makeConstant(-6), b.makeConstant(-15))));

  /* -6 */
  emit_store(b, uav, test++, b.add(Op::SMax(ScalarType::eI32,
    b.makeConstant(-6), b.makeConstant(-15))));

  /* -34 */
  emit_store(b, uav, test++, b.add(Op::SMin(ScalarType::eI32,
    b.makeConstant(-34), b.makeConstant(28))));

  /* 28 */
  emit_store(b, uav, test++, b.add(Op::SMax(ScalarType::eI32,
    b.makeConstant(-34), b.makeConstant(28))));

  /* 7 */
  emit_store(b, uav, test++, b.add(Op::SClamp(ScalarType::eI32,
    b.makeConstant(7), b.makeConstant(-10), b.makeConstant(10))));

  /* -10 */
  emit_store(b, uav, test++, b.add(Op::SClamp(ScalarType::eI32,
    b.makeConstant(-40), b.makeConstant(-10), b.makeConstant(10))));

  /* 10 */
  emit_store(b, uav, test++, b.add(Op::SClamp(ScalarType::eI32,
    b.makeConstant(67), b.makeConstant(-10), b.makeConstant(10))));

  /* 3 */
  emit_store(b, uav, test++, b.add(Op::UMin(ScalarType::eU32,
    b.makeConstant(3u), b.makeConstant(-1u))));

  /* -1u */
  emit_store(b, uav, test++, b.add(Op::UMax(ScalarType::eU32,
    b.makeConstant(3u), b.makeConstant(-1u))));

  /* 82 */
  emit_store(b, uav, test++, b.add(Op::UClamp(ScalarType::eU32,
    b.makeConstant(82u), b.makeConstant(18u), b.makeConstant(100u))));

  /* 18 */
  emit_store(b, uav, test++, b.add(Op::UClamp(ScalarType::eU32,
    b.makeConstant(0u), b.makeConstant(18u), b.makeConstant(100u))));

  /* 100 */
  emit_store(b, uav, test++, b.add(Op::UClamp(ScalarType::eU32,
    b.makeConstant(-1u), b.makeConstant(18u), b.makeConstant(100u))));

  return run_passes(b);
}


Builder test_pass_arithmetic_constant_fold_int16() {
  /* Mostly interesting for testing overflow behaviour */
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* 4464 */
  emit_store(b, uav, test++, b.add(Op::IAdd(ScalarType::eU16,
    b.makeConstant(uint16_t(50000u)), b.makeConstant(uint16_t(20000u)))));

  /* -1us -> 65536u */
  emit_store(b, uav, test++, b.add(Op::ISub(ScalarType::eU16,
    b.makeConstant(uint16_t(2u)), b.makeConstant(uint16_t(3u)))));

  /* -32768 */
  emit_store(b, uav, test++, b.add(Op::ISub(ScalarType::eI16,
    b.makeConstant(uint16_t(30000u)), b.makeConstant(uint16_t(-2768u)))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::IAbs(ScalarType::eI16, b.makeConstant(int16_t(-1)))));

  /* -1 */
  emit_store(b, uav, test++, b.add(Op::INeg(ScalarType::eI16, b.makeConstant(int16_t(1)))));

  return run_passes(b);
}


Builder test_pass_arithmetic_constant_fold_int16_vec2() {
  /* Don't bother retesting all the special cases here, just make
   * sure that each component receives the correct result */
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  auto stype = BasicType(ScalarType::eI16, 2u);
  auto utype = BasicType(ScalarType::eU16, 2u);

  /* 19, 97 */
  emit_store(b, uav, test++, b.add(Op::IAdd(stype,
    b.makeConstant(int16_t(-7), int16_t(43)),
    b.makeConstant(int16_t(26), int16_t(54)))));

  /* 13, -11 */
  emit_store(b, uav, test++, b.add(Op::ISub(stype,
    b.makeConstant(int16_t(-30), int16_t(20)),
    b.makeConstant(int16_t(-43), int16_t(31)))));

  /* 20, -8 */
  emit_store(b, uav, test++, b.add(Op::IMul(stype,
    b.makeConstant(int16_t(5), int16_t(-1)),
    b.makeConstant(int16_t(4), int16_t(8)))));

  /* 2, 128 */
  emit_store(b, uav, test++, b.add(Op::UShr(utype,
    b.makeConstant(uint16_t(10), uint16_t(256)),
    b.makeConstant(uint16_t(2),  uint16_t(1)))));

  /* -4, 5 */
  emit_store(b, uav, test++, b.add(Op::SShr(stype,
    b.makeConstant(int16_t(0x8000), int16_t(20)),
    b.makeConstant(int16_t(13),     int16_t(2)))));

  /* -2, 60 */
  emit_store(b, uav, test++, b.add(Op::SBitExtract(stype,
    b.makeConstant(int16_t(0x4321), int16_t(0xa3cd)),
    b.makeConstant(int16_t(13),     int16_t(4)),
    b.makeConstant(int16_t(2),      int16_t(8)))));

  /* 2, 188 */
  emit_store(b, uav, test++, b.add(Op::UBitExtract(stype,
    b.makeConstant(uint16_t(0x4321), uint16_t(0xabcd)),
    b.makeConstant(uint16_t(13),     uint16_t(4)),
    b.makeConstant(uint16_t(2),      uint16_t(8)))));

  return run_passes(b);

}


Builder test_pass_arithmetic_constant_fold_bool() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::BNot(ScalarType::eBool, b.makeConstant(true))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::BNot(ScalarType::eBool, b.makeConstant(false))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::BOr(ScalarType::eBool,
    b.makeConstant(false), b.makeConstant(false))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::BOr(ScalarType::eBool,
    b.makeConstant(false), b.makeConstant(true))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::BOr(ScalarType::eBool,
    b.makeConstant(true), b.makeConstant(false))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::BOr(ScalarType::eBool,
    b.makeConstant(true), b.makeConstant(true))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::BAnd(ScalarType::eBool,
    b.makeConstant(false), b.makeConstant(false))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::BAnd(ScalarType::eBool,
    b.makeConstant(true), b.makeConstant(false))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::BAnd(ScalarType::eBool,
    b.makeConstant(false), b.makeConstant(true))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::BAnd(ScalarType::eBool,
    b.makeConstant(true), b.makeConstant(true))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::BEq(ScalarType::eBool,
    b.makeConstant(false), b.makeConstant(false))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::BEq(ScalarType::eBool,
    b.makeConstant(true), b.makeConstant(false))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::BEq(ScalarType::eBool,
    b.makeConstant(false), b.makeConstant(true))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::BEq(ScalarType::eBool,
    b.makeConstant(true), b.makeConstant(true))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::BNe(ScalarType::eBool,
    b.makeConstant(false), b.makeConstant(false))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::BNe(ScalarType::eBool,
    b.makeConstant(true), b.makeConstant(false))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::BNe(ScalarType::eBool,
    b.makeConstant(false), b.makeConstant(true))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::BNe(ScalarType::eBool,
    b.makeConstant(true), b.makeConstant(true))));

  return run_passes(b);
}


Builder test_pass_arithmetic_constant_fold_compare() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FEq(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(0.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::FEq(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(1.0f))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FEq(ScalarType::eBool,
    b.makeConstant(-1.0f), b.makeConstant(1.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::FNe(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(0.0f))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FNe(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(1.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::FNe(ScalarType::eBool,
    b.makeConstant(-1.0f), b.makeConstant(1.0f))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FLt(ScalarType::eBool,
    b.makeConstant(-1.0f), b.makeConstant(-2.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::FLt(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(2.0f))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FLt(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(1.0f))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FLe(ScalarType::eBool,
    b.makeConstant(-1.0f), b.makeConstant(-2.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::FLe(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(2.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::FLe(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(1.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::FGt(ScalarType::eBool,
    b.makeConstant(-1.0f), b.makeConstant(-2.0f))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FGt(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(2.0f))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FGt(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(1.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::FGe(ScalarType::eBool,
    b.makeConstant(-1.0f), b.makeConstant(-2.0f))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FGe(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(2.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::FGe(ScalarType::eBool,
    b.makeConstant(1.0f), b.makeConstant(1.0f))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::FIsNan(ScalarType::eBool, b.makeConstant(1.0f))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::IEq(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(16))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::IEq(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(17))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::INe(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(16))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::INe(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(17))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::SLt(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(16))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::SLt(ScalarType::eBool,
    b.makeConstant(-10), b.makeConstant(16))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::SLt(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(10))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::SLe(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(16))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::SLe(ScalarType::eBool,
    b.makeConstant(10), b.makeConstant(16))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::SLe(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(10))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::SGt(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(16))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::SGt(ScalarType::eBool,
    b.makeConstant(10), b.makeConstant(-16))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::SGt(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(10))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::SGe(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(16))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::SGe(ScalarType::eBool,
    b.makeConstant(10), b.makeConstant(16))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::SGe(ScalarType::eBool,
    b.makeConstant(16), b.makeConstant(-10))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::ULt(ScalarType::eBool,
    b.makeConstant(16u), b.makeConstant(16u))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::ULt(ScalarType::eBool,
    b.makeConstant(10u), b.makeConstant(16u))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::ULt(ScalarType::eBool,
    b.makeConstant(16u), b.makeConstant(10u))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::ULe(ScalarType::eBool,
    b.makeConstant(16u), b.makeConstant(16u))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::ULe(ScalarType::eBool,
    b.makeConstant(10u), b.makeConstant(16u))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::ULe(ScalarType::eBool,
    b.makeConstant(16u), b.makeConstant(10u))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::UGt(ScalarType::eBool,
    b.makeConstant(16u), b.makeConstant(16u))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::UGt(ScalarType::eBool,
    b.makeConstant(10u), b.makeConstant(16u))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::UGt(ScalarType::eBool,
    b.makeConstant(16u), b.makeConstant(10u))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::UGe(ScalarType::eBool,
    b.makeConstant(16u), b.makeConstant(16u))));

  /* 0 */
  emit_store(b, uav, test++, b.add(Op::UGe(ScalarType::eBool,
    b.makeConstant(10u), b.makeConstant(16u))));

  /* 1 */
  emit_store(b, uav, test++, b.add(Op::UGe(ScalarType::eBool,
    b.makeConstant(16u), b.makeConstant(10u))));

  return run_passes(b);
}


Builder test_pass_arithmetic_constant_fold_select() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* 20 */
  emit_store(b, uav, test++, b.add(Op::Select(ScalarType::eU32,
    b.makeConstant(true), b.makeConstant(20u), b.makeConstant(10u))));

  /* 100 */
  emit_store(b, uav, test++, b.add(Op::Select(ScalarType::eU32,
    b.makeConstant(false), b.makeConstant(20u), b.makeConstant(10u))));

  return run_passes(b);
}


Builder test_pass_arithmetic_identities_bool() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* not not not a -> not a. then: not ine -> ieq */
  auto v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  auto v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  auto a = v0;
  a = b.add(Op::BNot(ScalarType::eBool, a));
  a = b.add(Op::BNot(ScalarType::eBool, a));
  a = b.add(Op::BNot(ScalarType::eBool, a));

  emit_store(b, uav, test++, a);

  /* a == a -> true */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BEq(ScalarType::eBool, v0, v0));
  emit_store(b, uav, test++, a);

  /* a != a -> false */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BNe(ScalarType::eBool, v0, v0));
  emit_store(b, uav, test++, a);

  /* !a == b -> a != b */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BEq(ScalarType::eBool, b.add(Op::BNot(ScalarType::eBool, v0)), v1));
  emit_store(b, uav, test++, a);

  /* And by extension: !a == a -> false */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BEq(ScalarType::eBool, b.add(Op::BNot(ScalarType::eBool, v0)), v0));
  emit_store(b, uav, test++, a);

  /* a == !b -> a != b */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BEq(ScalarType::eBool, v0, b.add(Op::BNot(ScalarType::eBool, v1))));
  emit_store(b, uav, test++, a);

  /* And by extension: !a == !b -> a == b */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BEq(ScalarType::eBool,
    b.add(Op::BNot(ScalarType::eBool, v0)),
    b.add(Op::BNot(ScalarType::eBool, v1))));
  emit_store(b, uav, test++, a);

  /* !a != b -> a == b */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BNe(ScalarType::eBool, b.add(Op::BNot(ScalarType::eBool, v0)), v1));
  emit_store(b, uav, test++, a);

  /* a != !b -> a == b */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BNe(ScalarType::eBool, v0, b.add(Op::BNot(ScalarType::eBool, v1))));
  emit_store(b, uav, test++, a);

  /* a && a -> a */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BAnd(ScalarType::eBool, v0, v0));
  emit_store(b, uav, test++, a);

  /* a && false -> false */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BAnd(ScalarType::eBool, v0, b.makeConstant(false)));
  emit_store(b, uav, test++, a);

  /* a && true -> a */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BAnd(ScalarType::eBool, v0, b.makeConstant(true)));
  emit_store(b, uav, test++, a);

  /* a || false -> a */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BOr(ScalarType::eBool, v0, b.makeConstant(false)));
  emit_store(b, uav, test++, a);

  /* a || true -> true */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BOr(ScalarType::eBool, v0, b.makeConstant(true)));
  emit_store(b, uav, test++, a);

  /* a || a -> a */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BOr(ScalarType::eBool, v0, v0));
  emit_store(b, uav, test++, a);

  /* !a && !b -> !(a || b) */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BAnd(ScalarType::eBool,
    b.add(Op::BNot(ScalarType::eBool, v0)),
    b.add(Op::BNot(ScalarType::eBool, v1))));
  emit_store(b, uav, test++, a);

  /* !a || !b -> !(a && b) */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eBool);

  a = b.add(Op::BOr(ScalarType::eBool,
    b.add(Op::BNot(ScalarType::eBool, v0)),
    b.add(Op::BNot(ScalarType::eBool, v1))));
  emit_store(b, uav, test++, a);

  return run_passes(b);
}


Builder test_pass_arithmetic_identities_compare() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* a == a (float) -> !isnan(a) */
  auto v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eF32);
  auto v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eF32);

  auto a = b.add(Op::FEq(ScalarType::eBool, v0, v0));
  emit_store(b, uav, test++, a);

  /* a != a (float) -> !isnan(a) */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FNe(ScalarType::eBool, v0, v0));
  emit_store(b, uav, test++, a);

  /* isnan(not nan) -> false */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FIsNan(ScalarType::eBool, b.add(Op::FClamp(ScalarType::eF32,
    v0, b.makeConstant(0.0f), b.makeConstant(1.0f)).setFlags(OpFlag::eNoNan))));
  emit_store(b, uav, test++, a);

  /* a < a -> false */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eI32);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eI32);

  a = b.add(Op::SLt(ScalarType::eBool, v0, v0));
  emit_store(b, uav, test++, a);

  /* a >= a -> true */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eI32);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eI32);

  a = b.add(Op::SGe(ScalarType::eBool, v0, v0));
  emit_store(b, uav, test++, a);

  /* float not a < a must stay as-is */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eF32);

  a = b.add(Op::BNot(ScalarType::eBool, b.add(Op::FLt(ScalarType::eBool, v0, v1))));
  emit_store(b, uav, test++, a);

  /* a < b || a == b || a > b !isnan a && !isnan b */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eF32);

  a = b.add(Op::BOr(ScalarType::eBool,
    b.add(Op::BOr(ScalarType::eBool,
      b.add(Op::FEq(ScalarType::eBool, v1, v0)),
      b.add(Op::FLt(ScalarType::eBool, v0, v1)))),
    b.add(Op::FGt(ScalarType::eBool, v0, v1))));
  emit_store(b, uav, test++, a);

  /* same thing again but with a constant */
  v0 = emit_load(b, srv, 2u * test + 0u, ScalarType::eF32);
  v1 = b.makeConstant(0.0f);

  a = b.add(Op::BOr(ScalarType::eBool,
    b.add(Op::BOr(ScalarType::eBool,
      b.add(Op::FEq(ScalarType::eBool, v1, v0)),
      b.add(Op::FLt(ScalarType::eBool, v0, v1)))),
    b.add(Op::FGt(ScalarType::eBool, v0, v1))));
  emit_store(b, uav, test++, a);

  return run_passes(b);
}


Builder test_pass_arithmetic_identities_select() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* cast(and(x, select(cond, -1, 0))) -> select(cond, x, 0) */
  auto v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);

  auto a = b.add(Op::Select(ScalarType::eU32, v0, b.makeConstant(-1u), b.makeConstant(0u)));
  a = b.add(Op::IAnd(ScalarType::eU32, a, b.makeConstant(0x40200000u)));
  a = b.add(Op::Cast(ScalarType::eF32, a));

  emit_store(b, uav, test++, a);

  /* select(!x, a, b) -> select(x, b, a) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);

  a = b.add(Op::BNot(ScalarType::eBool, v0));
  a = b.add(Op::Select(ScalarType::eU32, a, b.makeConstant(-1u), b.makeConstant(0u)));

  emit_store(b, uav, test++, a);

  /* select(x, a, a) -> a */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  auto v1 = emit_load(b, srv, 2u * test + 1u, ScalarType::eI32);

  a = b.add(Op::Select(ScalarType::eI32, v0, v1, v1));
  emit_store(b, uav, test++, a);

  /* and(select(x, -1, 0), select(y, -1, 0)) -> select(x && y, -1, 0) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::IAnd(ScalarType::eI32,
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1), b.makeConstant(0))),
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(-1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* and(select(x, -1, 0), select(y, 0, -1)) -> select(x && !y, -1, 0) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::IAnd(ScalarType::eI32,
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1), b.makeConstant(0))),
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(0), b.makeConstant(-1)))));
  emit_store(b, uav, test++, a);

  /* or(select(x, -1, 0), select(y, -1, 0)) -> select(x || y, -1, 0) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::IOr(ScalarType::eI32,
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1), b.makeConstant(0))),
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(-1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* xor(select(x, -1, 0), select(y, -1, 0)) -> select(x != y, -1, 0) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::IXor(ScalarType::eI32,
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1), b.makeConstant(0))),
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(-1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* xor(select(x, 0, 1), select(y, 1, 0)) -> select(x == y, 0, 1) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::IXor(ScalarType::eI32,
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(0), b.makeConstant(1))),
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* Cursed constant folded merge pattern */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::IOr(ScalarType::eI32,
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(1), b.makeConstant(2))),
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(1), b.makeConstant(2)))));
  emit_store(b, uav, test++, a);

  /* select(x, a, b) == a -> x */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);

  a = b.add(Op::IEq(ScalarType::eBool, b.makeConstant(-1),
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* select(x, a, b) == b -> !x */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);

  a = b.add(Op::IEq(ScalarType::eBool, b.makeConstant(0),
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* select(x, a, b) != a -> !x */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);

  a = b.add(Op::INe(ScalarType::eBool, b.makeConstant(-1),
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* select(x, a, b) != b -> x */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);

  a = b.add(Op::INe(ScalarType::eBool, b.makeConstant(0),
    b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* select(c1, select(c2, a, b), select(c3, a, b)) -> select(c1 && c2 || !c1 && c3, a, b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);
  auto v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eBool);

  a = b.add(Op::Select(ScalarType::eI32, v0,
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(-1), b.makeConstant(0))),
    b.add(Op::Select(ScalarType::eI32, v2, b.makeConstant(-1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* select(c1, select(c2, a, b), select(!c3, a, b)) -> select(c1 && c2 || !(c1 || c3), a, b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);
  v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eBool);

  a = b.add(Op::Select(ScalarType::eI32, v0,
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(-1), b.makeConstant(0))),
    b.add(Op::Select(ScalarType::eI32, v2, b.makeConstant(0), b.makeConstant(-1)))));
  emit_store(b, uav, test++, a);

  /* select(c1, select(c2, a, b), b) -> select(c1 && c2, a, b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::Select(ScalarType::eI32, v0,
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(-1), b.makeConstant(0))),
    b.makeConstant(0)));
  emit_store(b, uav, test++, a);

  /* select(c1, select(c2, b, a), b) -> select(c1 && !c2, a, b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::Select(ScalarType::eI32, v0,
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(0), b.makeConstant(-1))),
    b.makeConstant(0)));
  emit_store(b, uav, test++, a);

  /* select(c1, a, select(c2, a, b)) -> select(c1 || c2, a, b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1),
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(-1), b.makeConstant(0)))));
  emit_store(b, uav, test++, a);

  /* select(c1, a, select(c2, b, a)) -> select(c1 || !c2, a, b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eBool);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eBool);

  a = b.add(Op::Select(ScalarType::eI32, v0, b.makeConstant(-1),
    b.add(Op::Select(ScalarType::eI32, v1, b.makeConstant(0), b.makeConstant(-1)))));
  emit_store(b, uav, test++, a);

  return run_passes(b);
}


Builder test_pass_arithmetic_propagate_sign() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* a * (-b * c) -> - (a * b * c) */
  auto v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  auto v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);
  auto v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eF32);

  auto a = b.add(Op::FNeg(ScalarType::eF32, v1));
  a = b.add(Op::FMul(ScalarType::eF32, a, v2));
  a = b.add(Op::FMul(ScalarType::eF32, a, v0));
  emit_store(b, uav, test++, a);

  /* |a| * |b| -> |a * b| */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FMul(ScalarType::eF32,
    b.add(Op::FAbs(ScalarType::eF32, v0)),
    b.add(Op::FAbs(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* by extension: -|a| * |b| -> -|a * b| */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FMul(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, b.add(Op::FAbs(ScalarType::eF32, v0)))),
    b.add(Op::FAbs(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* -a + b -> b - a */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FAdd(ScalarType::eF32, b.add(Op::FNeg(ScalarType::eF32, v0)), v1));
  emit_store(b, uav, test++, a);

  /* a + -b -> a - b */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FAdd(ScalarType::eF32, v0, b.add(Op::FNeg(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* -a + -b -> -(a + b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FAdd(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, v0)),
    b.add(Op::FNeg(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* -a - -b -> b - a */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FSub(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, v0)),
    b.add(Op::FNeg(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* a - -b -> a + b */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FSub(ScalarType::eF32, v0,
    b.add(Op::FNeg(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* -a - b -> -(a + b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FSub(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, v0)), v1));
  emit_store(b, uav, test++, a);

  /* -(a - b) -> b - a */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FNeg(ScalarType::eF32,
    b.add(Op::FSub(ScalarType::eF32, v0, v1))));
  emit_store(b, uav, test++, a);

  /* -a * -b -> a * b */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FMul(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, v0)),
    b.add(Op::FNeg(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* -a / -b -> a / b */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FDiv(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, v0)),
    b.add(Op::FNeg(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* a / -b -> -(a / b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FDiv(ScalarType::eF32, v0,
    b.add(Op::FNeg(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* rcp(rcp(a)) -> a */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FRcp(ScalarType::eF32,
    b.add(Op::FRcp(ScalarType::eF32, v0))));
  emit_store(b, uav, test++, a);

  /* -rcp(-rcp(-rcp(a))) -> -rcp(a) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FNeg(ScalarType::eF32, b.add(Op::FRcp(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, b.add(Op::FRcp(ScalarType::eF32,
      b.add(Op::FNeg(ScalarType::eF32, b.add(Op::FRcp(ScalarType::eF32, v0))))))))))));
  emit_store(b, uav, test++, a);

  /* don't merge precise rcp */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FRcp(ScalarType::eF32,
    b.add(Op::FRcp(ScalarType::eF32, v0).setFlags(OpFlag::ePrecise))));
  emit_store(b, uav, test++, a);

  /* smax(a, -a) -> |a| */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eI32);

  a = b.add(Op::SMax(ScalarType::eI32, v0,
    b.add(Op::INeg(ScalarType::eI32, v0))));
  emit_store(b, uav, test++, a);

  /* smin(-a, a) -> -|a| */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eI32);

  a = b.add(Op::SMin(ScalarType::eI32,
    b.add(Op::INeg(ScalarType::eI32, v0)), v0));
  emit_store(b, uav, test++, a);

  /* fmax(a, -a) -> |a| */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FMax(ScalarType::eF32, v0,
    b.add(Op::FNeg(ScalarType::eF32, v0))));
  emit_store(b, uav, test++, a);

  /* fmin(-a, a) -> -|a| */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FMin(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, v0)), v0));
  emit_store(b, uav, test++, a);

  /* fmin(a, a) -> a */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FMin(ScalarType::eF32, v0, v0));
  emit_store(b, uav, test++, a);

  /* fmax(a, a) -> a */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FMax(ScalarType::eF32, v0, v0));
  emit_store(b, uav, test++, a);

  /* fmax(-a, -b) -> -fmin(a, b) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FMax(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, v0)),
    b.add(Op::FNeg(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  /* |-x| -> |x| */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FAbs(ScalarType::eF32,
    b.add(Op::FNeg(ScalarType::eF32, v0))));
  emit_store(b, uav, test++, a);

  /* ||x|| -> |x| */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FAbs(ScalarType::eF32,
    b.add(Op::FAbs(ScalarType::eF32, v0))));
  emit_store(b, uav, test++, a);

  /* 1.0f / x -> rcp(x) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FDiv(ScalarType::eF32, b.makeConstant(1.0f), v0));
  emit_store(b, uav, test++, a);

  /* -1.0f / x -> -rcp(x) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);

  a = b.add(Op::FDiv(ScalarType::eF32, b.makeConstant(-1.0f), v0));
  emit_store(b, uav, test++, a);

  /* const * -a -> a * -const */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = b.makeConstant(-2.0f);

  a = b.add(Op::FMul(ScalarType::eF32, v1, b.add(Op::FNeg(ScalarType::eF32, v0))));
  emit_store(b, uav, test++, a);

  /* select(x, |a|, |b|) -> |select(x, a, b)| */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);
  v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eBool);

  a = b.add(Op::Select(ScalarType::eF32, v2,
    b.add(Op::FAbs(ScalarType::eF32, v0)),
    b.add(Op::FAbs(ScalarType::eF32, v1))));
  emit_store(b, uav, test++, a);

  return run_passes(b);
}


Builder test_pass_arithmetic_fuse_mad() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* a + b * c = mad(b, c, a) */
  auto v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  auto v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);
  auto v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eF32);

  auto a = b.add(Op::FAdd(ScalarType::eF32, v0,
    b.add(Op::FMul(ScalarType::eF32, v1, v2))));
  emit_store(b, uav, test++, a);

  /* a - b * c = mad(-b, c, a) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);
  v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eF32);

  a = b.add(Op::FSub(ScalarType::eF32, v0,
    b.add(Op::FMul(ScalarType::eF32, v1, v2))));
  emit_store(b, uav, test++, a);

  /* a - b * (-c) = mad(b, c, a) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);
  v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eF32);

  a = b.add(Op::FSub(ScalarType::eF32, v0,
    b.add(Op::FMul(ScalarType::eF32, v1,
      b.add(Op::FNeg(ScalarType::eF32, v2))))));
  emit_store(b, uav, test++, a);

  /* a * b - c = mad(a, b, -c) */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);
  v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eF32);

  a = b.add(Op::FSub(ScalarType::eF32,
    b.add(Op::FMul(ScalarType::eF32, v0, v1)), v2));
  emit_store(b, uav, test++, a);

  return run_passes(b);
}


Builder test_pass_arithmetic_lower_legacy() {
  auto [b, uav, srv] = setup_builder();

  uint32_t test = 0u;

  /* plain mul_legacy */
  auto v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  auto v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  auto a = b.add(Op::FMulLegacy(ScalarType::eF32, v0, v1));
  emit_store(b, uav, test++, a);

  /* plain mad_legacy */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);
  auto v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eF32);

  a = b.add(Op::FMadLegacy(ScalarType::eF32, v0, v1, v2));
  emit_store(b, uav, test++, a);

  /* fused mul_legacy */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);
  v2 = emit_load(b, srv, 3u * test + 2u, ScalarType::eF32);

  a = b.add(Op::FAdd(ScalarType::eF32, v0,
    b.add(Op::FMul(ScalarType::eF32, v1, v2))));
  emit_store(b, uav, test++, a);

  /* mad_legacy with 16-bit vector operands */
  v0 = emit_load(b, srv, 3u * test + 0u, BasicType(ScalarType::eF16, 2u));
  v1 = emit_load(b, srv, 3u * test + 1u, BasicType(ScalarType::eF16, 2u));
  v2 = emit_load(b, srv, 3u * test + 2u, BasicType(ScalarType::eF16, 2u));

  a = b.add(Op::FMadLegacy(BasicType(ScalarType::eF16, 2u), v0, v1, v2));
  emit_store(b, uav, test++, a);

  /* pow_legacy */
  v0 = emit_load(b, srv, 3u * test + 0u, ScalarType::eF32);
  v1 = emit_load(b, srv, 3u * test + 1u, ScalarType::eF32);

  a = b.add(Op::FPowLegacy(ScalarType::eF32, v0, v1));
  emit_store(b, uav, test++, a);

  return run_passes(b);
}

}
