test_files = files([
  'test_main.cpp',

  'ir/test_ir_builder.cpp',
  'ir/test_ir_op.cpp',
  'ir/test_ir_serialize.cpp',
  'ir/test_ir_type.cpp',

  'util/test_util_small_vector.cpp',
  'util/test_util_vle.cpp',
])

executable('test_dxbc_spv', test_files,
  link_with   : [ lib_dxbc_spv ],
  install     : false)
