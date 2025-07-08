# DXBC-IR

## API Fundamentals
The `ir::Builder` class is used to add, modify and traverse instructions.

Each instruction is stored in a `ir::Op` with the following properties:
- A unique `SsaDef` with a non-zero ID. This is assigned automatically by
  the builder when the instruction is added.
- The opcode, `ir::OpCode`.
- Instruction flags, `ir::OpFlags`, which defines whether the instruction
  is precise or explicitly non-uniform.
- The return type of the instruction. Note that even instructions that do
  not return a value are still assigned a valid `SsaDef`.
- An array of operands, which may either be literals (up to 64 bits),
  or references to another instruction via its `SsaDef`.

### Types
Instruction return types have the following properties, as a top-down structure:
- Number of array dimensions. If 0, the type is not an array type.
- Number of array elements in each dimension. If the element count in the last valid dimension is 0, the array is unnbounded.
- Number of structure members. If the type is an array, each element will be one instance of the given structure. If the struct
  member count is 1, the element type is a scalar or vector. If the member count is 0, the type is considered `void` and the
  instruction does not return a value. In this case, the array dimension count must also be 0.
- Each struct member is represented as a scalar type (`ir::Type`) with a vector size. In the C API, the vector size is biased by 1,
  so that a vector size of 0 unambiguously refers to a scalar and a vector size of 1 refers to a `vec2`, etc. The maximum
  vector size is a `vec4`.

The `ir::Type::Unknown` scalar type is used when, at the time of recording an instruction, the exact type is not known. This is
often the case with load-store pairs. Unknown types will generally be resolved after SSA construction, and will be promoted to
`u32` in case the type is ambiguous.

## IR fundamentals
The design goal for this IR to be relatively easy to target for Direct3D Shader Models 1.0 through 5.1, while providing a number of transforms to inject
type info, translate scoped control flow to SPIR-V-like structured control flow constructs, and transition from a temporary register model to SSA form.
Translation to other IRs, such as SPIR-V, is intended to be simple, while retaining sufficient high-level information to write custom passes e.g. to map
resource bindings.

### Instruction layout
Declarative instructions occur before any actual code:
- `EntryPoint`
- `Constant*`
- `Set*`
- `Dcl*`
- `Debug*`

Forward-references are generally not allowed in order to ease parsing, this means that the definition for
any given argument ID will be known by the time it is consumed in an instruction. There are exceptions:
- `Debug*` instructions may target any instruction.
- `Branch*` and `Switch` instructions may forward-reference a block.
- `Phi` may contain forward-references to blocks as well as instructions.

Literal tokens are only allowed as the last set of operands to an instruction, in order to ease instruction
processing.

In the instruction listings below, references to other instructions are prefixed with a `%`,
whereas literals do not use a prefix.

### Debug instructions
| `ir::OpCode`         | Return type | Arguments...   |         |
|----------------------|-------------|----------------|---------|
| `DebugName`          | `void`      | `%instruction` | Literal |

It is not meaningful to set debug names for mode setting instructions (see below).
Otherwise, any instruction can be a valid target. The debug name may require multiple
operands depending on its size. Literal strings are null-terminated.

### Constant declarations
| `ir::OpCode`         | Return type | Arguments...                    |
|----------------------|-------------|---------------------------------|
| `Constant`           | any         | Literals for each member        |

Constant literals are flattened according to the type definition:
- For a scalar, one token is used.
- A vector of size `n` is stored as `n` consecutive scalars.
- For a structure, members are stored consecutively.
- For an array, elements are stored consecutively.

### Mode setting instructions

These instructions provide additional information that may affect the execution of the shader.

| `ir::OpCode`             | Return type | Arguments...  |                      |                        |                         |
|--------------------------|-------------|---------------|----------------------|------------------------|-------------------------|
| `SetCsWorkgroupSize`     | `void`      | `%EntryPoint` | `x`                  | `y`                    | `z`                     |
| `SetGsInstances`         | `void`      | `%EntryPoint` | `n`                  |                        |                         |
| `SetGsInputPrimitive`    | `void`      | `%EntryPoint` | `ir::PrimitiveType`  |                        |                         |
| `SetGsOutputVertices`    | `void`      | `%EntryPoint` | `n`                  |                        |                         |
| `SetGsOutputPrimitive`   | `void`      | `%EntryPoint` | `ir::PrimitiveType`  | `stream`               |                         |
| `SetPsEarlyFragmentTest` | `void`      | `%EntryPoint` |                      |                        |                         |
| `SetPsDepthGreaterEqual` | `void`      | `%EntryPoint` |                      |                        |                         |
| `SetPsDepthLessEqual`    | `void`      | `%EntryPoint` |                      |                        |                         |
| `SetTessPrimitive`       | `void`      | `%EntryPoint` | `ir::PrimitiveType`  | `ir::TessWindingOrder` | `ir::TessPartitioning`  |
| `SetTessDomain`          | `void`      | `%EntryPoint` | `ir::PrimitiveType`  |                        |                         |

