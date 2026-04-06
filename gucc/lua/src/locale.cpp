#include "gucc/lua/bindings.hpp"

#include "gucc/hwclock.hpp"
#include "gucc/locale.hpp"
#include "gucc/timezone.hpp"

namespace gucc::lua {

void register_locale(sol::table& gucc_table) {
    auto locale = make_subtable(gucc_table, "locale");

    locale["set_locale"] = [](std::string_view loc, std::string_view mountpoint) -> bool {
        return gucc::locale::set_locale(loc, mountpoint);
    };
    locale["prepare_locale_set"] = [](std::string_view loc, std::string_view mountpoint) -> bool {
        return gucc::locale::prepare_locale_set(loc, mountpoint);
    };
    locale["get_possible_locales"] = []() {
        return sol::as_table(gucc::locale::get_possible_locales());
    };
    locale["set_xkbmap"] = [](std::string_view xkbmap, std::string_view mountpoint) -> bool {
        return gucc::locale::set_xkbmap(xkbmap, mountpoint);
    };
    locale["get_x11_keymap_layouts"] = []() {
        return sol::as_table(gucc::locale::get_x11_keymap_layouts());
    };
    locale["parse_locale_gen"] = [](std::string_view content) {
        return sol::as_table(gucc::locale::parse_locale_gen(content));
    };
    locale["set_keymap"] = [](std::string_view keymap, std::string_view mountpoint) -> bool {
        return gucc::locale::set_keymap(keymap, mountpoint);
    };
    locale["get_vconsole_keymap_list"] = []() {
        return sol::as_table(gucc::locale::get_vconsole_keymap_list());
    };

    auto tz   = make_subtable(gucc_table, "timezone");
    tz["set"] = [](std::string_view timezone, std::string_view mountpoint) -> bool {
        return gucc::timezone::set_timezone(timezone, mountpoint);
    };
    tz["get_available"] = []() {
        return sol::as_table(gucc::timezone::get_available_timezones());
    };
    tz["get_regions"] = []() {
        return sol::as_table(gucc::timezone::get_timezone_regions());
    };
    tz["get_zones"] = [](std::string_view region) {
        return sol::as_table(gucc::timezone::get_timezone_zones(region));
    };

    auto hwclock       = make_subtable(gucc_table, "hwclock");
    hwclock["set_utc"] = [](std::string_view root_mountpoint) -> bool {
        return gucc::hwclock::set_hwclock_utc(root_mountpoint);
    };
    hwclock["set_localtime"] = [](std::string_view root_mountpoint) -> bool {
        return gucc::hwclock::set_hwclock_localtime(root_mountpoint);
    };
}

}  // namespace gucc::lua
