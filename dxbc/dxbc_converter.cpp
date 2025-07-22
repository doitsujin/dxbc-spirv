#include "dxbc_converter.h"

#include "../util/util_log.h"

namespace dxbc_spv::dxbc {

Converter::Converter(Container container, const Options& options)
: m_dxbc(std::move(container)), m_options(options) {

}


Converter::~Converter() {

}


bool Converter::convertShader(ir::Builder& builder) {
  if (!initSignature(m_isgn, m_dxbc.getInputSignatureChunk()) ||
      !initSignature(m_osgn, m_dxbc.getOutputSignatureChunk()) ||
      !initSignature(m_psgn, m_dxbc.getPatchConstantSignatureChunk()) ||
      !initParser(m_parser, m_dxbc.getCodeChunk()))
    return false;

  while (m_parser) {
    Instruction op = m_parser.parseInstruction();

    if (!op || !convertInstruction(builder, op))
      return false;
  }

  return true;
}


bool Converter::convertInstruction(ir::Builder& builder, const Instruction& op) {
  auto opCode = op.getOpToken().getOpCode();

  switch (opCode) {
    case OpCode::eNop:
      return true;

    case OpCode::eAdd:
    case OpCode::eAnd:
    case OpCode::eBreak:
    case OpCode::eBreakc:
    case OpCode::eCall:
    case OpCode::eCallc:
    case OpCode::eCase:
    case OpCode::eContinue:
    case OpCode::eContinuec:
    case OpCode::eCut:
    case OpCode::eDefault:
    case OpCode::eDerivRtx:
    case OpCode::eDerivRty:
    case OpCode::eDiscard:
    case OpCode::eDiv:
    case OpCode::eDp2:
    case OpCode::eDp3:
    case OpCode::eDp4:
    case OpCode::eElse:
    case OpCode::eEmit:
    case OpCode::eEmitThenCut:
    case OpCode::eEndIf:
    case OpCode::eEndLoop:
    case OpCode::eEndSwitch:
    case OpCode::eEq:
    case OpCode::eExp:
    case OpCode::eFrc:
    case OpCode::eFtoI:
    case OpCode::eFtoU:
    case OpCode::eGe:
    case OpCode::eIAdd:
    case OpCode::eIf:
    case OpCode::eIEq:
    case OpCode::eIGe:
    case OpCode::eILt:
    case OpCode::eIMad:
    case OpCode::eIMax:
    case OpCode::eIMin:
    case OpCode::eIMul:
    case OpCode::eINe:
    case OpCode::eINeg:
    case OpCode::eIShl:
    case OpCode::eIShr:
    case OpCode::eItoF:
    case OpCode::eLabel:
    case OpCode::eLd:
    case OpCode::eLdMs:
    case OpCode::eLog:
    case OpCode::eLoop:
    case OpCode::eLt:
    case OpCode::eMad:
    case OpCode::eMin:
    case OpCode::eMax:
    case OpCode::eCustomData:
    case OpCode::eMov:
    case OpCode::eMovc:
    case OpCode::eMul:
    case OpCode::eNe:
    case OpCode::eNot:
    case OpCode::eOr:
    case OpCode::eResInfo:
    case OpCode::eRet:
    case OpCode::eRetc:
    case OpCode::eRoundNe:
    case OpCode::eRoundNi:
    case OpCode::eRoundPi:
    case OpCode::eRoundZ:
    case OpCode::eRsq:
    case OpCode::eSample:
    case OpCode::eSampleC:
    case OpCode::eSampleClz:
    case OpCode::eSampleL:
    case OpCode::eSampleD:
    case OpCode::eSampleB:
    case OpCode::eSqrt:
    case OpCode::eSwitch:
    case OpCode::eSinCos:
    case OpCode::eUDiv:
    case OpCode::eULt:
    case OpCode::eUGe:
    case OpCode::eUMul:
    case OpCode::eUMad:
    case OpCode::eUMax:
    case OpCode::eUMin:
    case OpCode::eUShr:
    case OpCode::eUtoF:
    case OpCode::eXor:
    case OpCode::eDclResource:
    case OpCode::eDclConstantBuffer:
    case OpCode::eDclSampler:
    case OpCode::eDclIndexRange:
    case OpCode::eDclGsOutputPrimitiveTopology:
    case OpCode::eDclGsInputPrimitive:
    case OpCode::eDclMaxOutputVertexCount:
    case OpCode::eDclInput:
    case OpCode::eDclInputSgv:
    case OpCode::eDclInputSiv:
    case OpCode::eDclInputPs:
    case OpCode::eDclInputPsSgv:
    case OpCode::eDclInputPsSiv:
    case OpCode::eDclOutput:
    case OpCode::eDclOutputSgv:
    case OpCode::eDclOutputSiv:
    case OpCode::eDclTemps:
    case OpCode::eDclIndexableTemp:
    case OpCode::eDclGlobalFlags:
    case OpCode::eLod:
    case OpCode::eGather4:
    case OpCode::eSamplePos:
    case OpCode::eSampleInfo:
    case OpCode::eHsDecls:
    case OpCode::eHsControlPointPhase:
    case OpCode::eHsForkPhase:
    case OpCode::eHsJoinPhase:
    case OpCode::eEmitStream:
    case OpCode::eCutStream:
    case OpCode::eEmitThenCutStream:
    case OpCode::eInterfaceCall:
    case OpCode::eBufInfo:
    case OpCode::eDerivRtxCoarse:
    case OpCode::eDerivRtxFine:
    case OpCode::eDerivRtyCoarse:
    case OpCode::eDerivRtyFine:
    case OpCode::eGather4C:
    case OpCode::eGather4Po:
    case OpCode::eGather4PoC:
    case OpCode::eRcp:
    case OpCode::eF32toF16:
    case OpCode::eF16toF32:
    case OpCode::eUAddc:
    case OpCode::eUSubb:
    case OpCode::eCountBits:
    case OpCode::eFirstBitHi:
    case OpCode::eFirstBitLo:
    case OpCode::eFirstBitShi:
    case OpCode::eUBfe:
    case OpCode::eIBfe:
    case OpCode::eBfi:
    case OpCode::eBfRev:
    case OpCode::eSwapc:
    case OpCode::eDclStream:
    case OpCode::eDclFunctionBody:
    case OpCode::eDclFunctionTable:
    case OpCode::eDclInterface:
    case OpCode::eDclInputControlPointCount:
    case OpCode::eDclOutputControlPointCount:
    case OpCode::eDclTessDomain:
    case OpCode::eDclTessPartitioning:
    case OpCode::eDclTessOutputPrimitive:
    case OpCode::eDclHsMaxTessFactor:
    case OpCode::eDclHsForkPhaseInstanceCount:
    case OpCode::eDclHsJoinPhaseInstanceCount:
    case OpCode::eDclThreadGroup:
    case OpCode::eDclUavTyped:
    case OpCode::eDclUavRaw:
    case OpCode::eDclUavStructured:
    case OpCode::eDclThreadGroupSharedMemoryRaw:
    case OpCode::eDclThreadGroupSharedMemoryStructured:
    case OpCode::eDclResourceRaw:
    case OpCode::eDclResourceStructured:
    case OpCode::eLdUavTyped:
    case OpCode::eStoreUavTyped:
    case OpCode::eLdRaw:
    case OpCode::eStoreRaw:
    case OpCode::eLdStructured:
    case OpCode::eStoreStructured:
    case OpCode::eAtomicAnd:
    case OpCode::eAtomicOr:
    case OpCode::eAtomicXor:
    case OpCode::eAtomicCmpStore:
    case OpCode::eAtomicIAdd:
    case OpCode::eAtomicIMax:
    case OpCode::eAtomicIMin:
    case OpCode::eAtomicUMax:
    case OpCode::eAtomicUMin:
    case OpCode::eImmAtomicAlloc:
    case OpCode::eImmAtomicConsume:
    case OpCode::eImmAtomicIAdd:
    case OpCode::eImmAtomicAnd:
    case OpCode::eImmAtomicOr:
    case OpCode::eImmAtomicXor:
    case OpCode::eImmAtomicExch:
    case OpCode::eImmAtomicCmpExch:
    case OpCode::eImmAtomicIMax:
    case OpCode::eImmAtomicIMin:
    case OpCode::eImmAtomicUMax:
    case OpCode::eImmAtomicUMin:
    case OpCode::eSync:
    case OpCode::eDAdd:
    case OpCode::eDMax:
    case OpCode::eDMin:
    case OpCode::eDMul:
    case OpCode::eDEq:
    case OpCode::eDGe:
    case OpCode::eDLt:
    case OpCode::eDNe:
    case OpCode::eDMov:
    case OpCode::eDMovc:
    case OpCode::eDtoF:
    case OpCode::eFtoD:
    case OpCode::eEvalSnapped:
    case OpCode::eEvalSampleIndex:
    case OpCode::eEvalCentroid:
    case OpCode::eDclGsInstanceCount:
    case OpCode::eAbort:
    case OpCode::eDebugBreak:
    case OpCode::eDDiv:
    case OpCode::eDFma:
    case OpCode::eDRcp:
    case OpCode::eMsad:
    case OpCode::eDtoI:
    case OpCode::eDtoU:
    case OpCode::eItoD:
    case OpCode::eUtoD:
    case OpCode::eGather4S:
    case OpCode::eGather4CS:
    case OpCode::eGather4PoS:
    case OpCode::eGather4PoCS:
    case OpCode::eLdS:
    case OpCode::eLdMsS:
    case OpCode::eLdUavTypedS:
    case OpCode::eLdRawS:
    case OpCode::eLdStructuredS:
    case OpCode::eSampleLS:
    case OpCode::eSampleClzS:
    case OpCode::eSampleClampS:
    case OpCode::eSampleBClampS:
    case OpCode::eSampleDClampS:
    case OpCode::eSampleCClampS:
    case OpCode::eCheckAccessFullyMapped:
      /* TODO implement these */
      break;
  }

  Logger::err("Unhandled opcode ", opCode);
  return false;
}


bool Converter::initSignature(Signature& sig, util::ByteReader reader) {
  /* Chunk not present, this is fine */
  if (!reader)
    return true;

  if (!(sig = Signature(reader))) {
    Logger::err("Failed to parse signature chunk.");
    return false;
  }

  return true;
}


bool Converter::initParser(Parser& parser, util::ByteReader reader) {
  /* The code chunk is not optional and must be vaid */
  if (!reader) {
    Logger::err("No code chunk found in shader.");
    return false;
  }

  if (!(parser = Parser(reader))) {
    Logger::err("Failed to parse code chunk.");
    return false;
  }

  return true;
}


}
