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
