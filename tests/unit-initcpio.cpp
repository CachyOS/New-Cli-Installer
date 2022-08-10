#include "utils.hpp"

#include <cassert>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

#include <fmt/core.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/find.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/getlines.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/split.hpp>
#include <range/v3/view/transform.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

static constexpr auto MKINITCPIO_STR = R"(
# MODULES
MODULES=()

# BINARIES
BINARIES=()

# FILES
FILES=()

# HOOKS
HOOKS=(base udev autodetect modconf block filesystems keyboard fsck)
)";

static constexpr auto MKINITCPIO_TEST = R"(
# MODULES
MODULES=(crc32c-intel)

# BINARIES
BINARIES=()

# FILES
FILES=()

# HOOKS
HOOKS=(base usr lvm2 zfs)
)";

class Initcpio {
 public:
    Initcpio(const std::string_view& file_path) noexcept : m_file_path(file_path) {}
    
    bool write() const noexcept {
        auto&& file_content = utils::read_whole_file(m_file_path);
        std::string&& result = file_content | ranges::views::split('\n')
            | ranges::views::transform([&](auto&& rng) {
                  auto&& line = std::string_view(&*rng.begin(), static_cast<size_t>(ranges::distance(rng)));
                  if (line.starts_with("MODULES")) {
                      auto&& formatted_modules = modules | ranges::views::join(' ')
                                                         | ranges::to<std::string>();
                      return fmt::format("MODULES=({})", formatted_modules);
                  } else if (line.starts_with("FILES")) {
                      auto&& formatted_files = files | ranges::views::join(' ')
                                                     | ranges::to<std::string>();
                      return fmt::format("FILES=({})", formatted_files);
                  } else if (line.starts_with("HOOKS")) {
                      auto&& formatted_hooks = hooks | ranges::views::join(' ')
                                                     | ranges::to<std::string>();
                      return fmt::format("HOOKS=({})", formatted_hooks);
                  }
                  return std::string{line.data(), line.size()};
              })
            | ranges::views::join('\n')
            | ranges::to<std::string>();
        result += '\n';

        /* clang-format off */
        std::ofstream mhinitcpio_file{m_file_path.data()};
        if (!mhinitcpio_file.is_open()) { return false; }
        /* clang-format on */
        mhinitcpio_file << result;
        return true;
    }
    
    std::vector<std::string> modules{};
    std::vector<std::string> files{};
    std::vector<std::string> hooks{};

 private:
    std::string_view m_file_path{};
};

int main() {
    static constexpr std::string_view filename{"/tmp/mkinitcpio.conf"};

    // Open mkinitcpio file for writing.
    std::ofstream mhinitcpio_file{filename.data()};
    assert(mhinitcpio_file.is_open());

    // Setup mkinitcpio file.
    mhinitcpio_file << MKINITCPIO_STR;
    mhinitcpio_file.close();

    auto initcpio = Initcpio{filename};

    // Insert data.
    initcpio.modules.insert(initcpio.modules.end(), {"crc32c-intel"});
    initcpio.hooks.insert(initcpio.hooks.end(), {"base", "usr", "lvm2", "zfs"});

    // Write data.
    assert(initcpio.write());

    // Check if file is equal to test data.
    // "\n# MODULES\nMODULES=(crc32c-intel)\n\n# BINARIES\nBINARIES=()\n\n# FILES\nFILES=()\n\n# HOOKS\nHOOKS=(base usr lvm2 zfs)"\n
    const auto&& file_content = utils::read_whole_file(filename);
    assert(file_content == MKINITCPIO_TEST);

    // Cleanup.
    fs::remove(filename);
}
