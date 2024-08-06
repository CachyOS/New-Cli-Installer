#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/logger.hpp"

#include <filesystem>
#include <fstream>
#include <ranges>
#include <string_view>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

static constexpr auto LOCALE_CONF_TEST = R"(LANG="ru_RU.UTF-8"
LC_NUMERIC="ru_RU.UTF-8"
LC_TIME="ru_RU.UTF-8"
LC_MONETARY="ru_RU.UTF-8"
LC_PAPER="ru_RU.UTF-8"
LC_NAME="ru_RU.UTF-8"
LC_ADDRESS="ru_RU.UTF-8"
LC_TELEPHONE="ru_RU.UTF-8"
LC_MEASUREMENT="ru_RU.UTF-8"
LC_IDENTIFICATION="ru_RU.UTF-8"
LC_MESSAGES="ru_RU.UTF-8"
)"sv;

static constexpr auto LOCALE_GEN_TEST = R"(ru_RU.UTF-8 UTF-8
)"sv;

inline auto filtered_res(std::string_view content) noexcept -> std::string {
    auto&& result = content | std::ranges::views::split('\n')
        | std::ranges::views::filter([](auto&& rng) {
              auto&& line = std::string_view(&*rng.begin(), static_cast<size_t>(std::ranges::distance(rng)));
              return !line.empty() && !line.starts_with('#');
          })
        | std::ranges::views::join_with('\n')
        | std::ranges::to<std::string>();
    return result + '\n';
}

TEST_CASE("locale test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    // prepare test data
    static constexpr std::string_view folder_testpath{"/tmp/test-locale-unittest"};
    static constexpr std::string_view folder_path{"/tmp/test-locale-unittest/etc"};
    static constexpr std::string_view dest_locale_gen{"/tmp/test-locale-unittest/etc/locale.gen"};
    static constexpr std::string_view dest_locale_conf{"/tmp/test-locale-unittest/etc/locale.conf"};

    SECTION("set test locale")
    {
        fs::create_directories(folder_path);
        fs::copy_file(GUCC_TEST_DIR "/files/locale.gen", dest_locale_gen, fs::copy_options::overwrite_existing);

        REQUIRE(gucc::locale::prepare_locale_set("ru_RU.UTF-8"sv, folder_testpath));
        auto locale_conf_content = gucc::file_utils::read_whole_file(dest_locale_conf);
        REQUIRE_EQ(locale_conf_content, LOCALE_CONF_TEST);

        auto locale_gen_content = filtered_res(gucc::file_utils::read_whole_file(dest_locale_gen));
        REQUIRE_EQ(locale_gen_content, LOCALE_GEN_TEST);

        // Cleanup.
        fs::remove_all(folder_testpath);
    }
    SECTION("set locale at invalid file path")
    {
        REQUIRE(!gucc::locale::prepare_locale_set("ru_RU.UTF-8"sv, folder_testpath));
    }
}
