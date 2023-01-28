#include "utils.hpp"
#include "config.hpp"
#include "cpu.hpp"
#include "definitions.hpp"
#include "follow_process_log.hpp"
#include "initcpio.hpp"
#include "pacmanconf_repo.hpp"
#include "subprocess.h"
#include "tui.hpp"
#include "widgets.hpp"

#include <algorithm>      // for transform
#include <array>          // for array
#include <bit>            // for bit_cast
#include <cerrno>         // for errno, strerror
#include <chrono>         // for filesystem, seconds
#include <cstdint>        // for int32_t
#include <cstdio>         // for feof, fgets, pclose, perror, popen
#include <cstdlib>        // for exit, WIFEXITED, WIFSIGNALED
#include <filesystem>     // for exists, is_directory
#include <fstream>        // for ofstream
#include <iostream>       // for basic_istream, cin
#include <mutex>          // for mutex
#include <string>         // for operator==, string, basic_string, allocator
#include <sys/mount.h>    // for mount
#include <sys/wait.h>     // for waitpid
#include <thread>         // for sleep_for
#include <unistd.h>       // for execvp, fork
#include <unordered_map>  // for unordered_map

#include <fmt/ranges.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/contains.hpp>
#include <range/v3/algorithm/for_each.hpp>
#include <range/v3/algorithm/none_of.hpp>
#include <range/v3/algorithm/reverse.hpp>
#include <range/v3/algorithm/search.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/split.hpp>
#include <range/v3/view/transform.hpp>

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef NDEVENV
#include <cpr/api.h>
#include <cpr/response.h>
#include <cpr/status_codes.h>
#include <cpr/timeout.h>
#endif

/*
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wambiguous-reversed-operator"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#endif

#include <simdjson.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
*/

namespace fs = std::filesystem;

namespace utils {

bool is_connected() noexcept {
#ifdef NDEVENV
    /* clang-format off */
    auto r = cpr::Get(cpr::Url{"https://cachyos.org"},
             cpr::Timeout{1000});
    /* clang-format on */
    return cpr::status::is_success(static_cast<std::int32_t>(r.status_code)) || cpr::status::is_redirect(static_cast<std::int32_t>(r.status_code));
#else
    return true;
#endif
}

bool check_root() noexcept {
#ifdef NDEVENV
    return (utils::exec("whoami") == "root");
#else
    return true;
#endif
}

void clear_screen() noexcept {
    static constexpr auto CLEAR_SCREEN_ANSI = "\033[1;1H\033[2J";
    output_inter(FMT_COMPILE("{}"), CLEAR_SCREEN_ANSI);
}

void arch_chroot(const std::string_view& command, bool follow) noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint    = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("arch-chroot {} {} 2>>/tmp/cachyos-install.log 2>&1"), mountpoint, command);
    if (follow) {
        if (headless_mode) {
            utils::exec(cmd_formatted, true);
        } else {
            tui::detail::follow_process_log_widget({"/bin/sh", "-c", cmd_formatted});
        }
        return;
    }
    utils::exec(cmd_formatted);
}

void exec_follow(const std::vector<std::string>& vec, std::string& process_log, bool& running, subprocess_s& child, bool async) noexcept {
    if (!async) {
        spdlog::error("Implement me!");
        return;
    }

    std::vector<char*> args;
    std::transform(vec.cbegin(), vec.cend(), std::back_inserter(args),
        [=](const std::string& arg) -> char* { return std::bit_cast<char*>(arg.data()); });
    args.push_back(nullptr);

    static std::array<char, 1048576 + 1> data{};
    static std::mutex s_mutex{};
    std::memset(data.data(), 0, data.size());

    int32_t ret{-1};
    subprocess_s process{};
    char** command                             = args.data();
    static constexpr const char* environment[] = {"PATH=/sbin:/bin:/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin", nullptr};
    if ((ret = subprocess_create_ex(command, subprocess_option_enable_async | subprocess_option_combined_stdout_stderr, environment, &process)) != 0) {
        running = false;
        return;
    }

    child = process;

    uint32_t index{};
    uint32_t bytes_read;
    do {
        std::lock_guard<std::mutex> lock(s_mutex);

        bytes_read = subprocess_read_stdout(&process, data.data() + index,
            static_cast<uint32_t>(data.size()) - 1 - index);

        index += bytes_read;
        process_log = data.data();
    } while (bytes_read != 0);

    subprocess_join(&process, &ret);
    subprocess_destroy(&process);
    child   = process;
    running = false;
}

void exec(const std::vector<std::string>& vec) noexcept {
    std::int32_t status{};
    auto pid = fork();
    if (pid == 0) {
        std::vector<char*> args;
        std::transform(vec.cbegin(), vec.cend(), std::back_inserter(args),
            [=](const std::string& arg) -> char* { return const_cast<char*>(arg.data()); });
        args.push_back(nullptr);

        char** command = args.data();
        execvp(command[0], command);
    } else {
        do {
            waitpid(pid, &status, 0);
        } while ((!WIFEXITED(status)) && (!WIFSIGNALED(status)));
    }
}

// https://github.com/sheredom/subprocess.h
// https://gist.github.com/konstantint/d49ab683b978b3d74172
// https://github.com/arun11299/cpp-subprocess/blob/master/subprocess.hpp#L1218
// https://stackoverflow.com/questions/11342868/c-interface-for-interactive-bash
// https://github.com/hniksic/rust-subprocess
std::string exec(const std::string_view& command, const bool& interactive) noexcept {
    if (interactive) {
        const auto& ret_code = system(command.data());
        return std::to_string(ret_code);
    }

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.data(), "r"), pclose);

    if (!pipe) {
        spdlog::error("popen failed! '{}'", command);
        return "-1";
    }

    std::string result{};
    std::array<char, 128> buffer{};
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
    }

    if (result.ends_with('\n')) {
        result.pop_back();
    }

    return result;
}

auto read_whole_file(const std::string_view& filepath) noexcept -> std::string {
    // Use std::fopen because it's faster than std::ifstream
    auto* file = std::fopen(filepath.data(), "rb");
    if (file == nullptr) {
        spdlog::error("[READWHOLEFILE] '{}' read failed: {}", filepath, std::strerror(errno));
        return {};
    }

    std::fseek(file, 0u, SEEK_END);
    const std::size_t size = static_cast<std::size_t>(std::ftell(file));
    std::fseek(file, 0u, SEEK_SET);

    std::string buf;
    buf.resize(size);

    const std::size_t read = std::fread(buf.data(), sizeof(char), size, file);
    if (read != size) {
        spdlog::error("[READWHOLEFILE] '{}' read failed: {}", filepath, std::strerror(errno));
        return {};
    }
    std::fclose(file);

    return buf;
}

bool write_to_file(const std::string_view& data, const std::string_view& filepath) noexcept {
    std::ofstream file{filepath.data()};
    if (!file.is_open()) {
        spdlog::error("[WRITE_TO_FILE] '{}' open failed: {}", filepath, std::strerror(errno));
        return false;
    }
    file << data;
    return true;
}

void dump_to_log(const std::string& data) noexcept {
    spdlog::info("[DUMP_TO_LOG] :=\n{}", data);
}

bool prompt_char(const char* prompt, const char* color, char* read) noexcept {
    fmt::print("{}{}{}\n", color, prompt, RESET);

    std::string tmp{};
    if (std::getline(std::cin, tmp)) {
        const auto& read_char = (tmp.length() == 1) ? tmp[0] : '\0';
        if (read != nullptr)
            *read = read_char;

        return true;
    }

    return false;
}

auto make_multiline(const std::string_view& str, bool reverse, char delim) noexcept -> std::vector<std::string> {
    static constexpr auto functor = [](auto&& rng) {
        return std::string_view(&*rng.begin(), static_cast<size_t>(ranges::distance(rng)));
    };
    static constexpr auto second = [](auto&& rng) { return rng != ""; };

    auto&& view_res = str
        | ranges::views::split(delim)
        | ranges::views::transform(functor);

    std::vector<std::string> lines{};
    ranges::for_each(view_res | ranges::views::filter(second), [&](auto&& rng) { lines.emplace_back(rng); });
    if (reverse) {
        ranges::reverse(lines);
    }
    return lines;
}

auto make_multiline(const std::vector<std::string>& multiline, bool reverse, const std::string_view&& delim) noexcept -> std::string {
    std::string res{};
    for (const auto& line : multiline) {
        res += line;
        res += delim.data();
    }

    if (reverse) {
        ranges::reverse(res);
    }

    return res;
}

// install a pkg in the live session if not installed
void inst_needed(const std::string_view& pkg) noexcept {
    if (utils::exec(fmt::format(FMT_COMPILE("pacman -Qq {} &>/dev/null"), pkg), true) != "0") {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        utils::clear_screen();

        auto* config_instance = Config::instance();
        auto& config_data     = config_instance->data();

        const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
        if (headless_mode) {
            utils::exec(fmt::format(FMT_COMPILE("pacman -Sy --noconfirm {}"), pkg), true);
        } else {
            tui::detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("pacman -Sy --noconfirm {}"), pkg)});
        }
        // utils::exec(fmt::format(FMT_COMPILE("pacman -Sy --noconfirm {}"), pkg));
    }
}

// Unmount partitions.
void umount_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    auto mount_info             = utils::exec(fmt::format(FMT_COMPILE("mount | grep \"{}\" | {}"), mountpoint_info, "awk '{print $3}' | sort -r"));
#ifdef NDEVENV
    utils::exec("swapoff -a");
#endif

    const auto& lines = utils::make_multiline(mount_info);
    for (const auto& line : lines) {
#ifdef NDEVENV
        umount(line.c_str());
#else
        spdlog::debug("{}\n", line);
#endif
    }
}

// Securely destroy all data on a given device.
void secure_wipe() noexcept {
    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& device_info = std::get<std::string>(config_data["DEVICE"]);

#ifdef NDEVENV
    utils::inst_needed("wipe");

    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    if (headless_mode) {
        utils::exec(fmt::format(FMT_COMPILE("wipe -Ifre {}"), device_info), true);
    } else {
        tui::detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("wipe -Ifre {}"), device_info)});
    }
    // utils::exec(fmt::format(FMT_COMPILE("wipe -Ifre {}"), device_info));
#else
    spdlog::debug("{}\n", device_info);
#endif
}

void generate_fstab(const std::string_view& fstab_cmd) noexcept {
    spdlog::info("Generating with fstab '{}'", fstab_cmd);

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    utils::exec(fmt::format(FMT_COMPILE("{0} {1} > {1}/etc/fstab"), fstab_cmd, mountpoint));
    spdlog::info("Created fstab file:");

    const auto& fstab_content = utils::read_whole_file(fmt::format(FMT_COMPILE("{}/etc/fstab"), mountpoint));
    utils::dump_to_log(fstab_content);

    const auto& swap_file = fmt::format(FMT_COMPILE("{}/swapfile"), mountpoint);
    if (fs::exists(swap_file) && fs::is_regular_file(swap_file)) {
        spdlog::info("appending swapfile to the fstab..");
        utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/\\\\{0}//\" {0}/etc/fstab"), mountpoint));
    }

    // Edit fstab in case of btrfs subvolumes
    utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/subvolid=.*,subvol=\\/.*,//g\" {}/etc/fstab"), mountpoint));

    // remove any zfs datasets that are mounted by zfs
    const auto& msource_list = utils::make_multiline(utils::exec(fmt::format(FMT_COMPILE("cat {}/etc/fstab | grep \"^[a-z,A-Z]\" | {}"), mountpoint, "awk '{print $1}'")));
    for (const auto& msource : msource_list) {
        if (utils::exec(fmt::format(FMT_COMPILE("zfs list -H -o mountpoint,name | grep \"^/\"  | {} | grep \"^{}$\""), "awk '{print $2}'", msource), true) == "0")
            utils::exec(fmt::format(FMT_COMPILE("sed -e \"\\|^{}[[:space:]]| s/^#*/#/\" -i {}/etc/fstab"), msource, mountpoint));
    }
#endif
}

