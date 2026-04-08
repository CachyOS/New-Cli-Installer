#include "cachyos/logging.hpp"

// import gucc
#include "gucc/logger.hpp"

#include <chrono>       // for seconds
#include <memory>       // for make_shared, shared_ptr
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move

#include <spdlog/async.h>                     // for create_async
#include <spdlog/common.h>                    // for memory_buf_t, level
#include <spdlog/details/log_msg.h>           // for log_msg
#include <spdlog/pattern_formatter.h>         // for pattern_formatter
#include <spdlog/sinks/basic_file_sink.h>     // for basic_file_sink_mt
#include <spdlog/sinks/callback_sink.h>       // for callback_sink_mt
#include <spdlog/sinks/stdout_color_sinks.h>  // for stdout_color_sink_mt
#include <spdlog/spdlog.h>                     // for set_default_logger, ...

namespace cachyos::installer::logging {

namespace {
// The single shared logger created by init(); sinks are attached to it.
std::shared_ptr<spdlog::logger> g_logger;
}  // namespace

auto init(std::string_view log_file) -> std::shared_ptr<spdlog::logger> {
    g_logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>("cachyos_logger", std::string{log_file});
    spdlog::set_default_logger(g_logger);
    spdlog::set_pattern(std::string{kPattern});
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(5));

#ifndef COS_BUILD_STATIC
    // Route gucc's logging (and command output) through the same logger. In a
    // static build gucc shares this process's spdlog instance, so the
    // set_default_logger above already covers it (and set_logger is not built).
    gucc::logger::set_logger(g_logger);
#endif
    return g_logger;
}

void attach_stdout_sink() noexcept {
    if (!g_logger) {
        return;
    }
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_pattern(std::string{kPattern});
    g_logger->sinks().push_back(std::move(sink));
}

void attach_callback_sink(std::function<void(std::string_view)> on_line) noexcept {
    if (!g_logger || !on_line) {
        return;
    }
    // Format each record with the shared decorated pattern so the consumer sees
    // exactly what the file/stdout sinks contain. The callback sink serialises
    // calls under its own mutex, so reusing one formatter is safe.
    auto formatter = std::make_shared<spdlog::pattern_formatter>(std::string{kPattern});
    auto sink      = std::make_shared<spdlog::sinks::callback_sink_mt>(
        [on_line = std::move(on_line), formatter = std::move(formatter)](const spdlog::details::log_msg& msg) {
            spdlog::memory_buf_t buf;
            formatter->format(msg, buf);
            std::string_view formatted{buf.data(), buf.size()};
            while (!formatted.empty() && (formatted.back() == '\n' || formatted.back() == '\r')) {
                formatted.remove_suffix(1);
            }
            on_line(formatted);
        });
    g_logger->sinks().push_back(std::move(sink));
}

}  // namespace cachyos::installer::logging
