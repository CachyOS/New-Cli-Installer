#include "gucc/luks.hpp"
#include "gucc/error.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/logger.hpp"

#include <filesystem>
#include <string>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace {

// NOLINTNEXTLINE
using namespace gucc;

auto run_checked(const std::string& cmd, std::string context) noexcept -> Result<void> {
    if (!utils::exec_checked(cmd)) {
        return make_error(ErrorCode::SubprocessFailed, std::move(context));
    }
    return {};
}

// see https://wiki.archlinux.org/title/Dm-crypt/Device_encryption#With_a_keyfile_embedded_in_the_initramfs
auto setup_keyfile_impl(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags, bool use_luks2) noexcept -> Result<void> {
    namespace fs = std::filesystem;

    // 1. Generate keyfile if it doesn't exist yet
    if (!fs::exists(dest_file)) {
        // TODO(vnepogodin): refactor subprocess exit code and print stderr captured output on failure
        auto cmd = fmt::format(FMT_COMPILE("dd bs=512 count=4 if=/dev/urandom of={} iflag=fullblock &>/dev/null"), dest_file);
        if (auto res = run_checked(cmd, fmt::format("failed to generate keyfile {}", dest_file)); !res) {
            spdlog::error("Failed to generate(dd) luks keyfile: {}!", dest_file);
            return res;
        }
        spdlog::info("Generating a keyfile");
    }

    // 2. Set permissions to 600, so regular users are not able to read the keyfile
    utils::exec(fmt::format(FMT_COMPILE("chmod 600 {}"), dest_file));

    // 3. Add keyfile to luks
    spdlog::info("Adding the keyfile to the LUKS configuration");
    auto add_key_res = use_luks2
        ? crypto::luks2_add_key(dest_file, partition, additional_flags)
        : crypto::luks1_add_key(dest_file, partition, additional_flags);
    if (!add_key_res) {
        spdlog::error("Something went wrong with adding the LUKS key. Is {} the right partition?", partition);
        return add_key_res;
    }

    // 4. Add keyfile to initcpio
    const auto& mkinitcpio_conf = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);

    // TODO(vnepogodin): that should gathered from dest_file and be relative to mountpoint
    const auto& cmd = fmt::format(FMT_COMPILE("grep -q '/crypto_keyfile.bin' {0} || sed -i '/FILES/ s~)~/crypto_keyfile.bin)~' {0}"), mkinitcpio_conf);
    if (auto res = run_checked(cmd, fmt::format("failed to add keyfile to {}", mkinitcpio_conf)); !res) {
        spdlog::error("Failed to add keyfile to {}", mkinitcpio_conf);
        return res;
    }

    spdlog::info("Adding keyfile to the initcpio");
    utils::arch_chroot("mkinitcpio -P", mountpoint);
    return {};
}

}  // namespace

namespace gucc::crypto {

// TODO(vnepogodin): they are mostly equal. refactor this shit
auto luks1_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void> {
    logger::register_secret(luks_pass);
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup open --type luks1 {} {} &>/dev/null"), luks_pass, partition, luks_name);
    return run_checked(cmd, fmt::format("failed to open luks1 partition {}", partition));
}

auto luks1_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    logger::register_secret(luks_pass);
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup -q {} --type luks1 luksFormat {} &>/dev/null"), luks_pass, additional_flags, partition);
    return run_checked(cmd, fmt::format("failed to format luks1 partition {}", partition));
}

auto luks2_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> Result<void> {
    logger::register_secret(luks_pass);
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup open --type luks2 {} {} &>/dev/null"), luks_pass, partition, luks_name);
    return run_checked(cmd, fmt::format("failed to open luks2 partition {}", partition));
}

auto luks2_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    logger::register_secret(luks_pass);
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup -q {} --type luks2 luksFormat {} &>/dev/null"), luks_pass, additional_flags, partition);
    return run_checked(cmd, fmt::format("failed to format luks2 partition {}", partition));
}

auto luks1_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    auto cmd = fmt::format(FMT_COMPILE("cryptsetup -q {} luksAddKey {} {} &>/dev/null"), additional_flags, partition, dest_file);
    return run_checked(cmd, fmt::format("failed to add luks key to {}", partition));
}

auto luks2_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    auto cmd = fmt::format(FMT_COMPILE("cryptsetup -q {} luksAddKey {} {} &>/dev/null"), additional_flags, partition, dest_file);
    return run_checked(cmd, fmt::format("failed to add luks key to {}", partition));
}

auto luks1_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    return setup_keyfile_impl(dest_file, mountpoint, partition, additional_flags, false);
}

auto luks2_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags) noexcept -> Result<void> {
    return setup_keyfile_impl(dest_file, mountpoint, partition, additional_flags, true);
}

}  // namespace gucc::crypto
