project('fmt', 'cpp',
    version : '10.0.0',
    license : 'BSD',
    default_options : [
      'cpp_std=c++20',
      'default_library=static',
    ]
)

fmt_private_cpp_args = [ ]
fmt_interface_cpp_args = [ ]
libtype = get_option('default_library')
if libtype == 'shared'
    fmt_private_cpp_args += [ '-DFMT_EXPORT' ]
    fmt_interface_cpp_args += [ '-DFMT_SHARED' ]
endif

fmt_inc = include_directories('include')
fmt_lib = library('fmt',
    sources : [
        'src/format.cc',
        'src/os.cc'
    ],
    cpp_args : fmt_private_cpp_args,
    include_directories : fmt_inc
)

fmt_dep = declare_dependency(
    include_directories : fmt_inc,
    compile_args : fmt_interface_cpp_args,
    link_with : fmt_lib
)

fmt_header_only_dep = declare_dependency(
    include_directories : fmt_inc,
    compile_args : '-DFMT_HEADER_ONLY'
)
