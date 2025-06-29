project('dxbc-spirv', ['cpp'],
  version         : '0.1.0',
  meson_version   : '>= 1.0',
  default_options : [ 'cpp_std=c++17', 'warning_level=2' ])

dxbc_spv_files = files([
  'util/util_float16.cpp',
])

static_library('dxbc_spv', dxbc_spv_files)
