source_path = '../src/'

test_libreq = shared_library('test_libreq',
    sources : [
        source_path + 'config.cpp',
        source_path + 'utils.cpp',
        source_path + 'disk.cpp',
        source_path + 'drivers.cpp',
        source_path + 'widgets.cpp',
        source_path + 'follow_process_log.cpp',
        source_path + 'crypto.cpp',
        source_path + 'misc.cpp',
        source_path + 'simple_tui.cpp',
        source_path + 'tui.cpp',
    ],
    include_directories : [include_directories(source_path), include_directories('../gucc/include')],
    dependencies: deps,
    link_with: [gucc_lib]
)

executable(
  'test-exec-interactive',
  files('unit-exec-interactive.cpp'),
  dependencies: deps,
  link_with: [test_libreq, gucc_lib],
  include_directories: [include_directories(source_path), include_directories('../gucc/include')],
  install: false)

executable(
  'test-process-tailing',
  files('unit-proccess-tailing.cpp'),
  dependencies: deps,
  link_with: [test_libreq, gucc_lib],
  include_directories: [include_directories(source_path), include_directories('../gucc/include')],
  install: false)