All operands bar the `%EntryPoint` operand are literal constants.

### Type conversion instructions
| `ir::OpCode`         | Return type      | Argument         |
|----------------------|------------------|------------------|
| `ConvertFtoF`        | any              | `%value`         |
| `ConvertFtoI`        | any              | `%value`         |
| `ConvertItoF`        | any              | `%value`         |
| `ConvertItoI`        | any              | `%value`         |
| `Cast`               | any              | `%value`         |
| `ConsumeAs`          | any              | `%value`         |

If the result type and source type are the same, the conversion operation is a no-op and will be removed by a lowering pass.

Semantics are as follows:
- `ConvertFtoF` converts between float types of a different width. If the result type is smaller than the
  source type, round-to-zero semantics are required but no specific denorm behaviour is specified.
- `ConvertFToI` is a saturating conversion from a float type to a signed or unsigned integer, with round-to-zero semantics.
- `ConvertIToF` is a value-preserving conversion from a signed or unsigned integer to any float type with round-even semantics.
- `ConvertItoI` converts between integer types of different size. If the result type is larger than the source and the source
  is signed, it will be sign-extended, otherwise it will be sign-extended. If the result type is smaller, excess bits are discarded.
  If the two types only differ in signedness, this instruction is identical in behaviour to `Cast` and will be lowered to it.
- `Cast` is a bit-pattern preserving cast between different types that must have the same bit size. Vector types are allowed.

`ConsumeAs` is a helper instruction that is used to resolve and back-propagate expression types in an untyped IR, and will be
lowered to `Cast` and `Convert` instructions as necessary. It is the only instruction that is allowed to take a source operand
with a scalar type of `ir::Type::Unknown`. In the final shader binary, no `ConsumeAs` instructions shall remain.

### Variable declaration instructions
| `ir::OpCode`         | Return type      | Arguments...     |                  |           |                |                     |                 |
|----------------------|------------------|------------------|------------------|-----------|----------------|---------------------|-----------------|
| `DclInput`           | see below        | `%EntryPoint`    | location         | component | `ir::InterpolationModes` |           |                 |
| `DclInputBuiltIn`    | any              | `%EntryPoint`    | `ir::BuiltIn`    | `ir::InterpolationModes` | |                     |                 |
| `DclOutput`          | see below        | `%EntryPoint`    | location         | component | stream (GS)    |                     |                 |
| `DclOutputBuiltIn`   | any              | `%EntryPoint`    | `ir::BuiltIn`    | stream (GS) |              |                     |                 |
| `DclSpecConstant`    | any              | `%EntryPoint`    | spec id          | default   |                |                     |                 |
| `DclPushData`        | any              | `%EntryPoint`    | push data offset | `ir::ShaderStageMask` |    |                     |                 |
| `DclSampler`         | any              | `%EntryPoint`    | space            | register  | count          |                     |                 |
| `DclCbv`             | any              | `%EntryPoint`    | space            | register  | count          |                     |                 |
| `DclSrv`             | any              | `%EntryPoint`    | space            | register  | count          | `ir::ResourceKind`  |                 |
| `DclUav`             | any              | `%EntryPoint`    | space            | register  | count          | `ir::ResourceKind`  | `ir::UavFlags`  |
| `DclUavCounter`      | `u32`            | `%EntryPoint`    | `%DclUav` uav    |           |                |                     |                 |
| `DclLds`             | any              | `%EntryPoint`    |                  |           |                |                     |                 |
| `DclScratch`         | any              | `%EntryPoint`    |                  |           |                |                     |                 |
| `DclTmp`             | any              | `%EntryPoint`    |                  |           |                |                     |                 |
| `DclParam`           | any              |                  |                  |           |                |                     |                 |

