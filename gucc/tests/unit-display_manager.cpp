#include "doctest_compatibility.h"

#include "gucc/display_manager.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/logger.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;
using gucc::display_manager::Kind;

namespace {

class TempRoot final {
 public:
    TempRoot()
      : m_path(fs::temp_directory_path() / fs::path{fmt::format("gucc-dm-{}", std::random_device{}())}) {
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

void touch_unit(const fs::path& root, std::string_view service_name) {
    const auto units_dir = root / "usr" / "lib" / "systemd" / "system";
    fs::create_directories(units_dir);
    std::ofstream{units_dir / (std::string{service_name} + ".service")};
}

void touch_greeter(const fs::path& root, std::string_view greeter_name) {
    const auto greeters = root / "usr" / "share" / "xgreeters";
    fs::create_directories(greeters);
    std::ofstream{greeters / std::string{greeter_name}};
}

}  // namespace

TEST_CASE("display_manager::to_string and from_string round-trip")
{
    for (const auto k : gucc::display_manager::known_kinds()) {
        const auto name = gucc::display_manager::to_string(k);
        REQUIRE_FALSE(name.empty());
        const auto parsed = gucc::display_manager::from_string(name);
        REQUIRE(parsed.has_value());
        REQUIRE_EQ(*parsed, k);
    }
}

TEST_CASE("display_manager::from_string")
{
    SECTION("known names")
    {
        REQUIRE_EQ(gucc::display_manager::from_string("gdm"sv), std::optional{Kind::Gdm});
        REQUIRE_EQ(gucc::display_manager::from_string("sddm"sv), std::optional{Kind::Sddm});
        REQUIRE_EQ(gucc::display_manager::from_string("cosmic-greeter"sv), std::optional{Kind::CosmicGreeter});
    }
    SECTION("unknown name returns nullopt")
    {
        REQUIRE_FALSE(gucc::display_manager::from_string("xdm"sv).has_value());
        REQUIRE_FALSE(gucc::display_manager::from_string(""sv).has_value());
    }
}

TEST_CASE("display_manager::known_kinds")
{
    const auto kinds = gucc::display_manager::known_kinds();
    REQUIRE_EQ(kinds.size(), 7u);
    REQUIRE_EQ(kinds.front(), Kind::Plasmalogin);
}

TEST_CASE("display_manager::detect_installed")
{
    SECTION("no units installed returns nullopt")
    {
        TempRoot root;
        REQUIRE_FALSE(gucc::display_manager::detect_installed(root.path().string()).has_value());
    }
    SECTION("returns the only installed DM")
    {
        TempRoot root;
        touch_unit(root.path(), "sddm");
        REQUIRE_EQ(gucc::display_manager::detect_installed(root.path().string()),
            std::optional{Kind::Sddm});
    }
    SECTION("respects preference order when multiple DMs are installed")
    {
        TempRoot root;
        touch_unit(root.path(), "gdm");
        touch_unit(root.path(), "lightdm");
        // lightdm is preferred over gdm.
        REQUIRE_EQ(gucc::display_manager::detect_installed(root.path().string()),
            std::optional{Kind::Lightdm});
    }
}

TEST_CASE("display_manager::enable")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("returns false when the unit is not present")
    {
        TempRoot root;
        REQUIRE_FALSE(gucc::display_manager::enable(Kind::Sddm, root.path().string()));
    }
}

TEST_CASE("display_manager::pick_lightdm_greeter")
{
    SECTION("missing directory returns nullopt")
    {
        TempRoot root;
        REQUIRE_FALSE(gucc::display_manager::pick_lightdm_greeter(
            (root.path() / "missing").string()).has_value());
    }
    SECTION("only the default gtk greeter returns nullopt")
    {
        TempRoot root;
        touch_greeter(root.path(), "lightdm-gtk-greeter");
        REQUIRE_FALSE(gucc::display_manager::pick_lightdm_greeter(
            (root.path() / "usr" / "share" / "xgreeters").string()).has_value());
    }
    SECTION("non-greeter files are ignored")
    {
        TempRoot root;
        touch_greeter(root.path(), "some-random-file");
        touch_greeter(root.path(), "lightdm-other-thing");  // missing -greeter suffix
        REQUIRE_FALSE(gucc::display_manager::pick_lightdm_greeter(
            (root.path() / "usr" / "share" / "xgreeters").string()).has_value());
    }
    SECTION("returns the lex-first non-default greeter")
    {
        TempRoot root;
        touch_greeter(root.path(), "lightdm-gtk-greeter");
        touch_greeter(root.path(), "lightdm-webkit2-greeter");
        touch_greeter(root.path(), "lightdm-slick-greeter");
        const auto picked = gucc::display_manager::pick_lightdm_greeter(
            (root.path() / "usr" / "share" / "xgreeters").string());
        REQUIRE(picked.has_value());
        REQUIRE_EQ(*picked, "lightdm-slick-greeter");
    }
}

TEST_CASE("display_manager::configure_lightdm_greeter")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("no greeter installed leaves lightdm.conf untouched")
    {
        TempRoot root;
        const auto conf = root.path() / "etc" / "lightdm" / "lightdm.conf";
        fs::create_directories(conf.parent_path());
        constexpr auto kOriginal = "#greeter-session=example-gtk-gnome\n"sv;
        gucc::file_utils::create_file_for_overwrite(conf.string(), kOriginal);

        REQUIRE(gucc::display_manager::configure_lightdm_greeter(root.path().string()));
        REQUIRE_EQ(gucc::file_utils::read_whole_file(conf.string()), kOriginal);
    }
    SECTION("rewrites a commented greeter-session line in place")
    {
        TempRoot root;
        touch_greeter(root.path(), "lightdm-gtk-greeter");
        touch_greeter(root.path(), "lightdm-slick-greeter");
        const auto conf = root.path() / "etc" / "lightdm" / "lightdm.conf";
        fs::create_directories(conf.parent_path());
        constexpr auto kOriginal = "[Seat:*]\n#greeter-session=example-gtk-gnome\n"sv;
        gucc::file_utils::create_file_for_overwrite(conf.string(), kOriginal);

        REQUIRE(gucc::display_manager::configure_lightdm_greeter(root.path().string()));

        const auto out = gucc::file_utils::read_whole_file(conf.string());
        REQUIRE(out.contains("greeter-session=lightdm-slick-greeter\n"sv));
        REQUIRE_FALSE(out.contains("example-gtk-gnome"sv));
    }
    SECTION("missing lightdm.conf is a no-op success")
    {
        TempRoot root;
        touch_greeter(root.path(), "lightdm-slick-greeter");
        REQUIRE(gucc::display_manager::configure_lightdm_greeter(root.path().string()));
        REQUIRE_FALSE(fs::exists(root.path() / "etc" / "lightdm" / "lightdm.conf"));
    }
}
