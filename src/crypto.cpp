#include "crypto.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/io_utils.hpp"
#include "gucc/luks.hpp"
#include "gucc/string_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

/* clang-format off */
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
        const auto& lines        = gucc::utils::make_multiline(src, false, ' ');
        config_data["PARTITION"] = lines[0];
        success                  = true;
        screen.ExitLoopClosure()();
    };
    const auto& content = fmt::format(FMT_COMPILE("\n{}\n"), text);
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
    /*const auto& ignore_part = utils::list_non_crypt();

    const auto& parts = gucc::utils::make_multiline(ignore_part);
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

    // Try to open the luks partition with the credentials given. If successful show this, otherwise
    // show the error
    detail::infobox_widget("\nPlease wait...\n");
#ifdef NDEVENV
    if (!gucc::crypto::luks1_open(luks_password, partition, luks_root_name)) {
        spdlog::error("Failed to open luks1 partition {} with name {}", partition, luks_root_name);
        detail::msgbox_widget("\nFailed to open luks1 partition\n");
        return false;
    }
#endif

    const auto& devlist = gucc::utils::exec(fmt::format(FMT_COMPILE("lsblk -o NAME,TYPE,FSTYPE,SIZE,MOUNTPOINT {} | grep \"crypt\\|NAME\\|MODEL\\|TYPE\\|FSTYPE\\|SIZE\""), partition));
    detail::msgbox_widget(devlist);

    return true;
}

bool luks_setup() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

#ifdef NDEVENV
    gucc::utils::exec("modprobe -a dm-mod dm_crypt");
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

    if (!gucc::crypto::luks1_format(luks_password, partition, command)) {
        spdlog::error("Failed to format luks1 partition {} with additional flags {}", partition, command);
        detail::msgbox_widget("\nFailed to format luks1 partition\n");
    }

    // Now open the encrypted partition or LV
    if (!gucc::crypto::luks1_open(luks_password, partition, luks_root_name)) {
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

    tui::luks_encrypt(value);
    return true;
}

void luks_express() noexcept {
    tui::luks_encrypt("--pbkdf-force-iterations 200000");
}

void luks_show() noexcept {
    static constexpr auto luks_success = "Done!";
    const auto& lsblk                  = gucc::utils::exec(R"(lsblk -o NAME,TYPE,FSTYPE,SIZE | grep "part\|crypt\|NAME\|TYPE\|FSTYPE\|SIZE")");
    const auto& content                = fmt::format(FMT_COMPILE("\n{}\n \n{}"), luks_success, lsblk);
    detail::msgbox_widget(content, size(HEIGHT, GREATER_THAN, 5));
}

bool check_tpm_available() noexcept {
#ifdef NDEVENV
    return gucc::crypto::tpm2_available();
#else
    return true;  // For testing in dev environment
#endif
}

bool select_pcrs(std::string& pcrs) noexcept {
    // PCR descriptions for user selection
    static constexpr auto pcr_body = "\nSelect PCR (Platform Configuration Register) values for TPM2 binding.\n \n"
                                     "Recommended: 0,2,4,7 (firmware, drivers, bootloader, Secure Boot)\n \n"
                                     "PCR 0: Firmware code (BIOS/UEFI)\n"
                                     "PCR 2: Option ROM code\n"
                                     "PCR 4: Boot manager code\n"
                                     "PCR 7: Secure Boot state\n"
                                     "PCR 8: Kernel command line (optional)\n"
                                     "PCR 9: Initrd (optional)\n";

    std::string value{"0,2,4,7"};
    if (!detail::inputbox_widget(value, pcr_body, size(HEIGHT, GREATER_THAN, 10))) {
        return false;
    }

    pcrs = value;
    return true;
}

bool luks_setup_with_tpm() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

#ifdef NDEVENV
    gucc::utils::exec("modprobe -a dm-mod dm_crypt");
#endif
    config_data["INCLUDE_PART"] = "part\\|lvm";
    utils::umount_partitions();
    utils::find_partitions();

    // Select partition to encrypt
    /* clang-format off */
    static constexpr auto luks_encrypt_body = "Select a partition to encrypt with LUKS2 + TPM2.";
    if (!tui::select_crypt_partition(luks_encrypt_body)) { return false; }
    /* clang-format on */

    // Enter name of the Luks partition and get password to create it
    auto& luks_root_name = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    auto& luks_password  = std::get<std::string>(config_data["PASSWD"]);
    /* clang-format off */
    if (!tui::get_cryptname(luks_root_name)) { return false; }
    if (!tui::get_crypt_password(luks_password)) { return false; }
    /* clang-format on */

    // Get PCR values
    std::string pcrs{};
    if (!tui::select_pcrs(pcrs)) {
        return false;
    }
    config_data["TPM2_PCRS"] = pcrs;

    return true;
}

