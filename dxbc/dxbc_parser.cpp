#include "dxbc_parser.h"
#include "dxbc_container.h"

#include "../util/util_log.h"

namespace dxbc_spv::dxbc {

static const std::array<InstructionLayout, 235> g_instructionLayouts = {{
  /* Add */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* And */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* Break */
  { },
  /* Breakc */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* Call */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Callc */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool     },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Case */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* Continue */
  { },
  /* Continuec */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* Cut */
  { },
  /* Default */
  { },
  /* DerivRtx */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DerivRty */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Discard */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* Div */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dp2 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dp3 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dp4 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Else */
  { },
  /* Emit */
  { },
  /* EmitThenCut */
  { },
  /* EndIf */
  { },
  /* EndLoop */
  { },
  /* EndSwitch */
  { },
  /* Eq */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
  }} },
  /* Exp */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Frc */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* FtoI */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* FtoU */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Ge */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
  }} },
  /* IAdd */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* If */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* IEq */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool   },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* IGe */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eI32  },
    { OperandKind::eSrcReg, ir::ScalarType::eI32  },
  }} },
  /* ILt */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eI32  },
    { OperandKind::eSrcReg, ir::ScalarType::eI32  },
  }} },
  /* IMad */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* IMax */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* IMin */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* IMul */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* INe */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
  }} },
  /* INeg */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* IShl */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* IShr */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* ItoF */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* Label */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* Ld */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv },
  }} },
  /* LdMs */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* Log */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Loop */
  { },
  /* Lt */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
  }} },
  /* Mad */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Min */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Max */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* CustomData */
  { },
  /* Mov */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Movc */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eBool     },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Mul */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Ne */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
  }} },
  /* Nop */
  { },
  /* Not */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* Or */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* ResInfo */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Ret */
  { },
  /* Retc */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* RoundNe */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* RoundNi */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* RoundPi */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* RoundZ */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Rsq */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Sample */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* SampleC */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleClz */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleL */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleD */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleB */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* Sqrt */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Switch */
  { {{
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* SinCos */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* UDiv */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* ULt */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
  }} },
  /* UGe */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
  }} },
  /* UMul */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* UMad */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* UMax */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* UMin */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* UShr */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* UtoF */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* Xor */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* DclResource */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclConstantBuffer */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eCbv },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclSampler */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eSampler  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclIndexRange */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclGsOutputPrimitiveTopology */
  { },
  /* DclGsInputPrimitive */
  { },
  /* DclMaxOutputVertexCount */
  { {{
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclInput */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* DclInputSgv */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclInputSiv */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclInputPs */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* DclInputPsSgv */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclInputPsSiv */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclOutput */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* DclOutputSgv */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclOutputSiv */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclTemps */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclIndexableTemp */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
    { OperandKind::eImm32, ir::ScalarType::eU32 },
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclGlobalFlags */
  { },
  /* Reserved0 */
  { },
  /* Lod */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* Gather4 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* SamplePos */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32   },
  }} },
  /* SampleInfo */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Reserved1 */
  { },
  /* HsDecls */
  { },
  /* HsControlPointPhase */
  { },
  /* HsForkPhase */
  { },
  /* HsJoinPhase */
  { },
  /* EmitStream */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* CutStream */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* EmitThenCutStream */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* InterfaceCall */
  { {{
    { OperandKind::eImm32,  ir::ScalarType::eU32     },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown },
  }} },
  /* BufInfo */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* DerivRtxCoarse */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DerivRtxFine */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DerivRtyCoarse */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DerivRtyFine */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Gather4C */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* Gather4Po */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* Gather4PoC */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* Rcp */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* F32toF16 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* F16toF32 */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* UAddc */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* USubb */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* CountBits */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* FirstBitHi */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* FirstBitLo */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* FirstBitShi */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* UBfe */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* IBfe */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* Bfi */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* BfRev */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* Swapc */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown },
    { OperandKind::eSrcReg, ir::ScalarType::eBool    },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown },
  }} },
  /* DclStream */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown },
  }} },
  /* DclFunctionBody */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eUnknown },
  }} },
  /* DclFunctionTable */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eUnknown },
    { OperandKind::eImm32, ir::ScalarType::eUnknown },
  }} },
  /* DclInterface */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eUnknown },
    { OperandKind::eImm32, ir::ScalarType::eUnknown },
    { OperandKind::eImm32, ir::ScalarType::eUnknown },
  }} },
  /* DclInputControlPointCount */
  { },
  /* DclOutputControlPointCount */
  { },
  /* DclTessDomain */
  { },
  /* DclTessPartitioning */
  { },
  /* DclTessOutputPrimitive */
  { },
  /* DclHsMaxTessFactor */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eF32 },
  }} },
  /* DclHsForkPhaseInstanceCount */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclHsJoinPhaseInstanceCount */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclThreadGroup */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
    { OperandKind::eImm32, ir::ScalarType::eU32 },
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclUavTyped */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUav },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclUavRaw */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUav },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclUavStructured */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUav },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclThreadGroupSharedMemoryRaw */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclThreadGroupSharedMemoryStructured */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclResourceRaw */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclResourceStructured */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eSrv },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* LdUavTyped */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUav      },
  }} },
  /* StoreUavTyped */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUav      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* LdRaw */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* StoreRaw */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* LdStructured */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* StoreStructured */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* AtomicAnd */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* AtomicOr */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* AtomicXor */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* AtomicCmpStore */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* AtomicIAdd */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* AtomicIMax */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* AtomicIMin */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* AtomicUMax */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* AtomicUMin */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicAlloc */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eUav },
  }} },
  /* ImmAtomicConsume */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eUav },
  }} },
  /* ImmAtomicIAdd */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicAnd */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicOr */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicXor */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicExch */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicCmpExch */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicIMax */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicIMin */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicUMax */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* ImmAtomicUMin */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* Sync */
  { },
  /* DAdd */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DMax */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DMin */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DMul */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DEq */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DGe */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DLt */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DNe */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DMov */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DMovc */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DtoF */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* FtoD */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* EvalSnapped */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* EvalSampleIndex */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* EvalCentroid */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DclGsInstanceCount */
  { {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* Abort */
  { },
  /* DebugBreak */
  { },
  /* ReservedBegin11_1 */
  { },
  /* DDiv */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DFma */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DRcp */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* Msad */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* DtoI */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DtoU */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* ItoD */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* UtoD */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* ReservedBegin11_2 */
  { },
  /* Gather4S */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* Gather4CS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* Gather4PoS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* Gather4PoCS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* LdS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* LdMsS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* LdUavTypedS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUav      },
  }} },
  /* LdRawS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* LdStructuredS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* SampleLS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleClzS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleClampS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleBClampS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleDClampS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleCClampS */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* CheckAccessFullyMapped */
  { {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
  }} },
}};


