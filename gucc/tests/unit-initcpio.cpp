#include "gucc/file_utils.hpp"
#include "gucc/initcpio.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

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

static constexpr auto MKINITCPIO_INVALID_TEST = R"(
# MODULES
MODULES=()

 (
# BINARIES
BINARIES=()
)
# FILES
FILES=()
()
# HOOKS
HOOKS=(base udev autodetect modconf block filesystems keyboard fsck

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
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("default", callback_sink));

    using namespace gucc;  // NOLINT

    static constexpr std::string_view filename{"/tmp/mkinitcpio.conf"};

    // Open mkinitcpio file for writing.
    std::ofstream mkinitcpio_file{filename.data()};
    assert(mkinitcpio_file.is_open());

    // Setup mkinitcpio file.
    mkinitcpio_file << MKINITCPIO_STR;
    mkinitcpio_file.close();

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

    // check invalid file
    mkinitcpio_file = std::ofstream{filename.data()};
    assert(mkinitcpio_file.is_open());

    mkinitcpio_file << MKINITCPIO_INVALID_TEST;
    mkinitcpio_file.close();

    initcpio = detail::Initcpio{filename};
    assert(initcpio.parse_file());

    assert(initcpio.modules.empty());
    assert(initcpio.files.empty());
    assert(initcpio.hooks.empty());

    // Cleanup.
    fs::remove(filename);

    // check empty file
    mkinitcpio_file = std::ofstream{filename.data()};
    assert(mkinitcpio_file.is_open());

    mkinitcpio_file << "";
    mkinitcpio_file.close();

    initcpio = detail::Initcpio{filename};
    assert(!initcpio.parse_file());
    assert(!initcpio.write());

    // Cleanup.
    fs::remove(filename);

    // check write to nonexistent file
    initcpio = detail::Initcpio{"/path/to/non/exist_file.conf"};
    assert(!initcpio.parse_file());
    assert(!initcpio.write());

    // check write to root file
    initcpio = detail::Initcpio{"/etc/mtab"};
    assert(!initcpio.parse_file());
    assert(!initcpio.write());
}
