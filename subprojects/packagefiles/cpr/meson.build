project('cpr', 'cpp',
    version: '1.10.4',
    license : 'MIT',
    default_options: ['cpp_std=c++20']
)

curl_dep = dependency('libcurl', required : true)

src_inc = include_directories('.')
cpr_inc = include_directories('include')
cpr_lib = static_library('cpr',
    sources : [
      'cpr/accept_encoding.cpp',
      'cpr/async.cpp',
      'cpr/auth.cpp',
      'cpr/bearer.cpp',
      'cpr/callback.cpp',
      'cpr/cert_info.cpp',
      'cpr/cookies.cpp',
      'cpr/cprtypes.cpp',
      'cpr/curl_container.cpp',
      'cpr/curlholder.cpp',
      'cpr/error.cpp',
      'cpr/file.cpp',
      'cpr/multipart.cpp',
      'cpr/parameters.cpp',
      'cpr/payload.cpp',
      'cpr/proxies.cpp',
      'cpr/proxyauth.cpp',
      'cpr/session.cpp',
      'cpr/threadpool.cpp',
      'cpr/timeout.cpp',
      'cpr/unix_socket.cpp',
      'cpr/util.cpp',
      'cpr/response.cpp',
      'cpr/redirect.cpp',
      'cpr/interceptor.cpp',
      'cpr/ssl_ctx.cpp',
      'cpr/curlmultiholder.cpp',
      'cpr/multiperform.cpp'
    ],
    include_directories : [cpr_inc, src_inc],
    dependencies : [curl_dep]
)

cpr_dep = declare_dependency(
    include_directories : [cpr_inc, src_inc],
    link_with : [cpr_lib]
)