The `count` parameter for `DclSampler`, `DclSrv`, `DclCbv` and `DclUav` instructions is a literal constant declareing the size of the
descriptor array, If the size is `0`, the array is unbounded. If `1`, the declaration consists of only a single descriptor, and the
address operand of any `DescriptorLoad` instruction accessing this descriptor will reference a constant `0`.

For `DclSrv` and `DclUav`, if the resource is is `ir::ResourceKind::Structured`, the return type is a two-dimensional array of a scalar type,
where the inner array represents one structure.

The `DclUavCounter` references the `DclUav` instruction to declare a UAV counter for. Any UAV that is not referenced by such an instruction
is assumed to not have a UAV counter.

If the type of a `DclSpecConstant` is an aggregate type (i.e. not a scalar), it will consume multiple spec IDs, one per flattened scalar,
starting with the declared specialization constant ID.

`DclPushData` and `DclSpecConstant` have no restriction on how many times they can occur in a shader. As an example, it is possible that
there is only one push data block consisting of a struct, or that the same struct is unrolled into one `DclPushData` instruction per member.

`DclScratch` is used to declare local arrays and can only be accessed via `ScratchLoad` and `ScratchStore` instructions.

`Dcl*` instructions themselves cannot be used as SSA values directly. Only specialized load, store and atomic instructions
can use them, as well as specialized resource query and input interpolation instructions.

`DclParam` instructions are not necessarily unique to any given function. Their purpose is merely to provide type information
as well as optionally having a debug name attached to them, since there is no other way to encode type information.

The `ir::InterpolationMode` parameter for input declarations is always `None` outside of pixel shaders. Inside pixel shaders, it is
implicitly set to `flat` for integer types.

The return type of `DclInput` and `DclOutput` can be a 32-bit scalar or vector type, or a sized array of scalars or vectors.
In pixel shaders, `DclInput` instructions with an integer type must set the `Flat` interpolation mode.

For any `DclOutput` instruction in hull shaders, or corresponding `DclInput` instructions in domain shaders, control point data will
always have a sized array type, whereas patch constants use a scalar or vector type. The exception here is that tessellation factor
built-ins are also exposed as an array, but they are inherently always patch constants.

Likewise, `DclInput*` instructions for per-vertex inputs in geometry shaders and hull shaders will use a sized array type.

For `DclOutput*` instructions, the `stream` parameter is only defined in geometry shaders.

#### Resource return types
The return type for a typed resource declaration (i.e. image or typed buffer) is a scalar or vector of the sampled type.

For raw buffers, the type is `u32[]`, but any `BufferLoad`, `BufferStore` and `BufferAtomic` operations performed
on it may use arbitrary scalar or vector types.

For structured buffers, the type is an unsized array of an arbitrary type. Typically, this will be of the form `u32[n][]`,
where `n` corresponds to the number of dwords in a structure.

#### UAV flags
- `Coherent`: UAV is globally coherent, writes must be made visible to `Global` scope.
- `ReadOnly`: UAV is only accessed for reading.
- `WriteOnly`: UAV is only accessed for writing.
- `RasterizerOrdered`: UAV is only accessed between `RovScopedLockBegin` and `RovScopedLockEnd`.
- `FixedFormat`: The resource format matches the declared type exactly.
    Must be set for typed buffer and image UAVs accessed with atomics.

### Semantic declaration
| `ir::OpCode`         | Return type      | Arguments...     |                  |           |
|----------------------|------------------|------------------|------------------|-----------|
| `Semantic`           | `%void`          | `%Dcl*`          | index            | name      |

Semantic operations can target any input or output declaration, including built-ins. The index
and name parameters are a literal integer and literal string, respectively.

### Composite instructions
Shader I/O and certain resource access operations may use arrays, structs or vectors, which can be accessed via the following instructions:

| `ir::OpCode`         | Return type      | Arguments...           |                           |          |
|----------------------|------------------|------------------------|---------------------------|----------|
| `CompositeInsert `   | any              | `%composite`           | `%address` into composite | `%value` |
| `CompositeExtract`   | any              | `%composite`           | `%address` into composite |          |
| `CompositeConstruct` | any composite    | List of `%members`     |                           |          |

