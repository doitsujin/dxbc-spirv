subdir('api')

test_files = files([
  'test_main.cpp',

  'dxbc/test_dxbc_common.cpp',
  'dxbc/test_dxbc_signature.cpp',

  'ir/test_ir_builder.cpp',
  'ir/test_ir_input_map.cpp',
  'ir/test_ir_op.cpp',
  'ir/test_ir_serialize.cpp',
  'ir/test_ir_type.cpp',
  'ir/test_ir_type_propagation.cpp',

  'util/test_util_small_vector.cpp',
  'util/test_util_vle.cpp',
])

executable('test_dxbc_spv', test_files,
  link_with   : [ lib_dxbc_spv ],
  install     : false)

executable('test_spirv_lowering', files('test_spirv.cpp'),
  link_with   : [ lib_dxbc_spv, lib_dxbc_spv_test_api ],
  install     : false)
