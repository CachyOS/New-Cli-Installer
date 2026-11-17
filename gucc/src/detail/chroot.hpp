#pragma once

#include "gucc/error.hpp"
#include "gucc/process.hpp"

#include <cstdint>  // for uint64_t

#include <functional>   // for function
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::utils::detail {

/// One mount(2) call of the arch-chroot setup.
struct ChrootMount {
    const char* source{};
    std::string target;
    const char* fstype{};
    const char* data{};
    unsigned long flags{};  // NOLINT(google-runtime-int): mount(2) type
    /// failure is ignored
    bool optional{false};
    /// MS_PRIVATE after mounting
    bool make_private{false};
};

/// Host facts the plan depends on.
struct ChrootHost {
    bool efivars{false};
    /// where the host resolv.conf gets bound
    std::optional<std::string> resolv_target;
};

/// Owns the mounts it made.
class [[nodiscard]] ChrootSession final {
 public:
    static auto enter(std::string_view root) noexcept -> Result<ChrootSession>;

    ~ChrootSession();

    ChrootSession(ChrootSession&&) noexcept = default;

    ChrootSession(const ChrootSession&)                        = delete;
    auto operator=(const ChrootSession&) -> ChrootSession&     = delete;
    auto operator=(ChrootSession&&) noexcept -> ChrootSession& = delete;

 private:
    struct Owned {
        std::string path;
        std::uint64_t mnt_id{};
    };

    ChrootSession() = default;

    std::vector<Owned> m_mounts;
};

/// A resolve conf symlink resolved inside @p root, std::nullopt when the target has none.
[[nodiscard]] auto resolv_bind_target(std::string_view root) -> std::optional<std::string>;

/// Mirrors arch-chroot behaviour in order.
/// @note Paths already mounted are skipped unless a parent is mounted by this plan.
[[nodiscard]] auto plan_chroot_mounts(std::string_view root, const ChrootHost& host, const std::function<bool(const std::string&)>& is_mountpoint)
    -> std::vector<ChrootMount>;

/// argv prefix for running a command inside @p root.
[[nodiscard]] auto chroot_prefix(ChrootMode mode, std::string_view root) -> std::vector<std::string>;

}  // namespace gucc::utils::detail
