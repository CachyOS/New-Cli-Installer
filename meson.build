project('cachyos-installer', 'cpp',
        version: '0.8.0',
        license: 'GPLv3',
        meson_version: '>=0.55.0',
        default_options: ['cpp_std=c++23',
                          'buildtype=debugoptimized',
                          'warning_level=3',
                          'werror=false',
                          'b_ndebug=if-release'])

is_debug_build = get_option('buildtype').startswith('debug')
is_dev_environment = get_option('devenv')
is_tests_build = get_option('build_tests')
do_build_static = get_option('build_static')
cc = meson.get_compiler('cpp')
if cc.get_id() == 'clang'
    specific_cc_flags = [
        '-nostdlib++',
        #'-stdlib=libc++',
        #'-nodefaultlibs',
        #'-fexperimental-library',
        '-fstrict-vtable-pointers',
        '-fexperimental-new-pass-manager',
    ]
    #specific_link_flags = [
    #   '-stdlib=libc++',
    #]
    add_global_arguments(cc.get_supported_arguments(specific_cc_flags), language : 'cpp')
    add_global_link_arguments(cc.get_supported_link_arguments(specific_link_flags), language : 'cpp')
endif

if is_debug_build
    add_global_arguments('-D_GLIBCXX_ASSERTIONS', language : 'cpp')
    add_global_arguments('-D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS=1', language : 'cpp')
    add_global_arguments('-D_LIBCPP_ENABLE_ASSERTIONS=1', language : 'cpp')
endif

if not is_dev_environment
    add_global_arguments('-DNDEVENV', language : 'cpp')
endif

add_global_arguments('-DSPDLOG_DISABLE_DEFAULT_LOGGER', language : 'cpp')
add_global_arguments('-DSPDLOG_FMT_EXTERNAL', language : 'cpp')

version_commit_hash = run_command('git', 'rev-parse', '--short', 'HEAD', check: false).stdout().strip()
build_timestamp = run_command('date', '+%Y%m%d', check: true).stdout().strip()

git_version = ''
if version_commit_hash == ''
    git_version = meson.project_version() + '-' + build_timestamp
else
    git_version = meson.project_version() + '-' + version_commit_hash + '-' + build_timestamp
endif
add_global_arguments('-DINSTALLER_VERSION="' + git_version + '"', language : 'cpp')

# gucc test path
add_global_arguments('-DGUCC_TEST_DIR="' + meson.current_source_dir() + '/gucc/tests/"', language : 'cpp')
add_global_arguments('-DGUCC_TOP_DIR="' + meson.current_source_dir() + '/"', language : 'cpp')

# Common dependencies
spdlog = dependency('spdlog', version : ['>=1.12.0'], fallback : ['spdlog', 'spdlog_dep'])
fmt = dependency('fmt', version : ['>=10.1.0'], fallback : ['fmt', 'fmt_dep'])
ftxui = dependency('ftxui', modules : ['ftxui::screen', 'ftxui::dom', 'ftxui::component'], fallback : ['ftxui', 'ftxui_dep'])
rapidjson = dependency('rapidjson', version : ['>=1.1.0'], fallback : ['rapidjson', 'rapidjson_dep'])
ctre = dependency('ctre', version : ['>=3.8.0'], fallback : ['ctre', 'ctre_dep'])
ranges = dependency('range-v3', version : ['>=0.11.0'], fallback : ['range-v3', 'range_dep'])
tomlplusplus = dependency('tomlplusplus', version : ['>=3.4.0'], fallback : ['tomlplusplus', 'tomlplusplus_dep'], default_options: ['compile_library=false'])
#glibmm = dependency('glibmm-2.4', version : ['>=2.56.0'])
cpr = dependency('cpr', version : ['>=1.10.0'], fallback : ['cpr', 'cpr_dep'])

src_files = files(
    'src/view.hpp',
    'src/definitions.hpp',
    'src/config.cpp', 'src/config.hpp',
    'src/utils.cpp', 'src/utils.hpp',
    'src/chwd_profiles.cpp', 'src/chwd_profiles.hpp',
    'src/disk.cpp', 'src/disk.hpp',
    'src/drivers.cpp', 'src/drivers.hpp',
    'src/widgets.cpp', 'src/widgets.hpp',
    'src/follow_process_log.cpp', 'src/follow_process_log.hpp',
    'src/crypto.cpp', 'src/crypto.hpp',
    'src/misc.cpp', 'src/misc.hpp',
    'src/simple_tui.cpp', 'src/simple_tui.hpp',
    'src/tui.cpp', 'src/tui.hpp',
    'src/main.cpp',
)

possible_cc_flags = [
    '-Wshadow',

    '-Wnon-virtual-dtor',

    '-Wold-style-cast',
    '-Wcast-align',
    '-Wunused',
    '-Woverloaded-virtual',

    '-Wpedantic', # non-standard C++
    '-Wconversion', # type conversion that may lose data
    '-Wsign-conversion',
    '-Wnull-dereference',
    '-Wdouble-promotion', # float to double

    '-Wformat=2',
    '-Wimplicit-fallthrough', # fallthrough without an explicit annotation
]

if cc.get_id() == 'gcc'
    possible_cc_flags += [
        '-Wmisleading-indentation',

        '-Wduplicated-cond',
        '-Wduplicated-branches',
        '-Wlogical-op',
        '-Wuseless-cast',
        '-Wno-restrict',

        #'-Wsuggest-attribute=cold',
        #'-Wsuggest-attribute=format',
        '-Wsuggest-attribute=malloc',
        '-Wsuggest-attribute=noreturn',
        '-Wsuggest-attribute=pure',
        '-Wsuggest-final-methods',
        '-Wsuggest-final-types',
        '-Wdiv-by-zero',
        '-Wanalyzer-double-fclose',
        '-Wanalyzer-double-free',
        '-Wanalyzer-malloc-leak',
        '-Wanalyzer-use-after-free',

        ## some more analyzer flags
        '-Wanalyzer-tainted-allocation-size',
        '-Wanalyzer-use-of-uninitialized-value',
        '-Wanalyzer-use-of-pointer-in-stale-stack-frame',
        '-Wanalyzer-free-of-non-heap',
        '-Wanalyzer-mismatching-deallocation',
        '-Wanalyzer-null-dereference',
        '-Wanalyzer-possible-null-dereference',
    ]
endif

if not is_debug_build
   if cc.get_id() == 'gcc'
      possible_cc_flags += [
         '-flto',
         '-fwhole-program',
         '-fuse-linker-plugin',
      ]
   else
      possible_cc_flags += [
         '-flto=thin',
         '-fwhole-program-vtables',
      ]
   endif

   possible_cc_flags += ['-fdata-sections', '-ffunction-sections']
   if do_build_static
      possible_link_flags = ['-Wl,--gc-sections', '-static-libgcc', '-static-libstdc++']
   endif
   add_project_link_arguments(cc.get_supported_link_arguments(possible_link_flags), language : 'cpp')
endif

add_project_arguments(cc.get_supported_arguments(possible_cc_flags), language : 'cpp')

deps = [fmt, spdlog, ftxui, ranges, rapidjson, ctre, tomlplusplus, cpr]

subdir('gucc')

executable(
  'cachyos-installer',
  src_files,
  dependencies: deps,
  link_with: gucc_lib,
  include_directories: [include_directories('src'), include_directories('gucc/include')],
  install: true)

install_data(
  'src/install-repo.awk',
  install_dir: '/usr/share/cachyos-installer'
)

if is_tests_build
    subdir('tests')
endif

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
