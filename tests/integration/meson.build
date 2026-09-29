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

test_tamper_cli = executable(
  'test_tamper_cli',
  'test_tamper_cli.cpp',
  dependencies: videotrust_dep,
  install: false,
)

test_correlation_fixtures = executable(
  'test_correlation_fixtures',
  'test_correlation_fixtures.cpp',
  dependencies: videotrust_dep,
  install: false,
)

test_preservation_fixtures = executable(
  'test_preservation_fixtures',
  'test_preservation_fixtures.cpp',
  dependencies: videotrust_dep,
  install: false,
)

python = import('python').find_installation('python3')
inspect_test_script = meson.project_source_root() / 'tests/integration/test_inspect_cli.py'
inspection_schema = meson.project_source_root() / 'docs/schemas/media-signing-inspection-0.1.json'

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
  'inspect_integration',
  verify_runner,
  args: [
    '-lc',
    'set -euo pipefail; '
    + 'OMS_SIGN_ANNEXB="' + oms_sign_annexb.full_path() + '" '
    + '"' + gen_script + '" "' + fixture_dir + '"; '
    + '"' + python.full_path() + '" '
    + '"' + inspect_test_script + '" '
    + '"' + video_trust.full_path() + '" '
    + '"' + fixture_dir + '" '
    + '"' + inspection_schema + '"',
  ],
  depends: [video_trust, oms_sign_annexb],
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

test(
  'tamper_integration',
  verify_runner,
  args: [
    '-lc',
    'set -euo pipefail; '
    + 'OMS_SIGN_ANNEXB="' + oms_sign_annexb.full_path() + '" '
    + '"' + gen_script + '" "' + fixture_dir + '"; '
    + '"' + test_tamper_cli.full_path() + '" '
    + '"' + video_trust.full_path() + '" '
    + '"' + fixture_dir + '"',
  ],
  depends: [video_trust, oms_sign_annexb, test_tamper_cli],
  timeout: 180,
  is_parallel: false,
)

test(
  'correlation_fixture_integration',
  verify_runner,
  args: [
    '-lc',
    'set -euo pipefail; '
    + 'OMS_SIGN_ANNEXB="' + oms_sign_annexb.full_path() + '" '
    + '"' + gen_script + '" "' + fixture_dir + '"; '
    + '"' + video_trust.full_path() + '" tamper --codec h264 '
    + '--operation strip-signing-sei --force '
    + '"' + fixture_dir + '/h264/signed.h264" '
    + '-o "' + fixture_dir + '/h264/correlation-stripped.h264"; '
    + '"' + video_trust.full_path() + '" tamper --codec h265 '
    + '--operation strip-signing-sei --force '
    + '"' + fixture_dir + '/h265/signed.h265" '
    + '-o "' + fixture_dir + '/h265/correlation-stripped.h265"; '
    + '"' + test_correlation_fixtures.full_path() + '" '
    + '"' + fixture_dir + '"',
  ],
  depends: [video_trust, oms_sign_annexb, test_correlation_fixtures],
  timeout: 180,
  is_parallel: false,
)

test(
  'preservation_fixture_integration',
  verify_runner,
  args: [
    '-lc',
    'set -euo pipefail; '
    + 'OMS_SIGN_ANNEXB="' + oms_sign_annexb.full_path() + '" '
    + '"' + gen_script + '" "' + fixture_dir + '"; '
    + '"' + video_trust.full_path() + '" tamper --codec h264 '
    + '--operation strip-signing-sei --force '
    + '"' + fixture_dir + '/h264/signed.h264" '
    + '-o "' + fixture_dir + '/h264/correlation-stripped.h264"; '
    + '"' + video_trust.full_path() + '" tamper --codec h265 '
    + '--operation strip-signing-sei --force '
    + '"' + fixture_dir + '/h265/signed.h265" '
    + '-o "' + fixture_dir + '/h265/correlation-stripped.h265"; '
    + 'mkdir -p "' + fixture_dir + '/h264/tamper-work" '
    + '"' + fixture_dir + '/h265/tamper-work"; '
    + '"' + video_trust.full_path() + '" tamper --codec h264 '
    + '--operation truncate --count 3 --force '
    + '"' + fixture_dir + '/h264/signed.h264" '
    + '-o "' + fixture_dir + '/h264/tamper-work/trunc.h264"; '
    + '"' + video_trust.full_path() + '" tamper --codec h265 '
    + '--operation truncate --count 3 --force '
    + '"' + fixture_dir + '/h265/signed.h265" '
    + '-o "' + fixture_dir + '/h265/tamper-work/trunc.h265"; '
    + '"' + video_trust.full_path() + '" tamper --codec h264 '
    + '--operation corrupt-vcl --force '
    + '"' + fixture_dir + '/h264/signed.h264" '
    + '-o "' + fixture_dir + '/h264/tamper-work/corrupt.h264"; '
    + '"' + video_trust.full_path() + '" tamper --codec h265 '
    + '--operation corrupt-vcl --force '
    + '"' + fixture_dir + '/h265/signed.h265" '
    + '-o "' + fixture_dir + '/h265/tamper-work/corrupt.h265"; '
    + '"' + test_preservation_fixtures.full_path() + '" '
    + '"' + fixture_dir + '"',
  ],
  depends: [video_trust, oms_sign_annexb, test_preservation_fixtures],
  timeout: 180,
  is_parallel: false,
)
