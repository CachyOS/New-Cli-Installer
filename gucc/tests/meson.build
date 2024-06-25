source_path = '../src/'

test_libreq = shared_library('test_libreq',
    sources : [
        source_path + 'file_utils.cpp',
        source_path + 'cpu.cpp',
        source_path + 'pacmanconf_repo.cpp',
        source_path + 'initcpio.cpp',
    ],
    include_directories : [include_directories(source_path)],
    dependencies: deps
)

executable(
  'test-initcpio',
  files('unit-initcpio.cpp'),
  dependencies: deps,
  link_with: [test_libreq],
  include_directories: [include_directories(source_path)],
  install: false)

executable(
  'test-pacmanconf',
  files('unit-pacmanconf.cpp'),
  dependencies: deps,
  link_with: [test_libreq],
  include_directories: [include_directories(source_path)],
  install: false)
