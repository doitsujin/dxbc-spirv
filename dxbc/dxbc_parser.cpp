#include "dxbc_parser.h"
#include "dxbc_container.h"

#include "../util/util_log.h"

namespace dxbc_spv::dxbc {

static const std::array<InstructionLayout, 235> g_instructionLayouts = {{
  /* Add */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* And */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* Break */
  { 0u },
  /* Breakc */
  { 1u, {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* Call */
  { 1u, {{
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Callc */
  { 2u, {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool     },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Case */
  { 1u, {{
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* Continue */
  { 0u },
  /* Continuec */
  { 1u, {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* Cut */
  { 0u },
  /* Default */
  { 0u },
  /* DerivRtx */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DerivRty */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Discard */
  { 1u, {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* Div */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dp2 */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dp3 */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Dp4 */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Else */
  { 0u },
  /* Emit */
  { 0u },
  /* EmitThenCut */
  { 0u },
  /* EndIf */
  { 0u },
  /* EndLoop */
  { 0u },
  /* EndSwitch */
  { 0u },
  /* Eq */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
  }} },
  /* Exp */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Frc */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* FtoI */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* FtoU */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Ge */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
  }} },
  /* IAdd */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* If */
  { 1u, {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* IEq */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* IGe */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eI32  },
    { OperandKind::eSrcReg, ir::ScalarType::eI32  },
  }} },
  /* ILt */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eI32  },
    { OperandKind::eSrcReg, ir::ScalarType::eI32  },
  }} },
  /* IMad */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* IMax */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* IMin */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* IMul */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eI32    },
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* INe */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* INeg */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* IShl */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* IShr */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eI32    },
    { OperandKind::eSrcReg, ir::ScalarType::eI32    },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* ItoF */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* Label */
  { 1u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* Ld */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
  }} },
  /* LdMs */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* Log */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Loop */
  { 0u },
  /* Lt */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
  }} },
  /* Mad */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Min */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Max */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* CustomData */
  { 0u },
  /* Mov */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Movc */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eBool     },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Mul */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Ne */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32  },
  }} },
  /* Nop */
  { 0u },
  /* Not */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* Or */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* ResInfo */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Ret */
  { 0u },
  /* Retc */
  { 1u, {{
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
  }} },
  /* RoundNe */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* RoundNi */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* RoundPi */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* RoundZ */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Rsq */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Sample */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* SampleC */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleClz */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleL */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleD */
  { 6u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleB */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* Sqrt */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Switch */
  { 1u, {{
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* SinCos */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* UDiv */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* ULt */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
  }} },
  /* UGe */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
    { OperandKind::eSrcReg, ir::ScalarType::eU32  },
  }} },
  /* UMul */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32    },
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* UMad */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* UMax */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* UMin */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* UShr */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32    },
    { OperandKind::eSrcReg, ir::ScalarType::eU32    },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* UtoF */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* Xor */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* DclResource */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclConstantBuffer */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eCbv },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclSampler */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eSampler  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclIndexRange */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclGsOutputPrimitiveTopology */
  { 0u },
  /* DclGsInputPrimitive */
  { 0u },
  /* DclMaxOutputVertexCount */
  { 1u, {{
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclInput */
  { 1u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* DclInputSgv */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclInputSiv */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclInputPs */
  { 1u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* DclInputPsSgv */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclInputPsSiv */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclOutput */
  { 1u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* DclOutputSgv */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclOutputSiv */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclTemps */
  { 1u, {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclIndexableTemp */
  { 3u, {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
    { OperandKind::eImm32, ir::ScalarType::eU32 },
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclGlobalFlags */
  { 0u },
  /* Reserved0 */
  { 0u },
  /* Lod */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* Gather4 */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* SamplePos */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* SampleInfo */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* Reserved1 */
  { },
  /* HsDecls */
  { 0u },
  /* HsControlPointPhase */
  { 0u },
  /* HsForkPhase */
  { 0u },
  /* HsJoinPhase */
  { 0u },
  /* EmitStream */
  { 1u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* CutStream */
  { 1u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* EmitThenCutStream */
  { 1u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
  }} },
  /* InterfaceCall */
  { },
  /* BufInfo */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* DerivRtxCoarse */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DerivRtxFine */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DerivRtyCoarse */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DerivRtyFine */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* Gather4C */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* Gather4Po */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* Gather4PoC */
  { 6u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* Rcp */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* F32toF16 */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* F16toF32 */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* UAddc */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* USubb */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* CountBits */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* FirstBitHi */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32    },
  }} },
  /* FirstBitLo */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* FirstBitShi */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32    },
  }} },
  /* UBfe */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32    },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32    },
  }} },
  /* IBfe */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eI32    },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32    },
  }} },
  /* Bfi */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* BfRev */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* Swapc */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown },
    { OperandKind::eSrcReg, ir::ScalarType::eBool    },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown },
  }} },
  /* DclStream */
  { 1u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown },
  }} },
  /* DclFunctionBody */
  { },
  /* DclFunctionTable */
  { },
  /* DclInterface */
  { },
  /* DclInputControlPointCount */
  { 0u },
  /* DclOutputControlPointCount */
  { 0u },
  /* DclTessDomain */
  { 0u },
  /* DclTessPartitioning */
  { 0u },
  /* DclTessOutputPrimitive */
  { 0u },
  /* DclHsMaxTessFactor */
  { 1u, {{
    { OperandKind::eImm32, ir::ScalarType::eF32 },
  }} },
  /* DclHsForkPhaseInstanceCount */
  { 1u, {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclHsJoinPhaseInstanceCount */
  { 1u, {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclThreadGroup */
  { 3u, {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
    { OperandKind::eImm32, ir::ScalarType::eU32 },
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* DclUavTyped */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUav },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclUavRaw */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUav },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclUavStructured */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUav },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclThreadGroupSharedMemoryRaw */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclThreadGroupSharedMemoryStructured */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
    { OperandKind::eImm32,  ir::ScalarType::eU32      },
  }} },
  /* DclResourceRaw */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* DclResourceStructured */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eSrv },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
    { OperandKind::eImm32,  ir::ScalarType::eU32 },
  }} },
  /* LdUavTyped */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eUav      },
  }} },
  /* StoreUavTyped */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUav      },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* LdRaw */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* StoreRaw */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* LdStructured */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* StoreStructured */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* AtomicAnd */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* AtomicOr */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* AtomicXor */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* AtomicCmpStore */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* AtomicIAdd */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* AtomicIMax */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* AtomicIMin */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* AtomicUMax */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* AtomicUMin */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicAlloc */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eUav },
  }} },
  /* ImmAtomicConsume */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eDstReg, ir::ScalarType::eUav },
  }} },
  /* ImmAtomicIAdd */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicAnd */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicOr */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicXor */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicExch */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicCmpExch */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicIMax */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicIMin */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicUMax */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* ImmAtomicUMin */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* Sync */
  { 0u },
  /* DAdd */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DMax */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DMin */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DMul */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DEq */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DGe */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DLt */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DNe */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DMov */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DMovc */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eBool },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
    { OperandKind::eSrcReg, ir::ScalarType::eF64  },
  }} },
  /* DtoF */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* FtoD */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* EvalSnapped */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* EvalSampleIndex */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32    },
    { OperandKind::eSrcReg, ir::ScalarType::eF32    },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32 },
  }} },
  /* EvalCentroid */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF32 },
  }} },
  /* DclGsInstanceCount */
  { 1u, {{
    { OperandKind::eImm32, ir::ScalarType::eU32 },
  }} },
  /* Abort */
  { },
  /* DebugBreak */
  { },
  /* ReservedBegin11_1 */
  { },
  /* DDiv */
  { 3u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DFma */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DRcp */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* Msad */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* DtoI */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eI32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* DtoU */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eU32 },
    { OperandKind::eSrcReg, ir::ScalarType::eF64 },
  }} },
  /* ItoD */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eI32 },
  }} },
  /* UtoD */
  { 2u, {{
    { OperandKind::eDstReg, ir::ScalarType::eF64 },
    { OperandKind::eSrcReg, ir::ScalarType::eU32 },
  }} },
  /* ReservedBegin11_2 */
  { },
  /* Gather4S */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* Gather4CS */
  { 6u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* Gather4PoS */
  { 6u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
  }} },
  /* Gather4PoCS */
  { 7u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* LdS */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* LdMsS */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
  }} },
  /* LdUavTypedS */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eUav      },
  }} },
  /* LdRawS */
  { 4u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eI32      },
    { OperandKind::eSrcReg, ir::ScalarType::eU32      },
  }} },
  /* LdStructuredS */
  { 5u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eAnyI32   },
    { OperandKind::eSrcReg, ir::ScalarType::eUnknown  },
  }} },
  /* SampleLS */
  { 6u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleClzS */
  { 6u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleClampS */
  { 6u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleBClampS */
  { 7u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* SampleDClampS */
  { 8u, {{
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
  { 7u, {{
    { OperandKind::eDstReg, ir::ScalarType::eUnknown  },
    { OperandKind::eDstReg, ir::ScalarType::eU32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eSrv      },
    { OperandKind::eSrcReg, ir::ScalarType::eSampler  },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
    { OperandKind::eSrcReg, ir::ScalarType::eF32      },
  }} },
  /* CheckAccessFullyMapped */
  { 2u, {{
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

      switch (kind) {
        case ExtendedOpcodeType::eSampleControls:
          m_sampleControls = SampleControlToken(dword >> 6u);
          break;

        case ExtendedOpcodeType::eResourceDim:
          m_resourceDim = ResourceDimToken(dword >> 6u);
          break;

        case ExtendedOpcodeType::eResourceReturnType:
          m_resourceType = ResourceReturnToken(dword >> 6u);
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

  /* Table of extended tokens */
  std::array<std::pair<ExtendedOpcodeType, uint32_t>, 3u> extTokens = {{
    { ExtendedOpcodeType::eSampleControls,      uint32_t(m_sampleControls)  },
    { ExtendedOpcodeType::eResourceDim,         uint32_t(m_resourceDim)     },
    { ExtendedOpcodeType::eResourceReturnType,  uint32_t(m_resourceType)    },
  }};

  /* Emit extended tokens */
  for (const auto& e : extTokens) {
    if (e.second) {
      tokens.back() |= ExtendedTokenBit;
      tokens.push_back(uint32_t(e.first) | (e.second << 6u));
    }
  }

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


ExtendedOpcodeType OpToken::extractExtendedOpcodeType(uint32_t token) {
  return ExtendedOpcodeType(util::bextract(token, 0u, 6u));
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

    return Swizzle();
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

  for (uint32_t i = 0u; i < layout.operandCount; i++) {
    Operand operand(tokenReader, layout.operands[i], *this);

    if (!operand) {
      resetOnError();
      return;
    }

    addOperand(operand);
  }

  /* There shouldn't be any bytes left in the reader */
  if (!m_token.isCustomData() && tokenReader.getRemaining())
    Logger::err("Instruction ", token.getOpCode(), " has unhandled operands.");
}


uint32_t Instruction::addOperand(const Operand& operand) {
  uint8_t index = uint8_t(m_operands.size());
  m_operands.push_back(operand);

  switch (operand.getInfo().kind) {
    case OperandKind::eSrcReg: m_srcOperands.at(m_numSrcOperands++) = index; break;
    case OperandKind::eDstReg: m_dstOperands.at(m_numDstOperands++) = index; break;
    case OperandKind::eImm32:  m_immOperands.at(m_numImmOperands++) = index; break;
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
        result.operandCount -= 2u;
        break;

      case OpCode::eDclSampler:
      case OpCode::eDclResource:
      case OpCode::eDclResourceRaw:
      case OpCode::eDclResourceStructured:
      case OpCode::eDclUavTyped:
      case OpCode::eDclUavRaw:
      case OpCode::eDclUavStructured:
        result.operandCount -= 1u;
        break;

      default:
        break;
    }
  }

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

  for (uint32_t i = 0u; i < layout.operandCount; i++) {
    const auto* operand = [&] () -> const Operand* {
      switch (layout.operands[i].kind) {
        case OperandKind::eNone:
        case OperandKind::eIndex:  break;
        case OperandKind::eDstReg: return nDst < getDstCount() ? &getDst(nDst++) : nullptr;
        case OperandKind::eSrcReg: return nSrc < getSrcCount() ? &getSrc(nSrc++) : nullptr;
        case OperandKind::eImm32:  return nImm < getImmCount() ? &getImm(nImm++) : nullptr;
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