const InstructionLayout* getInstructionLayout(OpCode op) {
  auto index = uint32_t(op);

  return index < g_instructionLayouts.size()
    ? &g_instructionLayouts[index]
    : nullptr;
}




ShaderInfo::ShaderInfo(util::ByteReader& reader) {
  if (!reader.read(m_versionToken) ||
      !reader.read(m_lengthToken))
    resetOnError();
}


bool ShaderInfo::write(util::ByteWriter& writer) const {
  return writer.write(m_versionToken) &&
         writer.write(m_lengthToken);
}


void ShaderInfo::resetOnError() {
  *this = ShaderInfo();
}




OpToken::OpToken(util::ByteReader& reader) {
  if (!reader.read(m_token)) {
    Logger::err("Failed to read opcode token.");
    return;
  }

  if (isCustomData()) {
    /* Read length token, skip everything else */
    if (!reader.read(m_length)) {
      Logger::err("Failed to read custom data length token.");
      resetOnError();
      return;
    }
  } else {
    /* Read extended dwords */
    uint32_t dword = m_token;

    while (dword & ExtendedTokenBit) {
      if (!reader.read(dword)) {
        Logger::err("Failed to read extended opcode token.");
        resetOnError();
        return;
      }

      /* Parse actual extended token */
      auto kind = extractExtendedOpcodeType(dword);
      auto data = extractExtendedOpcodePayload(dword);

      switch (kind) {
        case ExtendedOpcodeType::eSampleControls:
          m_sampleControls = SampleControlToken(data);
          break;

        case ExtendedOpcodeType::eResourceDim:
          m_resourceDim = ResourceDimToken(data);
          break;

        case ExtendedOpcodeType::eResourceReturnType:
          m_resourceType = ResourceTypeToken(data);
          break;

        default:
          Logger::err("Unhandled extended opcode token ", uint32_t(kind));
          break;
      }
    }
  }
}


