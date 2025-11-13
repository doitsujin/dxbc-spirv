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
- Number of array elements in each dimension. If the element count in the last valid dimension is 0, the array is unbounded.
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

### Instruction flags
Every instruction has an opcode, a return type (which can be `void`), and optional `ir::OpFlags`. These flags are defined as follows:
- `Precise`: Defines that any transforms altering the result of the instruction are invalid.
  Only used on instructions returning floating point data. Similar to the `NoContraction` decoration in SPIR-V.
- `NonUniform`: Used on a descriptor load instruction to indicate that the descriptor itself may be non-uniform in the relevant scope.
- `SparseFeedback`: Used on instructions reading from an SRV or UAV descriptor to indicate that the return value includes an opaque
  sparse feedback value in addition to the value actually read from the resource.
- `NoNan`: Indicates that the result of an instruction cannot be NaN.
- `NoInf`: Indicates that the result of an instruction cannot be infinite.
- `NoSz`: Indicates that signed zero does not need to be preserved.

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
| `ir::OpCode`         | Return type | Arguments...   |          |        |
|----------------------|-------------|----------------|----------|--------|
| `DebugName`          | `void`      | `%instruction` | String   |        |
| `DebugMemberName`    | `void`      | `%instruction` | `member` | String |
| `Drain`              | any         | `%instruction` |          |        |

It is not meaningful to set debug names for mode setting instructions (see below).
Otherwise, any instruction can be a valid target. The debug name may require multiple
operands depending on its size. Literal strings are null-terminated.

The `DebugMemberName` instruction can be applied to `Dcl*` instructions that declare a
struct type, in order to assign debug names to individual struct members.

`Drain` is a pseudo-instruction that either returns void or returns the result of the
argument unmodified, and will not be touched by any code passes. It is only intended
for debugging purposes and will not be included in any valid shader program.

### Constant declarations
| `ir::OpCode`         | Return type | Arguments...                    |
|----------------------|-------------|---------------------------------|
| `Constant`           | any         | Literals for each member        |
| `Undef`              | any         | None                            |
| `MinValue`           | scalar      | None                            |
| `MaxValue`           | scalar      | None                            |

Constant literals are flattened according to the type definition:
- For a scalar, one token is used.
- A vector of size `n` is stored as `n` consecutive scalars.
- For a structure, members are stored consecutively.
- For an array, elements are stored consecutively.

`Undef` returns an undefined value of any given type, and is commonly found as an operand
to `Phi` instructions. For robustness reasons, it is advised to zero-initialize variables
anyway.

`MinValue` and `MaxValue` instructions can occur anywhere in the code and return the
lowest and highest representable number for a given data type that is not infinite.
These instructions are useful for handling min-precision types in some cases, and
**must** be lowered to constants before lowering to the target IR.

### Mode setting instructions

These instructions provide additional information that may affect the execution of the shader.

| `ir::OpCode`             | Return type | Arguments...  |                      |                        |                         |
|--------------------------|-------------|---------------|----------------------|------------------------|-------------------------|
| `SetCsWorkgroupSize`     | `void`      | `%EntryPoint` | `x`                  | `y`                    | `z`                     |
| `SetGsInstances`         | `void`      | `%EntryPoint` | `n`                  |                        |                         |
| `SetGsInputPrimitive`    | `void`      | `%EntryPoint` | `ir::PrimitiveType`  |                        |                         |
| `SetGsOutputVertices`    | `void`      | `%EntryPoint` | `n`                  |                        |                         |
| `SetGsOutputPrimitive`   | `void`      | `%EntryPoint` | `ir::PrimitiveType`  | `stream mask`          |                         |
| `SetPsEarlyFragmentTest` | `void`      | `%EntryPoint` |                      |                        |                         |
| `SetPsDepthGreaterEqual` | `void`      | `%EntryPoint` |                      |                        |                         |
| `SetPsDepthLessEqual`    | `void`      | `%EntryPoint` |                      |                        |                         |
| `SetTessPrimitive`       | `void`      | `%EntryPoint` | `ir::PrimitiveType`  | `ir::TessWindingOrder` | `ir::TessPartitioning`  |
| `SetTessDomain`          | `void`      | `%EntryPoint` | `ir::PrimitiveType`  |                        |                         |
| `SetTessControlPoints`   | `void`      | `%EntryPoint` | `n_in`               | `n_out`                |                         |
| `SetFpMode`              | `f*`        | `%EntryPoint` | `ir::RoundMode`      | `ir::DenormMode`       |                         |

