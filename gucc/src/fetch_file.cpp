#include "gucc/fetch_file.hpp"

#include <chrono>  // for chrono_literals

#include <cpr/api.h>
#include <cpr/response.h>
#include <cpr/status_codes.h>
#include <cpr/timeout.h>

using namespace std::string_view_literals;

namespace {

auto fetch_file(std::string_view url) noexcept -> std::optional<std::string> {
    using namespace std::chrono_literals;

    auto timeout     = cpr::Timeout{30s};
    auto response    = cpr::Get(cpr::Url{url}, timeout);
    auto status_code = static_cast<std::int32_t>(response.status_code);

    static constexpr auto FILE_URL_PREFIX = "file://"sv;
    if (cpr::status::is_success(status_code) || (url.starts_with(FILE_URL_PREFIX) && status_code == 0 && !response.text.empty())) {
        return std::make_optional<std::string>(std::move(response.text));
    }
    return std::nullopt;
}

}  // namespace

namespace gucc::fetch {

auto fetch_file_from_url(std::string_view url, std::string_view fallback_url) noexcept -> std::optional<std::string> {
    auto fetch_content = fetch_file(url);
    if (fetch_content) {
        return std::make_optional<std::string>(std::move(*fetch_content));
    }

    fetch_content = fetch_file(fallback_url);
    if (fetch_content) {
        return std::make_optional<std::string>(std::move(*fetch_content));
    }
    return std::nullopt;
}

}  // namespace gucc::fetch
