gucc_lib = library('gucc',
    sources : [
        'src/io_utils.cpp',
        'src/file_utils.cpp',
        'src/string_utils.cpp',
        'src/cpu.cpp',
        'src/pacmanconf_repo.cpp',
        'src/initcpio.cpp',
        'src/luks.cpp',
        'src/zfs.cpp',
        'src/btrfs.cpp',
        'src/user.cpp',
        'src/locale.cpp',
    ],
    include_directories : [include_directories('include')],
    dependencies: deps
)

if is_tests_build
    subdir('tests')
endif
