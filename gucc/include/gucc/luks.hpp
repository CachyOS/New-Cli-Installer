#ifndef LUKS_HPP
#define LUKS_HPP

#include <cstdint>      // for int32_t
#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::crypto {

enum class LuksVersion : std::int32_t { Luks1 = 1, Luks2 = 2 };

struct Tpm2Config final {
    std::string pcrs{"0,2,4,7"};  // Default secure PCRs
    std::string device{"auto"};
};

// LUKS1 functions
auto luks1_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> bool;
auto luks1_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags = {}) noexcept -> bool;
auto luks1_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags = {}) noexcept -> bool;
auto luks1_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags = {}) noexcept -> bool;

// LUKS2 functions
auto luks2_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> bool;
auto luks2_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags = {}) noexcept -> bool;
auto luks2_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags = {}) noexcept -> bool;

// TPM2 functions
auto tpm2_available() noexcept -> bool;
auto tpm2_enroll(std::string_view partition, std::string_view passphrase, const Tpm2Config& config) noexcept -> bool;

}  // namespace gucc::crypto

#endif  // LUKS_HPP
