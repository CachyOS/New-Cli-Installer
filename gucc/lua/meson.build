lua_dep = dependency('lua', version : ['>=5.3'], required : false)
if not lua_dep.found()
    lua_dep = dependency('lua5.4', required : false)
endif
if not lua_dep.found()
    lua_dep = dependency('lua5.3', required : false)
endif
if not lua_dep.found()
    lua_dep = dependency('luajit', required : false)
endif
if not lua_dep.found()
    error('Lua dependency not found. Install lua 5.3+/5.4 or LuaJIT, or disable build_lua_bindings.')
endif

sol2_dep = dependency('sol2', fallback : ['sol2', 'sol2_dep'])

gucc_lua_lib = library('gucc-lua',
    sources : [
        'src/bindings.cpp',
        'src/pacmanconf.cpp',
        'src/initcpio.cpp',
        'src/fstab.cpp',
        'src/bootloader.cpp',
        'src/user.cpp',
        'src/locale.cpp',
        'src/partitioning.cpp',
        'src/kernel_params.cpp',
        'src/services.cpp',
        'src/mount.cpp',
        'src/file_utils.cpp',
        'src/package_profiles.cpp',
        'src/cpu.cpp',
    ],
    include_directories : [
        include_directories('include'),
        include_directories('../include'),
    ],
    dependencies: [deps, lua_dep, sol2_dep],
    link_with: gucc_lib,
    install: true,
)
