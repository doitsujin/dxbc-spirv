test_api_files = files([
  'test_api.cpp',
  'test_api_arithmetic.cpp',
  'test_api_io.cpp',
  'test_api_misc.cpp',
  'test_api_pass_arithmetic.cpp',
  'test_api_pass_buffer_kind.cpp',
  'test_api_pass_lower_io.cpp',
  'test_api_pass_scalarize.cpp',
  'test_api_resources.cpp',
  'test_api_spirv.cpp',
])

lib_dxbc_spv_test_api = static_library('test_api', test_api_files,
  link_with   : [ lib_dxbc_spv ])

executable('test_api_dump', files([ 'test_api_dump.cpp']),
  link_with   : [ lib_dxbc_spv_test_api, lib_dxbc_spv ],
  install     : false)
