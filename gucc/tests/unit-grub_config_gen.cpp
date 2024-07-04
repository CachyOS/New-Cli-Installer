#include "gucc/bootloader.hpp"

#include <cassert>

#include <algorithm>
#include <string>
#include <string_view>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/split.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using namespace std::string_literals;
using namespace std::string_view_literals;

static constexpr auto GRUB_DEFAULTS_TEST = R"(# GRUB boot loader configuration

GRUB_DEFAULT=0
GRUB_TIMEOUT=5
GRUB_DISTRIBUTOR="Arch"
GRUB_CMDLINE_LINUX_DEFAULT="loglevel=3 quiet"
GRUB_CMDLINE_LINUX=""

# Preload both GPT and MBR modules so that they are not missed
GRUB_PRELOAD_MODULES="part_gpt part_msdos"

# Uncomment to enable booting from LUKS encrypted devices
#GRUB_ENABLE_CRYPTODISK=y

# Set to 'countdown' or 'hidden' to change timeout behavior,
# press ESC key to display menu.
GRUB_TIMEOUT_STYLE=menu

# Uncomment to use basic console
GRUB_TERMINAL_INPUT=console

# Uncomment to disable graphical terminal
#GRUB_TERMINAL_OUTPUT=console

# The resolution used on graphical terminal
# note that you can use only modes which your graphic card supports via VBE
# you can see them in real GRUB with the command `videoinfo'
GRUB_GFXMODE=auto

# Uncomment to allow the kernel use the same resolution used by grub
GRUB_GFXPAYLOAD_LINUX=keep

# Uncomment if you want GRUB to pass to the Linux kernel the old parameter
# format "root=/dev/xxx" instead of "root=/dev/disk/by-uuid/xxx"
#GRUB_DISABLE_LINUX_UUID=true

# Uncomment to disable generation of recovery mode menu entries
GRUB_DISABLE_RECOVERY=true

# Uncomment and set to the desired menu colors.  Used by normal and wallpaper
# modes only.  Entries specified as foreground/background.
#GRUB_COLOR_NORMAL="light-blue/black"
#GRUB_COLOR_HIGHLIGHT="light-cyan/blue"

# Uncomment one of them for the gfx desired, a image background or a gfxtheme
#GRUB_BACKGROUND="/path/to/wallpaper"
#GRUB_THEME="/path/to/gfxtheme"

# Uncomment to get a beep at GRUB start
#GRUB_INIT_TUNE="480 440 1"

# Uncomment to make GRUB remember the last selection. This requires
# setting 'GRUB_DEFAULT=saved' above.
#GRUB_SAVEDEFAULT=true

# Uncomment to disable submenus in boot menu
#GRUB_DISABLE_SUBMENU=y

# Probing for other operating systems is disabled for security reasons. Read
# documentation on GRUB_DISABLE_OS_PROBER, if still want to enable this
# functionality install os-prober and uncomment to detect and include other
# operating systems.
#GRUB_DISABLE_OS_PROBER=false
)"sv;

static constexpr auto GRUB_OPTIONALS_TEST = R"(GRUB_DEFAULT=saved
GRUB_TIMEOUT=10
GRUB_DISTRIBUTOR="CachyOS"
GRUB_CMDLINE_LINUX_DEFAULT="nowatchdog nvme_load=YES zswap.enabled=0 splash quiet"
GRUB_CMDLINE_LINUX="quiet"
GRUB_PRELOAD_MODULES="part_gpt part_msdos part_efi"
GRUB_ENABLE_CRYPTODISK=y
GRUB_TIMEOUT_STYLE=menu
GRUB_TERMINAL_INPUT=console
GRUB_TERMINAL_OUTPUT=console
GRUB_GFXMODE=auto
GRUB_GFXPAYLOAD_LINUX=keep
GRUB_DISABLE_LINUX_UUID=true
GRUB_DISABLE_RECOVERY=true
GRUB_COLOR_NORMAL="light-blue/yellow"
GRUB_COLOR_HIGHLIGHT="light-cyan/yellow"
GRUB_BACKGROUND="/path/to/wallpaper/here"
GRUB_THEME="/path/to/gfxtheme-smth"
GRUB_INIT_TUNE="380 420 2"
GRUB_SAVEDEFAULT=true
GRUB_DISABLE_SUBMENU=y
GRUB_DISABLE_OS_PROBER=false
)"sv;

inline auto filtered_res(std::string_view content) noexcept -> std::string {
    auto&& result = content | ranges::views::split('\n')
        | ranges::views::filter([](auto&& rng) {
              auto&& line = std::string_view(&*rng.begin(), static_cast<size_t>(ranges::distance(rng)));
              return !line.empty() && !line.starts_with("# ");
          })
        | ranges::views::join('\n')
        | ranges::to<std::string>();
    return result + '\n';
}

int main() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("default", callback_sink));

    // default config
    {
        const gucc::bootloader::GrubConfig grub_config{};
        const auto& grub_config_content = gucc::bootloader::gen_grub_config(grub_config);
        assert(grub_config_content == GRUB_DEFAULTS_TEST);
    }
    // optionals set
    {
        const gucc::bootloader::GrubConfig grub_config{
            .default_entry         = "saved"s,
            .grub_timeout          = 10,
            .grub_distributor      = "CachyOS"s,
            .cmdline_linux_default = "nowatchdog nvme_load=YES zswap.enabled=0 splash quiet"s,
            .cmdline_linux         = "quiet"s,
            .preload_modules       = "part_gpt part_msdos part_efi"s,
            .enable_cryptodisk     = true,
            .timeout_style         = "menu"s,
            .terminal_input        = "console"s,
            .terminal_output       = "console"s,
            .gfxmode               = "auto"s,
            .gfxpayload_linux      = "keep"s,
            .disable_linux_uuid    = true,
            .disable_recovery      = true,
            .color_normal          = "light-blue/yellow"s,
            .color_highlight       = "light-cyan/yellow"s,
            .background            = "/path/to/wallpaper/here"s,
            .theme                 = "/path/to/gfxtheme-smth"s,
            .init_tune             = "380 420 2"s,
            .savedefault           = true,
            .disable_submenu       = true,
            .disable_os_prober     = false,
        };
        const auto& grub_config_content = filtered_res(gucc::bootloader::gen_grub_config(grub_config));
        assert(grub_config_content == GRUB_OPTIONALS_TEST);
    }
}