For `CompositeConstruct`, the constituents must match the composite member type exactly. It is not allowed to pass flattened scalar values.

The `%address` parameter is a vector or scalar of an integer type.

### Sparse feedback
Resource access instructions that return sparse feedback will return a struct with two members:
- An unsigned integer containing the sparse feedback value.
- A scalar or vector containing the value retrieved from the resource

The sparse feedback value should only be used with the `CheckSparseAccess` instruction.

| `ir::OpCode`         | Return type | Arguments... |
|----------------------|-------------|--------------|
| `CheckSparseAccess`  | `bool`      | `%feedback`  |

### Load/Store instructions
| `ir::OpCode`         | Return type  | Arguments...       |                                |          |
|----------------------|--------------|--------------------|--------------------------------|----------|
| `ParamLoad`          | any          | `%Function`        | `%DclParam` ...                |          |
| `TmpLoad`            | any          | `%DclTmp` variable |                                |          |
| `TmpStore`           | `void`       | `%DclTmp` variable | `%value`                       |          |
| `ScratchLoad`        | any          | `%DclScratch` var. | `%address` into scratch array  |          |
| `ScratchStore`       | `void`       | `%DclScratch` var. | `%address` into scratch array  | `%value` |
| `LdsLoad`            | any          | `%DclLds` variable | `%address` into LDS            |          |
| `LdsStore`           | `void`       | `%DclLds` variable | `%address` into LDS            | `%value` |
| `PushDataLoad`       | any          | `%DclPushData`     | `%address` into push data      |          |
| `SpecConstantLoad`   | any          | `%DclSpecConstant` | `%address` into spec constant  |          |
| `InputLoad`          | any          | `%DclInput*`       | `%address` into input type     |          |
| `OutputLoad`         | any          | `%DclOutput*`      | `%address` into output type    |          |
| `OutputStore`        | any          | `%DclOutput*`      | `%address` into output type    | `%value` |
| `DescriptorLoad`     | descriptor   | `%Dcl*` variable   | `%index` into descriptor array |          |
| `BufferLoad`         | any          | `%descriptor`      | `%address` into CBV / SRV / UAV|          |
| `BufferStore`        | any          | `%descriptor` (UAV)| `%address` into UAV            | `%value` |
| `BufferQuerySize`    | `u32`        | `%descriptor`      |                                |          |
| `MemoryLoad`         | any          | `%Pointer`         | `%address` into pointee type   |          |
| `MemoryStore`        | any          | `%Pointer`         | `%address` into pointee type   | `%value` |

Note that `SrvLoad`, `UavLoad` and `UavStore` instructions can only be used on raw, structured or typed buffer instructions. Image
resources can only be accessed via image instructions.

The `%address` parameter for any of the given instructions can be `null` if the referenced objects should be read or written as a whole,
or a scalar or vector type that traverses the referenced type, with array dimensions first (outer to inner, may be dynamic), then the
struct member (must point to a constant), and then the vector component index (must be a constant). This is similar to SPIR-V access chains.

For `ParamLoad`, the `%Function` parameter *must* point to the function that the instruction is used in, and the `%DclParam`
parameter *must* be one of the function parameters of that function. Parameters can only be loaded as a whole.

Note that descriptor operand used in `BufferLoad` and `BufferStore` instructions, as well as all other resource access instructions,
*must* be a `DescriptorLoad` instruction. If the descriptor for `Buffer*` instructions is a UAV or SRV descriptor, it may be a raw or
structured buffer, or a typed buffer. In the typed buffer case, the given address is the index of the typed element within the buffer.

A `BufferLoad` instruction *may* return a scalar, vector, or sparse feedback struct.

The value returned by `BufferQuerySize` is the structure or element count, even for raw buffers. This differs from D3D semantics,
where the corresponding instruction would return the total byte size instead.

All `TmpLoad` and `TmpStore` instructions will be eliminated during SSA construction.

If the return type for any given `BufferLoad` or `MemoryLoad` instruction is a vector, even though the source type after fully traversing
`%address` is scalar, then multiple consecutive scalars will be loaded at once. The same goes for `*Store` instructions where `%value` is
a vector, but the final destination type is scalar. This can allow for more efficient memory access patterns in some cases.

