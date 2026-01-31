#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <string>  // for string

namespace tui {
void luks_menu_advanced() noexcept;

// TPM2 functions
bool check_tpm_available() noexcept;
bool select_pcrs(std::string& pcrs) noexcept;
bool luks_setup_with_tpm() noexcept;
void luks_encrypt_tpm() noexcept;
}  // namespace tui

#endif  // CRYPTO_HPP