// Set system hostname
void set_hostname(const std::string_view& hostname) noexcept {
    spdlog::info("Setting hostname {}", hostname);
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" > {}/etc/hostname"), hostname, mountpoint));
    const auto& cmd = fmt::format(FMT_COMPILE("echo -e \"#<ip-address>\\t<hostname.domain.org>\\t<hostname>\\n127.0.0.1\\tlocalhost.localdomain\\tlocalhost\\t{0}\\n::1\\tlocalhost.localdomain\\tlocalhost\\t{0}\">{1}/etc/hosts"), hostname, mountpoint);
    utils::exec(cmd);
#endif
}

// Set system language
void set_locale(const std::string_view& locale) noexcept {
    spdlog::info("Selected locale: {}", locale);
#ifdef NDEVENV
    auto* config_instance          = Config::instance();
    auto& config_data              = config_instance->data();
    const auto& mountpoint         = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& locale_config_path = fmt::format(FMT_COMPILE("{}/etc/locale.conf"), mountpoint);
    const auto& locale_gen_path    = fmt::format(FMT_COMPILE("{}/etc/locale.gen"), mountpoint);

    static constexpr auto locale_config_part = R"(LANG="{0}"
LC_NUMERIC="{0}"
LC_TIME="{0}"
LC_MONETARY="{0}"
LC_PAPER="{0}"
LC_NAME="{0}"
LC_ADDRESS="{0}"
LC_TELEPHONE="{0}"
LC_MEASUREMENT="{0}"
LC_IDENTIFICATION="{0}"
LC_MESSAGES="{0}")";

    std::ofstream locale_config_file{locale_config_path};
    locale_config_file << fmt::format(locale_config_part, locale);

    utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/#{0}/{0}/\" {1}"), locale, locale_gen_path));

    // Generate locales
    utils::arch_chroot("locale-gen", false);
#endif
}

void set_xkbmap(const std::string_view& xkbmap) noexcept {
    spdlog::info("Selected xkbmap: {}", xkbmap);
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    utils::exec(fmt::format(FMT_COMPILE("echo -e \"Section \"\\\"InputClass\"\\\"\\nIdentifier \"\\\"system-keyboard\"\\\"\\nMatchIsKeyboard \"\\\"on\"\\\"\\nOption \"\\\"XkbLayout\"\\\" \"\\\"{0}\"\\\"\\nEndSection\" > {1}/etc/X11/xorg.conf.d/00-keyboard.conf"), xkbmap, mountpoint));
#endif
}

void set_timezone(const std::string_view& timezone) noexcept {
    spdlog::info("Timezone is set to {}", timezone);
#ifdef NDEVENV
    utils::arch_chroot(fmt::format(FMT_COMPILE("ln -sf /usr/share/zoneinfo/{} /etc/localtime"), timezone), false);
#endif
}

void set_hw_clock(const std::string_view& clock_type) noexcept {
    spdlog::info("Clock type is: {}", clock_type);
#ifdef NDEVENV
    utils::arch_chroot(fmt::format(FMT_COMPILE("hwclock --systohc --{}"), clock_type), false);
#endif
}

// Create user on the system
void create_new_user(const std::string_view& user, const std::string_view& password, const std::string_view& shell) noexcept {
    spdlog::info("default shell: [{}]", shell);

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    if (shell.ends_with("zsh") || shell.ends_with("fish")) {
        const auto& packages  = fmt::format(FMT_COMPILE("cachyos-{}-config"), (shell.ends_with("zsh")) ? "zsh" : "fish");
        const auto& hostcache = std::get<std::int32_t>(config_data["hostcache"]);
        const auto& cmd       = (hostcache) ? "pacstrap" : "pacstrap -c";

        const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
        if (headless_mode) {
            utils::exec(fmt::format(FMT_COMPILE("{} {} {} |& tee /tmp/pacstrap.log"), cmd, mountpoint, packages), true);
        } else {
            tui::detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("{} {} {} |& tee /tmp/pacstrap.log"), cmd, mountpoint, packages)});
        }
    }

    // Create the user, set password, then remove temporary password file
    utils::arch_chroot("groupadd sudo", false);
    utils::arch_chroot(fmt::format(FMT_COMPILE("groupadd {}"), user), false);
    utils::arch_chroot(fmt::format(FMT_COMPILE("useradd {0} -m -g {0} -G sudo,storage,power,network,video,audio,lp,sys,input -s {1}"), user, shell), false);
    spdlog::info("add user to groups");

    // check if user has been created
    const auto& user_check = utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} getent passwd {}"), mountpoint, user));
    if (user_check.empty()) {
        spdlog::error("User has not been created!");
    }
    std::error_code err{};
    utils::exec(fmt::format(FMT_COMPILE("echo -e \"{0}\\n{0}\" > /tmp/.passwd"), password));
    const auto& ret_status = utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} passwd {} < /tmp/.passwd &>/dev/null"), mountpoint, user), true);
    spdlog::info("create user pwd: {}", ret_status);
    fs::remove("/tmp/.passwd", err);

    // Set up basic configuration files and permissions for user
    // arch_chroot "cp /etc/skel/.bashrc /home/${USER}"
    utils::arch_chroot(fmt::format(FMT_COMPILE("chown -R {0}:{0} /home/{0}"), user), false);
    const auto& sudoers_file = fmt::format(FMT_COMPILE("{}/etc/sudoers"), mountpoint);
    if (fs::exists(sudoers_file)) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i '/NOPASSWD/!s/# %sudo/%sudo/g' {}"), sudoers_file));
    }
#else
    spdlog::debug("user := {}, password := {}", user, password);
#endif
}

// Set password for root user
void set_root_password(const std::string_view& password) noexcept {
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);

    std::error_code err{};
    utils::exec(fmt::format(FMT_COMPILE("echo -e \"{0}\n{0}\" > /tmp/.passwd"), password));
    utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} passwd root < /tmp/.passwd &>/dev/null"), mountpoint));
    fs::remove("/tmp/.passwd", err);
#else
    spdlog::debug("root password := {}", password);
#endif
}

// Finds all available partitions according to type(s) specified and generates a list
// of them. This also includes partitions on different devices.
void find_partitions() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["PARTITIONS"]        = {};
    config_data["NUMBER_PARTITIONS"] = 0;
    auto& number_partitions          = std::get<std::int32_t>(config_data["NUMBER_PARTITIONS"]);
    const auto& include_part         = std::get<std::string>(config_data["INCLUDE_PART"]);

    // get the list of partitions and also include the zvols since it is common to mount filesystems directly on them.  It should be safe to include them here since they present as block devices.
    static constexpr auto other_piece = "sed 's/part$/\\/dev\\//g' | sed 's/lvm$\\|crypt$/\\/dev\\/mapper\\//g' | awk '{print $3$1 \" \" $2}' | awk '!/mapper/{a[++i]=$0;next}1;END{while(x<length(a))print a[++x]}' ; zfs list -Ht volume -o name,volsize 2>/dev/null | awk '{printf \"/dev/zvol/%s %s\\n\", $1, $2}'";
    const auto& partitions_tmp        = utils::exec(fmt::format(FMT_COMPILE("lsblk -lno NAME,SIZE,TYPE | grep '{}' | {}"), include_part, other_piece));

    spdlog::info("found partitions:\n{}", partitions_tmp);

    // create a raid partition list
    // old_ifs="$IFS"
    // IFS=$'\n'
    // raid_partitions=($(lsblk -lno NAME,SIZE,TYPE | grep raid | awk '{print $1,$2}' | uniq))
    // IFS="$old_ifs"

    // add raid partitions to partition_list
    // for i in "${raid_partitions[@]}"
    // do
    //    partition_list="${partition_list} /dev/md/${i}"
    // done

    const auto& partition_list = utils::make_multiline(partitions_tmp, true);
    config_data["PARTITIONS"]  = partition_list;
    number_partitions          = static_cast<std::int32_t>(partition_list.size());

    // for test delete /dev:sda8
    // delete_partition_in_list "/dev/sda8"

    // Deal with partitioning schemes appropriate to mounting, lvm, and/or luks.
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    if ((include_part == "part\\|lvm\\|crypt") && (((system_info == "UEFI") && (number_partitions < 2)) || ((system_info == "BIOS") && (number_partitions == 0)))) {
        // Deal with incorrect partitioning for main mounting function
        static constexpr auto PartErrBody = "\nBIOS systems require a minimum of one partition (ROOT).\n \nUEFI systems require a minimum of two partitions (ROOT and UEFI).\n";
        tui::detail::msgbox_widget(PartErrBody);
        tui::create_partitions();
        return;
    } else if (include_part == "part\\|crypt" && (number_partitions == 0)) {
        // Ensure there is at least one partition for LVM
        static constexpr auto LvmPartErrBody = "\nThere are no viable partitions available to use Logical Volume Manager.\nA minimum of one is required.\n\nIf LVM is already in use, deactivating it will allow the partition(s)\nused for its Physical Volume(s) to be used again.\n";
        tui::detail::msgbox_widget(LvmPartErrBody);
        tui::create_partitions();
        return;
    } else if (include_part == "part\\|lvm" && (number_partitions < 2)) {
        // Ensure there are at least two partitions for LUKS
        static constexpr auto LuksPartErrBody = "\nA minimum of two partitions are required for encryption:\n\n1. Root (/) - standard or lvm partition types.\n\n2. Boot (/boot or /boot/efi) - standard partition types only\n(except lvm where using BIOS Grub).\n";
        tui::detail::msgbox_widget(LuksPartErrBody);
        tui::create_partitions();
        return;
    }
}

auto get_pkglist_base(const std::string_view& packages) noexcept -> std::vector<std::string> {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& zfs       = std::get<std::int32_t>(config_data["ZFS"]);

    auto pkg_list = utils::make_multiline(packages, false, ' ');

    const auto pkg_count = pkg_list.size();
    for (std::size_t i = 0; i < pkg_count; ++i) {
        const auto& pkg = pkg_list[i];
        pkg_list.emplace_back(fmt::format(FMT_COMPILE("{}-headers"), pkg));
    }
    if (zfs == 1) {
        pkg_list.insert(pkg_list.cend(), {"zfs-utils", "linux-cachyos-zfs"});
    }
    pkg_list.insert(pkg_list.cend(), {"amd-ucode", "intel-ucode"});
    pkg_list.insert(pkg_list.cend(), {"base", "base-devel", "mkinitcpio", "vim", "wget", "micro", "nano", "networkmanager", "openssh", "ripgrep", "sed", "rsync", "power-profiles-daemon", "pacman-contrib", "paru"});
    pkg_list.insert(pkg_list.cend(), {"alacritty", "btop", "cachy-browser", "cachyos-fish-config", "cachyos-ananicy-rules", "cachyos-hello", "cachyos-hooks", "cachyos-kernel-manager", "cachyos-keyring"});
    pkg_list.insert(pkg_list.cend(), {"cachyos-mirrorlist", "cachyos-v3-mirrorlist", "cachyos-rate-mirrors", "cachyos-packageinstaller", "cachyos-settings", "cachyos-zsh-config", "mhwd-cachyos", "mhwd-db-cachyos"});

    return pkg_list;
}

