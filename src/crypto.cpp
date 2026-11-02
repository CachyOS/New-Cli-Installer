#include "crypto.hpp"
#include "installer_state.hpp"
#include "utils.hpp"
#include "widgets.hpp"

#include "cachyos/installer_data.hpp"

// import gucc
#include "gucc/kmod.hpp"
#include "gucc/luks.hpp"
#include "gucc/process.hpp"
#include "gucc/string_utils.hpp"

#include <optional>  // for optional
#include <string>    // for string
#include <utility>   // for move

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

#ifdef NDEVENV
#include "follow_process_log.hpp"
#endif

namespace tui {

static constexpr auto luks_menu_body  = "Devices and volumes encrypted using dm_crypt cannot be accessed or even seen without being unlocked via a key or password.";
static constexpr auto luks_menu_body2 = "A seperate boot partition without encryption or logical volume management (LVM - unless using BIOS Grub) is required.";
static constexpr auto luks_menu_body3 = "The Automatic option uses default encryption settings, and is recommended for beginners. Otherwise, it is possible to specify cypher and key size parameters manually.";

static constexpr auto luks_pass_body = "Enter a password to un/encrypt the partition.\n\nThis should not be the same as the Root account or user account passwords.";

bool select_crypt_partition(std::string_view text) noexcept {
    auto& disk    = tui::state().disk;
    auto selected = detail::select_value(disk.partitions, text).transform(cachyos::installer::data::parse_device_name);
    if (!selected) {
        return false;
    }
    disk.partition = std::move(*selected);
    return true;
}

bool get_cryptname(std::string& cryptname) noexcept {
    std::string value{"cryptroot"};
    static constexpr auto luks_cryptname_body = "Specify a name for the encrypted block device.\n\nIt is not necessary to prefix it with /dev/mapper/. An example has been provided.";
    if (!detail::inputbox_widget(value, luks_cryptname_body)) {
        return false;
    }
    cryptname = std::string{gucc::utils::trim(value)};
    return true;
}

bool luks_open() noexcept {
    auto& st = tui::state();
    st.inputs.ctx.crypto.luks_root_name.clear();
    utils::umount_partitions();
    utils::find_partitions(utils::PartitionPurpose::OpenLuks);

    // Filter out partitions that don't contain crypt device
    /*const auto& ignore_part = utils::list_non_crypt();

    const auto& parts = gucc::utils::make_multiline(ignore_part);
    for (const auto& part : parts) {
        utils::delete_partition_in_list(part);
    }*/

    // stop if no encrypted partition found
    if (st.disk.partitions.empty()) {
        detail::msgbox_widget("No LUKS-encrypted partition found.");
        return false;
    }

    // Select encrypted partition to open
    /* clang-format off */
    if (!tui::select_crypt_partition(luks_menu_body)) { return false; }
    /* clang-format on */

    // Enter name of the Luks partition and get password to open it
    const auto& partition = st.disk.partition;
    auto& luks_root_name  = st.inputs.ctx.crypto.luks_root_name;
    std::string luks_password{};
    /* clang-format off */
    if (!tui::get_cryptname(luks_root_name)) { return false; }
    // an existing volume's password is typed once
    if (!detail::inputbox_widget(luks_password, luks_pass_body, true)) { return false; }
    /* clang-format on */
    luks_password = std::string{gucc::utils::trim(luks_password)};

    // Try to open the luks partition with the credentials given. If successful show this, otherwise
    // show the error
    detail::infobox_widget("Please wait...");
#ifdef NDEVENV
    if (!gucc::crypto::luks_open(gucc::crypto::LuksVersion::Luks1, luks_password, partition, luks_root_name)) {
        spdlog::error("Failed to open luks1 partition {} with name {}", partition, luks_root_name);
        detail::msgbox_widget("Failed to open luks1 partition");
        return false;
    }
#endif

    const auto devlist = utils::device_listing("NAME,TYPE,FSTYPE,SIZE,MOUNTPOINT"sv, "type=='crypt' || fstype=='crypto_LUKS'"sv, partition);
    // dry-run skips cryptsetup, so nothing was actually opened
    const auto opened = gucc::utils::default_runner().dry_run() ? "Would open"sv : "Opened"sv;
    detail::msgbox_widget(fmt::format(FMT_COMPILE("{} {} as {}.\n\n{}"), opened, partition, luks_root_name, devlist));

    return true;
}

auto luks_setup() noexcept -> std::optional<std::string> {
#ifdef NDEVENV
    if (!gucc::kmod::load({"dm-mod"sv, "dm_crypt"sv})) {
        spdlog::error("Failed to load dm-mod/dm_crypt kernel modules");
    }
#endif
    utils::umount_partitions();
    utils::find_partitions(utils::PartitionPurpose::Encrypt);

    // Select partition to encrypt
    /* clang-format off */
    static constexpr auto luks_encrypt_body = "Select a partition to encrypt.";
    if (!tui::select_crypt_partition(luks_encrypt_body)) { return std::nullopt; }
    if (!tui::get_cryptname(tui::state().inputs.ctx.crypto.luks_root_name)) { return std::nullopt; }
    /* clang-format on */

    return detail::password_prompt(luks_pass_body, "Re-enter the password."sv);
}

void luks_encrypt([[maybe_unused]] std::string_view luks_password, [[maybe_unused]] std::string_view command) noexcept {
    // Encrypt selected partition or LV with credentials given
    detail::infobox_for("Please wait...", std::chrono::seconds(2));
#ifdef NDEVENV
    const auto& st             = tui::state();
    const auto& partition      = st.disk.partition;
    const auto& luks_root_name = st.inputs.ctx.crypto.luks_root_name;

    if (!gucc::crypto::luks_format(gucc::crypto::LuksVersion::Luks1, luks_password, partition, command)) {
        spdlog::error("Failed to format luks1 partition {} with additional flags {}", partition, command);
        detail::msgbox_widget("Failed to format luks1 partition");
    }

    // Now open the encrypted partition or LV
    if (!gucc::crypto::luks_open(gucc::crypto::LuksVersion::Luks1, luks_password, partition, luks_root_name)) {
        spdlog::error("Failed to open luks1 partition {} with name {}", partition, luks_root_name);
        detail::msgbox_widget("Failed to open luks1 partition");
    }
#endif
}

void luks_default(std::string_view luks_password) noexcept {
    tui::luks_encrypt(luks_password, "");
}

bool luks_key_define(std::string_view luks_password) noexcept {
    std::string value{"-s 512 -c aes-xts-plain64"};
    static constexpr auto luks_cipher_key = "Once the specified flags have been amended, they will automatically be used with the 'cryptsetup -q luksFormat /dev/...' command.\n\nNOTE: Key files are not supported; they can be added manually post-installation. Do not specify any additional flags such as -v (--verbose) or -y (--verify-passphrase).";
    if (!detail::inputbox_widget(value, luks_cipher_key)) {
        return false;
    }

    value = std::string{gucc::utils::trim(value)};
    tui::luks_encrypt(luks_password, value);
    return true;
}

void luks_express(std::string_view luks_password) noexcept {
    tui::luks_encrypt(luks_password, "--pbkdf-force-iterations 200000");
}

void luks_show() noexcept {
    static constexpr auto luks_success = "Done!";
    const auto lsblk                   = utils::device_listing("NAME,TYPE,FSTYPE,SIZE"sv, "type=='part' || type=='crypt'"sv);
    const auto& content                = fmt::format(FMT_COMPILE("{}\n\n{}"), luks_success, lsblk);
    detail::msgbox_widget(content);
}

void luks_menu_advanced() noexcept {
    const std::vector<std::string> menu_entries = {
        "Open Encrypted Partition",
        "Automatic LUKS Encryption",
        "Define Key-Size and Cypher",
        "Express LUKS",
        "Back",
    };

    const auto& content = fmt::format(FMT_COMPILE("{}\n\n{}\n\n{}"), luks_menu_body, luks_menu_body2, luks_menu_body3);
    const auto selected = detail::select_from_menu(menu_entries, content);
    /* clang-format off */
    if (!selected) { return; }

    switch (*selected) {
    case 0:
        tui::luks_open();
        break;
    case 1: {
        const auto pass = tui::luks_setup();
        if (!pass) { return; }
        tui::luks_default(*pass);
        tui::luks_show();
        break;
    }
    case 2: {
        const auto pass = tui::luks_setup();
        if (!pass || !tui::luks_key_define(*pass)) { return; }
        tui::luks_show();
        break;
    }
    case 3: {
        const auto pass = tui::luks_setup();
        if (!pass) { return; }
        tui::luks_express(*pass);
        tui::luks_show();
        break;
    }
    default:
        break;
    }
    /* clang-format on */
}

}  // namespace tui
