#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/keyboard.hpp"
#include "gucc/logger.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <system_error>

#include <fmt/format.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {

class TempRoot final {
 public:
    TempRoot()
      : m_path(fs::temp_directory_path() / fs::path{fmt::format("gucc-keyboard-{}", std::random_device{}())}) {
        fs::create_directories(m_path);
    }
    ~TempRoot() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempRoot(const TempRoot&)                    = delete;
    auto operator=(const TempRoot&) -> TempRoot& = delete;
    TempRoot(TempRoot&&)                         = delete;
    auto operator=(TempRoot&&) -> TempRoot&      = delete;

    [[nodiscard]] auto path() const noexcept -> const fs::path& { return m_path; }

 private:
    fs::path m_path;
};

}  // namespace

TEST_CASE("keyboard::make_vconsole_conf")
{
    SECTION("keymap only (font defaults to nullopt)")
    {
        REQUIRE_EQ(gucc::keyboard::make_vconsole_conf("us"sv), "KEYMAP=us\n");
    }
    SECTION("keymap with font")
    {
        REQUIRE_EQ(gucc::keyboard::make_vconsole_conf("de-latin1"sv, "lat9w-16"sv),
            "KEYMAP=de-latin1\nFONT=lat9w-16\n");
    }
    SECTION("explicit nullopt font is omitted")
    {
        REQUIRE_EQ(gucc::keyboard::make_vconsole_conf("us"sv, std::nullopt), "KEYMAP=us\n");
    }
    SECTION("engaged-but-empty font emits empty FONT= line")
    {
        REQUIRE_EQ(gucc::keyboard::make_vconsole_conf("us"sv, std::optional<std::string_view>{""sv}),
            "KEYMAP=us\nFONT=\n");
    }
}

TEST_CASE("keyboard::make_x11_keymap_conf")
{
    SECTION("layout only")
    {
        const auto body = gucc::keyboard::make_x11_keymap_conf("us"sv);
        REQUIRE_EQ(body, R"(Section "InputClass"
    Identifier "system-keyboard"
    MatchIsKeyboard "on"
    Option "XkbLayout" "us"
EndSection
)");
    }
    SECTION("full xkb triple")
    {
        const auto body = gucc::keyboard::make_x11_keymap_conf("us"sv, "pc105"sv, "intl"sv, "ctrl:nocaps"sv);
        REQUIRE_EQ(body, R"(Section "InputClass"
    Identifier "system-keyboard"
    MatchIsKeyboard "on"
    Option "XkbLayout" "us"
    Option "XkbModel" "pc105"
    Option "XkbVariant" "intl"
    Option "XkbOptions" "ctrl:nocaps"
EndSection
)");
    }
    SECTION("nullopt fields are skipped")
    {
        const auto body = gucc::keyboard::make_x11_keymap_conf(
            "us"sv, std::nullopt, "intl"sv, std::nullopt);
        REQUIRE_EQ(body, R"(Section "InputClass"
    Identifier "system-keyboard"
    MatchIsKeyboard "on"
    Option "XkbLayout" "us"
    Option "XkbVariant" "intl"
EndSection
)");
    }
}

TEST_CASE("keyboard::apply")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("writes vconsole.conf and X11 keymap conf")
    {
        TempRoot root;
        const gucc::keyboard::Config cfg{
            .console_keymap = "us",
            .console_font   = "lat9w-16",
            .x11_layout     = "us",
            .x11_model      = "pc105",
            // x11_variant left as default nullopt — should be skipped in output
            .x11_options    = "ctrl:nocaps",
        };
        REQUIRE(gucc::keyboard::apply(cfg, root.path().string()));

        const auto vconsole = gucc::file_utils::read_whole_file((root.path() / "etc" / "vconsole.conf").string());
        REQUIRE_EQ(vconsole, "KEYMAP=us\nFONT=lat9w-16\n");

        const auto xorg = gucc::file_utils::read_whole_file(
            (root.path() / "etc" / "X11" / "xorg.conf.d" / "00-keyboard.conf").string());
        CHECK(xorg.contains("Option \"XkbLayout\" \"us\""));
        CHECK(xorg.contains("Option \"XkbModel\" \"pc105\""));
        REQUIRE_FALSE(xorg.contains("Option \"XkbVariant\""));
        CHECK(xorg.contains("Option \"XkbOptions\" \"ctrl:nocaps\""));
    }
    SECTION("nullopt console_keymap skips vconsole.conf")
    {
        TempRoot root;
        const gucc::keyboard::Config cfg{
            .x11_layout = "us",
        };
        REQUIRE(gucc::keyboard::apply(cfg, root.path().string()));

        CHECK_FALSE(fs::exists(root.path() / "etc" / "vconsole.conf"));
        CHECK(fs::exists(root.path() / "etc" / "X11" / "xorg.conf.d" / "00-keyboard.conf"));
    }
    SECTION("nullopt x11_layout skips X11 conf")
    {
        TempRoot root;
        const gucc::keyboard::Config cfg{
            .console_keymap = "us",
        };
        REQUIRE(gucc::keyboard::apply(cfg, root.path().string()));

        CHECK(fs::exists(root.path() / "etc" / "vconsole.conf"));
        CHECK_FALSE(fs::exists(root.path() / "etc" / "X11" / "xorg.conf.d" / "00-keyboard.conf"));
    }
    SECTION("default Config (all nullopt) succeeds and writes nothing")
    {
        TempRoot root;
        REQUIRE(gucc::keyboard::apply({}, root.path().string()));
        CHECK_FALSE(fs::exists(root.path() / "etc"));
    }
    SECTION("overwrites existing files")
    {
        TempRoot root;
        fs::create_directories(root.path() / "etc");
        gucc::file_utils::create_file_for_overwrite(
            (root.path() / "etc" / "vconsole.conf").string(), "KEYMAP=stale\n");

        const gucc::keyboard::Config cfg{.console_keymap = "us"};
        REQUIRE(gucc::keyboard::apply(cfg, root.path().string()));

        REQUIRE_EQ(gucc::file_utils::read_whole_file((root.path() / "etc" / "vconsole.conf").string()), "KEYMAP=us\n");
    }
}
