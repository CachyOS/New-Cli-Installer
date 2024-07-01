#include "gucc/locale.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::locale {

auto set_locale(std::string_view locale, std::string_view mountpoint) noexcept -> bool {
    const auto& locale_config_path = fmt::format(FMT_COMPILE("{}/etc/locale.conf"), mountpoint);
    const auto& locale_gen_path    = fmt::format(FMT_COMPILE("{}/etc/locale.gen"), mountpoint);

    static constexpr auto LOCALE_CONFIG_PART = R"(LANG="{0}"
LC_NUMERIC="{0}"
LC_TIME="{0}"
LC_MONETARY="{0}"
LC_PAPER="{0}"
LC_NAME="{0}"
LC_ADDRESS="{0}"
LC_TELEPHONE="{0}"
LC_MEASUREMENT="{0}"
LC_IDENTIFICATION="{0}"
LC_MESSAGES="{0}"
)";

    {
        const auto& locale_config_text = fmt::format(LOCALE_CONFIG_PART, locale);
        if (!file_utils::create_file_for_overwrite(locale_config_path, locale_config_text)) {
            spdlog::error("Failed to open locale config for writing {}", locale_config_path);
            return false;
        }
    }

    // TODO(vnepogodin): refactor and make backups of locale config and locale gen
    utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/#{0}/{0}/\" {1}"), locale, locale_gen_path));

    // Generate locales
    if (!utils::arch_chroot_checked("locale-gen", mountpoint)) {
        spdlog::error("Failed to run locale-gen with locale '{}'", locale);
        return false;
    }

    // NOTE: maybe we should also write into /etc/default/locale if /etc/default exists and is a dir?
    return true;
}

}  // namespace gucc::locale
