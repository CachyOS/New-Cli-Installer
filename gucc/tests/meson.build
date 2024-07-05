# gucc test path
add_global_arguments('-DGUCC_TEST_DIR="' + meson.current_source_dir() + '"', language : 'cpp')

executable(
  'test-initcpio',
  files('unit-initcpio.cpp'),
  dependencies: deps,
  link_with: [gucc_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-pacmanconf',
  files('unit-pacmanconf.cpp'),
  dependencies: deps,
  link_with: [gucc_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-fstab_gen',
  files('unit-fstab_gen.cpp'),
  dependencies: deps,
  link_with: [gucc_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-crypttab_gen',
  files('unit-crypttab_gen.cpp'),
  dependencies: deps,
  link_with: [gucc_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-grub_config_gen',
  files('unit-grub_config_gen.cpp'),
  dependencies: deps,
  link_with: [gucc_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-mtab',
  files('unit-mtab.cpp'),
  dependencies: deps,
  link_with: [gucc_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-locale',
  files('unit-locale.cpp'),
  dependencies: deps,
  link_with: [gucc_lib],
  include_directories: [include_directories('../include')],
  install: false)
