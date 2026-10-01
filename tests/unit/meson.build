test_session = executable(
  'test_session',
  'test_session.cpp',
  dependencies: videotrust_dep,
)
test('session_lifecycle', test_session)

python = import('python').find_installation('python3')

test_live = executable(
  'test_live',
  'test_live.cpp',
  dependencies: videotrust_dep,
)
test('live_semantic_model', test_live)

test_live_contract = executable(
  'test_live_contract',
  'test_live_contract.cpp',
  dependencies: videotrust_dep,
)
test('live_contract_rendering', test_live_contract)

test(
  'live_contract_json_schema',
  python,
  args: [
    meson.project_source_root() / 'tests/contract/test_live_schema.py',
    meson.project_source_root() / 'docs/schemas/media-signing-live-session-event-0.1.json',
    test_live_contract.full_path(),
  ],
)

test_mapping = executable(
  'test_mapping',
  'test_mapping.cpp',
  dependencies: videotrust_dep,
)
test('upstream_mapping', test_mapping)

test_inspection_mapping = executable(
  'test_inspection_mapping',
  'test_inspection_mapping.cpp',
  dependencies: videotrust_dep,
)
test('inspection_mapping', test_inspection_mapping)

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

test_correlation = executable(
  'test_correlation',
  'test_correlation.cpp',
  dependencies: videotrust_dep,
)
test('artifact_nal_correlation', test_correlation)

test_preservation = executable(
  'test_preservation',
  'test_preservation.cpp',
  dependencies: videotrust_dep,
)
test('preservation_derivation', test_preservation)

test_preservation_render = executable(
  'test_preservation_render',
  'test_preservation_render.cpp',
  dependencies: videotrust_dep,
)
test('preservation_json_render', test_preservation_render)

test_render = executable(
  'test_render',
  'test_render.cpp',
  dependencies: videotrust_dep,
)
test('render_text_json', test_render)

test(
  'inspection_json_schema',
  python,
  args: [
    meson.project_source_root() / 'tests/contract/test_inspection_schema.py',
    meson.project_source_root() / 'docs/schemas/media-signing-inspection-0.1.json',
    test_render.full_path(),
  ],
)

test(
  'preservation_json_schema',
  python,
  args: [
    meson.project_source_root() / 'tests/contract/test_preservation_schema.py',
    meson.project_source_root() / 'docs/schemas/media-signing-preservation-assessment-0.1.json',
    test_preservation_render.full_path(),
  ],
)

test_sign = executable(
  'test_sign',
  'test_sign.cpp',
  dependencies: videotrust_dep,
)
test('sign_options_and_exit', test_sign)

test_tamper = executable(
  'test_tamper',
  'test_tamper.cpp',
  dependencies: videotrust_dep,
)
test('tamper_ops_and_classify', test_tamper)
