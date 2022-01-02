project('simdjson', 'cpp',
    version : '1.0.2',
    license : 'Apache-2.0',
    default_options : [
      'cpp_std=c++17',
      'default_library=static',
    ]
)

simdjson_lib = library('simdjson',
    sources : ['singleheader/simdjson.cpp'],
    cpp_args : ['-DSIMDJSON_THREADS_ENABLED=1', '-Wno-ambiguous-reversed-operator'],
    include_directories: include_directories('singleheader')
)

simdjson_dep = declare_dependency(
    include_directories: include_directories('singleheader'),
    compile_args : ['-DSIMDJSON_THREADS_ENABLED=1', '-Wno-ambiguous-reversed-operator'],
    link_with : simdjson_lib
)

simdjson_multiple_headers = declare_dependency(
    include_directories: include_directories('include'),
    compile_args : ['-DSIMDJSON_THREADS_ENABLED=1', '-Wno-ambiguous-reversed-operator'],
)
