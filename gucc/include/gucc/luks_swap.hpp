#ifndef LUKS_SWAP_HPP
#define LUKS_SWAP_HPP

#include <cstdint>      // for uint32_t
#include <string>       // for string
#include <string_view>  // for string_view

namespace gucc::luks_swap {

/// Configuration for random-key LUKS swap partition.
struct RandomKeyConfig {
    /// crypttab name; the opened device is /dev/mapper/<mapper_name>.
    /// Defaults to "swap".
    std::string mapper_name{"swap"};
    /// Source block device for the swap partition. Required. Accepts any
    /// form crypttab accepts (e.g. "/dev/sda3", "PARTUUID=…",
    /// "/dev/disk/by-uuid/…").
    std::string source_device;
    /// Cipher spec for cryptsetup. Defaults to the wiki-canonical
    /// `aes-xts-plain64` — the `:hash` suffix only matters for
    /// password-derived keys, not random keys.
    std::string cipher{"aes-xts-plain64"};
    /// Key size in bits. 512 = AES-256-XTS (cryptsetup luksFormat norm).
    std::uint32_t key_size{512};
    /// Encryption sector size in bytes. 4096 matches modern 4K-native
    /// devices; set to 0 to omit the `sector-size=` option entirely.
    std::uint32_t sector_size{4096};
};

/// Build a single /etc/crypttab line for the random-key swap config.
[[nodiscard]] auto make_crypttab_line(const RandomKeyConfig& cfg) noexcept -> std::string;

/// Build the corresponding /etc/fstab line for /dev/mapper/<mapper_name>.
[[nodiscard]] auto make_fstab_line(std::string_view mapper_name) noexcept -> std::string;

/// Append the crypttab entry for @p cfg to <root>/etc/crypttab.
auto add_crypttab_entry(const RandomKeyConfig& cfg,
    std::string_view root_mountpoint) noexcept -> bool;

/// Append the fstab entry for /dev/mapper/<mapper_name> to
/// <root>/etc/fstab.
auto add_fstab_entry(std::string_view mapper_name, std::string_view root_mountpoint) noexcept -> bool;

/// Replace any existing swap line in fstab with the crypttab mapper entry.
auto replace_swap_in_fstab(std::string_view mapper_name, std::string_view root_mountpoint) noexcept -> bool;

}  // namespace gucc::luks_swap

#endif  // LUKS_SWAP_HPP
