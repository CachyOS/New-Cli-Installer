project('cachyos-installer', 'cpp',
        version: '0.0.1',
        license: 'GPLv3',
        meson_version: '>=0.55.0',
        default_options: ['cpp_std=c++20',
                          'buildtype=debugoptimized',
                          'warning_level=3',
                          'werror=true',
                          'b_ndebug=if-release'])

cc = meson.get_compiler('cpp')

# Common dependencies
fmt = dependency('fmt', version : ['>=8.0.0'], fallback : ['fmt', 'fmt_dep'])
ftxui = dependency('ftxui', version : ['>=0.11.1'], modules : ['ftxui::screen', 'ftxui::dom', 'ftxui::component'], fallback : ['ftxui', 'ftxui_dep'])
nlohmann_json = dependency('nlohmann_json', version : ['>=3.10.4'], fallback : ['nlohmann_json', 'nlohmann_json_dep'])

src_files = files(
    'src/definitions.hpp',
    'src/utils.cpp', 'src/utils.hpp',
    'src/main.cpp',
)

possible_cc_flags = [
        '-Wshadow',

        '-Wnon-virtual-dtor',

        '-Wcast-align',
        '-Wunused',
        '-Woverloaded-virtual',

        '-Wpedantic', # non-standard C++
        '-Wconversion', # type conversion that may lose data
        '-Wnull-dereference',
        '-Wdouble-promotion', # float to double

        '-Wformat=2',
]

if cc.get_id() == 'gcc'
        possible_cc_flags += [
                '-Wmisleading-indentation',

                '-Wduplicated-cond',
                '-Wduplicated-branches',
                '-Wlogical-op',
        ]
endif

if get_option('buildtype') != 'debug'
   if cc.get_id() == 'gcc'
      possible_cc_flags += [
         '-flto',
         '-fwhole-program',
      ]
   else
      possible_cc_flags += [
         '-flto=thin',
      ]
   endif
endif

add_project_arguments(cc.get_supported_arguments(possible_cc_flags), language : 'cpp')

executable(
  'cachyos-installer',
  src_files,
  dependencies: [fmt, ftxui, nlohmann_json],
  include_directories: [include_directories('src')],
  install: true)

summary(
  {
    'Build type': get_option('buildtype'),
  },
  bool_yn: true
)

clangtidy = find_program('clang-tidy', required: false)

if clangtidy.found()
    run_target(
        'tidy',
        command: [
            clangtidy,
            '-checks=*,-fuchsia-default-arguments',
            '-p', meson.build_root()
        ] + src_files)
endif