All operands bar the `%EntryPoint` operand are literal constants.

The `SetFpMode` applies default optimization properties and denorm behaviour for all instructions that produce floating point results
of the type declared via the instruction's return type. Optimization flags are declared via `OpFlags`.

Per-instruction `OpFlags` that affect floating point instructions are additive to the default mode, i.e. if the default mode specifies
`Precise` then any instructions returning the given type will also be assumed to be `Precise`, regardless of whether the flag is
enabled for that particular instruction.

As for the rounding mode, only round-to-zero and round-to-nearest-even are allowed.

If no `SetFpMode` instruction is present for any given float type, its default optimization flags can be assumed to be `0`, and
the rounding and denorm modes remain undefined.

Environments that cannot easily support this should try to apply the FP32 defaults, or ignore the instruction.

### Type conversion instructions
| `ir::OpCode`            | Return type      | Argument         |
|-------------------------|------------------|------------------|
| `ConvertFtoF`           | any              | `%value`         |
| `ConvertFtoI`           | any              | `%value`         |
| `ConvertItoF`           | any              | `%value`         |
| `ConvertItoI`           | any              | `%value`         |
| `ConvertF32toPackedF16` | `u32`            | `%value`         |
| `ConvertPackedF16toF32` | `vec2<f32>`      | `%value`         |
| `Cast`                  | any              | `%value`         |
| `ConsumeAs`             | any              | `%value`         |

If the result type and source type are the same, the conversion operation is a no-op and will be removed by a lowering pass.

Semantics are as follows:
- `ConvertFtoF` converts between float types of a different width. For conversions from `f64`, round-even semantics are required
  but denorms may or may not get flushed.
- `ConvertFToI` is a conversion from a float type to a signed or unsigned integer, with round-to-zero semantics.
- `ConvertIToF` is a value-preserving conversion from a signed or unsigned integer to any float type with round-even semantics.
- `ConvertItoI` converts between integer types of different size. If the result type is larger than the source and the source
  is signed, it will be sign-extended, otherwise it will be zero-extended. If the result type is smaller, excess bits are discarded.
  If the two types only differ in signedness, this instruction is identical in behaviour to `Cast` and will be lowered to it.
- `ConvertF32toPackedF16` takes a vector of `f32`, performs a conversion to `f16`, and packs both into a single unsigned integer with
  the first component of the vector stored in the lower 16 bits of the result. This instruction does not require hardware support
  for 16-bit floating point arithmetic. The float conversion must use round-to-zero semantics.
- `ConvertPackedF16toF32` takes a single `u32` as two packed `f16` and converts them to an `f32` vector. This instruction does not require
  hardware support for 16-bit floating point arithmetic.
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
| `DclSpecConstant`    | scalar           | `%EntryPoint`    | spec id          | default   |                |                     |                 |
| `DclPushData`        | any              | `%EntryPoint`    | push data offset | `ir::ShaderStageMask` |    |                     |                 |
| `DclSampler`         | `void`           | `%EntryPoint`    | space            | register  | count          |                     |                 |
| `DclCbv`             | any              | `%EntryPoint`    | space            | register  | count          |                     |                 |
| `DclSrv`             | any              | `%EntryPoint`    | space            | register  | count          | `ir::ResourceKind`  |                 |
| `DclUav`             | any              | `%EntryPoint`    | space            | register  | count          | `ir::ResourceKind`  | `ir::UavFlags`  |
| `DclUavCounter`      | `u32`            | `%EntryPoint`    | `%DclUav` uav    |           |                |                     |                 |
| `DclLds`             | any              | `%EntryPoint`    |                  |           |                |                     |                 |
| `DclScratch`         | any              | `%EntryPoint`    |                  |           |                |                     |                 |
| `DclTmp`             | any              | `%EntryPoint`    |                  |           |                |                     |                 |
| `DclXfb`             | `void`           | `%DclOutput*`    | xfb buffer       | stride    | offset         |                     |                 |
| `DclParam`           | scalar           |                  |                  |           |                |                     |                 |