bool OpToken::write(util::ByteWriter& writer) const {
  util::small_vector<uint32_t, 4u> tokens = { };
  tokens.push_back(m_token);

  /* Set extended tokens */
  if (m_sampleControls)
    tokens.push_back(m_sampleControls.asToken());

  if (m_resourceDim)
    tokens.push_back(m_resourceDim.asToken());

  if (m_resourceType)
    tokens.push_back(m_resourceType.asToken());

  /* Set extended bit for tokens */
  for (size_t i = 0u; i + 1u < tokens.size(); i++)
    tokens[i] |= ExtendedTokenBit;

  /* Emit raw dword tokens */
  for (const auto& dw : tokens) {
    if (!writer.write(dw))
      return false;
  }

  return true;
}


void OpToken::resetOnError() {
  m_token = 0u;
}




Operand::Operand(util::ByteReader& reader, const OperandInfo& info, Instruction& op)
: Operand(info, RegisterType::eImm32, ComponentCount::e1Component) {
  /* Pretend that immediate operands are essentially properly encoded operands
   * with a single immediate field to make this easier to work with. */
  if (info.kind == OperandKind::eImm32) {
    if (!reader.read(m_imm[0u])) {
      Logger::err("Failed to read literal operand");
      resetOnError();
    }

    return;
  }

  /* For non-immediate operands, parse the operand token itself */
  if (!reader.read(m_token)) {
    Logger::err("Failed to read operand token");
    resetOnError();
    return;
  }

  /* Parse follow-up tokens */
  uint32_t dword = m_token;

  while (dword & ExtendedTokenBit) {
    if (!reader.read(dword)) {
      Logger::err("Failed to read extended operand token");
      resetOnError();
      return;
    }

    /* Parse extended token */
    auto kind = extractExtendedOperandType(dword);

    switch (kind) {
      case ExtendedOperandType::eModifiers:
        m_modifiers = OperandModifiers(dword);
        break;

      default:
        Logger::err("Unhandled extended operand token: ", uint32_t(kind));
        break;
    }
  }

  /* Read immediate value or index tokens, depending on the operand type */
  auto type = getRegisterType();

  if (type == RegisterType::eImm32 || type == RegisterType::eImm64) {
    uint32_t dwordCount = 0u;

    switch (getComponentCount()) {
      case ComponentCount::e1Component:
        dwordCount = type == RegisterType::eImm64 ? 2u : 1u;
        break;

      case ComponentCount::e4Component:
        dwordCount = 4u;
        break;

      default:
        Logger::err("Unhandled component count for immediate: ", uint32_t(getComponentCount()));
        break;
    }

    for (uint32_t i = 0u; i < dwordCount; i++) {
      if (!reader.read(m_imm[i])) {
        Logger::err("Failed to read immediate value");
        resetOnError();
        return;
      }
    }
  } else {
    for (uint32_t i = 0u; i < getIndexDimensions(); i++) {
      auto kind = getIndexType(i);

      /* Read absolute offset first */
      uint32_t absoluteDwords = 0u;

      switch (kind) {
        case IndexType::eImm32:
        case IndexType::eImm32PlusRelative:
          absoluteDwords = 1u;
          break;

        case IndexType::eImm64:
        case IndexType::eImm64PlusRelative:
          Logger::err("64-bit indexing not supported");
          absoluteDwords = 2u;
          break;

        case IndexType::eRelative:
          break;

        default:
          Logger::err("Unhandled index type: ", uint32_t(kind));
          break;
      }

      if (absoluteDwords) {
        bool success = reader.read(m_imm[i]);

        if (absoluteDwords > 1u)
          success = success && reader.skip((absoluteDwords - 1u) * sizeof(uint32_t));

        if (!success) {
          Logger::err("Failed to read absolute index");
          resetOnError();
          return;
        }
      }

      /* Recursively parse relative operand. The order in which operands are
       * added to the instruction does not matter much, so indices occuring
       * before actual data operands is fine. */
      if (hasRelativeIndexing(kind)) {
        OperandInfo indexInfo = { };
        indexInfo.kind = OperandKind::eIndex;
        indexInfo.type = ir::ScalarType::eU32;

        Operand index(reader, indexInfo, op);

        if (!index) {
          resetOnError();
          return;
        }

        m_idx[i] = op.addOperand(std::move(index));
      }
    }
  }
}


