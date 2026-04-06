#include "gucc/lua/bindings.hpp"

#include "gucc/pacmanconf_repo.hpp"
#include "gucc/repos.hpp"

namespace gucc::lua {

void register_pacmanconf(sol::table& gucc_table) {
    auto pacmanconf                = make_subtable(gucc_table, "pacmanconf");
    pacmanconf["push_repos_front"] = [](std::string_view file_path, std::string_view value) -> bool {
        return gucc::detail::pacmanconf::push_repos_front(file_path, value);
    };
    pacmanconf["get_repo_list"] = [gucc_table](std::string_view file_path) {
        auto list = gucc::detail::pacmanconf::get_repo_list(file_path);
        return sol::as_table(list);
    };

    auto repos                     = make_subtable(gucc_table, "repos");
    repos["install_cachyos_repos"] = []() -> bool {
        return gucc::repos::install_cachyos_repos();
    };
}

}  // namespace gucc::lua
