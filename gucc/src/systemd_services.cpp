#include "gucc/systemd_services.hpp"
#include "gucc/io_utils.hpp"

#include <filesystem>   // for exists
#include <string>       // for string
#include <string_view>  // for string_view

#include <fmt/compile.h>
#include <fmt/format.h>

namespace fs = std::filesystem;

using namespace std::string_view_literals;

namespace {

enum class Scope : bool {
    System,
    Global,
};

auto systemctl(std::string_view verb, Scope scope, std::string_view service_name, std::string_view root_mountpoint) noexcept -> gucc::Result<void> {
    const bool global = scope == Scope::Global;
    const auto cmd    = fmt::format(FMT_COMPILE("systemctl {}{} {}"), global ? "--global "sv : ""sv, verb, service_name);
    return gucc::utils::arch_chroot_or_error(cmd, root_mountpoint, verb);
}

}  // namespace

namespace gucc::services {

auto enable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> Result<void> {
    return systemctl("enable"sv, Scope::System, service_name, root_mountpoint);
}

auto disable_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> Result<void> {
    return systemctl("disable"sv, Scope::System, service_name, root_mountpoint);
}

auto enable_user_systemd_service(std::string_view service_name, std::string_view root_mountpoint) noexcept -> Result<void> {
    return systemctl("enable"sv, Scope::Global, service_name, root_mountpoint);
}

auto systemd_unit_exists(std::string_view unit_name, std::string_view root_mountpoint) noexcept -> bool {
    auto full_name = std::string{unit_name};
    if (!unit_name.contains('.')) {
        full_name += ".service";
    }
    return fs::exists(fmt::format(FMT_COMPILE("{}/usr/lib/systemd/system/{}"), root_mountpoint, full_name));
}

}  // namespace gucc::services