### Atomic instructions
| `ir::OpCode`         | Return type      | Arguments...      |                              |                |                |                |
|----------------------|------------------|-------------------|------------------------------|----------------|----------------|----------------|
| `LdsAtomic`          | `void` or scalar | `%DclLds`         | `%address` into LDS type     | `%operands`    | `ir::AtomicOp` |                |
| `BufferAtomic`       | `void` or scalar | `%uav` descriptor | `%address` into UAV type     | `%operands`    | `ir::AtomicOp` |                |
| `ImageAtomic`        | `void` or scalar | `%uav` descriptor | `%layer`                     | `%coord`       | `%operands`    | `ir::AtomicOp` |
| `CounterAtomic`      | `void` or scalar | `%uav` counter    | `%operands`                  | `ir::AtomicOp` |                |                |
| `MemoryAtomic`       | `void` or scalar | `%Pointer`        | `%address` into pointee type | `%operands`    | `ir::AtomicOp` |                |

The `ir::AtomicOp` parameter is a literal enum value, thus the last parameter.

The `%layer` parameter is `null` for non-arrayed image types.

For `ir::AtomicOp::CompareExchange`, the first operand contains the desired value to compare
to, and the second operand will be the value to store if the comparison succeeds. The operation
returns the value at the given memory location before any exchange can take place.

### Image instructions
| `ir::OpCode`         | Return type      | Arguments...  |                   |             |              |              |             |              |          |                  |
|----------------------|------------------|---------------|-------------------|-------------|--------------|--------------|-------------|--------------|----------|------------------|
| `ImageLoad`          | any              | `%descriptor` | `%mip`            | `%layer`    | `%coord`     | `%sample`    | `%offset`   |              |          |                  |
| `ImageStore`         | `void`           | `%descriptor` | `%layer`          | `%coord`    | `%value`     |              |             |              |          |                  |
| `ImageAtomic`        | `void` or scalar | `%uav` descriptor | `%layer`      | `%coord`   | `%operands`   | `ir::AtomicOp` |           |              |          |                  |
| `ImageQuerySize`     | struct           | `%descriptor` | `%mip`            |             |              |              |             |              |          |                  |
| `ImageQueryMips`     | `u32`            | `%descriptor` |                   |             |              |              |             |              |          |                  |
| `ImageQuerySamples`  | `u32`            | `%descriptor` |                   |             |              |              |             |              |          |                  |
| `ImageSample`        | any              | `%descriptor` | `%sampler`        | `%coord`    | `%offset`    | `%lod_index` | `%lod_bias` | `%lod_clamp` | `%deriv` | `%depth_compare` |
| `ImageGather`        | any              | `%descriptor` | `%sampler`        | `%coord`    | `%offset`    | `%depth_compare` | `component` |          |          |                  |
| `ImageComputeLod`    | `vec2<f32>`      | `%descriptor` | `%sampler`        | `%coord`    |              |              |             |              |          |                  |

The `ImageQuerySize` instruction returns a struct with the following members:
- A scalar or vector containing the size of the queried mip level, in pixels
- The array layer count, which will always be 1 for non-layered images

The `ImageLoad`, `ImageSample` and `ImageGather` instructions may return a sparse feedback struct.

For `ImageLoad`, `ImageSample` and `ImageGather`, all operands after `%coord` may be `null`. The `component`
operand for `ImageGather` is a literal and will thus never be `null`.

For `ImageSample`, the `offset` parameter, if not `null`, is always a constant vector. For `ImageGather`, it may not be constant.

For `ImageSample`, the `deriv` parameter, if not `null`, is a struct containing the `x` and `y` derivative vectors, in that order.

### Pointer instructions
Raw pointers can be used to access memory via the `MemoryLoad`, `MemoryStore` and `MemoryAtomic` instructions.

| `ir::OpCode`         | Return type      | Arguments          |
|----------------------|------------------|--------------------|
| `Pointer`            | any              | `%address` (`u64`) |
| `PointerAddress`     | `u64`            | `%Pointer`         |

The return type of the `Pointer` instruction is the pointee type, and may be any scalar, vector, struct or
array type.

### Function declarations

| `ir::OpCode`         | Return type | Arguments...                                        |                   |
|----------------------|-------------|-----------------------------------------------------|-------------------|
| `Function`           | any         | List of parameter `%DclParam` references            |                   |
| `FunctionEnd`        | `void`      | None                                                |                   |
| `FunctionCall`       | any         | `%Function` function to call                        | `%params` list    |
| `EntryPoint`         | `void`      | `%Functions`...                                     | `ir::ShaderStage` |
| `Return`             | any         | `%value` (may be null if the function returns void) |                   |

