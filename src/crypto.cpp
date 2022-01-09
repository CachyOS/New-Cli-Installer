#include "crypto.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "widgets.hpp"

/* clang-format off */
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

using namespace ftxui;

#ifdef NDEVENV
#include "follow_process_log.hpp"
#endif

namespace tui {

static constexpr auto luks_menu_body  = "Devices and volumes encrypted using dm_crypt cannot be accessed or\neven seen without being unlocked via a key or password.";
static constexpr auto luks_menu_body2 = "A seperate boot partition without encryption or logical volume management\n(LVM - unless using BIOS Grub) is required.";
static constexpr auto luks_menu_body3 = "The Automatic option uses default encryption settings,\nand is recommended for beginners.\nOtherwise, it is possible to specify cypher and key size parameters manually.";

bool select_crypt_partition(const std::string_view& text) noexcept {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);

    auto screen = ScreenInteractive::Fullscreen();
    std::int32_t selected{};
    bool success{};
    auto ok_callback = [&] {
        const auto& src          = partitions[static_cast<std::size_t>(selected)];
        const auto& lines        = utils::make_multiline(src, false, " ");
        config_data["PARTITION"] = lines[0];
        success                  = true;
        screen.ExitLoopClosure()();
    };
    const auto& content = fmt::format("\n{}\n", text);
    detail::menu_widget(partitions, ok_callback, &selected, &screen, content, {.text_size = size(HEIGHT, GREATER_THAN, 1)});

    return success;
}

bool get_cryptname(std::string& cryptname) noexcept {
    std::string value{"cryptroot"};
    static constexpr auto luks_cryptname_body = "\nSpecify a name for the encrypted block device.\n \nIt is not necessary to prefix it with /dev/mapper/.\nAn example has been provided.\n";
    if (!detail::inputbox_widget(value, luks_cryptname_body, size(HEIGHT, GREATER_THAN, 4))) {
        return false;
    }

    cryptname = value;
    return true;
}

bool get_crypt_password(std::string& password) noexcept {
    std::string value{};
    static constexpr auto luks_pass_body = "\nEnter a password to un/encrypt the partition.\n \nThis should not be the same as\nthe Root account or user account passwords.\n";
    if (!detail::inputbox_widget(value, luks_pass_body, size(HEIGHT, GREATER_THAN, 4))) {
        return false;
    }

    password = value;
    return true;
}

bool luks_open() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["LUKS_ROOT_NAME"] = "";
    config_data["INCLUDE_PART"]   = "part\\|crypt\\|lvm";
    utils::umount_partitions();
    utils::find_partitions();

    // Filter out partitions that don't contain crypt device
    const auto& ignore_part = utils::list_non_crypt();

    /* const auto& parts = utils::make_multiline(ignore_part);
    for (const auto& part : parts) {
        utils::delete_partition_in_list(part);
    }*/

    // stop if no encrypted partition found
    const auto& partitions = std::get<std::vector<std::string>>(config_data["PARTITIONS"]);
    if (partitions.empty()) {
        detail::msgbox_widget("\nNo LUKS-encrypted partition found.\n");
        return false;
    }

    // Select encrypted partition to open
    /* clang-format off */
    if (!tui::select_crypt_partition(luks_menu_body)) { return false; }
    /* clang-format on */

    // Enter name of the Luks partition and get password to open it
    const auto& partition = std::get<std::string>(config_data["PARTITION"]);
    auto& luks_root_name  = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    auto& luks_password   = std::get<std::string>(config_data["PASSWD"]);
    /* clang-format off */
    if (!tui::get_cryptname(luks_root_name)) { return false; }
    if (!tui::get_crypt_password(luks_password)) { return false; }
    /* clang-format on */

    spdlog::info("partition: {}, luks_root_name: {}, luks_password: {}", partition, luks_root_name, luks_password);

    // Try to open the luks partition with the credentials given. If successful show this, otherwise
    // show the error
    detail::infobox_widget("\nPlease wait...\n");
#ifdef NDEVENV
    detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format("echo \"{}\" | cryptsetup open --type luks {} {}", luks_password, partition, luks_root_name)});
