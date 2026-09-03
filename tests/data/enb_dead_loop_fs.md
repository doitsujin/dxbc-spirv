# `enb_dead_loop_fs.dxbc`

The pixel shader that makes `CleanupControlFlowPass` crash. Captured from a live
Skyrim SE + ENBSeries using the RudyENB preset, run with `DXVK_SHADER_DUMP_PATH`
(1 out of 11,750 dumped shaders is the only one that fails).
That is how I came across this potential bug.

I dumped the dxbc and tried compiling it:

```
build/tools/dxbc_compiler --spv /dev/null tests/data/enb_dead_loop_fs.dxbc
# dxbc_compiler: ../ir/passes/ir_pass_cfg_cleanup.cpp:721:
#   Assertion `m_builder.getOp(block).getOpCode() == OpCode::eLabel' failed.
```

... which is what I was seeing in my Skyrim crash log.

## The pixel shader example

Disassembled from
`build/tools/dxbc_disasm tests/data/enb_dead_loop_fs.dxbc`

```
    15: mul r0.xy, v1.xyxx, cb12[43].xyxx
    16: max r0.xy, r0.xyxx, (0,0,0,0)
    17: mov r1.x, cb12[44].z
    18: mov r1.y, cb12[43].y
    19: min r0.xy, r0.xyxx, r1.xyxx
    20: sample      r0.z,   r0.xyxx, t2.xzyw, s2
    21: sample_l    r2.xyz, r0.xyxx, t1.xyzw, s1, 0.0

    22: mov r0.w, 1.000000f    ; <-- r0.w is set to the literal constant 1.0
    23: if_nz r0.w             ; <-- ...and immediately branched on. Always true.
    24:   mov o0.xyz, r2.xyzx  ;     output the unmodified colour
    25:   ret                  ;     and return. This return ALWAYS happens.
    26: endif

    ;; Everything from here down is unreachable. The compiler emitted it anyway.

    27: sample_l r0.x, r0.xyxx, t0.xyzw, s0, 0.0
    28: mul r3.xyz, r2.xyzx, (0.784728f, 0.669086f, 0.560479f, 0)
    29: mul r4.xyz, cb2[1].xxyx, (0.078125f, 0.138890f, 0.100000f, 0)
    30: div r0.yw, r4.xxxy, r0.xxxx
    31: mov r4.xyw, r3.xyxz
    32: mov r1.z, 1                    ; loop counter i = 1
    33: loop                           ; <-- a STRUCTURED LOOP inside the dead region
    34:   uge r1.w, r1.z, 11           ;     i >= 11 ?
    35:   breakc_nz r1.w               ;     if so, leave the loop
    36:   mad r5.xy, icb[r1.z].wwww, r0.ywyy, v1.xyxx   ; no idea what this does
    48:   iadd r1.z, r1.z, 1           ;     i++
    49: endloop
    50: dp3 r0.x, r4.xywx, (0.3f, 0.59f, 0.11f, 0)       ; idk, nice colors
    58: mad o0.xyz, r0.zzzz, r0.xywx, r2.xyzx
    59: ret
```

`mov r0.w, 1.0` followed by `if_nz r0.w` looks like a disabled feature toggle. ENB
generates its shaders at runtime from `.fx` sources and the user's preset, and "off"
leaves behind `if (1) { return original_colour; }`. But the compiler did not fold it away,
so the whole loop survives into the bytecode as dead code, which exposes the bug.

## The bug.

Here is my best guess at what is going on. Take it with a grain of salt.

According to my debugging, the problem is the *shape* of the CFG (an unreachable region
that contains a structured loop), which causes an invariant violation during cleanup
(`ir/ir_legalize.cpp:69` -- nice!)

1. After SSA construction and the arithmetic pass, `r0.w` is an IR constant, so
   `CleanupControlFlowPass::handleBranchConditional` folds the branch
   (`ir_pass_cfg_cleanup.cpp:292`). This is the cleanup pass run from the optimisation
   fixpoint loop at `ir_legalize.cpp:69`.
2. The loop header is now reachable only from its own continue block, where
   `isBlockReachable` deliberately does not count that back-edge (`:681-688`).
   The header is just removed.
3. `removeBlock` rewrites *phi* uses of the removed block (`:556`) and scrubs the
   *work list* (`:553`), but not the *branch operands*. The continue block's back edge
   now points at a def that no longer exists.
4. The continue block is no longer recognised as a continue block now, because
   `isContinueBlock` looks for a loop label naming it (`:643`), and that label was just
   deleted. So it is removed in turn.
5. `removeBlockTerminator` collects the targets of its terminator, including the
   back-edge to the already-deleted header, and calls `isBlockUsed` on it (`:612`).
   `Builder::removeNode` leaves a removed def as a dummy default-constructed `Op`
   (`ir_builder.cpp:327`), so this does not directly cause an error. Instead it reaches
   `getConstructForBlock`, whose `eLabel` assert fires.

Observed stack:

```
#5  getConstructForBlock    ir_pass_cfg_cleanup.cpp:721   <-- assert
#7  isBlockReachable        ir_pass_cfg_cleanup.cpp:668
#8  isBlockUsed             ir_pass_cfg_cleanup.cpp:704
#9  removeBlockTerminator   ir_pass_cfg_cleanup.cpp:612
#10 handleLabel             (removeBlock inlined)
#11 run                     ir_pass_cfg_cleanup.cpp:32
#13 legalizeIr              ir_legalize.cpp:69
```

`tests/ir/test_ir_cfg_cleanup.cpp` reproduces the same shape directly on the IR builder,
without needing the dxbc file.

## A proposed fix

The actual problem is the order of operations when removing the dead code. The pass
takes the iterative approach, it (mostly) seems to follow the control flow. Even if it
was more clever about it, a CFG with no topological ordering (like this one) would break
this approach if not very carefully implemented.

So my proposition is to break the loop by removing the back edge when processing the loop
header block.

My implementation of this (see `breakLoopBackEdge` at
`ir/passes/ir_pass_cfg_cleanup.cpp:586`) fixes both the test and the shader above.
It replaces the terminator of the continue block of the loop with `OpCode::eUnreachable`.
Across all 11,750 dumped Skyrim shaders the SPIR-V comes out byte-identical except for that
one, which now compiles. Every result passes `spirv-val`.

## Notes on the binary file

Dumped from ENBSeries' shader output. ENB compiles its `.fx` sources at runtime, and this
is what it handed to DXVK. It is ENB-generated compiled bytecode, so I am unsure whether
or not I can include it in the repo. That's why I left it out.
I can mail it to you for testing if you want.

The artificial test in `tests/ir/test_ir_cfg_cleanup.cpp` reproduces the same CFG
structure.

Update 26-09-03: Added the file