auto get_pkglist_desktop(const std::string_view& desktop_env) noexcept -> std::vector<std::string> {
    std::vector<std::string> pkg_list{};

    constexpr std::string_view kde{"kde"};
    constexpr std::string_view cutefish{"cutefish"};
    constexpr std::string_view xfce{"xfce"};
    constexpr std::string_view sway{"sway"};
    constexpr std::string_view wayfire{"wayfire"};
    constexpr std::string_view i3wm{"i3wm"};
    constexpr std::string_view gnome{"gnome"};
    constexpr std::string_view openbox{"openbox"};
    constexpr std::string_view bspwm{"bspwm"};
    constexpr std::string_view lxqt{"lxqt"};
    constexpr std::string_view cinnamon{"cinnamon"};
    constexpr std::string_view ukui{"ukui"};

    bool needed_xorg{};
    auto found = ranges::search(desktop_env, i3wm);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"i3-wm", "i3blocks", "i3lock-color", "i3status", "rofi", "polybar", "ly", "cachyos-picom-config", "dunst", "cachyos-i3wm-settings"});
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, sway);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"sway", "waybar", "wl-clipboard", "egl-wayland", "wayland-protocols", "wofi", "ly", "xorg-xhost", "xorg-xwayland"});
    }
    found = ranges::search(desktop_env, kde);
    if (!found.empty()) {
        /* clang-format off */
        static constexpr std::array to_be_inserted{"ark", "audiocd-kio", "bluedevil", "breeze-gtk",
            "cachyos-lavender-kde-theme-git", "cachyos-nord-kde-theme-git", "cachyos-iridescent-kde", "cachyos-emerald-kde-theme-git",
            "cachyos-kde-settings", "cachyos-themes-sddm", "char-white", "dolphin", "egl-wayland", "gwenview",
            "konsole", "kate", "kdeconnect", "kscreen", "kde-gtk-config", "khotkeys", "kinfocenter",
            "kinit", "kscreen", "kwallet-pam", "plasma-wayland-protocols", "plasma-wayland-session",
            "plasma-desktop", "plasma-framework", "plasma-nm", "plasma-pa", "plasma-workspace", "plasma-integration",
            "plasma-firewall", "plasma-browser-integration", "plasma-systemmonitor", "plasma-thunderbolt",
            "powerdevil", "ksysguard", "spectacle", "sddm", "sddm-kcm", "xsettingsd", "xdg-desktop-portal-kde"};
        /* clang-format on */
        pkg_list.insert(pkg_list.end(), std::move_iterator(to_be_inserted.begin()),
            std::move_iterator(to_be_inserted.end()));
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, xfce);
    if (!found.empty()) {
        /* clang-format off */
        static constexpr std::array to_be_inserted{"cachyos-xfce-settings", "blueman", "file-roller", "galculator",
            "gvfs", "gvfs-afc", "gvfs-gphoto2", "gvfs-mtp", "gvfs-nfs", "gvfs-smb",
            "lightdm", "lightdm-gtk-greeter", "lightdm-gtk-greeter-settings",
            "network-manager-applet", "parole", "ristretto", "thunar-archive-plugin", "thunar-media-tags-plugin",
            "xdg-user-dirs-gtk", "xed", "xfce4", "xfce4-battery-plugin", "xfce4-datetime-plugin", "xfce4-mount-plugin",
            "xfce4-netload-plugin", "xfce4-notifyd", "xfce4-pulseaudio-plugin", "xfce4-screensaver", "xfce4-screenshooter",
            "xfce4-taskmanager", "xfce4-wavelan-plugin", "xfce4-weather-plugin", "xfce4-whiskermenu-plugin", "xfce4-xkb-plugin"};
        /* clang-format on */
        pkg_list.insert(pkg_list.end(), std::move_iterator(to_be_inserted.begin()),
            std::move_iterator(to_be_inserted.end()));
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, gnome);
    if (!found.empty()) {
        /* clang-format off */
        static constexpr std::array to_be_inserted{"adwaita-icon-theme", "cachyos-gnome-settings",
            "cachyos-nord-gtk-theme-git", "eog", "evince", "file-roller", "gdm", "gedit", "gnome-calculator",
            "gnome-control-center", "gnome-disk-utility", "gnome-keyring", "gnome-nettool", "gnome-power-manager",
            "gnome-screenshot", "gnome-shell", "gnome-terminal", "gnome-themes-extra", "gnome-tweaks",
            "gnome-usage", "gvfs", "gvfs-afc", "gvfs-gphoto2", "gvfs-mtp", "gvfs-nfs", "gvfs-smb",
            "nautilus", "nautilus-sendto", "sushi", "totem", "xdg-user-dirs-gtk"};
        /* clang-format on */
        pkg_list.insert(pkg_list.end(), std::move_iterator(to_be_inserted.begin()),
            std::move_iterator(to_be_inserted.end()));
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, cutefish);
    if (!found.empty()) {
        /* clang-format off */
        static constexpr std::array to_be_inserted{"fishui", "cutefish-calculator", "cutefish-core", "cutefish-dock",
            "cutefish-filemanager", "cutefish-kwin-plugins", "cutefish-launcher", "cutefish-screenlocker",
            "cutefish-settings", "cutefish-statusbar", "cutefish-terminal", "cutefish-wallpapers",
            "cutefish-icons", "cutefish-qt-plugins", "libwnck3", "sddm"};
        /* clang-format on */
        pkg_list.insert(pkg_list.end(), std::move_iterator(to_be_inserted.begin()),
            std::move_iterator(to_be_inserted.end()));
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, wayfire);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"cachyos-wayfire-settings", "wayfire-desktop-git", "egl-wayland", "wayland-protocols", "wofi", "ly", "xorg-xhost", "xorg-xwayland"});
    }
    found = ranges::search(desktop_env, openbox);
    if (!found.empty()) {
        /* clang-format off */
        static constexpr std::array to_be_inserted{"cachyos-openbox-settings", "obconf", "libwnck3", "acpi", "arandr",
            "archlinux-xdg-menu", "dex", "dmenu", "dunst", "feh", "gtk-engine-murrine", "gvfs", "gvfs-afc", "gvfs-gphoto2",
            "gvfs-mtp", "gvfs-nfs", "gvfs-smb", "jgmenu", "jq", "lightdm", "lightdm-slick-greeter",
            "lxappearance-gtk3", "mpv", "network-manager-applet", "nitrogen", "obconf", "openbox",
            "pasystray", "picom", "polkit-gnome", "rofi", "scrot", "slock", "sysstat", "thunar",
            "thunar-archive-plugin", "thunar-media-tags-plugin", "thunar-volman", "tint2",
            "ttf-nerd-fonts-symbols-1000-em", "tumbler", "xbindkeys", "xcursor-neutral",
            "xdg-user-dirs-gtk", "xed", "xfce4-terminal"};
        /* clang-format on */
        pkg_list.insert(pkg_list.end(), std::move_iterator(to_be_inserted.begin()),
            std::move_iterator(to_be_inserted.end()));
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, lxqt);
    if (!found.empty()) {
        /* clang-format off */
        static constexpr std::array to_be_inserted{"audiocd-kio", "baka-mplayer", "breeze", "breeze-gtk",
            "featherpad", "gvfs", "gvfs-mtp", "kio-fuse", "libstatgrab", "libsysstat", "lm_sensors",
            "lxqt", "lxqt-archiver", "network-manager-applet", "oxygen-icons", "pavucontrol-qt",
            "print-manager", "qt5-translations", "sddm", "xdg-utils", "xscreensaver", "xsettingsd"};
        /* clang-format on */
        pkg_list.insert(pkg_list.end(), std::move_iterator(to_be_inserted.begin()),
            std::move_iterator(to_be_inserted.end()));
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, bspwm);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"bspwm", "sxhkd", "polybar", "lightdm", "cachyos-picom-config"});
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, cinnamon);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"cinnamon", "system-config-printer", "gnome-keyring", "gnome-terminal", "blueberry", "metacity", "lightdm", "lightdm-gtk-greeter"});
        needed_xorg = true;
    }
    found = ranges::search(desktop_env, ukui);
    if (!found.empty()) {
        pkg_list.insert(pkg_list.cend(), {"sddm", "thunar", "thunar-archive-plugin", "thunar-volman", "peony", "xfce4-terminal", "ukui", "mate-terminal"});
        pkg_list.insert(pkg_list.cend(), {"mate-system-monitor", "mate-control-center", "redshift", "gnome-screenshot", "accountsservice", "gvfs", "qt5-quickcontrols"});
        needed_xorg = true;
    }

    if (needed_xorg) {
        pkg_list.insert(pkg_list.cend(), {"libwnck3", "mesa-utils", "xf86-input-libinput", "xorg-xdpyinfo", "xorg-server", "xorg-xinit", "xorg-xinput", "xorg-xkill", "xorg-xrandr"});
        pkg_list.insert(pkg_list.cend(), {"xf86-video-amdgpu", "xf86-video-ati", "xf86-video-intel"});
    }
    pkg_list.insert(pkg_list.cend(), {"cachyos", "octopi", "awesome-terminal-fonts", "noto-fonts-emoji"});

    return pkg_list;
}

void install_from_pkglist(const std::string_view& packages) noexcept {
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& hostcache  = std::get<std::int32_t>(config_data["hostcache"]);
    const auto& cmd        = (hostcache) ? "pacstrap" : "pacstrap -c";

#ifdef NDEVENV
    const auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    if (headless_mode) {
        utils::exec(fmt::format(FMT_COMPILE("{} {} {} |& tee /tmp/pacstrap.log"), cmd, mountpoint, packages), true);
    } else {
        tui::detail::follow_process_log_widget({"/bin/sh", "-c", fmt::format(FMT_COMPILE("{} {} {} |& tee /tmp/pacstrap.log"), cmd, mountpoint, packages)});
    }
#else
    spdlog::info("Installing from pkglist: '{} {} {} |& tee /tmp/pacstrap.log'", cmd, mountpoint, packages);
#endif
}

