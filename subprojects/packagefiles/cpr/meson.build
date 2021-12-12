project('cpr', 'cpp',
    version: '1.7.2',
    license : 'MIT',
    default_options: ['cpp_std=c++11']
)

curl_dep = dependency('libcurl', required : true)

src_inc = include_directories('.')
cpr_inc = include_directories('include')
cpr_lib = static_library('cpr',
    sources : [
      'cpr/auth.cpp',
      'cpr/bearer.cpp',
      'cpr/cookies.cpp',
      'cpr/cprtypes.cpp',
      'cpr/curl_container.cpp',
      'cpr/curlholder.cpp',
      'cpr/error.cpp',
      'cpr/multipart.cpp',
      'cpr/parameters.cpp',
      'cpr/payload.cpp',
      'cpr/proxies.cpp',
      'cpr/proxyauth.cpp',
      'cpr/session.cpp',
      'cpr/timeout.cpp',
      'cpr/unix_socket.cpp',
      'cpr/util.cpp',
      'cpr/response.cpp',
      'cpr/redirect.cpp'
    ],
    include_directories : [cpr_inc, src_inc],
    dependencies : [curl_dep]
)

cpr_dep = declare_dependency(
    include_directories : [cpr_inc, src_inc],
    link_with : [cpr_lib]
)
