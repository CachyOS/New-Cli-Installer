#include "gucc/luks.hpp"
#include "gucc/error.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include "detail/initcpio_impl.hpp"

#include <cstddef>  // for size_t

#include <algorithm>     // for contains, count_if
#include <filesystem>    // for exists, permissions
#include <string>        // for string
#include <system_error>  // for error_code
#include <utility>       // for move
#include <vector>        // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_literals;
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

namespace gucc::crypto::detail {

auto count_enabled_luks_keyslots(std::string_view luks_dump) noexcept -> std::size_t {
    // TODO(vnepogodin): there has to be better ways getting count other than parsing raw luks dump, right???
    // only LUKS2 supports JSON format metadata..
    auto&& lines = utils::make_split_view(luks_dump);
    // LUKS1
    const auto luks1 = std::ranges::count_if(lines, [](std::string_view line) { return line.contains("ENABLED"sv); });

    // LUKS2 lists only used slots
    auto keyslots = utils::make_split_view(luks_dump)
        | std::ranges::views::drop_while([](std::string_view line) { return line != "Keyslots:"sv; })
        | std::ranges::views::drop(1)
        | std::ranges::views::take_while([](std::string_view line) { return line.starts_with(' ') || line.starts_with('\t'); });
    const auto luks2 = std::ranges::count_if(keyslots, [](std::string_view line) {
        const auto slot = utils::strip_prefix(line, "  "sv).and_then([](std::string_view rest) { return utils::split_once(rest, ':'); });
        return slot && !slot->first.empty() && std::ranges::all_of(slot->first, utils::is_digit);
    });
    return static_cast<std::size_t>(luks1 + luks2);
}

}  // namespace gucc::crypto::detail

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
    std::error_code ec;
    fs::permissions(dest_file, fs::perms::owner_read | fs::perms::owner_write, ec);
    if (ec) {
        spdlog::error("Failed to chmod luks keyfile: {}!", dest_file);
        return make_error(ErrorCode::FileIo, FMT_COMPILE("chmod 600 {}: {}"), dest_file, ec.message());
    }

    // 3. Add keyfile to luks
    spdlog::info("Adding the keyfile to the LUKS configuration");
    if (auto res = luks_add_key(dest_file, partition, additional_flags); !res) {
        spdlog::error("Something went wrong with adding the LUKS key. Is {} the right partition?", partition);
        return res;
    }

    // 4. Add keyfile to initcpio
    const auto& mkinitcpio_conf = utils::target_path(mountpoint, "/etc/mkinitcpio.conf"sv);

    // TODO(vnepogodin): that should gathered from dest_file and be relative to mountpoint
    static constexpr auto initcpio_keyfile = "/crypto_keyfile.bin"sv;
    auto initcpio                          = gucc::detail::Initcpio{mkinitcpio_conf};
    if (!initcpio.parse_file()) {
        return make_error(ErrorCode::ParseError, FMT_COMPILE("failed to parse {}"), mkinitcpio_conf);
    }
    if (!std::ranges::contains(initcpio.arrays().files, initcpio_keyfile)) {
        initcpio.arrays().files.emplace_back(initcpio_keyfile);
        if (!initcpio.write()) {
            return make_error(ErrorCode::FileIo, FMT_COMPILE("failed to add keyfile to {}"), mkinitcpio_conf);
        }
    }

    spdlog::info("Adding keyfile to the initcpio");
    return utils::arch_chroot_or_error("mkinitcpio -P"sv, mountpoint, "regenerate initramfs"sv);
}

auto luks_keyslot_count(std::string_view partition) noexcept -> Result<std::size_t> {
    const auto dump = utils::query_output({"/usr/bin/cryptsetup"s, "luksDump"s, std::string{partition}});
    if (!dump) {
        return make_error(ErrorCode::SubprocessFailed, FMT_COMPILE("dump luks header of {}"), partition);
    }
    return detail::count_enabled_luks_keyslots(*dump);
}

}  // namespace gucc::crypto