void install_base(const std::string_view& packages) noexcept {
    const auto& pkg_list  = utils::get_pkglist_base(packages);
    const auto& base_pkgs = utils::make_multiline(pkg_list, false, " ");

    spdlog::info(fmt::format(FMT_COMPILE("Preparing for pkgs to install: \"{}\""), base_pkgs));

#ifdef NDEVENV
    // Prep variables
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& zfs        = std::get<std::int32_t>(config_data["ZFS"]);

    const auto& initcpio_filename = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);
    auto initcpio                 = detail::Initcpio{initcpio_filename};

    // NOTE: make sure that we have valid initcpio config,
    // overwise we will end up here with unbootable system.
    initcpio.append_hooks({"base", "udev", "autodetect", "modconf", "block", "filesystems", "keyboard", "fsck"});

    // filter_packages
    utils::install_from_pkglist(base_pkgs);

    fs::copy_file("/etc/pacman.conf", fmt::format(FMT_COMPILE("{}/etc/pacman.conf"), mountpoint), fs::copy_options::overwrite_existing);

    static constexpr auto base_installed = "/mnt/.base_installed";
    std::ofstream{base_installed};

    // mkinitcpio handling for specific filesystems
    std::int32_t btrfs_root = 0;
    std::int32_t zfs_root   = 0;

    const auto& filesystem_type = utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint));
    spdlog::info("filesystem type on '{}' := '{}'", mountpoint, filesystem_type);
    if (filesystem_type == "btrfs") {
        btrfs_root = 1;
        initcpio.remove_hook("fsck");
        initcpio.append_module("btrfs");
    } else if (filesystem_type == "zfs") {
        zfs_root = 1;
        initcpio.remove_hook("fsck");
        initcpio.insert_hook("filesystems", "zfs");
        initcpio.append_file("\"/usr/lib/libgcc_s.so.1\"");
    }

    utils::recheck_luks();

    // add luks and lvm hooks as needed
    const auto& lvm  = std::get<std::int32_t>(config_data["LVM"]);
    const auto& luks = std::get<std::int32_t>(config_data["LUKS"]);
    spdlog::info("LVM := {}, LUKS := {}", lvm, luks);

    if (lvm == 1 && luks == 0) {
        initcpio.insert_hook("filesystems", "lvm2");
        spdlog::info("add lvm2 hook");
    } else if (lvm == 0 && luks == 1) {
        initcpio.insert_hook("keyboard", std::vector<std::string>{"consolefont", "keymap"});
        initcpio.insert_hook("filesystems", "encrypt");
        spdlog::info("add luks hook");
    } else if (lvm == 1 && luks == 1) {
        initcpio.insert_hook("keyboard", std::vector<std::string>{"consolefont", "keymap"});
        initcpio.insert_hook("filesystems", std::vector<std::string>{"encrypt", "lvm2"});
        spdlog::info("add lvm/luks hooks");
    }

    // Just explicitly flush the data to file,
    // if smth really happened between our calls.
    if (initcpio.parse_file()) {
        initcpio.write();
    }

    if (lvm + luks + btrfs_root + zfs_root > 0) {
        utils::arch_chroot("mkinitcpio -P");
        spdlog::info("re-run mkinitcpio");
    }

    utils::generate_fstab("genfstab -U -p");

    /* clang-format off */
    if (zfs == 0) { return; }
    /* clang-format on */

    // if we are using a zfs we should enable the zfs services
    utils::arch_chroot("systemctl enable zfs.target", false);
    utils::arch_chroot("systemctl enable zfs-import-cache", false);
    utils::arch_chroot("systemctl enable zfs-mount", false);
    utils::arch_chroot("systemctl enable zfs-import.target", false);

    // we also need create the cachefile
    utils::exec(fmt::format(FMT_COMPILE("zpool set cachefile=/etc/zfs/zpool.cache $(findmnt {} -lno SOURCE | {}) 2>>/tmp/cachyos-install.log"), mountpoint, "awk -F / '{print $1}'"), true);
    utils::exec(fmt::format(FMT_COMPILE("cp /etc/zfs/zpool.cache {}/etc/zfs/zpool.cache 2>>/tmp/cachyos-install.log"), mountpoint), true);
#endif
}

void install_desktop(const std::string_view& desktop) noexcept {
    // Create the base list of packages
    const auto& pkg_list        = utils::get_pkglist_desktop(desktop);
    const std::string& packages = utils::make_multiline(pkg_list, false, " ");

    spdlog::info(fmt::format(FMT_COMPILE("Preparing for desktop envs to install: \"{}\""), packages));
    utils::install_from_pkglist(packages);

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    config_data["DE"]     = std::string{desktop.data()};
    utils::enable_services();
}

void install_grub_uefi(const std::string_view& bootid, bool as_default) noexcept {
#ifdef NDEVENV
    fs::create_directory("/mnt/hostlvm");
    utils::exec("mount --bind /run/lvm /mnt/hostlvm");
#endif

    // if root is encrypted, amend /etc/default/grub
    const auto& root_name   = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_device = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {}"), root_name, "awk '/disk/ {print $1}'"));
    const auto& root_part   = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/part/p\" | {} | tr -cd '[:alnum:]'"), root_name, "awk '/part/ {print $1}'"));
#ifdef NDEVENV
    utils::boot_encrypted_setting();
#endif

    spdlog::info("root_name: {}. root_device: {}. root_part: {}", root_name, root_device, root_part);
    spdlog::info("Boot ID: {}", bootid);
    spdlog::info("Set as default: {}", as_default);

#ifdef NDEVENV
    auto* config_instance           = Config::instance();
    auto& config_data               = config_instance->data();
    const auto& uefi_mount          = std::get<std::string>(config_data["UEFI_MOUNT"]);
    const auto& mountpoint          = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& luks_dev            = std::get<std::string>(config_data["LUKS_DEV"]);
    const auto& grub_installer_path = fmt::format(FMT_COMPILE("{}/usr/bin/grub_installer.sh"), mountpoint);

    // grub config changes for zfs root
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint)) == "zfs") {
        // zfs needs ZPOOL_VDEV_NAME_PATH set to properly find the device
        utils::exec(fmt::format(FMT_COMPILE("echo ZPOOL_VDEV_NAME_PATH=YES >> {}/etc/environment"), mountpoint));
        setenv("ZPOOL_VDEV_NAME_PATH", "YES", 1);

        constexpr auto bash_codepart1 = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
export ZPOOL_VDEV_NAME_PATH=YES
pacman -S --noconfirm --needed grub efibootmgr dosfstools
# zfs is considered a sparse filesystem so we can't use SAVEDEFAULT
sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub
# we need to tell grub where the zfs root is)";

        const auto& mountpoint_source = utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o SOURCE {}"), mountpoint));
        const auto& zroot_var         = fmt::format(FMT_COMPILE("zroot=\"zfs={} rw\""), mountpoint_source);

        constexpr auto bash_codepart2 = R"(
sed -e '/^GRUB_CMDLINE_LINUX_DEFAULT=/s@"$@ '"${zroot}"'"@g' -e '/^GRUB_CMDLINE_LINUX=/s@"$@ '"${zroot}"'"@g' -i /etc/default/grub
sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub)";

        static constexpr auto mkconfig_codepart = "grub-mkconfig -o /boot/grub/grub.cfg";
        const auto& bash_code                   = fmt::format(FMT_COMPILE("{}\n{}\n{}\ngrub-install --target=x86_64-efi --efi-directory={} --bootloader-id={} --recheck\n{}\n"), bash_codepart1, zroot_var, bash_codepart2, uefi_mount, bootid, mkconfig_codepart);
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    } else {
        constexpr auto bash_codepart = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
pacman -S --noconfirm --needed grub efibootmgr dosfstools grub-btrfs grub-hook
findmnt | awk '/^\/ / {print $3}' | grep -q btrfs && sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub
lsblk -ino TYPE,MOUNTPOINT | grep " /$" | grep -q lvm && sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub)";

        static constexpr auto mkconfig_codepart = "grub-mkconfig -o /boot/grub/grub.cfg";
        const auto& bash_code                   = fmt::format(FMT_COMPILE("{}\ngrub-install --target=x86_64-efi --efi-directory={} --bootloader-id={} --recheck\n{}\n"), bash_codepart, uefi_mount, bootid, mkconfig_codepart);
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    }

    fs::permissions(grub_installer_path,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    // if the device is removable append removable to the grub-install
    const auto& removable = utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    if (utils::to_int(removable.data()) == 1) {
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/^grub-install /s/$/ --removable/g' -i {}"), grub_installer_path));
    }

    // If the root is on btrfs-subvolume, amend grub installation
    auto ret_status = utils::exec("mount | awk '$3 == \"/mnt\" {print $0}' | grep btrfs | grep -qv subvolid=5", true);
    if (ret_status != "0") {
        utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ grub-btrfs//g' -i {}"), grub_installer_path));
    }
    // If encryption used amend grub
    if (luks_dev != "") {
        const auto& luks_dev_formatted = utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | {}"), luks_dev, "awk '{print $1}'"));
        ret_status                     = utils::exec(fmt::format(FMT_COMPILE("echo \"sed -i \\\"s~GRUB_CMDLINE_LINUX=.*~GRUB_CMDLINE_LINUX=\\\\\\\"\"{}\\\\\\\"~g\\\"\" /etc/default/grub\" >> {}"), luks_dev_formatted, grub_installer_path), true);
        if (ret_status == "0") {
            spdlog::info("adding kernel parameter {}", luks_dev);
        }
    }

    // If Full disk encryption is used, use a keyfile
    const auto& fde = std::get<std::int32_t>(config_data["fde"]);
    if (fde == 1) {
        spdlog::info("Full disk encryption enabled");
        utils::exec(fmt::format(FMT_COMPILE("sed -i '3a\\grep -q \"^GRUB_ENABLE_CRYPTODISK=y\" /etc/default/grub || sed -i \"s/#GRUB_ENABLE_CRYPTODISK=y/GRUB_ENABLE_CRYPTODISK=y/\" /etc/default/grub' {}"), grub_installer_path));
    }

    std::error_code err{};

    // install grub
    utils::arch_chroot("grub_installer.sh");
    umount("/mnt/hostlvm");
    fs::remove("/mnt/hostlvm", err);

    // the grub_installer is no longer needed
    fs::remove(grub_installer_path, err);

    /* clang-format off */
    if (!as_default) { return; }
    /* clang-format on */

    utils::arch_chroot(fmt::format(FMT_COMPILE("mkdir -p {}/EFI/boot"), uefi_mount), false);
    spdlog::info("Grub efi binary status:(EFI/cachyos/grubx64.efi): {}", fs::exists(fmt::format(FMT_COMPILE("{0}/EFI/cachyos/grubx64.efi"), uefi_mount)));
    utils::arch_chroot(fmt::format(FMT_COMPILE("cp -r {0}/EFI/cachyos/grubx64.efi {0}/EFI/boot/bootx64.efi"), uefi_mount), false);
#endif
}

