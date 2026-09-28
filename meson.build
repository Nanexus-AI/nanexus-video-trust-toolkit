project(
  'nanexus-video-trust-toolkit',
  'cpp',
  version: '0.1.0',
  meson_version: '>=0.61.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'werror=false',
    'buildtype=debugoptimized',
  ],
)

fs = import('fs')

oms_prefix = get_option('oms_prefix')
if oms_prefix == ''
  oms_prefix = meson.project_source_root() / '.oms-prefix'
endif

openssl_dep = dependency('openssl', version: '>=3.0.0')

oms_lib_candidates = [
  oms_prefix / 'lib',
  oms_prefix / 'lib' / 'x86_64-linux-gnu',
  oms_prefix / 'lib64',
]

cxx = meson.get_compiler('cpp')
oms_lib = []
oms_lib_dir = oms_prefix / 'lib'
oms_found = false
foreach libdir : oms_lib_candidates
  if fs.is_dir(libdir)
    cand = cxx.find_library(
      'media-signing-framework',
      dirs: libdir,
      required: false,
    )
    if cand.found()
      oms_lib = [cand]
      oms_lib_dir = libdir
      oms_found = true
      break
    endif
  endif
endforeach

if not oms_found
  error(
    'ONVIF media-signing-framework not found under '
    + oms_prefix
    + '\nBuild it first:\n'
    + '  ./scripts/fetch-upstream.sh\n'
    + '  ./scripts/build-upstream.sh\n'
    + 'Or pass -Doms_prefix=/path/to/prefix',
  )
endif

oms_inc_dir = oms_prefix / get_option('includedir') / 'media-signing-framework'
if not fs.is_dir(oms_inc_dir)
  error('Missing ONVIF headers at ' + oms_inc_dir)
endif

oms_dep = declare_dependency(
  dependencies: oms_lib + [openssl_dep],
  compile_args: ['-isystem', oms_inc_dir],
  link_args: ['-Wl,-rpath,' + oms_lib_dir],
)

inc = include_directories('include')

videotrust_core = static_library(
  'videotrust_core',
  files(
    'src/core/annexb.cpp',
    'src/core/correlation.cpp',
    'src/core/error.cpp',
    'src/core/nal_classify.cpp',
    'src/core/result.cpp',
    'src/render/render.cpp',
    'src/signing/sign.cpp',
    'src/tamper/tamper.cpp',
    'src/upstream/session.cpp',
    'src/upstream/upstream_mapping.cpp',
    'src/verification/verify.cpp',
  ),
  include_directories: inc,
  dependencies: oms_dep,
  install: false,
)

videotrust_dep = declare_dependency(
  link_with: videotrust_core,
  include_directories: inc,
  dependencies: oms_dep,
)

video_trust = executable(
  'video-trust',
  'src/cli/main.cpp',
  dependencies: videotrust_dep,
  cpp_args: ['-DVIDEO_TRUST_VERSION="' + meson.project_version() + '"'],
  install: true,
)

oms_sign_annexb = executable(
  'oms_sign_annexb',
  'tests/tools/oms_sign_annexb.cpp',
  dependencies: videotrust_dep,
  install: false,
)

subdir('tests/unit')
subdir('tests/integration')

summary(
  {
    'oms_prefix': oms_prefix,
    'oms_libdir': oms_lib_dir,
    'oms_includedir': oms_inc_dir,
  },
  section: 'ONVIF Media Signing dependency',
)
