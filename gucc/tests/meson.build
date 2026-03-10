doctest_main_lib = static_library('doctest_main',
    sources: ['unit.cpp'],
    include_directories: [include_directories('.')],
    dependencies: doctest
)

gucc_test_names = [
    'block_devices',
    'btrfs',
    'crypto_detection',
    'crypttab_gen',
    'fetch_file',
    'fstab_gen',
    'grub_config_gen',
    'initcpio',
    'kernel_params',
    'limine_config_gen',
    'locale',
    'lvm',
    'mtab',
    'package_profiles',
    'pacmanconf',
    'partitioning_gen',
    'refind_config_gen',
    'refind_extra_kern_strings',
    'string_utils',
    'system_query',
    'systemd_homed',
    'systemd_repart',
    'timezone',
]

foreach t : gucc_test_names
    test_exe = executable(
        'test-' + t,
        files('unit-' + t + '.cpp'),
        dependencies: [deps, doctest],
        link_with: [gucc_lib, doctest_main_lib],
        include_directories: [include_directories('../include')],
        install: false,
    )
    test('test-' + t, test_exe)
endforeach
