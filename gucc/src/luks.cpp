#include "gucc/luks.hpp"
#include "gucc/io_utils.hpp"

#include <filesystem>

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace gucc::crypto {

auto luks1_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> bool {
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup open --type luks1 {} {} &>/dev/null"), luks_pass, partition, luks_name);
    return utils::exec_checked(cmd);
}

auto luks1_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags) noexcept -> bool {
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup -q {} --type luks1 luksFormat {} &>/dev/null"), luks_pass, additional_flags, partition);
    return utils::exec_checked(cmd);
}

auto luks1_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags) noexcept -> bool {
    auto cmd = fmt::format(FMT_COMPILE("cryptsetup -q {} luksAddKey {} {} &>/dev/null"), additional_flags, partition, dest_file);
    return utils::exec_checked(cmd);
}

// see https://wiki.archlinux.org/title/Dm-crypt/Device_encryption#With_a_keyfile_embedded_in_the_initramfs
auto luks1_setup_keyfile(std::string_view dest_file, std::string_view mountpoint, std::string_view partition, std::string_view additional_flags) noexcept -> bool {
    namespace fs = std::filesystem;

    // 1. Generate keyfile if it doesn't exist yet
    if (!fs::exists(dest_file)) {
        // TODO(vnepogodin): refactor subprocess exit code and print stderr captured output on failure
        auto cmd = fmt::format(FMT_COMPILE("dd bs=512 count=4 if=/dev/urandom of={} iflag=fullblock &>/dev/null"), additional_flags, partition, dest_file);
        if (!utils::exec_checked(cmd)) {
            spdlog::error("Failed to generate(dd) luks keyfile: {}!");
            return false;
        }
        spdlog::info("Generating a keyfile");
    }

    // 2. Set permissions to 600, so regular users are not able to read the keyfile
    utils::exec(fmt::format(FMT_COMPILE("chmod 600 {}"), dest_file));

    // 3. Add keyfile to luks
    spdlog::info("Adding the keyfile to the LUKS configuration");
    if (!crypto::luks1_add_key(dest_file, partition, additional_flags)) {
        spdlog::error("Something went wrong with adding the LUKS key. Is {} the right partition?", partition);
        return false;
    }

    // 4. Add keyfile to initcpio
    const auto& mkinitcpio_conf = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);

    // TODO(vnepogodin): that should gathered from dest_file and be relative to mountpoint
    const auto& cmd = fmt::format(FMT_COMPILE("grep -q '/crypto_keyfile.bin' {0} || sed -i '/FILES/ s~)~/crypto_keyfile.bin)~' {0}"), mkinitcpio_conf);
    if (!utils::exec_checked(cmd)) {
        spdlog::error("Failed to add keyfile to {}", mkinitcpio_conf);
        return false;
    }

    spdlog::info("Adding keyfile to the initcpio");
    utils::arch_chroot("mkinitcpio -P", mountpoint);
    return true;
}

auto luks2_open(std::string_view luks_pass, std::string_view partition, std::string_view luks_name) noexcept -> bool {
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup open --type luks2 {} {} &>/dev/null"), luks_pass, partition, luks_name);
    return utils::exec_checked(cmd);
}

auto luks2_format(std::string_view luks_pass, std::string_view partition, std::string_view additional_flags) noexcept -> bool {
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | cryptsetup -q {} --type luks2 luksFormat {} &>/dev/null"), luks_pass, additional_flags, partition);
    return utils::exec_checked(cmd);
}

auto luks2_add_key(std::string_view dest_file, std::string_view partition, std::string_view additional_flags) noexcept -> bool {
    auto cmd = fmt::format(FMT_COMPILE("cryptsetup -q {} luksAddKey {} {} &>/dev/null"), additional_flags, partition, dest_file);
    return utils::exec_checked(cmd);
}

auto tpm2_available() noexcept -> bool {
    namespace fs = std::filesystem;

    // Check for TPM device nodes
    if (!fs::exists("/dev/tpm0") && !fs::exists("/dev/tpmrm0")) {
        spdlog::debug("TPM device not found (/dev/tpm0 or /dev/tpmrm0)");
        return false;
    }

    // Check if systemd-cryptenroll is available
    if (!utils::exec_checked("command -v systemd-cryptenroll &>/dev/null")) {
        spdlog::debug("systemd-cryptenroll not found");
        return false;
    }

    return true;
}

auto tpm2_enroll(std::string_view partition, std::string_view passphrase, const Tpm2Config& config) noexcept -> bool {
    // systemd-cryptenroll requires the passphrase via stdin
    auto cmd = fmt::format(FMT_COMPILE("echo '{}' | systemd-cryptenroll --tpm2-device={} --tpm2-pcrs={} {} 2>/dev/null"),
        passphrase, config.device, config.pcrs, partition);

    if (!utils::exec_checked(cmd)) {
        spdlog::error("Failed to enroll TPM2 for partition {}", partition);
        return false;
    }

    spdlog::info("Successfully enrolled TPM2 for partition {}", partition);
    return true;
}

}  // namespace gucc::crypto
