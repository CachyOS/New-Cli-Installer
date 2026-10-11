#include "gucc/luks.hpp"
#include "gucc/error.hpp"
#include "gucc/io_utils.hpp"

#include <algorithm>   // for contains
#include <filesystem>  // for exists
#include <string>      // for string
#include <utility>     // for move

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

// NOLINTNEXTLINE
using namespace gucc;

namespace fs = std::filesystem;

constexpr auto version_name(crypto::LuksVersion version) noexcept -> std::string_view {
    return version == crypto::LuksVersion::Luks1 ? "luks1"sv : "luks2"sv;
}

// cryptsetup reads a non-tty passphrase up to the first newline
auto run_with_passphrase(std::string_view cmd, std::string_view passphrase, std::string_view what) noexcept -> Result<void> {
    return utils::exec_or_error_with_input(cmd, fmt::format(FMT_COMPILE("{}\n"), passphrase), what);
}

}  // namespace

namespace gucc::crypto {

auto luks_open(LuksVersion version, std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void> {
    const auto name = version_name(version);
    auto cmd        = fmt::format(FMT_COMPILE("cryptsetup open --type {} {} {}"), name, partition, luks_name);
    return run_with_passphrase(cmd, luks_pass, "open luks partition"sv);
}

auto luks_format(LuksVersion version, std::string_view luks_pass, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    const auto name = version_name(version);
    auto cmd        = fmt::format(FMT_COMPILE("cryptsetup -q {} --type {} luksFormat {}"), additional_flags, name, partition);
    return run_with_passphrase(cmd, luks_pass, "format luks partition"sv);
}

auto luks_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    auto cmd = fmt::format(FMT_COMPILE("cryptsetup -q {} luksAddKey {} {}"), additional_flags, partition, dest_file);
    return utils::exec_or_error(cmd, "add luks key"sv);
}

// see https://wiki.archlinux.org/title/Dm-crypt/Device_encryption#With_a_keyfile_embedded_in_the_initramfs
auto luks_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    // 1. Generate keyfile if it doesn't exist yet
    if (!fs::exists(dest_file)) {
        // TODO(vnepogodin): refactor subprocess exit code and print stderr captured output on failure
        auto cmd = fmt::format(FMT_COMPILE("dd bs=512 count=4 if=/dev/urandom of={} iflag=fullblock"), dest_file);
        if (auto res = utils::exec_or_error(cmd, "generate keyfile"sv); !res) {
            spdlog::error("Failed to generate luks keyfile: {}!", dest_file);
            return res;
        }
        spdlog::info("Generating a keyfile");
    }

    // 2. Set permissions to 600, so regular users are not able to read the keyfile
    const auto& chmod_cmd = fmt::format(FMT_COMPILE("chmod 600 {}"), dest_file);
    if (auto res = utils::exec_or_error(chmod_cmd, "set keyfile permissions"sv); !res) {
        spdlog::error("Failed to chmod luks keyfile: {}!", dest_file);
        return res;
    }

    // 3. Add keyfile to luks
    spdlog::info("Adding the keyfile to the LUKS configuration");
    if (auto res = luks_add_key(dest_file, partition, additional_flags); !res) {
        spdlog::error("Something went wrong with adding the LUKS key. Is {} the right partition?", partition);
        return res;
    }

    // 4. Add keyfile to initcpio
    const auto& mkinitcpio_conf = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);

    // TODO(vnepogodin): that should gathered from dest_file and be relative to mountpoint
    const auto& cmd = fmt::format(FMT_COMPILE("grep -q '/crypto_keyfile.bin' {0} || sed -i '/FILES/ s~)~/crypto_keyfile.bin)~' {0}"), mkinitcpio_conf);
    if (auto res = utils::exec_or_error(cmd, "failed to add keyfile"sv); !res) {
        spdlog::error("Failed to add keyfile to {}", mkinitcpio_conf);
        return res;
    }

    spdlog::info("Adding keyfile to the initcpio");
    return utils::arch_chroot_or_error("mkinitcpio -P"sv, mountpoint, "regenerate initramfs"sv);
}

}  // namespace gucc::crypto
