test_session = executable(
  'test_session',
  'test_session.cpp',
  dependencies: videotrust_dep,
)
test('session_lifecycle', test_session)

test_mapping = executable(
  'test_mapping',
  'test_mapping.cpp',
  dependencies: videotrust_dep,
)
test('upstream_mapping', test_mapping)

test_result = executable(
  'test_result',
  'test_result.cpp',
  dependencies: videotrust_dep,
)
test('result_and_exit_codes', test_result)

test_annexb = executable(
  'test_annexb',
  'test_annexb.cpp',
  dependencies: videotrust_dep,
)
test('annexb_reader', test_annexb)

test_render = executable(
  'test_render',
  'test_render.cpp',
  dependencies: videotrust_dep,
)
test('render_text_json', test_render)

test_sign = executable(
  'test_sign',
  'test_sign.cpp',
  dependencies: videotrust_dep,
)
test('sign_options_and_exit', test_sign)