WriteMask Operand::getWriteMask() const {
  auto count = getComponentCount();

  /* Four-component mask is encoded in the token  */
  if (count == ComponentCount::e4Component) {
    auto mode = getSelectionMode();

    if (mode == SelectionMode::eMask)
      return WriteMask(util::bextract(m_token, 4u, 4u));

    if (mode == SelectionMode::eSelect1)
      return WriteMask(1u << util::bextract(m_token, 4u, 2u));

    Logger::err("Unhandled selection mode: ", uint32_t(mode));
    return WriteMask();
  }

  /* Scalar operand  */
  if (count == ComponentCount::e1Component)
    return ComponentBit::eX;

  if (count != ComponentCount::e0Component)
    Logger::err("Unhandled component count: ", uint32_t(count));

  return WriteMask();
}


Swizzle Operand::getSwizzle() const {
  auto count = getComponentCount();

  /* Four-component mask is encoded in the token  */
  if (count == ComponentCount::e4Component) {
    auto mode = getSelectionMode();

    if (mode == SelectionMode::eSwizzle)
      return Swizzle(util::bextract(m_token, 4u, 8u));

    if (mode == SelectionMode::eSelect1) {
      auto c = Component(util::bextract(m_token, 4u, 2u));
      return Swizzle(c, c, c, c);
    }

    Logger::err("Unhandled selection mode for swizzle: ", uint32_t(mode));
    return Swizzle::identity();
  }

  /* Scalar operand  */
  if (count == ComponentCount::e1Component)
    return Swizzle();

  Logger::err("Unhandled coponent count: ", uint32_t(count));
  return Swizzle();
}


Operand& Operand::setWriteMask(WriteMask mask) {
  dxbc_spv_assert(getComponentCount() == ComponentCount::e4Component);

  m_token = util::binsert(m_token, uint32_t(uint8_t(mask)), 4u, 4u);
  return setSelectionMode(SelectionMode::eMask);
}


Operand& Operand::setSwizzle(Swizzle swizzle) {
  dxbc_spv_assert(getComponentCount() == ComponentCount::e4Component);

  m_token = util::binsert(m_token, uint32_t(uint8_t(swizzle)), 4u, 8u);
  return setSelectionMode(SelectionMode::eSwizzle);
}


Operand& Operand::setComponent(Component component) {
  dxbc_spv_assert(getComponentCount() == ComponentCount::e4Component);

  m_token = util::binsert(m_token, uint32_t(component), 4u, 2u);
  return setSelectionMode(SelectionMode::eSelect1);
}


Operand& Operand::setIndex(uint32_t dim, uint32_t absolute, uint32_t relative) {
  dxbc_spv_assert(dim < getIndexDimensions());

  bool hasAbsolute = absolute != 0u;
  bool hasRelative = relative < -1u;

  auto type = hasRelative
    ? (hasAbsolute ? IndexType::eImm32PlusRelative : IndexType::eRelative)
    : IndexType::eImm32;

  m_token = util::binsert(m_token, uint32_t(type), 22u + 3u * dim, 3u);

  m_imm[dim] = absolute;
  m_idx[dim] = uint8_t(relative);
  return *this;
}


Operand& Operand::addIndex(uint32_t absolute, uint32_t relative) {
  uint32_t dim = getIndexDimensions();

  return setIndexDimensions(dim + 1u).setIndex(dim, absolute, relative);
}


bool Operand::write(util::ByteWriter& writer, const Instruction& op) const {
  if (m_info.kind == OperandKind::eImm32) {
    /* Write a single dword */
    dxbc_spv_assert(getRegisterType() == RegisterType::eImm32);

    return writer.write(getImmediate<uint32_t>(0u));
  } else {
    /* Emit token, including modifiers */
    util::small_vector<uint32_t, 2u> tokens;
    tokens.push_back(m_token);

    if (m_modifiers) {
      tokens.back() |= ExtendedTokenBit;
      tokens.push_back(uint32_t(m_modifiers));
    }

    for (const auto& e : tokens) {
      if (!writer.write(e))
        return false;
    }

    /* Emit index operands */
    for (uint32_t i = 0u; i < getIndexDimensions(); i++) {
      auto type = getIndexType(i);

      if (hasAbsoluteIndexing(type)) {
        if (!writer.write(getIndex(i)))
          return false;
      }

      if (hasRelativeIndexing(type)) {
        const auto& operand = op.getRawOperand(getIndexOperand(i));

        if (!operand.write(writer, op))
          return false;
      }
    }

    return true;
  }
}


