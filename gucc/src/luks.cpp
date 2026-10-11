#include "gucc/luks.hpp"
#include "gucc/error.hpp"
#include "gucc/io_utils.hpp"

#include <filesystem>  // for exists
#include <string>      // for string

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace {

constexpr auto luks_version_name(gucc::crypto::LUKSVersion v) noexcept -> std::string_view {
    return v == gucc::crypto::LUKSVersion::V1 ? "luks1" : "luks2";
}

auto run_checked(const std::string& cmd, std::string context) noexcept -> gucc::Result<void> {
    if (!gucc::utils::exec_checked(cmd)) {
        return gucc::make_error(gucc::ErrorCode::SubprocessFailed, std::move(context));
    }
    return {};
}

// see https://wiki.archlinux.org/title/Dm-crypt/Device_encryption#With_a_keyfile_embedded_in_the_initramfs
auto setup_keyfile_impl(std::string_view dest_file, std::string_view mountpoint,
    std::string_view partition, std::string_view additional_flags) noexcept -> gucc::Result<void> {
    namespace fs = std::filesystem;

    // Generate keyfile if it doesn't exist yet.
    if (!fs::exists(dest_file)) {
        auto cmd = fmt::format(FMT_COMPILE("dd bs=512 count=4 if=/dev/urandom of={} iflag=fullblock"), dest_file);
        if (auto res = run_checked(cmd, fmt::format("failed to generate keyfile {}", dest_file)); !res) {
            spdlog::error("Failed to generate(dd) luks keyfile: {}!", dest_file);
            return res;
        }
        spdlog::info("Generating a keyfile");
    }

    // Set permissions to 600.
    gucc::utils::exec_checked(fmt::format(FMT_COMPILE("chmod 600 {}"), dest_file));

    // Add keyfile to LUKS.
    spdlog::info("Adding the keyfile to the LUKS configuration");
    if (auto res = gucc::crypto::luks_add_key(gucc::crypto::LUKSVersion::V2, dest_file, partition, additional_flags); !res) {
        spdlog::error("Something went wrong with adding the LUKS key. Is {} the right partition?", partition);
        return res;
    }

    // Add keyfile to initcpio.
    const auto mkinitcpio_conf = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);
    const auto cmd = fmt::format(FMT_COMPILE("grep -q '/crypto_keyfile.bin' {0} || sed -i '/FILES/ s~)~/crypto_keyfile.bin)~' {0}"), mkinitcpio_conf);
    if (auto res = run_checked(cmd, fmt::format("failed to add keyfile to {}", mkinitcpio_conf)); !res) {
        spdlog::error("Failed to add keyfile to {}", mkinitcpio_conf);
        return res;
    }

    spdlog::info("Adding keyfile to the initcpio");
    gucc::utils::arch_chroot("mkinitcpio -P", mountpoint);
    return {};
}

}  // namespace

namespace gucc::crypto {

auto luks_open(LUKSVersion version, std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void> {
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup open --type {} {} {}"),
        luks_pass, luks_version_name(version), partition, luks_name);
    return run_checked(cmd, fmt::format("failed to open {} partition {}", luks_version_name(version), partition));
}

auto luks_format(LUKSVersion version, std::string_view luks_pass, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup -q {} --type {} luksFormat {}"),
        luks_pass, additional_flags, luks_version_name(version), partition);
    return run_checked(cmd, fmt::format("failed to format {} partition {}", luks_version_name(version), partition));
}

auto luks_add_key(LUKSVersion version, std::string_view dest_file, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    auto cmd = fmt::format(FMT_COMPILE("cryptsetup -q {} luksAddKey {} {}"),
        additional_flags, partition, dest_file);
    return run_checked(cmd, fmt::format("failed to add luks key to {}", partition));
}

auto luks_setup_keyfile(LUKSVersion version, std::string_view dest_file, std::string_view mountpoint,
    std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    return setup_keyfile_impl(dest_file, mountpoint, partition, additional_flags);
}

// Convenience wrappers.

auto luks1_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void> {
    return luks_open(LUKSVersion::V1, luks_pass, partition, luks_name);
}

auto luks1_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    return luks_format(LUKSVersion::V1, luks_pass, partition, additional_flags);
}

auto luks1_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    return luks_add_key(LUKSVersion::V1, dest_file, partition, additional_flags);
}

auto luks1_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    return setup_keyfile_impl(dest_file, mountpoint, partition, additional_flags);
}

auto luks2_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void> {
    return luks_open(LUKSVersion::V2, luks_pass, partition, luks_name);
}

auto luks2_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    return luks_format(LUKSVersion::V2, luks_pass, partition, additional_flags);
}

auto luks2_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    return luks_add_key(LUKSVersion::V2, dest_file, partition, additional_flags);
}

auto luks2_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    return setup_keyfile_impl(dest_file, mountpoint, partition, additional_flags);
}

}  // namespace gucc::crypto