Only one `EntryPoint` instruction is allowed per shader. If an `EntryPoint` instruction is the target of a `DebugName`
instruction, that name should be considered the name of the shader module when lowering to the final shader binary,
rather than the name of the function itself.

For hull shaders, `EntryPoint` takes two functions arguments: A control point function, which is only allowed to write
control point outputs, and a patch constant function, which may read control point outputs and may write or read patch
constant outputs. The patch constant function will include barriers as necessary.

The `FunctionEnd` instruction must only occur outside of a block, see below.

### Structured control flow instructions
Structured control flow largely matches SPIR-V and can be translated directly, with the exception that there are
no dedicated `Selection` instructions to define constructs. Instead, this is done in a `Label` instruction.

| `ir::OpCode`          | Return type | Arguments...          |                      |                      |                      |                      |
|-----------------------|-------------|-----------------------|----------------------|----------------------|----------------------|----------------------|
| `Label`               | `void`      | `%Label` args...      | `ir::Construct`      |                      |                      |
| `Branch`              | `void`      | `%Label` target block |                      |                      |                      |                      |
| `BranchConditional`   | `void`      | `%cond`               | `%Label` if true     | `%Label` if false    |                      |                      |
| `Switch`              | `void`      | `%value` switch val   | `%Label` default     | `%value` case value  | `%Label` case block  |
| `Unreachable`         | `void`      |                       |                      |                      |                      |                      |
| `Phi`                 | any         | `%Label` source block | `%value`             |                      |                      |                      |

A label can define any of the given constructs:
- `ir::Construct::None`: Basic block that does not declare any special constructs.
- `ir::Construct::StructuredSelection`: If/Switch block. Takes one additional argument, the merge block.
- `ir::Construct::StructuredLoop`: Loop block. Takes two arguments, the merge block and continue target.
  Only `StructuredLoop` blocks are allowed to be targeted by back edges in the control flow graph.

Note that the `ir::Construct` parameter that determines the label type is last because it is a literal.

While `Label` begins a block, any `Branch*`, `Switch`, `Return` or `Unreachable` instruction will end it.

### Scoped control flow instructions
Scoped control flow is used to simplify translation from the source IR, and must be lowered to structured
control flow instructions before SSA construction and any further processing of the shader.

| `ir::OpCode`          | Return type | Arguments... |
|-----------------------|-------------|--------------|
| `ScopedIf`            | `void`      | `%cond`      |
| `ScopedElse`          | `void`      |              |
| `ScopedEndIf`         | `void`      |              |
| `ScopedLoop`          | `void`      |              |
| `ScopedLoopBreak`     | `void`      |              |
| `ScopedLoopContinue`  | `void`      |              |
| `ScopedEndLoop`       | `void`      |              |
| `ScopedSwitch`        | `void`      | `%value`     |
| `ScopedSwitchCase`    | `void`      | `value`      |
| `ScopedSwitchDefault` | `void`      |              |
| `ScopedSwitchBreak`   | `void`      |              |
| `ScopedEndSwitch`     | `void`      |              |

Note that `ScopedSwitchCase` takes the value as a literal operand that must be of the same
type as the `%value` parameter of the corresponding `ScopedSwitch` instruction.

### Memory and execution barriers

| `ir::OpCode`              | Return type | Arguments...          |                    |                       |
|---------------------------|-------------|-----------------------|--------------------|-----------------------|
| `Barrier`                 | `void`      | `ir::Scope` (exec)    | `ir::Scope` (mem)  | `ir::MemoryTypeMask`  |

If `exec` is `ir::Scope::Thread`, then this is a pure memory barrier. Memory barriers may occur in any
stage, barriers with a wider execution scope are only meaningful in hull and compute shader.

### Geometry shader instructions

| `ir::OpCode`              | Return type | Arguments... |
|---------------------------|-------------|--------------|
| `EmitVertex`              | `void`      | `stream`     |
| `EmitPrimitive`           | `void`      | `stream`     |

### Pixel shader instructions

