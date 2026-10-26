#pragma once

#include "gucc/error.hpp"
#include "gucc/partition.hpp"

#include <cinttypes>  // for int32_t

#include <memory>       // for unique_ptr
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::mount {

class [[nodiscard]] ScopedMount final {
 public:
    static auto mount(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts, std::string_view fstype) noexcept -> Result<ScopedMount>;
    static auto bind(std::string_view source, std::string_view target) noexcept -> Result<ScopedMount>;

    ~ScopedMount() = default;

    ScopedMount(ScopedMount&& other) noexcept                    = default;
    auto operator=(ScopedMount&& other) noexcept -> ScopedMount& = default;

    // NOTE: never call on moved.
    auto path() const noexcept -> std::string_view { return *m_mount; }

    // deleted
    ScopedMount(const ScopedMount&)                    = delete;
    auto operator=(const ScopedMount&) -> ScopedMount& = delete;

 private:
    struct Unmount {
        void operator()(std::string* path) const noexcept;
    };

    explicit ScopedMount(std::string_view path)
      : m_mount(std::make_unique<std::string>(path).release()) { }

    std::unique_ptr<std::string, Unmount> m_mount;
};

auto build_mount_argv(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts, std::string_view fstype = {}) noexcept -> std::vector<std::string>;

// Mount partition
auto mount_partition(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts, std::string_view fstype = {}) noexcept -> Result<void>;
/// Unmounts mountpoint
auto umount(std::string_view mountpoint, bool lazy = false) noexcept -> Result<void>;

/// Bind-mounts source into target.
auto bind_mount(std::string_view source, std::string_view target) noexcept -> Result<void>;

// Query partition
auto query_partition(std::string_view partition, std::int32_t& is_luks, std::int32_t& is_lvm, std::string& luks_name, std::string& luks_dev, std::string& luks_uuid) noexcept -> Result<void>;

/// @brief Formats, mounts, and creates a Partition entry for an ESP
/// @param device The partition device path
/// @param mountpoint The ESP mountpoint relative to root
/// @param base_mountpoint The base mountpoint of the installation
/// @param format Whether to format the partition
/// @param is_ssd Whether the device is an SSD
/// @return Partition struct on success, an error on failure
auto setup_esp_partition(std::string_view device, std::string_view mountpoint, std::string_view base_mountpoint, bool format, bool is_ssd) noexcept -> Result<fs::Partition>;

}  // namespace gucc::mount
