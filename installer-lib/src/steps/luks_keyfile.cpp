#include "cachyos/steps.hpp"
#include "detail/result.hpp"

// import gucc
#include "gucc/luks.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for contains
#include <ranges>     // for ranges::*
#include <variant>    // for get_if

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace cachyos::installer::steps {

auto luks_keyfile(InstallContext& ctx) noexcept -> std::expected<void, std::string> {
    const auto* layout = std::get_if<partition_strategy::CreateLayout>(&ctx.strategy);
    const auto* root   = gucc::utils::find_if_ptr(ctx.partition_schema, &gucc::fs::Partition::is_root);
    if (layout == nullptr || !layout->luks_passphrase || root == nullptr || !root->luks_uuid) {
        return {};
    }

    // no boot partition to unlock with keyfile
    ctx.crypto.is_fde = !std::ranges::contains(ctx.partition_schema, "/boot"sv, &gucc::fs::Partition::mountpoint);
    if (!ctx.crypto.is_fde) {
        return {};
    }
    const auto container = fmt::format(FMT_COMPILE("/dev/disk/by-uuid/{}"), *root->luks_uuid);
    return lift(gucc::crypto::luks_embed_keyfile(gucc::utils::target_path(ctx.mountpoint, "/crypto_keyfile.bin"sv), ctx.mountpoint, container, *layout->luks_passphrase));
}

}  // namespace cachyos::installer::steps
