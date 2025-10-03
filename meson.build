project('dxbc-spirv', ['cpp'],
  version         : '0.1.0',
  meson_version   : '>= 1.0',
  default_options : [ 'cpp_std=c++17', 'warning_level=2', 'cpp_eh=none' ])

dxbc_spv_files = files([
  'dxbc/dxbc_api.cpp',
  'dxbc/dxbc_container.cpp',
  'dxbc/dxbc_converter.cpp',
  'dxbc/dxbc_disasm.cpp',
  'dxbc/dxbc_interface.cpp',
  'dxbc/dxbc_io_map.cpp',
  'dxbc/dxbc_parser.cpp',
  'dxbc/dxbc_registers.cpp',
  'dxbc/dxbc_resources.cpp',
  'dxbc/dxbc_signature.cpp',
  'dxbc/dxbc_types.cpp',

  'sm3/sm3_types.cpp',
  'sm3/sm3_parser.cpp',
  'sm3/sm3_disasm.cpp',

  'ir/ir.cpp',
  'ir/ir_builder.cpp',
  'ir/ir_disasm.cpp',
  'ir/ir_divergence.cpp',
  'ir/ir_dominance.cpp',
  'ir/ir_legalize.cpp',
  'ir/ir_serialize.cpp',
  'ir/ir_utils.cpp',

  'ir/passes/ir_pass_arithmetic.cpp',
  'ir/passes/ir_pass_buffer_kind.cpp',
  'ir/passes/ir_pass_cfg_cleanup.cpp',
  'ir/passes/ir_pass_cfg_convert.cpp',
  'ir/passes/ir_pass_cse.cpp',
  'ir/passes/ir_pass_derivative.cpp',
  'ir/passes/ir_pass_descriptor_indexing.cpp',
  'ir/passes/ir_pass_function.cpp',
  'ir/passes/ir_pass_lower_consume.cpp',
  'ir/passes/ir_pass_lower_io.cpp',
  'ir/passes/ir_pass_lower_min16.cpp',
  'ir/passes/ir_pass_propagate_resource_types.cpp',
  'ir/passes/ir_pass_propagate_types.cpp',
  'ir/passes/ir_pass_remove_unused.cpp',
  'ir/passes/ir_pass_scalarize.cpp',
  'ir/passes/ir_pass_scratch.cpp',
  'ir/passes/ir_pass_ssa.cpp',
  'ir/passes/ir_pass_sync.cpp',

  'util/util_float16.cpp',
  'util/util_log.cpp',
  'util/util_md5.cpp',
  'util/util_swizzle.cpp',

  'spirv/spirv_builder.cpp',
  'spirv/spirv_mapping.cpp',
])

lib_dxbc_spv = static_library('dxbc_spv', dxbc_spv_files)

if get_option('enable_tests')
  subdir('tests')
endif

if get_option('enable_tools')
  subdir('tools')
endif

dxbc_spv_inc = include_directories('.')
dxbc_spv_dep = declare_dependency(
  include_directories : dxbc_spv_inc,
  link_with           : lib_dxbc_spv)