Operand& Operand::setSelectionMode(SelectionMode mode) {
  dxbc_spv_assert(getComponentCount() == ComponentCount::e4Component);

  m_token = util::binsert(m_token, uint32_t(mode), 2u, 2u);
  return *this;
}


void Operand::resetOnError() {
  m_token = 0u;
}


ExtendedOperandType Operand::extractExtendedOperandType(uint32_t token) {
  return ExtendedOperandType(util::bextract(token, 0u, 6u));
}



Instruction::Instruction(util::ByteReader& reader, const ShaderInfo& info) {
  /* Get some initial info out of the opcode token */
  auto tokenReader = util::ByteReader(reader);
  OpToken token(tokenReader);

  if (!token)
    return;

  auto byteSize = token.getLength() * sizeof(uint32_t);

  if (!byteSize) {
    Logger::err("Invalid instruction length:", token.getLength());
    return;
  }

  /* Get reader sub-range for the exact number of tokens required */
  tokenReader = reader.getRangeRelative(0u, byteSize);

  if (!tokenReader) {
    Logger::err("Invalid instruction length: ", token.getLength());
    return;
  }

  /* Advance base reader to the next instruction. */
  reader.skip(byteSize);

  /* Parse opcode token, including extended tokens */
  m_token = OpToken(tokenReader);

  /* Determine operand layout based on the shader
   * model and opcode, and parse the operands. */
  auto layout = getLayout(info);

  for (auto& operandInfo : layout.operands) {
    Operand operand(tokenReader, operandInfo, *this);

    if (!operand) {
      resetOnError();
      return;
    }

    addOperand(operand);
  }

  /* Copy custom data to local memory */
  if (m_token.isCustomData()) {
    m_customData.resize(tokenReader.getRemaining() / sizeof(uint32_t));

    for (auto& t : m_customData) {
      if (!tokenReader.read(t))
        Logger::err("Failed to read custom data blob.");
    }

  }

  /* Some instructions are padded with a zero dword for no reason, and
   * others have variable operand counts. Append unhandled operands as
   * raw dword immediates. */
  while (tokenReader.getRemaining()) {
    uint32_t dword = 0u;

    if (!tokenReader.read(dword))
      Logger::err("Operand token size not aligned to dwords.");

    Operand operand({ OperandKind::eExtra, ir::ScalarType::eU32 }, RegisterType::eImm32, ComponentCount::e1Component);
    operand.setImmediate(0u, dword);
    addOperand(operand);
  }
}


uint32_t Instruction::addOperand(const Operand& operand) {
  uint8_t index = uint8_t(m_operands.size());
  m_operands.push_back(operand);

  switch (operand.getInfo().kind) {
    case OperandKind::eSrcReg: m_srcOperands.push_back(index); break;
    case OperandKind::eDstReg: m_dstOperands.push_back(index); break;
    case OperandKind::eImm32:  m_immOperands.push_back(index); break;
    case OperandKind::eExtra:  m_extraOperands.push_back(index); break;
    case OperandKind::eNone:   dxbc_spv_unreachable();
    case OperandKind::eIndex:  break;
  }

  return index;
}


InstructionLayout Instruction::getLayout(const ShaderInfo& info) const {
  auto layout = getInstructionLayout(m_token.getOpCode());

  if (!layout) {
    Logger::err("No layout known for opcode: ", m_token.getOpCode());
    return InstructionLayout();
  }

  /* Adjust operand counts for resource declarations */
  auto result = *layout;
  auto [major, minor] = info.getVersion();

  if (major != 5u || minor != 1u) {
    switch (m_token.getOpCode()) {
      case OpCode::eDclConstantBuffer:
        result.operands.pop_back();
        result.operands.pop_back();
        break;

      case OpCode::eDclSampler:
      case OpCode::eDclResource:
      case OpCode::eDclResourceRaw:
      case OpCode::eDclResourceStructured:
      case OpCode::eDclUavTyped:
      case OpCode::eDclUavRaw:
      case OpCode::eDclUavStructured:
        result.operands.pop_back();
        break;

      default:
        break;
    }
  }

  for (size_t i = 0u; i < m_extraOperands.size(); i++)
    result.operands.push_back({ OperandKind::eExtra, ir::ScalarType::eU32 });

  return result;
}


