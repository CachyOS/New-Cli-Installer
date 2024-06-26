#include "gucc/luks.hpp"
#include "gucc/io_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

namespace gucc::crypto {

auto luks1_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> bool {
    auto cmd = fmt::format(FMT_COMPILE("echo \"{}\" | cryptsetup open --type luks1 {} {} &>/dev/null"), luks_pass, partition, luks_name);
    return utils::exec(cmd, true) == "0";
}

auto luks1_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags) noexcept -> bool {
    auto cmd = fmt::format(FMT_COMPILE("echo \"{}\" | cryptsetup -q {} --type luks1 luksFormat {} &>/dev/null"), luks_pass, additional_flags, partition);
    return utils::exec(cmd, true) == "0";
}

auto luks1_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags) noexcept -> bool {
    auto cmd = fmt::format(FMT_COMPILE("cryptsetup -q {} luksAddKey {} {} &>/dev/null"), additional_flags, partition, dest_file);
    return utils::exec(cmd, true) == "0";
}

}  // namespace gucc::crypto