| `ir::OpCode`              | Return type | Arguments... |                         |
|---------------------------|-------------|--------------|-------------------------|
| `Demote`                  | `void`      | None         |                         |
| `InterpolateAtCentroid`   | any         | `%DclInput`  |                         |
| `InterpolateAtSample`     | any         | `%DclInput`  | `%sample`               |
| `InterpolateAtOffset`     | any         | `%DclInput`  | `%offset` (`vec2<f32>`) |
| `DerivX`                  | any         | `%value`     | `ir::DerivativeMode`    |
| `DerivY`                  | any         | `%value`     | `ir::DerivativeMode`    |
| `RovScopedLockBegin`      | `void`      |              |                         |
| `RovScopedLockEnd`        | `void`      |              |                         |

### Comparison instructions
Component-wise comparisons that return a boolean vector.

| `ir::OpCode`              | Return type | Arguments... |      |
|---------------------------|-------------|--------------|------|
| `FEq`                     | `bool`      | `%a`         | `%b` |
| `FNe`                     | `bool`      | `%a`         | `%b` |
| `FLt`                     | `bool`      | `%a`         | `%b` |
| `FLe`                     | `bool`      | `%a`         | `%b` |
| `FGt`                     | `bool`      | `%a`         | `%b` |
| `FGe`                     | `bool`      | `%a`         | `%b` |
| `FIsNan`                  | `bool`      | `%a`         |      |
| `IEq`                     | `bool`      | `%a`         | `%b` |
| `INe`                     | `bool`      | `%a`         | `%b` |
| `SLt`                     | `bool`      | `%a`         | `%b` |
| `SGe`                     | `bool`      | `%a`         | `%b` |
| `ULt`                     | `bool`      | `%a`         | `%b` |
| `UGe`                     | `bool`      | `%a`         | `%b` |

With the exception of `FNe`, all float comparisons are ordered.

### Logical instructions
Instructions that operate purely on scalar boolean operands.

| `ir::OpCode`              | Return type | Arguments... |      |
|---------------------------|-------------|--------------|------|
| `BAnd`                    | `bool`      | `%a`         | `%b` |
| `BOr`                     | `bool`      | `%a`         | `%b` |
| `BEq`                     | `bool`      | `%a`         | `%b` |
| `BNe`                     | `bool`      | `%a`         | `%b` |
| `BNot`                    | `bool`      | `%a`         |      |

### Conditional instructions
Maps boolean values to values of any other type.

| `ir::OpCode`              | Return type | Arguments... |                  |                   |
|---------------------------|-------------|--------------|------------------|-------------------|
| `Select`                  | any         | `%cond`      | `%value` if true | `%value` if false |

### Float arithmetic instructions
| `ir::OpCode`              | Return type  | Arguments... |         |       |
|---------------------------|--------------|--------------|---------|-------|
| `FAbs`                    | float        | `%value`     |         |       |
| `FNeg`                    | float        | `%value`     |         |       |
| `FAdd`                    | float        | `%a`         | `%b`    |       |
| `FSub`                    | float        | `%a`         | `%b`    |       |
| `FMul`                    | float        | `%a`         | `%b`    |       |
| `FMulLegacy`              | float        | `%a`         | `%b`    |       |
| `FMad`                    | float        | `%a`         | `%b`    | `%c`  |
| `FMadLegacy`              | float        | `%a`         | `%b`    | `%c`  |
| `FDiv`                    | float        | `%a`         | `%b`    |       |
| `FRcp`                    | float        | `%a`         |         |       |
| `FSqrt`                   | float        | `%a`         |         |       |
| `FRsq`                    | float        | `%a`         |         |       |
| `FExp2`                   | `f32`        | `%a`         |         |       |
| `FLog2`                   | `f32`        | `%a`         |         |       |
| `FFract`                  | float        | `%a`         |         |       |
| `FRound`                  | float        | `%a`         | `mode`  |       |
| `FMin`                    | float        | `%a`         | `%b`    |       |
| `FMax`                    | float        | `%a`         | `%b`    |       |
| `FDot`                    | float        | `%a`         | `%b`    |       |
| `FDotLegacy`              | float        | `%a`         | `%b`    |       |
| `FClamp`                  | float        | `%a`         | `%lo`   | `%hi` |
| `FSin`                    | `f32`        | `%a`         |         |       |
| `FCos`                    | `f32`        | `%a`         |         |       |

