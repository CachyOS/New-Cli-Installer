#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/logger.hpp"

#include <filesystem>
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
    SECTION("set xkbmap layout")
    {
        fs::create_directories(folder_path);

        REQUIRE(gucc::locale::set_xkbmap("us"sv, folder_testpath));

        const auto& keyboard_conf_path = std::string{folder_testpath} + "/etc/X11/xorg.conf.d/00-keyboard.conf";
        REQUIRE(fs::exists(keyboard_conf_path));

        auto content = gucc::file_utils::read_whole_file(keyboard_conf_path);
        REQUIRE(content.contains("XkbLayout"));
        REQUIRE(content.contains("us"));
        REQUIRE(content.contains("InputClass"));

        // Cleanup
        fs::remove_all(folder_testpath);
    }
    SECTION("set xkbmap with variant")
    {
        fs::create_directories(folder_path);

        REQUIRE(gucc::locale::set_xkbmap("de"sv, folder_testpath));

        const auto& keyboard_conf_path = std::string{folder_testpath} + "/etc/X11/xorg.conf.d/00-keyboard.conf";
        REQUIRE(fs::exists(keyboard_conf_path));

        auto content = gucc::file_utils::read_whole_file(keyboard_conf_path);
        REQUIRE(content.contains("de"));

        // Cleanup
        fs::remove_all(folder_testpath);
    }
}

TEST_CASE("parse_locale_gen test")
{
    using gucc::locale::parse_locale_gen;

    SECTION("empty input")
    {
        auto result = parse_locale_gen(""sv);
        CHECK(result.empty());
    }
    SECTION("commented UTF-8 locales")
    {
        constexpr auto content = R"(#en_US.UTF-8 UTF-8
#de_DE.UTF-8 UTF-8
#ru_RU.UTF-8 UTF-8
)"sv;
        auto result = parse_locale_gen(content);
        REQUIRE(result.size() == 3);
        CHECK(result[0] == "en_US.UTF-8");
        CHECK(result[1] == "de_DE.UTF-8");
        CHECK(result[2] == "ru_RU.UTF-8");
    }
    SECTION("uncommented locale")
    {
        constexpr auto content = R"(en_US.UTF-8 UTF-8
)"sv;
        auto result = parse_locale_gen(content);
        REQUIRE(result.size() == 1);
        CHECK(result[0] == "en_US.UTF-8");
    }
    SECTION("filters out non UTF-8 locales")
    {
        constexpr auto content = R"(#en_US ISO-8859-1
#en_US.UTF-8 UTF-8
#de_DE ISO-8859-1
#ru_RU UTF-8
)"sv;
        auto result = parse_locale_gen(content);
        REQUIRE(result.size() == 2);
        CHECK(result[0] == "en_US.UTF-8");
        CHECK(result[1] == "ru_RU");
    }
    SECTION("skips header comments")
    {
        constexpr auto content = R"(# Configuration file for locale-gen
# This is a comment line that should be skip
#  Another comment with leading space
#en_US.UTF-8 UTF-8
)"sv;
        auto result = parse_locale_gen(content);
        REQUIRE(result.size() == 1);
        CHECK(result[0] == "en_US.UTF-8");
    }
    SECTION("mixed content")
    {
        constexpr auto content = R"(# locale.gen header
#  Comment line
#aa_DJ.UTF-8 UTF-8
#aa_DJ ISO-8859-1
en_US.UTF-8 UTF-8
#ru_RU.UTF-8 UTF-8
#ru_RU ISO-8859-5
)"sv;
        auto result = parse_locale_gen(content);
        REQUIRE(result.size() == 3);
        CHECK(result[0] == "aa_DJ.UTF-8");
        CHECK(result[1] == "en_US.UTF-8");
        CHECK(result[2] == "ru_RU.UTF-8");
    }
}
