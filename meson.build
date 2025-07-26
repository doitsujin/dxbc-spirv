project('dxbc-spirv', ['cpp'],
  version         : '0.1.0',
  meson_version   : '>= 1.0',
  default_options : [ 'cpp_std=c++17', 'warning_level=2', 'cpp_eh=none' ])

dxbc_spv_files = files([
  'dxbc/dxbc_container.cpp',
  'dxbc/dxbc_converter.cpp',
  'dxbc/dxbc_disasm.cpp',
  'dxbc/dxbc_io_map.cpp',
  'dxbc/dxbc_parser.cpp',
  'dxbc/dxbc_registers.cpp',
  'dxbc/dxbc_signature.cpp',
  'dxbc/dxbc_types.cpp',

  'ir/ir.cpp',
  'ir/ir_builder.cpp',
  'ir/ir_disasm.cpp',
  'ir/ir_serialize.cpp',
  'ir/ir_validation.cpp',

  'ir/passes/ir_pass_cfg_cleanup.cpp',
  'ir/passes/ir_pass_cfg_convert.cpp',

  'util/util_float16.cpp',
  'util/util_log.cpp',
  'util/util_md5.cpp',

  'spirv/spirv_builder.cpp',
  'spirv/spirv_mapping.cpp',
])

lib_dxbc_spv = static_library('dxbc_spv', dxbc_spv_files,
  include_directories : [ 'submodules/spirv_headers/include' ])

if get_option('enable_tests')
  subdir('tests')
endif

if get_option('enable_tools')
  subdir('tools')
endif
