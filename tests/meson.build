source_path = '../src/'

test_libreq = static_library('test_libreq',
    sources: [
        source_path + 'global_storage.cpp',
        source_path + 'utils.cpp',
        source_path + 'disk.cpp',
        source_path + 'drivers.cpp',
        source_path + 'widgets.cpp',
        source_path + 'follow_process_log.cpp',
        source_path + 'crypto.cpp',
        source_path + 'misc.cpp',
        source_path + 'installer_data.cpp',
        source_path + 'simple_tui.cpp',
        source_path + 'installer_config.cpp',
        source_path + 'tui.cpp',
    ],
    include_directories: [include_directories(source_path), include_directories('../gucc/include')],
    dependencies: deps,
    link_with: [gucc_lib]
)

top_test_names = [
    'exec-interactive',
    'installer_config',
    'proccess-tailing',
]

foreach t : top_test_names
    test_exe = executable(
        'test-' + t,
        files('unit-' + t + '.cpp'),
        dependencies: [deps, doctest],
        link_with: [test_libreq, gucc_lib],
        include_directories: [include_directories(source_path), include_directories('../gucc/include'), include_directories('../gucc/tests')],
        install: false,
    )
endforeach