The `count` parameter for `DclSampler`, `DclSrv`, `DclCbv` and `DclUav` instructions is a literal constant declaring the size of the
descriptor array, If the size is `0`, the array is unbounded. If `1`, the declaration consists of only a single descriptor, and the
address operand of any `DescriptorLoad` instruction accessing this descriptor will reference a constant `0`.

For `DclSrv` and `DclUav`, if the resource is `ir::ResourceKind::Structured`, the return type is a two-dimensional array of a scalar type,
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

The `ir::InterpolationModes` parameter for input declarations is not set outside of pixel shaders.
Inside pixel shaders, it will always be set to `Flat` for integer types.

The return type of `DclInput` and `DclOutput` can be a 32-bit scalar or vector type, or a sized array of scalars or vectors.
In pixel shaders, `DclInput` instructions with an integer type must set the `Flat` interpolation mode.

For any `DclOutput` instruction in hull shaders, or corresponding `DclInput` instructions in domain shaders, control point data will
always have a sized array type, whereas patch constants use a scalar or vector type. The exception here is that tessellation factor
built-ins are also exposed as an array, but they are inherently always patch constants.

Likewise, `DclInput*` instructions for per-vertex inputs in geometry shaders and hull shaders will use a sized array type.

For `DclOutput*` instructions, the `stream` parameter is only defined in geometry shaders.

The `DclXfb` instruction declares transform feedback properties for an **existing** output.

#### Resource return types
The return type for a typed resource declaration (i.e. image or typed buffer) is a scalar of the sampled type.
Instructions accessing typed methods will usually return or expect a `vec4` of the given type, with some exceptions
such as image sampling with depth comparison, which will return a scalar.

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

The `%address` parameter is a constant vector or scalar of an integer type.

### Sparse feedback
Instructions decorated with the `SparseFeedback` flag return a struct type with the following members:
- An `u32` containing the sparse feedback value.
- A scalar or vector containing the data retrieved from the resource.

The sparse feedback value should only be used with the `CheckSparseAccess` instruction. This abstraction
exists in lower-level IRs, and was adopted here because applications may choose to copy the raw feedback
value around and feed it back into a shader later, or potentially use it in external tools.

| `ir::OpCode`         | Return type | Arguments... |
|----------------------|-------------|--------------|
| `CheckSparseAccess`  | `bool`      | `%feedback`  |

### Load/Store instructions
| `ir::OpCode`         | Return type  | Arguments...       |                                |          |         |
|----------------------|--------------|--------------------|--------------------------------|----------|---------|
| `ParamLoad`          | any          | `%Function`        | `%DclParam` ...                |          |         |
| `TmpLoad`            | any          | `%DclTmp` variable |                                |          |         |
| `TmpStore`           | `void`       | `%DclTmp` variable | `%value`                       |          |         |
| `ScratchLoad`        | any          | `%DclScratch` var. | `%address` into scratch array  |          |         |
| `ScratchStore`       | `void`       | `%DclScratch` var. | `%address` into scratch array  | `%value` |         |
| `LdsLoad`            | any          | `%DclLds` variable | `%address` into LDS            |          |         |
| `LdsStore`           | `void`       | `%DclLds` variable | `%address` into LDS            | `%value` |         |
| `PushDataLoad`       | any          | `%DclPushData`     | `%address` into push data      |          |         |
| `InputLoad`          | any          | `%DclInput*`       | `%address` into input type     |          |         |
| `OutputLoad`         | any          | `%DclOutput*`      | `%address` into output type    |          |         |
| `OutputStore`        | any          | `%DclOutput*`      | `%address` into output type    | `%value` |         |
| `DescriptorLoad`     | descriptor   | `%Dcl*` variable   | `%index` into descriptor array |          |         |
| `BufferLoad`         | any          | `%descriptor`      | `%address` into CBV / SRV / UAV| `align`  |         |
| `BufferStore`        | any          | `%descriptor` (UAV)| `%address` into UAV            | `%value` | `align` |
| `BufferQuerySize`    | `u32`        | `%descriptor`      |                                |          |         |
| `MemoryLoad`         | any          | `%Pointer`         | `%address` into pointee type   | `align`  |         |
| `MemoryStore`        | any          | `%Pointer`         | `%address` into pointee type   | `%value` | `align` |
| `ConstantLoad`       | any          | `%Constant`        | `%address` into pointee type   |                    |

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