bool Instruction::write(util::ByteWriter& writer, const ShaderInfo& info) const {
  auto offset = writer.moveToEnd();

  /* Extract token so we can write the correct size later */
  auto token = m_token;

  if (!token.write(writer))
    return false;

  /* Emit operands */
  auto layout = getLayout(info);

  uint32_t nDst = 0u;
  uint32_t nSrc = 0u;
  uint32_t nImm = 0u;
  uint32_t nExtra = 0u;

  for (const auto& operandInfo : layout.operands) {
    const auto* operand = [&] () -> const Operand* {
      switch (operandInfo.kind) {
        case OperandKind::eNone:
        case OperandKind::eIndex:  break;
        case OperandKind::eDstReg: return nDst < getDstCount() ? &getDst(nDst++) : nullptr;
        case OperandKind::eSrcReg: return nSrc < getSrcCount() ? &getSrc(nSrc++) : nullptr;
        case OperandKind::eImm32:  return nImm < getImmCount() ? &getImm(nImm++) : nullptr;
        case OperandKind::eExtra:  return nExtra < getExtraCount() ? &getExtra(nExtra++) : nullptr;
      }

      return nullptr;
    } ();

    if (!operand) {
      Logger::err("Missing operands for instruction ", token.getOpCode());
      return false;
    }

    if (!operand->write(writer, *this))
      return false;
  }

  /* Set final length and re-emit opcode token */
  auto byteCount = writer.moveToEnd() - offset;
  token.setLength(byteCount / sizeof(uint32_t));

  writer.moveTo(offset);

  if (!token.write(writer))
    return false;

  writer.moveToEnd();
  return true;
}


void Instruction::resetOnError() {
  m_token = OpToken();
}




Parser::Parser(util::ByteReader reader) {
  ChunkHeader header(reader);
  ShaderInfo info(reader);

  size_t tokenSize = info.getDwordCount() * sizeof(uint32_t);

  if (header.size < tokenSize) {
    Logger::err(header.tag, " chunk too small, expected ", tokenSize, " bytes, got ", header.size);
    return;
  }

  /* Write back reader and shader parameters */
  m_reader = reader.getRange(sizeof(header), tokenSize);
  m_info = ShaderInfo(m_reader);
}


Instruction Parser::parseInstruction() {
  return Instruction(m_reader, m_info);
}




Builder::Builder(ShaderType type, uint32_t major, uint32_t minor)
: m_info(type, major, minor, 0u) {

}


Builder::~Builder() {

}


void Builder::add(Instruction ins) {
  m_instructions.push_back(std::move(ins));
}


bool Builder::write(util::ByteWriter& writer) const {
  /* Chunk header. The tag depends on the shader model used. */
  auto chunkOffset = writer.moveToEnd();

  ChunkHeader chunkHeader = { };
  chunkHeader.tag = m_info.getVersion().first >= 5
    ? util::FourCC("SHEX")
    : util::FourCC("SHDR");

  if (!chunkHeader.write(writer))
    return false;

  /* Shader bytecode header */
  auto dataOffset = writer.moveToEnd();

  if (!m_info.write(writer))
    return false;

  /* Emit instructions */
  for (const auto& e : m_instructions) {
    if (!e.write(writer, m_info))
      return false;
  }

  /* Compute byte and dword sizes and re-emit chunk header */
  auto finalOffset = writer.moveToEnd();

  writer.moveTo(chunkOffset);
  chunkHeader.size = finalOffset - dataOffset;

  if (!chunkHeader.write(writer))
    return false;

  /* Re-emit code header */
  writer.moveTo(dataOffset);

  auto info = m_info;
  info.setDwordCount(chunkHeader.size / sizeof(uint32_t));

  if (!info.write(writer))
    return false;

  return true;
}




std::ostream& operator << (std::ostream& os, ShaderType type) {
  switch (type) {
    case ShaderType::ePixel:    return os << "ps";
    case ShaderType::eVertex:   return os << "vs";
    case ShaderType::eGeometry: return os << "gs";
    case ShaderType::eHull:     return os << "hs";
    case ShaderType::eDomain:   return os << "ds";
    case ShaderType::eCompute:  return os << "cs";
  }

  return os << "ShaderType(" << uint32_t(type) << ")";
}


std::ostream& operator << (std::ostream& os, const ShaderInfo& info) {
  auto [major, minor] = info.getVersion();
  return os << info.getType() << "_" << major << "_" << minor << " (" << info.getDwordCount() << " tok)";
}

}