void install_refind() noexcept {
    spdlog::info("Installing refind...");
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);
    const auto& luks       = std::get<std::int32_t>(config_data["LUKS"]);
    const auto& luks_dev   = std::get<std::string>(config_data["LUKS_DEV"]);

    utils::inst_needed("refind");

    // Check if the volume is removable. If so, install all drivers
    const auto& root_name   = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_device = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {}"), root_name, "awk '/disk/ {print $1}'"));
    spdlog::info("root_name: {}. root_device: {}", root_name, root_device);

    // Clean the configuration in case there is previous one because the configuration part is not idempotent
    if (fs::exists("/mnt/boot/refind_linux.conf")) {
        std::error_code err{};
        fs::remove("/mnt/boot/refind_linux.conf", err);
    }

    // install refind
    const auto& removable = utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    if (utils::to_int(removable.data()) == 1) {
        utils::exec("refind-install --root /mnt --alldrivers --yes 2>>/tmp/cachyos-install.log &>/dev/null");

        const auto& initcpio_filename = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);
        auto initcpio                 = detail::Initcpio{initcpio_filename};

        // Remove autodetect hook
        initcpio.remove_hook("autodetect");
        spdlog::info("\"Autodetect\" hook was removed");
    } else if (luks == 1) {
        utils::exec("refind-install --root /mnt --alldrivers --yes 2>>/tmp/cachyos-install.log &>/dev/null");
    } else {
        utils::exec("refind-install --root /mnt 2>>/tmp/cachyos-install.log &>/dev/null");
    }

    // Mount as rw
    // sed -i 's/ro\ /rw\ \ /g' /mnt/boot/refind_linux.conf

    // Boot in graphics mode
    utils::exec(fmt::format(FMT_COMPILE("sed -i -e '/use_graphics_for/ s/^#*//' {}{}/EFI/refind/refind.conf"), mountpoint, uefi_mount));
    // Set appropriate rootflags if installed on btrs subvolume
    if (utils::exec("mount | awk '$3 == \"/mnt\" {print $0}' | grep btrfs | grep -qv subvolid=5", true) == "0") {
        const auto& rootflag = fmt::format(FMT_COMPILE("rootflags={}"), utils::exec("mount | awk '$3 == \"/mnt\" {print $6}' | sed 's/^.*subvol=/subvol=/' | sed -e 's/,.*$/,/p' | sed 's/)//g'"));
        utils::exec(fmt::format(FMT_COMPILE("sed -i \"s|\\\"$|\\ {}\\\"|g\" /mnt/boot/refind_linux.conf"), rootflag));
    }

    // LUKS and lvm with LUKS
    if (luks == 1) {
        const auto& mapper_name = utils::exec("mount | awk '/\\/mnt / {print $1}'");
        utils::exec(fmt::format(FMT_COMPILE("sed -i \"s|root=.* |{} root={} |g\" /mnt/boot/refind_linux.conf"), luks_dev, mapper_name));
        utils::exec("sed -i '/Boot with minimal options/d' /mnt/boot/refind_linux.conf");
    }
    // Lvm without LUKS
    else if (utils::exec("lsblk -i | sed -r 's/^[^[:alnum:]]+//' | grep \"/mnt$\" | awk '{print $6}'") == "lvm") {
        const auto& mapper_name = utils::exec("mount | awk '/\\/mnt / {print $1}'");
        utils::exec(fmt::format(FMT_COMPILE("sed -i \"s|root=.* |root={} |g\" /mnt/boot/refind_linux.conf"), mapper_name));
        utils::exec("sed -i '/Boot with minimal options/d' /mnt/boot/refind_linux.conf");
    }
    // Figure out microcode
    const auto& rootsubvol = utils::exec("findmnt -o TARGET,SOURCE | awk '/\\/mnt / {print $2}' | grep -o \"\\[.*\\]\" | cut -d \"[\" -f2 | cut -d \"]\" -f1 | sed 's/^\\///'");
    const auto& ucode      = utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qqs ucode 2>>/tmp/cachyos-install.log"), mountpoint));
    if (utils::to_int(utils::exec(fmt::format(FMT_COMPILE("echo {} | wc -l)"), ucode)).data()) > 1) {
        // set microcode
        if (utils::exec("findmnt -o TARGET,SOURCE | grep -q \"/mnt/boot \"", true) == "0") {
            // there is a separate boot, path to microcode is at partition root
            utils::exec("sed -i \"s|\\\"$| initrd=/intel-ucode.img initrd=/amd-ucode.img initrd=/initramfs-%v.img\\\"|g\" /mnt/boot/refind_linux.conf");
        } else if (!rootsubvol.empty()) {
            // Initramfs is on the root partition and root is on btrfs subvolume
            utils::exec(fmt::format(FMT_COMPILE("sed -i \"s|\\\"$| initrd={0}/boot/intel-ucode.img initrd={0}/boot/amd-ucode.img initrd={0}/boot/initramfs-%v.img\\\"|g\" /mnt/boot/refind_linux.conf"), rootsubvol));
        } else {
            // Initramfs is on the root partition
            utils::exec("sed -i \"s|\\\"$| initrd=/boot/intel-ucode.img initrd=/boot/amd-ucode.img initrd=/boot/initramfs-%v.img\\\"|g\" /mnt/boot/refind_linux.conf");
        }
    } else {
        if (utils::exec("findmnt -o TARGET,SOURCE | grep -q \"/mnt/boot \"", true) == "0") {
            // there is a separate boot, path to microcode is at partition root
            utils::exec(fmt::format(FMT_COMPILE("sed -i \"s|\\\"$| initrd=/{}.img initrd=/initramfs-%v.img\\\"|g\" /mnt/boot/refind_linux.conf"), ucode));
        } else if (!rootsubvol.empty()) {
            // Initramfs is on the root partition and root is on btrfs subvolume
            utils::exec(fmt::format(FMT_COMPILE("sed -i \"s|\\\"$| initrd={0}/boot/{1}.img initrd={0}/boot/initramfs-%v.img\\\"|g\" /mnt/boot/refind_linux.conf"), rootsubvol, ucode));
        } else {
            // Initramfs is on the root partition
            utils::exec(fmt::format(FMT_COMPILE(" sed -i \"s|\\\"$| initrd=/boot/{}.img initrd=/boot/initramfs-%v.img\\\"|g\" /mnt/boot/refind_linux.conf"), ucode));
        }
    }

    utils::install_from_pkglist("refind-theme-nord");
#endif
    spdlog::info("Refind was succesfully installed");
}

void install_systemd_boot() noexcept {
    spdlog::info("Installing systemd-boot...");
#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& uefi_mount = std::get<std::string>(config_data["UEFI_MOUNT"]);

    utils::arch_chroot(fmt::format(FMT_COMPILE("bootctl --path={} install"), uefi_mount), false);
    utils::install_from_pkglist("systemd-boot-manager");
    utils::arch_chroot("sdboot-manage gen", false);

    // Check if the volume is removable. If so, don't use autodetect
    const auto& root_name   = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_device = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {}"), root_name, "awk '/disk/ {print $1}'"));
    spdlog::info("root_name: {}. root_device: {}", root_name, root_device);
    const auto& removable = utils::exec(fmt::format(FMT_COMPILE("cat /sys/block/{}/removable"), root_device));
    if (utils::to_int(removable.data()) == 1) {
        const auto& mountpoint        = std::get<std::string>(config_data["MOUNTPOINT"]);
        const auto& initcpio_filename = fmt::format(FMT_COMPILE("{}/etc/mkinitcpio.conf"), mountpoint);
        auto initcpio                 = detail::Initcpio{initcpio_filename};

        // Remove autodetect hook
        initcpio.remove_hook("autodetect");
        spdlog::info("\"Autodetect\" hook was removed");
    }
#endif
    spdlog::info("Systemd-boot was installed");
}

void uefi_bootloader(const std::string_view& bootloader) noexcept {
#ifdef NDEVENV
    // Ensure again that efivarfs is mounted
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out.empty()) {
            if (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0) {
                perror("utils::uefi_bootloader");
                exit(1);
            }
        }
    }
#endif

    if (bootloader == "grub") {
        utils::install_grub_uefi("cachyos");
    } else if (bootloader == "refind") {
        utils::install_refind();
    } else if (bootloader == "systemd-boot") {
        utils::install_systemd_boot();
    } else {
        spdlog::error("Unknown bootloader!");
    }
}

void bios_bootloader(const std::string_view& bootloader) noexcept {
    spdlog::info("Installing bios bootloader '{}'...", bootloader);
#ifdef NDEVENV
    // if root is encrypted, amend /etc/default/grub
    utils::boot_encrypted_setting();

    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    const auto& lvm          = std::get<std::int32_t>(config_data["LVM"]);
    const auto& lvm_sep_boot = std::get<std::int32_t>(config_data["LVM_SEP_BOOT"]);
    const auto& luks_dev     = std::get<std::string>(config_data["LUKS_DEV"]);
    const auto& mountpoint   = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& device_info  = std::get<std::string>(config_data["DEVICE"]);

    // if /boot is LVM (whether using a seperate /boot mount or not), amend grub
    if ((lvm == 1 && lvm_sep_boot == 0) || lvm_sep_boot == 2) {
        utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/GRUB_PRELOAD_MODULES=\\\"/GRUB_PRELOAD_MODULES=\\\"lvm /g\" {}/etc/default/grub"), mountpoint));
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i {}/etc/default/grub"), mountpoint));
    }

    // If root is on btrfs volume, amend grub
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -no FSTYPE {}"), mountpoint)) == "btrfs") {
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i {}/etc/default/grub"), mountpoint));
    }

    // Same setting is needed for LVM
    if (lvm == 1) {
        utils::exec(fmt::format(FMT_COMPILE("sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i {}/etc/default/grub"), mountpoint));
    }

    const auto& grub_installer_path = fmt::format(FMT_COMPILE("{}/usr/bin/grub_installer.sh"), mountpoint);

    // grub config changes for zfs root
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o FSTYPE \"{}\""), mountpoint)) == "zfs") {
        // zfs needs ZPOOL_VDEV_NAME_PATH set to properly find the device
        utils::exec(fmt::format(FMT_COMPILE("echo ZPOOL_VDEV_NAME_PATH=YES >> {}/etc/environment"), mountpoint));
        setenv("ZPOOL_VDEV_NAME_PATH", "YES", 1);

        constexpr auto bash_codepart1 = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
export ZPOOL_VDEV_NAME_PATH=YES
pacman -S --noconfirm --needed grub os-prober
# zfs is considered a sparse filesystem so we can't use SAVEDEFAULT
sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub
# we need to tell grub where the zfs root is)";

        const auto& mountpoint_source = utils::exec(fmt::format(FMT_COMPILE("findmnt -ln -o SOURCE {}"), mountpoint));
        const auto& zroot_var         = fmt::format(FMT_COMPILE("zroot=\"zfs={} rw\""), mountpoint_source);

        constexpr auto bash_codepart2 = R"(
sed -e '/^GRUB_CMDLINE_LINUX_DEFAULT=/s@"$@ '"${zroot}"'"@g' -e '/^GRUB_CMDLINE_LINUX=/s@"$@ '"${zroot}"'"@g' -i /etc/default/grub
sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub
grub-install --target=i386-pc --recheck)";

        static constexpr auto mkconfig_codepart = "grub-mkconfig -o /boot/grub/grub.cfg";
        const auto& bash_code                   = fmt::format(FMT_COMPILE("{}\n{}\n{} {}\n{}\n"), bash_codepart1, zroot_var, bash_codepart2, device_info, mkconfig_codepart);
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    } else {
        constexpr auto bash_codepart = R"(#!/bin/bash
ln -s /hostlvm /run/lvm
pacman -S --noconfirm --needed grub os-prober grub-btrfs grub-hook
findmnt | awk '/^\/ / {print $3}' | grep -q btrfs && sed -e '/GRUB_SAVEDEFAULT/ s/^#*/#/' -i /etc/default/grub
grub-install --target=i386-pc --recheck)";

        static constexpr auto mkconfig_codepart = "grub-mkconfig -o /boot/grub/grub.cfg";
        const auto& bash_code                   = fmt::format(FMT_COMPILE("{} {}\n{}\n"), bash_codepart, device_info, mkconfig_codepart);
        std::ofstream grub_installer{grub_installer_path};
        grub_installer << bash_code;
    }

    // If the root is on btrfs-subvolume, amend grub installation
    auto ret_status = utils::exec("mount | awk '$3 == \"/mnt\" {print $0}' | grep btrfs | grep -qv subvolid=5", true);
    if (ret_status != "0") {
        utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ grub-btrfs//g' -i {}"), grub_installer_path));
    }

    // If encryption used amend grub
    if (luks_dev != "") {
        const auto& luks_dev_formatted = utils::exec(fmt::format(FMT_COMPILE("echo \"{}\" | {}"), luks_dev, "awk '{print $1}'"));
        ret_status                     = utils::exec(fmt::format(FMT_COMPILE("echo \"sed -i \\\"s~GRUB_CMDLINE_LINUX=.*~GRUB_CMDLINE_LINUX=\\\\\\\"\"{}\\\\\\\"~g\\\"\" /etc/default/grub\" >> {}"), luks_dev_formatted, grub_installer_path), true);
        if (ret_status == "0") {
            spdlog::info("adding kernel parameter {}", luks_dev);
        }
    }

    // If Full disk encryption is used, use a keyfile
    const auto& fde = std::get<std::int32_t>(config_data["fde"]);
    if (fde == 1) {
        spdlog::info("Full disk encryption enabled");
        utils::exec(fmt::format(FMT_COMPILE("sed  -i '3a\\grep -q \"^GRUB_ENABLE_CRYPTODISK=y\" /etc/default/grub || sed -i \"s/#GRUB_ENABLE_CRYPTODISK=y/GRUB_ENABLE_CRYPTODISK=y/\" /etc/default/grub' {}"), grub_installer_path));
    }

    // Remove os-prober if not selected
    constexpr std::string_view needle{"os-prober"};
    const auto& found = ranges::search(bootloader, needle);
    if (found.empty()) {
        utils::exec(fmt::format(FMT_COMPILE("sed -e 's/ os-prober//g' -i {}"), grub_installer_path));
    }

    std::error_code err{};

    fs::permissions(grub_installer_path,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    tui::detail::infobox_widget("\nPlease wait...\n");
    utils::exec(fmt::format(FMT_COMPILE("dd if=/dev/zero of={} seek=1 count=2047"), device_info));
    fs::create_directory("/mnt/hostlvm", err);
    utils::exec("mount --bind /run/lvm /mnt/hostlvm");

    // install grub
    utils::arch_chroot("grub_installer.sh");

    // the grub_installer is no longer needed - there still needs to be a better way to do this
    fs::remove(grub_installer_path, err);

    umount("/mnt/hostlvm");
    fs::remove("/mnt/hostlvm", err);
#endif
}