#endif

    const auto& devlist = utils::exec(fmt::format("lsblk -o NAME,TYPE,FSTYPE,SIZE,MOUNTPOINT {} | grep \"crypt\\|NAME\\|MODEL\\|TYPE\\|FSTYPE\\|SIZE\"", partition));
    detail::msgbox_widget(devlist);

    return true;
}

bool luks_setup() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

#ifdef NDEVENV
    utils::exec("modprobe -a dm-mod dm_crypt");
#endif
    config_data["INCLUDE_PART"] = "part\\|lvm";
    utils::umount_partitions();
    utils::find_partitions();

    // Select partition to encrypt
    /* clang-format off */
    static constexpr auto luks_encrypt_body = "Select a partition to encrypt.";
    if (!tui::select_crypt_partition(luks_encrypt_body)) { return false; }
    /* clang-format on */

    // Enter name of the Luks partition and get password to create it
    auto& luks_root_name = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    auto& luks_password  = std::get<std::string>(config_data["PASSWD"]);
    /* clang-format off */
    if (!tui::get_cryptname(luks_root_name)) { return false; }
    if (!tui::get_crypt_password(luks_password)) { return false; }
    /* clang-format on */

    return true;
}

void luks_encrypt([[maybe_unused]] const std::string_view& command) noexcept {
    // Encrypt selected partition or LV with credentials given
    detail::infobox_widget("\nPlease wait...\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
#ifdef NDEVENV
    auto* config_instance      = Config::instance();
    auto& config_data          = config_instance->data();
    const auto& partition      = std::get<std::string>(config_data["PARTITION"]);
    const auto& luks_root_name = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    const auto& luks_password  = std::get<std::string>(config_data["PASSWD"]);

    detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format("echo \"{}\" | cryptsetup -q {} {}", luks_password, command, partition)});

    // Now open the encrypted partition or LV
    detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format("echo \"{}\" | cryptsetup open {} {}", luks_password, partition, luks_root_name)});
#endif
}

void luks_default() noexcept {
    tui::luks_encrypt("--type luks1 luksFormat");
}

bool luks_key_define() noexcept {
    std::string value{"-s 512 -c aes-xts-plain64"};
    static constexpr auto luks_cipher_key = "\nOnce the specified flags have been amended,\nthey will automatically be used with the 'cryptsetup -q luksFormat /dev/...' command.\n \nNOTE: Key files are not supported;\nthey can be added manually post-installation.\nDo not specify any additional flags such as -v (--verbose) or -y (--verify-passphrase).";
    if (!detail::inputbox_widget(value, luks_cipher_key, size(HEIGHT, GREATER_THAN, 4))) {
        return false;
    }

    tui::luks_encrypt(value);
    return true;
}

void luks_express() noexcept {
    tui::luks_encrypt("--pbkdf-force-iterations 200000 --type luks1 luksFormat");
}

void luks_show() noexcept {
    static constexpr auto luks_success = "Done!";
    const auto& lsblk                  = utils::exec("lsblk -o NAME,TYPE,FSTYPE,SIZE | grep \"part\\|crypt\\|NAME\\|TYPE\\|FSTYPE\\|SIZE\"");
    const auto& content                = fmt::format("\n{}\n \n{}", luks_success, lsblk);
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

    auto screen = ScreenInteractive::Fullscreen();
    bool success{};
    std::int32_t selected{};
    auto ok_callback = [&] {
        success = true;
        screen.ExitLoopClosure()();
    };

    const auto& content = fmt::format("\n{}\n \n{}\n \n{}\n", luks_menu_body, luks_menu_body2, luks_menu_body3);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, content);
    /* clang-format off */
    if (!success) { return; }

    switch (selected) {
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
        if (!tui::luks_setup() && !tui::luks_key_define()) { return; }
        tui::luks_show();
        break;
    }
    case 3: {
        if (!tui::luks_setup()) { return; }
        tui::luks_express();
        tui::luks_show();
        break;
    }
    }
    /* clang-format on */
}

}  // namespace tui
