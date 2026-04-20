subdir('api')

test_files = [
  'test_main.cpp',

  'ir/test_ir_builder.cpp',
  'ir/test_ir_input_map.cpp',
  'ir/test_ir_op.cpp',
  'ir/test_ir_serialize.cpp',
  'ir/test_ir_type.cpp',
  'ir/test_ir_type_propagation.cpp',

  'util/test_util_small_vector.cpp',
  'util/test_util_vle.cpp',
]

if get_option('enable_sm5')
  test_files += [
    'dxbc/test_dxbc_common.cpp',
    'dxbc/test_dxbc_signature.cpp',
  ]
endif

executable('test_dxbc_spv', files(test_files),
  link_with   : [ lib_dxbc_spv ],
  install     : false)

if get_option('enable_spirv')
  executable('test_spirv_lowering', files('test_spirv.cpp'),
    link_with   : [ lib_dxbc_spv, lib_dxbc_spv_test_api ],
    install     : false)
endif
