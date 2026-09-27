# Integration tests for video-trust verify / sign (Annex-B).
# Fixtures are generated at test time (synthetic media + test-only PKI).

fixture_dir = meson.current_build_dir() / 'fixtures'
gen_script = meson.project_source_root() / 'scripts' / 'gen-verify-fixtures.sh'

test_verify_cli = executable(
  'test_verify_cli',
  'test_verify_cli.cpp',
  dependencies: videotrust_dep,
  install: false,
)

test_sign_cli = executable(
  'test_sign_cli',
  'test_sign_cli.cpp',
  dependencies: videotrust_dep,
  install: false,
)

verify_runner = find_program('bash')

test(
  'verify_integration',
  verify_runner,
  args: [
    '-lc',
    'set -euo pipefail; '
    + 'OMS_SIGN_ANNEXB="' + oms_sign_annexb.full_path() + '" '
    + '"' + gen_script + '" "' + fixture_dir + '"; '
    + '"' + test_verify_cli.full_path() + '" '
    + '"' + video_trust.full_path() + '" '
    + '"' + fixture_dir + '"',
  ],
  depends: [video_trust, oms_sign_annexb, test_verify_cli],
  timeout: 180,
  is_parallel: false,
)

test(
  'sign_integration',
  verify_runner,
  args: [
    '-lc',
    'set -euo pipefail; '
    + 'OMS_SIGN_ANNEXB="' + oms_sign_annexb.full_path() + '" '
    + '"' + gen_script + '" "' + fixture_dir + '"; '
    + '"' + test_sign_cli.full_path() + '" '
    + '"' + video_trust.full_path() + '" '
    + '"' + fixture_dir + '"',
  ],
  depends: [video_trust, oms_sign_annexb, test_sign_cli],
  timeout: 180,
  is_parallel: false,
)