The `align` parameter for `Memory*` and `Buffer` instructions is a literal constant that represents the smallest guaranteed alignment for
the operation, and will be at least equal to the size of the scalar type being accessed.

The `ConstantLoad` instruction can be used to dynamically index into a constant array. If the array is a vector array, the vector
component index must be constant.

### Atomic instructions
| `ir::OpCode`         | Return type      | Arguments...      |                              |                |                |                |
|----------------------|------------------|-------------------|------------------------------|----------------|----------------|----------------|
| `LdsAtomic`          | `void` or scalar | `%DclLds`         | `%address` into LDS type     | `%operands`    | `ir::AtomicOp` |                |
| `BufferAtomic`       | `void` or scalar | `%uav` descriptor | `%address` into UAV type     | `%operands`    | `ir::AtomicOp` |                |
| `ImageAtomic`        | `void` or scalar | `%uav` descriptor | `%layer`                     | `%coord`       | `%operands`    | `ir::AtomicOp` |
| `CounterAtomic`      | `void` or scalar | `%uav` counter    | `ir::AtomicOp` (inc/dec)     |                |                |                |
| `MemoryAtomic`       | `void` or scalar | `%Pointer`        | `%address` into pointee type | `%operands`    | `ir::AtomicOp` |                |

The `ir::AtomicOp` parameter is a literal enum value, thus the last parameter.

The `%layer` parameter is `null` for non-arrayed image types.

#### Operations
Unless otherwise noted, the `%operand` parameter is a scalar integer whose type matches that of
the return type and resource type exactly.

- `Load`: Atomically loads a value. This instruction is generated when an atomic instruction has
  no side effects. Must not have a `void` return type. `%operand` will be `null`.
- `Store`: Atomically stores a value. This instruction is generated when the result of an atomic
  exchange instruction goes unused.
- `Inc` and `Dec` are equivalent to `Add` or `Sub` with a constant value of `1`, respectively.
  `%operand` will be `null`.
- `CompareExchange` takes a `vec2<u32/i32>` as an operand: The value to compare to first, and the
  value to store on success second.

### Image instructions
| `ir::OpCode`         | Return type      | Arguments...  |                   |             |              |              |             |              |          |           |       |             |
|----------------------|------------------|---------------|-------------------|-------------|--------------|--------------|-------------|--------------|----------|-----------|------ |-------------|
| `ImageLoad`          | see below        | `%descriptor` | `%mip`            | `%layer`    | `%coord`     | `%sample`    | `%offset`   |              |          |           |       |             |
| `ImageStore`         | `void`           | `%descriptor` | `%layer`          | `%coord`    | `%value`     |              |             |              |          |           |       |             |
| `ImageAtomic`        | `void` or scalar | `%uav` descriptor | `%layer`      | `%coord`   | `%operands`   | `ir::AtomicOp` |           |              |          |           |       |             |
| `ImageQuerySize`     | struct, see below| `%descriptor` | `%mip`            |             |              |              |             |              |          |           |       |             |
| `ImageQueryMips`     | `u32`            | `%descriptor` |                   |             |              |              |             |              |          |           |       |             |
| `ImageQuerySamples`  | `u32`            | `%descriptor` |                   |             |              |              |             |              |          |           |       |             |
| `ImageSample`        | see below        | `%descriptor` | `%sampler`        | `%layer`    | `%coord`     | `%offset`    | `%lod_index` | `%lod_bias` | `%lod_clamp` | `%dx` | `%dy` | `%depth_compare` |
| `ImageGather`        | see below        | `%descriptor` | `%sampler`        | `%layer`    | `%coord`     | `%offset`    | `%depth_compare` | `component` |          |       |       |             |
| `ImageComputeLod`    | `vec2<f32>`      | `%descriptor` | `%sampler`        | `%coord`    |              |              |             |              |          |           |       |             |

