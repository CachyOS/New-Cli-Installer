doctest_main_lib = static_library('doctest_main',
    sources : [
        'unit.cpp',
    ],
    include_directories : [include_directories('.')],
    dependencies: doctest
)

executable(
  'test-initcpio',
  files('unit-initcpio.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-pacmanconf',
  files('unit-pacmanconf.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-fstab_gen',
  files('unit-fstab_gen.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-crypttab_gen',
  files('unit-crypttab_gen.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-grub_config_gen',
  files('unit-grub_config_gen.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-mtab',
  files('unit-mtab.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-locale',
  files('unit-locale.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-package_profiles',
  files('unit-package_profiles.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-fetch_file',
  files('unit-fetch_file.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-kernel_params',
  files('unit-kernel_params.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-btrfs',
  files('unit-btrfs.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-refind_config_gen',
  files('unit-refind_config_gen.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-limine_config_gen',
  files('unit-limine_config_gen.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-refind_extra_kern_strings',
  files('unit-refind_extra_kern_strings.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-string_utils',
  files('unit-string_utils.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)

executable(
  'test-luks',
  files('unit-luks.cpp'),
  dependencies: [deps, doctest],
  link_with: [gucc_lib, doctest_main_lib],
  include_directories: [include_directories('../include')],
  install: false)
