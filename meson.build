project('dxbc-spirv', ['cpp'],
  version         : '0.1.0',
  meson_version   : '>= 1.0',
  default_options : [ 'cpp_std=c++17', 'warning_level=2', 'cpp_eh=none' ])

dxbc_spv_files = files([
  'ir/ir.cpp',
  'ir/ir_builder.cpp',
  'ir/ir_disasm.cpp',
  'ir/ir_serialize.cpp',
  'ir/ir_validation.cpp',

  'util/util_float16.cpp',

  'spirv/spirv_builder.cpp',
  'spirv/spirv_mapping.cpp',
])

lib_dxbc_spv = static_library('dxbc_spv', dxbc_spv_files,
  include_directories : [ 'submodules/spirv_headers/include' ])

if get_option('enable_tests')
  subdir('tests')
endif
