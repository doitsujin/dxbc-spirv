# dxbc-spirv

An SSA-based compiler for Direct3D Shader Models 5.1 and older.

This implements a custom IR, which can be trivially lowered to SPIR-V or translated to other, similar IRs.
Please refer to the [documentation](https://github.com/doitsujin/dxbc-spirv/blob/main/ir/ir.md) for an
instruction reference.

## Feature support

### DXBC
- Minimum precision is supported, with both `min10float` and `min16float` being lowered to
a 16-bit or 32-bit floating point type depending on the provided compile options.
Min-precision integer types are lowered to 16 or 32-bit integers accordingly.

- Shader Model 5.1 resource declarations and dynamic descriptor indexing are supported.

- Shader Model 5.0 interfaces and class linkage are **not** supported. Other that one
  tech demo, there are no known real-world uses of this feature.

### SPIR-V
- The built-in SPIR-V lowering targets SPIR-V 1.6 with the Vulkan memory model, and
  optionally `SPV_KHR_float_controls2` depending on device capabilities. Backwards
  compatibility to older SPIR-V versions is not a priority and not planned.

## Building

```
meson setup builddir

# To enable building command line tools
meson configure -Denable_tools=true

cd builddir
ninja
```

## Tools

### Disassembler

This project provides a custom DXBC disassembler for debugging purposes. Its output does
**not** match that of d3dcompiler in that it does not parse resource metadata or shader
statistics, and some instruction or enum names may differ, however the overall structure
and instruction format are similar.

Usage:

```
./dxbc_disasm shader.dxbc
```

### Compiler

To compile a standalone DXBC shader to a SPIR-V binary, run:

```
./dxbc_compiler shader.dxbc --spv shader.spv
```

Please note that the resulting SPIR-V binary is likely not useful as-is due to the way
resources and shader I/O are lowered. This is primarily intended for debug and validation
purposes, applications should instead use dxbc-spirv as a library and implement custom
lowering passes.

To view the internal IR for a shader, run:

```
./dxbc_compiler shader.dxbc --ir-asm | less -R
```
