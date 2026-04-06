#include "gucc/lua/bindings.hpp"

#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

namespace gucc::lua {

void register_file_utils(sol::table& gucc_table) {
    auto fu = make_subtable(gucc_table, "file_utils");

    fu["read_whole_file"] = [](std::string_view filepath) -> std::string {
        return gucc::file_utils::read_whole_file(filepath);
    };

    fu["write_to_file"] = [](std::string_view data, std::string_view filepath) -> bool {
        return gucc::file_utils::write_to_file(data, filepath);
    };

    fu["create_file_for_overwrite"] = [](std::string_view filepath, std::string_view data) -> bool {
        return gucc::file_utils::create_file_for_overwrite(filepath, data);
    };

    auto su = make_subtable(gucc_table, "string_utils");

    su["make_multiline"] = [gucc_table](std::string_view str_obj, sol::object reverse_obj, sol::object delim_obj) {
        sol::state_view lua(gucc_table.lua_state());
        bool reverse = false;
        if (reverse_obj.is<bool>()) {
            reverse = reverse_obj.as<bool>();
        }
        char delim = '\n';
        if (delim_obj.is<std::string>()) {
            auto d = delim_obj.as<std::string>();
            if (!d.empty()) {
                delim = d[0];
            }
        }
        return sol::as_table(gucc::utils::make_multiline(str_obj, reverse, delim));
    };

    su["join"] = [](sol::table lines_table, sol::object delim_obj) -> std::string {
        std::vector<std::string> lines;
        lines_table.for_each([&](sol::object, sol::object val) {
            if (val.is<std::string>()) {
                lines.push_back(val.as<std::string>());
            }
        });
        char delim = '\n';
        if (delim_obj.is<std::string>()) {
            auto d = delim_obj.as<std::string>();
            if (!d.empty()) {
                delim = d[0];
            }
        }
        return gucc::utils::join(lines, delim);
    };

    su["trim"] = [](std::string_view str) -> std::string {
        return std::string{gucc::utils::trim(str)};
    };

    su["ltrim"] = [](std::string_view str) -> std::string {
        return std::string{gucc::utils::ltrim(str)};
    };

    su["rtrim"] = [](std::string_view str) -> std::string {
        return std::string{gucc::utils::rtrim(str)};
    };

    su["contains"] = [](std::string_view str, std::string_view needle) -> bool {
        return gucc::utils::contains(str, needle);
    };
}

}  // namespace gucc::lua
