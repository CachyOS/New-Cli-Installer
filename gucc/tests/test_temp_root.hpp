#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

#include <format>

namespace gucc::tests {

class TempRoot final {
 public:
    explicit TempRoot(std::string_view prefix = "gucc-test")
      : m_path(std::filesystem::temp_directory_path() / std::format("{}-{}", prefix, std::random_device{}())) {
        std::filesystem::create_directories(m_path);
    }
    ~TempRoot() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    TempRoot(const TempRoot&)                    = delete;
    auto operator=(const TempRoot&) -> TempRoot& = delete;
    TempRoot(TempRoot&&)                         = delete;
    auto operator=(TempRoot&&) -> TempRoot&      = delete;

    [[nodiscard]] auto path() const noexcept -> const std::filesystem::path& { return m_path; }

 private:
    std::filesystem::path m_path;
};

}  // namespace gucc::tests
