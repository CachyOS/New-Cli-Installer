#include <cstdio>

#include <cachyos/installer_config.hpp>
#include <gucc/error.hpp>

auto main() -> int {
    auto config = cachyos::installer::parse_installer_config(R"({"menus": 1, "headless_mode": true})");
    if (!config) {
        std::puts(config.error().c_str());
        return 1;
    }
    auto validated = cachyos::installer::validate_headless_config(*config);
    if (validated || validated.error().empty()) {
        return 1;
    }
    const auto error = gucc::make_error(gucc::ErrorCode::InvalidArgument, FMT_COMPILE("probe {}"), 42);
    return gucc::to_string(error.error()).contains("probe 42") ? 0 : 1;
}
