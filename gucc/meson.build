gucc_lib = library('gucc',
    sources : [
        'src/io_utils.cpp',
        'src/file_utils.cpp',
        'src/string_utils.cpp',
        'src/fs_utils.cpp',
        'src/cpu.cpp',
        'src/pacmanconf_repo.cpp',
        'src/initcpio.cpp',
        'src/luks.cpp',
        'src/zfs.cpp',
        'src/btrfs.cpp',
        'src/user.cpp',
        'src/locale.cpp',
        'src/fstab.cpp',
        'src/crypttab.cpp',
        'src/bootloader.cpp',
        'src/systemd_services.cpp',
        'src/autologin.cpp',
        'src/mtab.cpp',
        'src/umount_partitions.cpp',
    ],
    include_directories : [include_directories('include')],
    dependencies: deps
)

if is_tests_build
    subdir('tests')
endif
