#include "gucc/file_utils.hpp"
#include "gucc/initcpio.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

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
MODULES=(radeon crc32c-intel)

# BINARIES
BINARIES=()

# FILES
FILES=()

# HOOKS
HOOKS=(base udev autodetect modconf block filesystems keyboard fsck btrfs usr lvm2 zfs)
)";

int main() {
    using namespace gucc;  // NOLINT

    static constexpr std::string_view filename{"/tmp/mkinitcpio.conf"};

    // Open mkinitcpio file for writing.
    std::ofstream mhinitcpio_file{filename.data()};
    assert(mhinitcpio_file.is_open());

    // Setup mkinitcpio file.
    mhinitcpio_file << MKINITCPIO_STR;
    mhinitcpio_file.close();

    auto initcpio = detail::Initcpio{filename};

    // Insert data.
    initcpio.append_module("radeon");
    initcpio.append_hook("btrfs");
    initcpio.append_module("crc32c-intel");
    initcpio.append_hooks({"usr", "lvm2", "zfs"});

    // Write data.
    assert(initcpio.write());

    // Checking insertion of equal items
    assert(!initcpio.append_module("radeon"));
    assert(!initcpio.append_hook("btrfs"));
    assert(!initcpio.append_module("crc32c-intel"));
    assert(!initcpio.insert_hook("btrfs", {"usr", "lvm2", "zfs"}));

    // Write data.
    assert(initcpio.write());

    // Check if file is equal to test data.
    // "\n# MODULES\nMODULES=(crc32c-intel)\n\n# BINARIES\nBINARIES=()\n\n# FILES\nFILES=()\n\n# HOOKS\nHOOKS=(base usr lvm2 zfs)"\n
    const auto& file_content = file_utils::read_whole_file(filename);
    assert(file_content == MKINITCPIO_TEST);

    // Cleanup.
    fs::remove(filename);
}