Note that `FDot*` instructions takes vector arguments and returns a scalar. This instruction
should be lowered to a sequence of `FMul*` and `FMad*` instructions.

The `mode` parameter for `FRound` is a constant enum `ir::RoundMode`.

The `*Legacy` instructions follow D3D9 rules w.r.t. multiplication.

### Bitwise instructions
| `ir::OpCode`              | Return type  | Arguments... |           |           |          |
|---------------------------|--------------|--------------|-----------|-----------|----------|
| `IAnd`                    | integer      | `%a`         | `%b`      |           |          |
| `IOr`                     | integer      | `%a`         | `%b`      |           |          |
| `IXor`                    | integer      | `%a`         | `%b`      |           |          |
| `INot`                    | integer      | `%a`         |           |           |          |
| `IBitInsert`              | integer      | `%base`      | `%insert` | `%offset` | `%count` |
| `UBitExtract`             | integer      | `%value`     | `%offset` | `%count`  |          |
| `SBitExtract`             | integer      | `%value`     | `%offset` | `%count`  |          |
| `IShl`                    | integer      | `%base`      | `%insert` | `%offset` | `%count` |
| `SShr`                    | integer      | `%value`     | `%offset` | `%count`  |          |
| `UShr`                    | integer      | `%value`     | `%offset` | `%count`  |          |
| `IBitCount`               | integer      | `%value`     |           |           |          |
| `IBitReverse`             | integer      | `%value`     |           |           |          |
| `IFindLsb`                | integer      | `%value`     |           |           |          |
| `SFindMsb`                | integer      | `%value`     |           |           |          |
| `UFindMsb`                | integer      | `%value`     |           |           |          |

Note that the `FindMsb` instructions follow SPIR-V semantics rather than D3D semantics.

### Integer arithmetic instructions
| `ir::OpCode`              | Return type  | Arguments... |           |           |
|---------------------------|--------------|--------------|-----------|-----------|
| `IAdd`                    | integer      | `%a`         | `%b`      |           |
| `IAddCarry`               | `vec2<i*/u*>`| `%a`         | `%b`      |           |
| `ISub`                    | integer      | `%a`         | `%b`      |           |
| `ISubBorrow`              | `vec2<i*/u*>`| `%a`         | `%b`      |           |
| `INeg`                    | integer      | `%a`         |           |           |
| `IMul`                    | integer      | `%a`         | `%b`      |           |
| `SDiv`                    | integer      | `%a`         | `%b`      |           |
| `UDiv`                    | integer      | `%a`         | `%b`      |           |
| `SMin`                    | integer      | `%a`         | `%b`      |           |
| `SMax`                    | integer      | `%a`         | `%b`      |           |
| `SClamp`                  | integer      | `%a`         | `%b`      | `%c`      |
| `UMin`                    | integer      | `%a`         | `%b`      |           |
| `UMax`                    | integer      | `%a`         | `%b`      |           |
| `UClamp`                  | integer      | `%a`         | `%b`      | `%c`      |
| `UMsad`                   | integer      | `%ref`       | `%src`    | `%accum`  |

## Serialization
The custom IR can be serialized for in-memory storage as a sequence of tokens that use variable-length encoding to save memory.

Variable-length encoding works in such a way that the number of leading 1 bits in the first byte equals the
number of bytes that follow the first byte. For example:
- `0b0xxxxxxx` is a single-byte token with a 7-bit value.
- `0b110xxxxx` starts a three-byte token with 21 bits in total.

Bytes within each token are stored in big-endian order, and are treated as signed integers. This means that
if the most significant bit of the encoded token is 1, the token must be sign-extended when decoding.

Each instruction is encoded as follows:
- The opcode token, laid out as follows:
  - The opcode itself (10 bits)
  - The instruction flags (2 bits)
  - The number of argument tokens that follow, excluding the type tokens (remaining bits).
- A list of tokens declaring the return type. Types are encoded as follows:
  - A single header token declaring array dimensionality (2 bits) and struct member count (remaining bits).
  - For each array dimension, one token storing the array size in that dumension.
  - For each struct type, a bit field consisting of the scalar type (5 bits) and vector size (2 bits).
- The argument tokens. All non-literal argument tokens are encoded as signed integers relative to the current instruction ID in order to optimize the common case of accessing
  previous instruction results, with the exception of the special value `0`. Since self-references are impossible, this special value indicates a null argument.
