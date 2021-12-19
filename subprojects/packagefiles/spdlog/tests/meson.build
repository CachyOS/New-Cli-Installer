test_src = [
    'test_file_helper.cpp',
    'test_file_logging.cpp',
    'test_daily_logger.cpp',
    'test_misc.cpp',
    'test_eventlog.cpp',
    'test_pattern_formatter.cpp',
    'test_async.cpp',
    'test_registry.cpp',
    'test_macros.cpp',
    'utils.cpp',
    'main.cpp',
    'test_mpmc_q.cpp',
    'test_dup_filter.cpp',
    'test_fmt_helper.cpp',
    'test_stdout_api.cpp',
    'test_backtrace.cpp',
    'test_create_dir.cpp',
    'test_cfg.cpp',
    'test_time_point.cpp',
    'test_stopwatch.cpp'
]

tests_exe = executable('tests_exe', test_src, dependencies : [spdlog_dep])
test('tests', tests_exe)
