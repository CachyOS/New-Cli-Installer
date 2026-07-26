#include "cachyos/logging.hpp"

// import gucc
#include "gucc/logger.hpp"

#include <chrono>       // for seconds
#include <memory>       // for make_shared, shared_ptr
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move
#include <vector>       // for erase, vector

#include <spdlog/async.h>                     // for create_async
#include <spdlog/common.h>                    // for memory_buf_t, sink_ptr, level
#include <spdlog/details/log_msg.h>           // for log_msg
#include <spdlog/pattern_formatter.h>         // for pattern_formatter
#include <spdlog/sinks/basic_file_sink.h>     // for basic_file_sink_mt
#include <spdlog/sinks/callback_sink.h>       // for callback_sink_mt
#include <spdlog/sinks/stdout_color_sinks.h>  // for stdout_color_sink_mt
#include <spdlog/spdlog.h>                    // for set_default_logger, set_level

namespace {
constinit std::shared_ptr<spdlog::logger> g_logger;
constinit spdlog::sink_ptr g_stdout_sink;
constinit spdlog::sink_ptr g_callback_sink;

void remove_sink(spdlog::sink_ptr& sink) noexcept {
    if (!g_logger || !sink) {
        return;
    }
    auto& sinks = g_logger->sinks();
    std::erase(sinks, sink);
    sink.reset();
}
}  // namespace

namespace cachyos::installer::logging {

auto init(std::string_view log_file) -> std::shared_ptr<spdlog::logger> {
    g_logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>("cachyos_logger", std::string{log_file});
    spdlog::set_default_logger(g_logger);
    spdlog::set_pattern(std::string{kPattern});
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(5));

#ifndef COS_BUILD_STATIC
    // Set gucc logger.
    gucc::logger::set_logger(g_logger);
#endif
    return g_logger;
}

void attach_stdout_sink() noexcept {
    if (!g_logger || g_stdout_sink) {
        return;
    }
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_pattern(std::string{kPattern});
    g_stdout_sink = sink;
    g_logger->sinks().push_back(std::move(sink));
}

void detach_stdout_sink() noexcept {
    remove_sink(g_stdout_sink);
}

void attach_callback_sink(std::function<void(std::string_view)> on_line) noexcept {
    if (!g_logger || !on_line) {
        return;
    }
    detach_callback_sink();

    // format records with same pattern
    auto formatter = std::make_shared<spdlog::pattern_formatter>(std::string{kPattern});
    auto sink      = std::make_shared<spdlog::sinks::callback_sink_mt>(
        [on_line = std::move(on_line), formatter = std::move(formatter)](const spdlog::details::log_msg& msg) {
            spdlog::memory_buf_t buf;
            formatter->format(msg, buf);
            std::string_view formatted{buf.data(), buf.size()};
            const auto last = formatted.find_last_not_of("\r\n");
            on_line(last == std::string_view::npos ? std::string_view{} : formatted.substr(0, last + 1));
        });
    g_callback_sink = sink;
    g_logger->sinks().push_back(std::move(sink));
}

void detach_callback_sink() noexcept {
    remove_sink(g_callback_sink);
}

}  // namespace cachyos::installer::logging
