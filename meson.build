project('dxbc-spirv', ['cpp'],
  version         : '0.1.0',
  meson_version   : '>= 1.0',
  default_options : [ 'cpp_std=c++17', 'warning_level=2' ])

dxbc_spv_files = files([
  'ir/ir.cpp',
  'ir/ir_builder.cpp',

  'util/util_float16.cpp',
])

lib_dxbc_spv = static_library('dxbc_spv', dxbc_spv_files)

if get_option('enable_tests')
  subdir('tests')
endif