void luks_encrypt_tpm() noexcept {
    detail::infobox_widget("\nEncrypting with LUKS2 + TPM2...\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
#ifdef NDEVENV
    auto* config_instance      = Config::instance();
    auto& config_data          = config_instance->data();
    const auto& partition      = std::get<std::string>(config_data["PARTITION"]);
    const auto& luks_root_name = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    const auto& luks_password  = std::get<std::string>(config_data["PASSWD"]);
    const auto& tpm2_pcrs      = std::get<std::string>(config_data["TPM2_PCRS"]);

    // Format with LUKS2
    if (!gucc::crypto::luks2_format(luks_password, partition, "")) {
        spdlog::error("Failed to format LUKS2 partition {}", partition);
        detail::msgbox_widget("\nFailed to format LUKS2 partition\n");
        return;
    }

    // Enroll TPM2
    gucc::crypto::Tpm2Config tpm_config{};
    tpm_config.pcrs = tpm2_pcrs;
    if (!gucc::crypto::tpm2_enroll(partition, luks_password, tpm_config)) {
        spdlog::error("Failed to enroll TPM2 for partition {}", partition);
        detail::msgbox_widget("\nFailed to enroll TPM2. The partition is encrypted but TPM auto-unlock is not configured.\nYou can still unlock with password.\n");
    }

    // Open the encrypted partition
    if (!gucc::crypto::luks2_open(luks_password, partition, luks_root_name)) {
        spdlog::error("Failed to open LUKS2 partition {} with name {}", partition, luks_root_name);
        detail::msgbox_widget("\nFailed to open LUKS2 partition\n");
        return;
    }

    // Update config to indicate TPM enrollment
    config_data["TPM2_ENABLED"] = 1;
    config_data["LUKS_VERSION"] = 2;
#endif
}

void luks_menu_advanced() noexcept {
    const std::vector<std::string> menu_entries = {
        "Open Encrypted Partition",
        "Automatic LUKS Encryption",
        "Define Key-Size and Cypher",
        "Express LUKS",
        "LUKS2 with TPM2",
        "Back",
    };

    auto screen = ScreenInteractive::Fullscreen();
    bool success{};
    std::int32_t selected{};
    auto ok_callback = [&] {
        success = true;
        screen.ExitLoopClosure()();
    };

    const auto& content      = fmt::format(FMT_COMPILE("\n{}\n \n{}\n \n{}\n"), luks_menu_body, luks_menu_body2, luks_menu_body3);
    const auto& content_size = size(HEIGHT, LESS_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    detail::menu_widget(menu_entries, ok_callback, &selected, &screen, content, {.content_size = content_size});
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
    case 4: {
        // LUKS2 with TPM2
        if (!tui::check_tpm_available()) {
            detail::msgbox_widget("\nTPM2 device not found or systemd-cryptenroll not available.\n");
            return;
        }
        if (!tui::luks_setup_with_tpm()) { return; }
        tui::luks_encrypt_tpm();
        tui::luks_show();
        break;
    }
    }
    /* clang-format on */
}

}  // namespace tui
