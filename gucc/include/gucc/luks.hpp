#ifndef LUKS_HPP
#define LUKS_HPP

#include <string_view>  // for string_view

namespace gucc::crypto {

auto luks1_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> bool;
auto luks1_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags = {}) noexcept -> bool;
auto luks1_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags = {}) noexcept -> bool;
auto luks1_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags = {}) noexcept -> bool;

}  // namespace gucc::crypto

#endif  // LUKS_HPP