void install_bootloader(const std::string_view& bootloader) noexcept {
    /* clang-format off */
    if (!utils::check_base()) { return; }
    /* clang-format on */

    auto* config_instance   = Config::instance();
    auto& config_data       = config_instance->data();
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    if (system_info == "BIOS")
        utils::bios_bootloader(bootloader);
    else
        utils::uefi_bootloader(bootloader);
}

// List partitions to be hidden from the mounting menu
std::string list_mounted() noexcept {
    utils::exec("lsblk -l | awk '$7 ~ /mnt/ {print $1}' > /tmp/.mounted");
    return utils::exec("echo /dev/* /dev/mapper/* | xargs -n1 2>/dev/null | grep -f /tmp/.mounted");
}

std::string list_containing_crypt() noexcept {
    return utils::exec("blkid | awk '/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

std::string list_non_crypt() noexcept {
    return utils::exec("blkid | awk '!/TYPE=\"crypto_LUKS\"/{print $1}' | sed 's/.$//'");
}

void get_cryptroot() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Identify if /mnt or partition is type "crypt" (LUKS on LVM, or LUKS alone)
    if ((utils::exec("lsblk | sed -r 's/^[^[:alnum:]]+//' | awk '/\\/mnt$/ {print $6}' | grep -q crypt", true) != "0")
        || (utils::exec("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt$/,/part/p\" | awk '{print $6}' | grep -q crypt", true) != "0")) {
        return;
    }

    config_data["LUKS"] = 1;
    auto& luks_name     = std::get<std::string>(config_data["LUKS_ROOT_NAME"]);
    luks_name           = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    // Get the name of the Luks device
    if (utils::exec("lsblk -i | grep -q -e \"crypt /mnt\"", true) != "0") {
        // Mountpoint is not directly on LUKS device, so we need to get the crypt device above the mountpoint
        luks_name = utils::exec("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt$/,/crypt/p\" | awk '/crypt/ {print $1}'");
    }

    const auto& check_cryptparts = [&luks_name](const auto& cryptparts, auto functor) {
        for (const auto& cryptpart : cryptparts) {
            if (!utils::exec(fmt::format(FMT_COMPILE("lsblk -lno NAME {} | grep \"{}\""), cryptpart, luks_name)).empty()) {
                functor(cryptpart);
            }
        }
    };

    // Check if LUKS on LVM (parent = lvm /dev/mapper/...)
    auto temp_out = utils::exec("lsblk -lno NAME,FSTYPE,TYPE,MOUNTPOINT | grep \"lvm\" | grep \"/mnt$\" | grep -i \"crypto_luks\" | uniq | awk '{print \"/dev/mapper/\"$1}'");
    if (!temp_out.empty()) {
        const auto& cryptparts    = utils::make_multiline(temp_out);
        const auto& check_functor = [&](const auto& cryptpart) {
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("cryptdevice={}:{}"), cryptpart, luks_name);
            config_data["LVM"]      = 1;
        };
        check_cryptparts(cryptparts, check_functor);
        return;
    }

    // Check if LVM on LUKS
    temp_out = utils::exec("lsblk -lno NAME,FSTYPE,TYPE | grep \" crypt$\" | grep -i \"LVM2_member\" | uniq | awk '{print \"/dev/mapper/\"$1}'");
    if (!temp_out.empty()) {
        const auto& cryptparts    = utils::make_multiline(temp_out);
        const auto& check_functor = [&]([[maybe_unused]] const auto& cryptpart) {
            auto& luks_uuid         = std::get<std::string>(config_data["LUKS_UUID"]);
            luks_uuid               = utils::exec("lsblk -ino NAME,FSTYPE,TYPE,MOUNTPOINT,UUID | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt /,/part/p\" | awk '/crypto_LUKS/ {print $4}'");
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), luks_uuid, luks_name);
            config_data["LVM"]      = 1;
        };
        check_cryptparts(cryptparts, check_functor);
        return;
    }

    // Check if LUKS alone (parent = part /dev/...)
    temp_out = utils::exec("lsblk -lno NAME,FSTYPE,TYPE,MOUNTPOINT | grep \"/mnt$\" | grep \"part\" | grep -i \"crypto_luks\" | uniq | awk '{print \"/dev/\"$1}'");
    if (!temp_out.empty()) {
        const auto& cryptparts    = utils::make_multiline(temp_out);
        const auto& check_functor = [&](const auto& cryptpart) {
            auto& luks_uuid         = std::get<std::string>(config_data["LUKS_UUID"]);
            luks_uuid               = utils::exec(fmt::format(FMT_COMPILE("lsblk -lno UUID,TYPE,FSTYPE {} | grep \"part\" | grep -i \"crypto_luks\" | {}"), cryptpart, "awk '{print $1}'"));
            config_data["LUKS_DEV"] = fmt::format(FMT_COMPILE("cryptdevice=UUID={}:{}"), luks_uuid, luks_name);
        };
        check_cryptparts(cryptparts, check_functor);
        return;
    }
}

void recheck_luks() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    const auto& root_name = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");

    // Check if there is separate encrypted /boot partition
    if (utils::exec("lsblk | grep '/mnt/boot' | grep -q 'crypt'", true) == "0") {
        config_data["LUKS"] = 1;
    }
    // Check if root is encrypted and there is no separate /boot
    else if ((utils::exec("lsblk | grep \"/mnt$\" | grep -q 'crypt'", true) == "0") && utils::exec("lsblk | grep \"/mnt/boot$\"", false).empty()) {
        config_data["LUKS"] = 1;
    }
    // Check if root is on encrypted lvm volume
    else if ((utils::exec("lsblk | grep '/mnt/boot' | grep -q 'crypt'", true) == "0") && (utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {} | grep -q crypt"), root_name, "awk '{print $6}'"), true) == "0")) {
        config_data["LUKS"] = 1;
    }
}

void get_cryptboot() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // If /boot is encrypted
    if ((utils::exec("lsblk | sed -r 's/^[^[:alnum:]]+//' | awk '/\\/mnt\\/boot$/ {print $6}' | grep -q crypt", true) != "0")
        || (utils::exec("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt\\/boot$/,/part/p\" | awk '{print $6}' | grep -q crypt", true) != "0")) {
        return;
    }
    config_data["LUKS"] = 1;

    // Mountpoint is directly on the LUKS device, so LUKS device is the same as root name
    std::string boot_name{utils::exec("mount | awk '/\\/mnt\\/boot / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g")};
    // Get UUID of the encrypted /boot
    std::string boot_uuid{utils::exec("lsblk -lno UUID,MOUNTPOINT | awk '/\\mnt\\/boot$/ {print $1}'")};

    // Get the name of the Luks device
    if (utils::exec("lsblk -i | grep -q -e \"crypt /mnt\"", true) != "0") {
        // Mountpoint is not directly on LUKS device, so we need to get the crypt device above the mountpoint
        boot_name = utils::exec("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt\\/boot$/,/crypt/p\" | awk '/crypt/ {print $1}'");
        boot_uuid = utils::exec("lsblk -ino NAME,FSTYPE,TYPE,MOUNTPOINT,UUID | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/\\/mnt\\/boot /,/part/p\" | awk '/crypto_LUKS/ {print $4}'");
    }

    // Check if LVM on LUKS
    if (utils::exec("lsblk -lno TYPE,MOUNTPOINT | grep \"/mnt/boot$\" | grep -q lvm", true) == "0") {
        config_data["LVM"] = 1;
    }

    // Add cryptdevice to LUKS_DEV, if not already present (if on same LVM on LUKS as /)
    auto& luks_dev    = std::get<std::string>(config_data["LUKS_DEV"]);
    const auto& found = ranges::search(luks_dev, boot_uuid);
    if (found.empty()) {
        luks_dev = fmt::format(FMT_COMPILE("{} cryptdevice=UUID={}:{}"), luks_dev, boot_uuid, boot_name);
    }
}

void boot_encrypted_setting() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["fde"] = 0;
    auto& fde          = std::get<std::int32_t>(config_data["fde"]);

    // Check if there is separate /boot partition
    if (utils::exec("lsblk | grep \"/mnt/boot$\"").empty()) {
        // There is no separate /boot parition
        const auto& root_name = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
        const auto& luks      = std::get<std::int32_t>(config_data["LUKS"]);
        // Check if root is encrypted
        if ((luks == 1)
            || (utils::exec(fmt::format(FMT_COMPILE("lsblk \"/dev/mapper/{}\" | grep -q 'crypt'"), root_name), true) == "0")
            || (utils::exec("lsblk | grep \"/mnt$\" | grep -q 'crypt'", true) == "0")
            // Check if root is on encrypted lvm volume
            || (utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {} | grep -q crypt"), root_name, "awk '{print $6}'"), true) == "0")) {
            fde = 1;
            utils::setup_luks_keyfile();
        }
        return;
    }
    // There is a separate /boot. Check if it is encrypted
    const auto& boot_name = utils::exec("mount | awk '/\\/mnt\\/boot / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    if ((utils::exec("lsblk | grep '/mnt/boot' | grep -q 'crypt'", true) == "0")
        // Check if the /boot is inside encrypted lvm volume
        || (utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/disk/p\" | {} | grep -q crypt"), boot_name, "awk '{print $6}'"), true) == "0")
        || (utils::exec(fmt::format(FMT_COMPILE("lsblk \"/dev/mapper/{}\" | grep -q 'crypt'"), boot_name), true) == "0")) {
        fde = 1;
        utils::setup_luks_keyfile();
    }
}

// Ensure that a partition is mounted
bool check_mount() noexcept {
#ifdef NDEVENV
    auto* config_instance       = Config::instance();
    auto& config_data           = config_instance->data();
    const auto& mountpoint_info = std::get<std::string>(config_data["MOUNTPOINT"]);
    if (utils::exec(fmt::format(FMT_COMPILE("findmnt -nl {}"), mountpoint_info)) == "") {
        return false;
    }
#endif
    return true;
}

// Ensure that CachyOS has been installed
bool check_base() noexcept {
#ifdef NDEVENV
    if (!check_mount()) {
        return false;
    }
    if (!fs::exists("/mnt/usr/bin/pacman")) {
        tui::detail::msgbox_widget("\nThe CachyOS base must be installed first.\n");
        return false;
    }
#endif
    return true;
}