The `ImageQuerySize` instruction returns a struct with the following members:
- A scalar or vector containing the size of the queried mip level, in pixels
- The array layer count, which will always be 1 for non-layered images

The `ImageLoad`, `ImageSample` and `ImageGather` instructions return a sparse feedback struct if
the `SparseFeedback` flag is set on the instruction.

For `ImageLoad`, `ImageSample` without depth comparison, and `ImageGather`, the returned texel type
is a `vec4` of the scalar type declared for the resource. For `ImageSample` with depth comparison,
the return type is scalar.

For `ImageLoad`, `ImageSample` and `ImageGather`, all operands after `%coord` may be `null`. The `component`
operand for `ImageGather` is a literal and will thus never be `null`.

For `ImageSample`, the `offset` parameter, if not `null`, is always a constant vector. For `ImageGather`, it may not be constant.

For `ImageSample`, the `%dx` and `%dy` parameters, if not `null`, contain derivatives of the same type as the texture coordinate.

For `ImageSample`, if the `%depth_compare` parameter is not `null`, the instruction must return a scalar float.

### Pointer instructions
Raw pointers can be used to access memory via the `MemoryLoad`, `MemoryStore` and `MemoryAtomic` instructions.

| `ir::OpCode`         | Return type      | Arguments          |                |
|----------------------|------------------|--------------------|----------------|
| `Pointer`            | any              | `%address` (`u64`) | `ir::UavFlags` |

The return type of the `Pointer` instruction is the pointee type, and may be any scalar, vector, struct or
array type.

The UAV flags on the `Pointer` instruction may define read-only, write-only and coherence flags.

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
constant outputs. The patch constant function **does not** include any barriers, these will have to be inserted when
lowering as necessary.

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

| `ir::OpCode`          | Return type | Arguments... |              |
|-----------------------|-------------|--------------|--------------|
| `ScopedIf`            | `void`      | `%EndIf`     | `%cond`      |
| `ScopedElse`          | `void`      | `%If`        |              |
| `ScopedEndIf`         | `void`      | `%If`        |              |
| `ScopedLoop`          | `void`      | `%EndLoop`   |              |
| `ScopedLoopBreak`     | `void`      | `%Loop`      |              |
| `ScopedLoopContinue`  | `void`      | `%Loop`      |              |
| `ScopedEndLoop`       | `void`      | `%Loop`      |              |
| `ScopedSwitch`        | `void`      | `%EndSwitch` | `%value`     |
| `ScopedSwitchCase`    | `void`      | `%Switch`    | `value`      |
| `ScopedSwitchDefault` | `void`      | `%Switch`    |              |
| `ScopedSwitchBreak`   | `void`      | `%Switch`    |              |
| `ScopedEndSwitch`     | `void`      | `%Switch`    |              |

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

| `ir::OpCode`              | Return type | Arguments...         |                         |                         |
|---------------------------|-------------|----------------------|-------------------------|-------------------------|
| `Demote`                  | `void`      | None                 |                         |                         |
| `InterpolateAtCentroid`   | any         | `%DclInput`          | `%address`              |                         |
| `InterpolateAtSample`     | any         | `%DclInput`          | `%address`              | `%sample`               |
| `InterpolateAtOffset`     | any         | `%DclInput`          | `%address`              | `%offset` (`vec2<f32>`) |
| `DerivX`                  | any         | `%value`             | `ir::DerivativeMode`    |                         |
| `DerivY`                  | any         | `%value`             | `ir::DerivativeMode`    |                         |
| `RovScopedLockBegin`      | `void`      | `ir::MemoryTypeMask` | `ir::RovScope`          |                         |
| `RovScopedLockEnd`        | `void`      | `ir::MemoryTypeMask` |                         |                         |

