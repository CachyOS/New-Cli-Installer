#pragma once

#include <cstdint>  // for uint64_t

#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <spdlog/spdlog.h>

namespace gucc::detail::lsblk_json {

/// Command output is merged stdout+stderr.
constexpr auto payload(std::string_view output) noexcept -> std::string_view {
    const auto start = output.find('{');
    return start == std::string_view::npos ? std::string_view{} : output.substr(start);
}

/// Trailing stderr after the closing brace is ignored.
inline auto parse(rapidjson::Document& document, std::string_view output) noexcept -> bool {
    const auto json = payload(output);
    document.Parse<rapidjson::kParseStopWhenDoneFlag>(json.data(), json.size());
    return !document.HasParseError() && document.IsObject();
}

inline auto parse_or_log(rapidjson::Document& document, std::string_view output) noexcept -> bool {
    if (parse(document, output)) {
        return true;
    }
    spdlog::error("Failed to parse lsblk output: {}", document.HasParseError() ? rapidjson::GetParseError_En(document.GetParseError()) : "not a JSON object");
    return false;
}

inline auto member(const rapidjson::Value& obj, std::string_view key) noexcept -> const rapidjson::Value* {
    if (!obj.IsObject()) {
        return nullptr;
    }
    const auto it = obj.FindMember(rapidjson::Value{rapidjson::StringRef(key.data(), key.size())});
    return it == obj.MemberEnd() ? nullptr : &it->value;
}

inline auto string(const rapidjson::Value& obj, std::string_view key) noexcept -> std::optional<std::string_view> {
    const auto* value = member(obj, key);
    if (value == nullptr || !value->IsString()) {
        return std::nullopt;
    }
    return std::string_view{value->GetString(), value->GetStringLength()};
}

inline auto uint64(const rapidjson::Value& obj, std::string_view key) noexcept -> std::optional<std::uint64_t> {
    const auto* value = member(obj, key);
    if (value == nullptr || !value->IsUint64()) {
        return std::nullopt;
    }
    return value->GetUint64();
}

inline auto boolean(const rapidjson::Value& obj, std::string_view key) noexcept -> std::optional<bool> {
    const auto* value = member(obj, key);
    if (value == nullptr || !value->IsBool()) {
        return std::nullopt;
    }
    return value->GetBool();
}

inline auto owned_string(const rapidjson::Value& obj, std::string_view key) -> std::optional<std::string> {
    return string(obj, key).transform([](std::string_view value) { return std::string{value}; });
}

/// Empty range when @p key is absent or not an array.
inline auto array(const rapidjson::Value& obj, std::string_view key) noexcept -> rapidjson::Value::ConstArray {
    static const rapidjson::Value kEmpty{rapidjson::kArrayType};
    const auto* value = member(obj, key);
    return (value != nullptr && value->IsArray()) ? value->GetArray() : kEmpty.GetArray();
}

/// Non-empty string entries of the MOUNTPOINTS array
inline auto mountpoints(const rapidjson::Value& obj) -> std::vector<std::string> {
    return array(obj, "mountpoints")
        | std::ranges::views::filter([](const rapidjson::Value& entry) { return entry.IsString() && entry.GetStringLength() > 0; })
        | std::ranges::views::transform([](const rapidjson::Value& entry) { return std::string{entry.GetString(), entry.GetStringLength()}; })
        | std::ranges::to<std::vector<std::string>>();
}

}  // namespace gucc::detail::lsblk_json