void id_system() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Apple System Detection
    const auto& sys_vendor = utils::exec("cat /sys/class/dmi/id/sys_vendor");
    if ((sys_vendor == "Apple Inc.") || (sys_vendor == "Apple Computer, Inc."))
        utils::exec("modprobe -r -q efivars || true");  // if MAC
    else
        utils::exec("modprobe -q efivarfs");  // all others

    // BIOS or UEFI Detection
    static constexpr auto efi_path = "/sys/firmware/efi/";
    if (fs::exists(efi_path) && fs::is_directory(efi_path)) {
        // Mount efivarfs if it is not already mounted
        const auto& mount_out = utils::exec("mount | grep /sys/firmware/efi/efivars");
        if (mount_out.empty()) {
            if (mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, "") != 0) {
                perror("utils::id_system");
                exit(1);
            }
        }
        config_data["SYSTEM"] = "UEFI";
    }

    // init system
    const auto& init_sys = utils::exec("cat /proc/1/comm");
    auto& h_init         = std::get<std::string>(config_data["H_INIT"]);
    if (init_sys == "systemd")
        h_init = "systemd";

    // TODO: Test which nw-client is available, including if the service according to $H_INIT is running
    if (h_init == "systemd" && utils::exec("systemctl is-active NetworkManager") == "active")
        config_data["NW_CMD"] = "nmtui";
}

void install_cachyos_repo() noexcept {
    const auto& add_arch_specific_repo = [](auto&& isa_level, auto&& repo_name, const auto& isa_levels, [[maybe_unused]] auto&& repos_data) {
        if (!ranges::contains(isa_levels, isa_level)) {
            spdlog::warn("{} is not supported", isa_level);
            return;
        }
        spdlog::info("{} is supported", isa_level);

        const auto& repo_list = detail::pacmanconf::get_repo_list("/etc/pacman.conf");
        if (ranges::contains(repo_list, fmt::format(FMT_COMPILE("[{}]"), repo_name))) {
            spdlog::info("'{}' is already added!", repo_name);
            return;
        }

#ifdef NDEVENV
        static constexpr auto pacman_conf             = "/etc/pacman.conf";
        static constexpr auto pacman_conf_cachyos     = "./pacman.conf";
        static constexpr auto pacman_conf_path_backup = "/etc/pacman.conf.bak";
        std::error_code err{};

        if (!fs::copy_file(pacman_conf, pacman_conf_cachyos, err)) {
            spdlog::error("Failed to copy pacman config [{}]", err.message());
            return;
        }

        detail::pacmanconf::push_repos_front(pacman_conf_cachyos, repos_data);

        spdlog::info("backup old config");
        fs::rename(pacman_conf, pacman_conf_path_backup, err);

        spdlog::info("CachyOS {} Repo changed", repo_name);
        fs::rename(pacman_conf_cachyos, pacman_conf, err);
#endif
    };

    // Check if it's already been applied
    const auto& repo_list = detail::pacmanconf::get_repo_list("/etc/pacman.conf");
    if (!ranges::none_of(repo_list, [](auto&& repo_name) {
            static constexpr std::array cachyos_repos{"[cachyos]", "[cachyos-v3]", "[cachyos-v4]"};
            return ranges::contains(cachyos_repos, repo_name);
        })) {
        spdlog::info("Repo is already added!");
        return;
    }

#ifdef NDEVENV
    utils::exec("pacman-key --recv-keys F3B607488DB35A47 --keyserver keyserver.ubuntu.com", true);
    utils::exec("pacman-key --lsign-key F3B607488DB35A47", true);
#endif

    const auto& isa_levels = utils::get_isa_levels();

    static constexpr auto CACHYOS_V1_REPO_STR = R"(
[cachyos]
Include = /etc/pacman.d/cachyos-mirrorlist
)";
    static constexpr auto CACHYOS_V3_REPO_STR = R"(
[cachyos-v3]
Include = /etc/pacman.d/cachyos-v3-mirrorlist
)";
    static constexpr auto CACHYOS_V4_REPO_STR = R"(
[cachyos-v4]
Include = /etc/pacman.d/cachyos-v4-mirrorlist
)";

    add_arch_specific_repo("x86_64", "cachyos", isa_levels, CACHYOS_V1_REPO_STR);
    add_arch_specific_repo("x86_64_v3", "cachyos-v3", isa_levels, CACHYOS_V3_REPO_STR);
    add_arch_specific_repo("x86_64_v4", "cachyos-v4", isa_levels, CACHYOS_V4_REPO_STR);
}

bool handle_connection() noexcept {
    bool connected{utils::is_connected()};

#ifdef NDEVENV
    static constexpr std::int32_t CONNECTION_TIMEOUT = 15;
    if (!connected) {
        warning_inter("An active network connection could not be detected, waiting 15 seconds ...\n");

        std::int32_t time_waited{};

        while (!(connected = utils::is_connected())) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            if (time_waited++ >= CONNECTION_TIMEOUT) {
                break;
            }
        }

        if (!connected) {
            char type{};

            while (utils::prompt_char("An active network connection could not be detected, do you want to connect using wifi? [y/n]", RED, &type)) {
                if (type != 'n') {
                    show_iwctl();
                }

                break;
            }

            connected = utils::is_connected();
        }
    }

    if (connected) {
        utils::install_cachyos_repo();
        utils::exec("yes | pacman -Sy --noconfirm", true);
    }
#else
    utils::install_cachyos_repo();
#endif

    return connected;
}

void show_iwctl() noexcept {
    info_inter("\nInstructions to connect to wifi using iwctl:\n");
    info_inter("1 - To find your wifi device name (ex: wlan0) type `device list`\n");
    info_inter("2 - type `station wlan0 scan`, and wait couple seconds\n");
    info_inter("3 - type `station wlan0 get-networks` (find your wifi Network name ex. my_wifi)\n");
    info_inter("4 - type `station wlan0 connect my_wifi` (don't forget to press TAB for auto completion!\n");
    info_inter("5 - type `station wlan0 show` (status should be connected)\n");
    info_inter("6 - type `exit`\n");

    while (utils::prompt_char("Press a key to continue...", CYAN)) {
        utils::exec("iwctl", true);
        break;
    }
}

void set_keymap() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();
    const auto& keymap    = std::get<std::string>(config_data["KEYMAP"]);

    utils::exec(fmt::format(FMT_COMPILE("loadkeys {}"), keymap));
}

void enable_autologin([[maybe_unused]] const std::string_view& dm, [[maybe_unused]] const std::string_view& user) noexcept {
#ifdef NDEVENV
    // enable autologin
    if (dm == "gdm") {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^AutomaticLogin=*/AutomaticLogin={}/g' /mnt/etc/gdm/custom.conf"), user));
        utils::exec("sed -i 's/^AutomaticLoginEnable=*/AutomaticLoginEnable=true/g' /mnt/etc/gdm/custom.conf");
        utils::exec("sed -i 's/^TimedLoginEnable=*/TimedLoginEnable=true/g' /mnt/etc/gdm/custom.conf");
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^TimedLogin=*/TimedLoginEnable={}/g' /mnt/etc/gdm/custom.conf"), user));
        utils::exec("sed -i 's/^TimedLoginDelay=*/TimedLoginDelay=0/g' /mnt/etc/gdm/custom.conf");
    } else if (dm == "lightdm") {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^#autologin-user=/autologin-user={}/' /mnt/etc/lightdm/lightdm.conf"), user));
        utils::exec("sed -i 's/^#autologin-user-timeout=0/autologin-user-timeout=0/' /mnt/etc/lightdm/lightdm.conf");

        utils::arch_chroot("groupadd -r autologin", false);
        utils::arch_chroot(fmt::format(FMT_COMPILE("gpasswd -a {} autologin"), user), false);
    } else if (dm == "sddm") {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^User=/User={}/g' /mnt/etc/sddm.conf"), user));
    } else if (dm == "lxdm") {
        utils::exec(fmt::format(FMT_COMPILE("sed -i 's/^# autologin=dgod/autologin={}/g' /mnt/etc/lxdm/lxdm.conf"), user));
    }
#endif
}

void set_schedulers() noexcept {
#ifdef NDEVENV
    static constexpr auto rules_path = "/mnt/etc/udev/rules.d/60-ioscheduler.rules";
    if (!fs::exists(rules_path)) {
        static constexpr auto ioscheduler_rules = R"(# set scheduler for NVMe
ACTION=="add|change", KERNEL=="nvme[0-9]n[0-9]", ATTR{queue/scheduler}="none"
# set scheduler for SSD and eMMC
ACTION=="add|change", KERNEL=="sd[a-z]|mmcblk[0-9]*", ATTR{queue/rotational}=="0", ATTR{queue/scheduler}="mq-deadline"
# set scheduler for rotating disks
ACTION=="add|change", KERNEL=="sd[a-z]", ATTR{queue/rotational}=="1", ATTR{queue/scheduler}="bfq")";
        std::ofstream rules{rules_path};
        rules << ioscheduler_rules;
    }
    utils::exec("vim /mnt/etc/udev/rules.d/60-ioscheduler.rules", true);
#else
    utils::exec("vim /etc/udev/rules.d/60-ioscheduler.rules", true);
#endif
}

void set_swappiness() noexcept {
#ifdef NDEVENV
    static constexpr auto rules_path = "/mnt/etc/sysctl.d/99-sysctl.conf";
    if (!fs::exists(rules_path)) {
        static constexpr auto sysctl_rules = R"(vm.swappiness = 10
vm.vfs_cache_pressure = 50
#vm.dirty_ratio = 3)";
        std::ofstream rules{rules_path};
        rules << sysctl_rules;
    }
    utils::exec("vim /mnt/etc/sysctl.d/99-sysctl.conf", true);
#else
    utils::exec("vim /etc/sysctl.d/99-sysctl.conf", true);
#endif
}