The memory type mask argument in the `RovScopedLock*` instructions declares which types of UAV
memory should be made visible or available before or after the given lock operation. This will
result in an additional memory barrier when lowering to SPIR-V.

The `RovScope` defines the locking granularity.

### Comparison instructions
Component-wise comparisons that return a boolean. Operands must be scalar.

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
| `SLe`                     | `bool`      | `%a`         | `%b` |
| `SGt`                     | `bool`      | `%a`         | `%b` |
| `SGe`                     | `bool`      | `%a`         | `%b` |
| `ULt`                     | `bool`      | `%a`         | `%b` |
| `ULe`                     | `bool`      | `%a`         | `%b` |
| `UGt`                     | `bool`      | `%a`         | `%b` |
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
| `FPow`                    | `f32`        | `%base`      | `%exp`  |       |
| `FPowLegacy`              | `f32`        | `%base`      | `%exp`  |       |

Note that `FDot*` instructions takes vector arguments and returns a scalar. This instruction
**must** be lowered to a sequence of `FMul*` and `FMad*` instructions.

The `mode` parameter for `FRound` is a constant enum `ir::RoundMode`.

The `*Legacy` instructions follow D3D9 rules w.r.t. multiplication by zero and **must** be lowered.

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
| `IShl`                    | integer      | `%value`     | `%count`  |           |          |
| `SShr`                    | integer      | `%value`     | `%count`  |           |          |
| `UShr`                    | integer      | `%value`     | `%count`  |           |          |
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
| `IAddCarry`               | `vec2<u32>`  | `%a`         | `%b`      |           |
| `ISub`                    | integer      | `%a`         | `%b`      |           |
| `ISubBorrow`              | `vec2<u32>`  | `%a`         | `%b`      |           |
| `IAbs`                    | integer      | `%a`         |           |           |
| `INeg`                    | integer      | `%a`         |           |           |
| `IMul`                    | integer      | `%a`         | `%b`      |           |
| `UDiv`                    | integer      | `%a`         | `%b`      |           |
| `UMod`                    | integer      | `%a`         | `%b`      |           |
| `SMin`                    | integer      | `%a`         | `%b`      |           |
| `SMax`                    | integer      | `%a`         | `%b`      |           |
| `SClamp`                  | integer      | `%a`         | `%b`      | `%c`      |
| `UMin`                    | integer      | `%a`         | `%b`      |           |
| `UMax`                    | integer      | `%a`         | `%b`      |           |
| `UClamp`                  | integer      | `%a`         | `%b`      | `%c`      |
| `UMsad`                   | integer      | `%ref`       | `%src`    | `%accum`  |
| `SMulExtended`            | `vec2<i32>`  | `%a`         | `%b`      |           |
| `UMulExtended`            | `vec2<u32>`  | `%a`         | `%b`      |           |

The instructions `IAddCarry`, `ISubBorrow` and `*MulExtended` return a two-component vector with the low bits in the first
component, and the high bits (carry / borrow bits) in the second component. These instructions only support 32-bit types.

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
  - The instruction flags (8 bits)
- The operand count
- A list of tokens declaring the return type. Types are encoded as follows:
  - A single header token declaring array dimensionality (2 bits) and struct member count (remaining bits).
  - For each array dimension, one token storing the array size in that dimension.
  - For each struct type, a bit field consisting of the scalar type (5 bits) and vector size (2 bits).
- The argument tokens. All non-literal argument tokens are encoded as signed integers relative to the current instruction ID in order to optimize the common case of accessing
  previous instruction results, with the exception of the special value `0`. Since self-references are impossible, this special value indicates a null argument.
