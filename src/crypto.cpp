#include "crypto.hpp"
#include "global_storage.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/io_utils.hpp"
#include "gucc/kmod.hpp"
#include "gucc/luks.hpp"
#include "gucc/string_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

/* clang-format off */
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

using namespace ftxui;
using namespace std::string_literals;
using namespace std::string_view_literals;

#ifdef NDEVENV
#include "follow_process_log.hpp"
#endif

namespace tui {

static constexpr auto luks_menu_body  = "Devices and volumes encrypted using dm_crypt cannot be accessed or\neven seen without being unlocked via a key or password.";
static constexpr auto luks_menu_body2 = "A seperate boot partition without encryption or logical volume management\n(LVM - unless using BIOS Grub) is required.";
static constexpr auto luks_menu_body3 = "The Automatic option uses default encryption settings,\nand is recommended for beginners.\nOtherwise, it is possible to specify cypher and key size parameters manually.";

bool select_crypt_partition(const std::string_view& text) noexcept {
    const auto& partitions = config::get<std::vector<std::string>>("PARTITIONS");
    const auto& content    = fmt::format(FMT_COMPILE("\n{}\n"), text);
    const auto selected    = detail::select_from_menu(partitions, content, {.text_size = size(HEIGHT, GREATER_THAN, 1)});
    if (!selected) {
        return false;
    }
    config::get<std::string>("PARTITION") = std::string{gucc::utils::first_field(partitions.at(*selected))};
    return true;
}

bool get_cryptname(std::string& cryptname) noexcept {
    std::string value{"cryptroot"};
    static constexpr auto luks_cryptname_body = "\nSpecify a name for the encrypted block device.\n \nIt is not necessary to prefix it with /dev/mapper/.\nAn example has been provided.\n";
    if (!detail::inputbox_widget(value, luks_cryptname_body, size(HEIGHT, GREATER_THAN, 4))) {
        return false;
    }
    cryptname = std::string{gucc::utils::trim(value)};
    return true;
}

bool get_crypt_password(std::string& password) noexcept {
    std::string value{};
    static constexpr auto luks_pass_body = "\nEnter a password to un/encrypt the partition.\n \nThis should not be the same as\nthe Root account or user account passwords.\n";
    if (!detail::inputbox_widget(value, luks_pass_body, size(HEIGHT, GREATER_THAN, 4))) {
        return false;
    }
    password = std::string{gucc::utils::trim(value)};
    return true;
}

bool luks_open() noexcept {
    config::get<std::string>("LUKS_ROOT_NAME").clear();
    utils::umount_partitions();
    utils::find_partitions(utils::PartitionPurpose::OpenLuks);

    // Filter out partitions that don't contain crypt device
    /*const auto& ignore_part = utils::list_non_crypt();

    const auto& parts = gucc::utils::make_multiline(ignore_part);
    for (const auto& part : parts) {
        utils::delete_partition_in_list(part);
    }*/

    // stop if no encrypted partition found
    if (config::get<std::vector<std::string>>("PARTITIONS").empty()) {
        detail::msgbox_widget("\nNo LUKS-encrypted partition found.\n");
        return false;
    }

    // Select encrypted partition to open
    /* clang-format off */
    if (!tui::select_crypt_partition(luks_menu_body)) { return false; }
    /* clang-format on */

    // Enter name of the Luks partition and get password to open it
    const auto& partition = config::get<std::string>("PARTITION");
    auto& luks_root_name  = config::get<std::string>("LUKS_ROOT_NAME");
    auto& luks_password   = config::get<std::string>("PASSWD");
    /* clang-format off */
    if (!tui::get_cryptname(luks_root_name)) { return false; }
    if (!tui::get_crypt_password(luks_password)) { return false; }
    /* clang-format on */

    // Try to open the luks partition with the credentials given. If successful show this, otherwise
    // show the error
    detail::infobox_widget("\nPlease wait...\n");
#ifdef NDEVENV
    if (!gucc::crypto::luks_open(gucc::crypto::LuksVersion::Luks1, luks_password, partition, luks_root_name)) {
        spdlog::error("Failed to open luks1 partition {} with name {}", partition, luks_root_name);
        detail::msgbox_widget("\nFailed to open luks1 partition\n");
        return false;
    }
#endif

    const auto devlist = gucc::utils::query_output(fmt::format(FMT_COMPILE(R"(lsblk -o NAME,TYPE,FSTYPE,SIZE,MOUNTPOINT -Q "type=='crypt' || fstype=='crypto_LUKS'" {})"), partition)).value_or("\nFailed to list block devices\n"s);
    detail::msgbox_widget(devlist);

    return true;
}

bool luks_setup() noexcept {
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
    if (!tui::select_crypt_partition(luks_encrypt_body)) { return false; }
    /* clang-format on */

    // Enter name of the Luks partition and get password to create it
    return tui::get_cryptname(config::get<std::string>("LUKS_ROOT_NAME"))
        && tui::get_crypt_password(config::get<std::string>("PASSWD"));
}

void luks_encrypt([[maybe_unused]] const std::string_view& command) noexcept {
    // Encrypt selected partition or LV with credentials given
    detail::infobox_for("\nPlease wait...\n", std::chrono::seconds(2));
#ifdef NDEVENV
    const auto& partition      = config::get<std::string>("PARTITION");
    const auto& luks_root_name = config::get<std::string>("LUKS_ROOT_NAME");
    const auto& luks_password  = config::get<std::string>("PASSWD");

    if (!gucc::crypto::luks_format(gucc::crypto::LuksVersion::Luks1, luks_password, partition, command)) {
        spdlog::error("Failed to format luks1 partition {} with additional flags {}", partition, command);
        detail::msgbox_widget("\nFailed to format luks1 partition\n");
    }

    // Now open the encrypted partition or LV
    if (!gucc::crypto::luks_open(gucc::crypto::LuksVersion::Luks1, luks_password, partition, luks_root_name)) {
        spdlog::error("Failed to open luks1 partition {} with name {}", partition, luks_root_name);
        detail::msgbox_widget("\nFailed to open luks1 partition\n");
    }
#endif
}

void luks_default() noexcept {
    tui::luks_encrypt("");
}

bool luks_key_define() noexcept {
    std::string value{"-s 512 -c aes-xts-plain64"};
    static constexpr auto luks_cipher_key = "\nOnce the specified flags have been amended,\nthey will automatically be used with the 'cryptsetup -q luksFormat /dev/...' command.\n \nNOTE: Key files are not supported;\nthey can be added manually post-installation.\nDo not specify any additional flags such as -v (--verbose) or -y (--verify-passphrase).";
    if (!detail::inputbox_widget(value, luks_cipher_key, size(HEIGHT, GREATER_THAN, 4))) {
        return false;
    }

    value = std::string{gucc::utils::trim(value)};
    tui::luks_encrypt(value);
    return true;
}

void luks_express() noexcept {
    tui::luks_encrypt("--pbkdf-force-iterations 200000");
}

void luks_show() noexcept {
    static constexpr auto luks_success = "Done!";
    const auto lsblk                   = gucc::utils::query_output(R"(lsblk -o NAME,TYPE,FSTYPE,SIZE -Q "type=='part' || type=='crypt'")"sv).value_or("\nFailed to list block devices\n"s);
    const auto& content                = fmt::format(FMT_COMPILE("\n{}\n \n{}"), luks_success, lsblk);
    detail::msgbox_widget(content, size(HEIGHT, GREATER_THAN, 5));
}

void luks_menu_advanced() noexcept {
    const std::vector<std::string> menu_entries = {
        "Open Encrypted Partition",
        "Automatic LUKS Encryption",
        "Define Key-Size and Cypher",
        "Express LUKS",
        "Back",
    };

    const auto& content      = fmt::format(FMT_COMPILE("\n{}\n \n{}\n \n{}\n"), luks_menu_body, luks_menu_body2, luks_menu_body3);
    const auto& content_size = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    const auto selected      = detail::select_from_menu(menu_entries, content, {.content_size = content_size});
    /* clang-format off */
    if (!selected) { return; }

    switch (*selected) {
    case 0:
        tui::luks_open();
        break;
    case 1: {
        if (!tui::luks_setup()) { return; }
        tui::luks_default();
        tui::luks_show();
        break;
    }
    case 2: {
        if (!tui::luks_setup() || !tui::luks_key_define()) { return; }
        tui::luks_show();
        break;
    }
    case 3: {
        if (!tui::luks_setup()) { return; }
        tui::luks_express();
        tui::luks_show();
        break;
    }
    default:
        break;
    }
    /* clang-format on */
}

}  // namespace tui