bool parse_config() noexcept {
    using namespace rapidjson;

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    spdlog::info("CachyOS installer version '{}'", INSTALLER_VERSION);
    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    spdlog::info("SYSTEM: '{}'", system_info);

    // 1. Open file for reading.
    static constexpr auto file_path = "settings.json";
    std::ifstream ifs{file_path};
    if (!ifs.is_open()) {
        fmt::print(stderr, "Config not found running with defaults\n");
        return true;
    }

    IStreamWrapper isw{ifs};

    // 2. Parse a JSON.
    Document doc;
    doc.ParseStream(isw);

    // Document is a JSON value represents the root of DOM. Root can be either an object or array.
    assert(doc.IsObject());

    assert(doc.HasMember("menus"));
    assert(doc["menus"].IsInt());

    config_data["menus"] = doc["menus"].GetInt();

    auto& headless_mode = std::get<std::int32_t>(config_data["HEADLESS_MODE"]);
    if (doc.HasMember("headless_mode")) {
        assert(doc["headless_mode"].IsBool());
        headless_mode = static_cast<std::int32_t>(doc["headless_mode"].GetBool());
    }

    if (headless_mode) {
        spdlog::info("Running in HEADLESS mode!");
    } else {
        spdlog::info("Running in NORMAL mode!");
    }

    if (doc.HasMember("device")) {
        assert(doc["device"].IsString());
        config_data["DEVICE"] = std::string{doc["device"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"device\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("partitions")) {
        assert(doc["partitions"].IsArray());

        std::vector<std::string> ready_parts{};
        for (const auto& partition_map : doc["partitions"].GetArray()) {
            assert(partition_map.IsObject());

            const auto& part_obj = partition_map.GetObject();
            assert(partition_map["name"].IsString());
            assert(partition_map["mountpoint"].IsString());
            assert(partition_map["size"].IsString());
            assert(partition_map["type"].IsString());

            // Validate partition type.
            const auto& part_type = std::string{partition_map["type"].GetString()};

            using namespace std::literals;
            static constexpr std::array valid_types{"root"sv, "boot"sv, "additional"sv};
            if (!ranges::contains(valid_types, part_type)) {
                fmt::print(stderr, "partition type '{}' is invalid! Valid types: {}.\n", part_type, valid_types);
                return false;
            }

            // Just to save some space push as single string instead of a new type.
            auto&& part_data = fmt::format(FMT_COMPILE("{}\t{}\t{}\t{}"), partition_map["name"].GetString(),
                partition_map["mountpoint"].GetString(), partition_map["size"].GetString(), part_type);
            ready_parts.push_back(std::move(part_data));
        }
        config_data["READY_PARTITIONS"] = std::move(ready_parts);
    } else if (headless_mode) {
        fmt::print(stderr, "\"partitions\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("fs_name")) {
        assert(doc["fs_name"].IsString());
        config_data["FILESYSTEM_NAME"] = std::string{doc["fs_name"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"fs_name\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("mount_opts")) {
        assert(doc["mount_opts"].IsString());
        config_data["MOUNT_OPTS"] = std::string{doc["mount_opts"].GetString()};
    }

    if (doc.HasMember("hostname")) {
        assert(doc["hostname"].IsString());
        config_data["HOSTNAME"] = std::string{doc["hostname"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"hostname\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("locale")) {
        assert(doc["locale"].IsString());
        config_data["LOCALE"] = std::string{doc["locale"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"locale\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("xkbmap")) {
        assert(doc["xkbmap"].IsString());
        config_data["XKBMAP"] = std::string{doc["xkbmap"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"xkbmap\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("timezone")) {
        assert(doc["timezone"].IsString());
        config_data["TIMEZONE"] = std::string{doc["timezone"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"timezone\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("user_name") && doc.HasMember("user_pass") && doc.HasMember("user_shell")) {
        assert(doc["user_name"].IsString());
        assert(doc["user_pass"].IsString());
        assert(doc["user_shell"].IsString());
        config_data["USER_NAME"]  = std::string{doc["user_name"].GetString()};
        config_data["USER_PASS"]  = std::string{doc["user_pass"].GetString()};
        config_data["USER_SHELL"] = std::string{doc["user_shell"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"user_name\", \"user_pass\", \"user_shell\" fields are required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("root_pass")) {
        assert(doc["root_pass"].IsString());
        config_data["ROOT_PASS"] = std::string{doc["root_pass"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"root_pass\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("kernel")) {
        assert(doc["kernel"].IsString());
        config_data["KERNEL"] = std::string{doc["kernel"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"kernel\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("desktop")) {
        assert(doc["desktop"].IsString());
        config_data["DE"] = std::string{doc["desktop"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"desktop\" field is required in HEADLESS mode!\n");
        return false;
    }

    if (doc.HasMember("bootloader")) {
        assert(doc["bootloader"].IsString());
        config_data["BOOTLOADER"] = std::string{doc["bootloader"].GetString()};
    } else if (headless_mode) {
        fmt::print(stderr, "\"bootloader\" field is required in HEADLESS mode!\n");
        return false;
    }

    std::string drivers_type{"free"};
    if (doc.HasMember("drivers_type")) {
        assert(doc["drivers_type"].IsString());
        drivers_type = doc["drivers_type"].GetString();

        if (drivers_type != "nonfree" && drivers_type != "free") {
            spdlog::error("Unknown value: {}!", drivers_type);
            drivers_type = "free";
        }
    }

    config_data["DRIVERS_TYPE"] = drivers_type;

    if (doc.HasMember("post_install")) {
        assert(doc["post_install"].IsString());
        config_data["POST_INSTALL"] = std::string{doc["post_install"].GetString()};
    }
    return true;
}

void setup_luks_keyfile() noexcept {
    // Add keyfile to luks
    const auto& root_name          = utils::exec("mount | awk '/\\/mnt / {print $1}' | sed s~/dev/mapper/~~g | sed s~/dev/~~g");
    const auto& root_part          = utils::exec(fmt::format(FMT_COMPILE("lsblk -i | tac | sed -r 's/^[^[:alnum:]]+//' | sed -n -e \"/{}/,/part/p\" | {} | tr -cd '[:alnum:]'"), root_name, "awk '/part/ {print $1}'"));
    const auto& number_of_lukskeys = to_int(utils::exec(fmt::format(FMT_COMPILE("cryptsetup luksDump /dev/\"{}\" | grep \"ENABLED\" | wc -l"), root_part)));
    if (number_of_lukskeys < 4) {
        // Create a keyfile
#ifdef NDEVENV
        if (!fs::exists("/mnt/crypto_keyfile.bin")) {
            const auto& ret_status = utils::exec("dd bs=512 count=4 if=/dev/urandom of=/mnt/crypto_keyfile.bin", true);
            /* clang-format off */
            if (ret_status == "0") { spdlog::info("Generating a keyfile"); }
            /* clang-format on */
        }
        utils::exec("chmod 000 /mnt/crypto_keyfile.bin");
        spdlog::info("Adding the keyfile to the LUKS configuration");
        auto ret_status = utils::exec(fmt::format(FMT_COMPILE("cryptsetup --pbkdf-force-iterations 200000 luksAddKey /dev/\"{}\" /mnt/crypto_keyfile.bin"), root_part), true);
        /* clang-format off */
        if (ret_status != "0") { spdlog::info("Something went wrong with adding the LUKS key. Is /dev/{} the right partition?", root_part); }
        /* clang-format on */

        // Add keyfile to initcpio
        ret_status = utils::exec("grep -q '/crypto_keyfile.bin' /mnt/etc/mkinitcpio.conf || sed -i '/FILES/ s~)~/crypto_keyfile.bin)~' /mnt/etc/mkinitcpio.conf", true);
        /* clang-format off */
        if (ret_status == "0") { spdlog::info("Adding keyfile to the initcpio"); }
        /* clang-format on */
        utils::arch_chroot("mkinitcpio -P");
#endif
    }
}

void grub_mkconfig() noexcept {
    utils::arch_chroot("grub-mkconfig -o /boot/grub/grub.cfg");
    // check_for_error "grub-mkconfig" $?
}

void set_lightdm_greeter() {
    auto* config_instance      = Config::instance();
    auto& config_data          = config_instance->data();
    const auto& mountpoint     = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& xgreeters_path = fmt::format(FMT_COMPILE("{}/usr/share/xgreeters"), mountpoint);
    /* clang-format off */
    if (!fs::exists(xgreeters_path)) { return; }
    for (const auto& name : fs::directory_iterator{xgreeters_path}) {
        const auto& temp = name.path().filename().string();
        if (!temp.starts_with("lightdm-") && !temp.ends_with("-greeter")) { continue; }
        /* clang-format on */
        if (temp == "lightdm-gtk-greeter") {
            continue;
        }
        utils::exec(fmt::format(FMT_COMPILE("sed -i -e \"s/^.*greeter-session=.*/greeter-session={}/\" {}/etc/lightdm/lightdm.conf"), temp, mountpoint));
        break;
    }
}

void enable_services() noexcept {
    spdlog::info("Enabling services...");

#ifdef NDEVENV
    auto* config_instance  = Config::instance();
    auto& config_data      = config_instance->data();
    const auto& mountpoint = std::get<std::string>(config_data["MOUNTPOINT"]);
    const auto& zfs        = std::get<std::int32_t>(config_data["ZFS"]);

    static constexpr std::array enable_systemd{"avahi-daemon", "bluetooth", "cronie", "ModemManager", "NetworkManager", "org.cups.cupsd", "tlp", "haveged", "ufw", "apparmor", "fstrim.timer"};
    for (auto&& service : enable_systemd) {
        if (!fs::exists(fmt::format(FMT_COMPILE("{}/usr/lib/systemd/system/{}.service"), mountpoint, service))) {
            continue;
        }
        utils::arch_chroot(fmt::format(FMT_COMPILE("systemctl enable {}"), service), false);
        spdlog::info("enabled service {}", service);
    }

    utils::arch_chroot("systemctl disable pacman-init", false);
    spdlog::info("enabled service pacman-init");

    // enable display manager for systemd
    const auto& temp = fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq"), mountpoint);
    if (utils::exec(fmt::format(FMT_COMPILE("{} lightdm &> /dev/null"), temp), true) == "0") {
        utils::set_lightdm_greeter();
        utils::arch_chroot("systemctl enable lightdm", false);
    } else if (utils::exec(fmt::format(FMT_COMPILE("{} sddm &> /dev/null"), temp), true) == "0") {
        utils::arch_chroot("systemctl enable sddm", false);
    } else if (utils::exec(fmt::format(FMT_COMPILE("{} gdm &> /dev/null"), temp), true) == "0") {
        utils::arch_chroot("systemctl enable gdm", false);
    } else if (utils::exec(fmt::format(FMT_COMPILE("{} lxdm &> /dev/null"), temp), true) == "0") {
        utils::arch_chroot("systemctl enable lxdm", false);
    } else if (utils::exec(fmt::format(FMT_COMPILE("{} ly &> /dev/null"), temp), true) == "0") {
        utils::arch_chroot("systemctl enable ly", false);
    }

    /* clang-format off */
    if (zfs == 0) { return; }
    /* clang-format on */

    // if we are using a zfs we should enable the zfs services
    utils::arch_chroot("systemctl enable zfs.target", false);
    utils::arch_chroot("systemctl enable zfs-import-cache", false);
    utils::arch_chroot("systemctl enable zfs-mount", false);
    utils::arch_chroot("systemctl enable zfs-import.target", false);

    // we also need create the cachefile
    utils::exec(fmt::format(FMT_COMPILE("zpool set cachefile=/etc/zfs/zpool.cache $(findmnt {} -lno SOURCE | {}) 2>>/tmp/cachyos-install.log"), mountpoint, "awk -F / '{print $1}'"), true);
    utils::exec(fmt::format(FMT_COMPILE("cp /etc/zfs/zpool.cache {}/etc/zfs/zpool.cache 2>>/tmp/cachyos-install.log"), mountpoint), true);
#endif
}

void final_check() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    config_data["CHECKLIST"] = "";
    auto& checklist          = std::get<std::string>(config_data["CHECKLIST"]);

    // Check if base is installed
    if (!fs::exists("/mnt/.base_installed")) {
        checklist += "- Base is not installed\n";
        return;
    }

    const auto& system_info = std::get<std::string>(config_data["SYSTEM"]);
    const auto& mountpoint  = std::get<std::string>(config_data["MOUNTPOINT"]);
    // Check if bootloader is installed
    if (system_info == "BIOS") {
        if (utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} pacman -Qq grub &> /dev/null"), mountpoint), true) != "0") {
            checklist += "- Bootloader is not installed\n";
        }
    }

    // Check if fstab is generated
    if (utils::exec(fmt::format(FMT_COMPILE("grep -qv '^#' {}/etc/fstab 2>/dev/null"), mountpoint), true) != "0") {
        checklist += "- Fstab has not been generated\n";
    }

    // Check if video-driver has been installed
    //[[ ! -e /mnt/.video_installed ]] && echo "- $_GCCheck" >> ${CHECKLIST}

    // Check if locales have been generated
    if (to_int(utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} locale -a | wc -l"), mountpoint), true)) < 3) {
        checklist += "- Locales have not been generated\n";
    }

    // Check if root password has been set
    if (utils::exec(fmt::format(FMT_COMPILE("arch-chroot {} passwd --status root | cut -d' ' -f2 | grep -q 'NP'"), mountpoint), true) == "0") {
        checklist += "- Root password is not set\n";
    }

    // check if user account has been generated
    if (!fs::exists("/mnt/home")) {
        checklist += "- No user accounts have been generated\n";
    }
}

}  // namespace utils
