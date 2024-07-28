#include "gucc/bootloader.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/logger.hpp"

#include <cassert>

#include <filesystem>
#include <fstream>
#include <ranges>
#include <string_view>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

static constexpr auto REFIND_CONF_TEST = R"(extra_kernel_version_strings linux-lts,linux,linux-zen
)"sv;

inline auto filtered_res(std::string_view content) noexcept -> std::string {
    auto&& result = content | std::ranges::views::split('\n')
        | std::ranges::views::filter([](auto&& rng) {
              auto&& line = std::string_view(&*rng.begin(), static_cast<size_t>(std::ranges::distance(rng)));
              return line.starts_with("extra_kernel_version_strings");
          })
        | std::ranges::views::join_with('\n')
        | std::ranges::to<std::string>();
    return result + '\n';
}

int main() {
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    // prepare test data
    static constexpr std::string_view file_testpath{"/tmp/test-extrakernverstr-refind.conf"};
    static constexpr std::string_view file_sample_path{GUCC_TEST_DIR "/files/refind.conf-sample"};
    fs::copy_file(file_sample_path, file_testpath, fs::copy_options::overwrite_existing);

    // test valid extkernstr with valid file.
    const std::vector<std::string> extra_kernel_strings{"linux-lts", "linux", "linux-zen"};
    assert(gucc::bootloader::refind_write_extra_kern_strings(file_testpath, extra_kernel_strings));

    auto refind_conf_content = gucc::file_utils::read_whole_file(file_testpath);
    auto filtered_content    = filtered_res(refind_conf_content);

    // Cleanup.
    fs::remove(file_testpath);

    assert(filtered_content == REFIND_CONF_TEST);

    // test empty kernel strings with valid file.
    fs::copy_file(file_sample_path, file_testpath, fs::copy_options::overwrite_existing);
    assert(!gucc::bootloader::refind_write_extra_kern_strings(file_testpath, {}));
    auto sample_conf_content = gucc::file_utils::read_whole_file(file_testpath);
    refind_conf_content      = gucc::file_utils::read_whole_file(file_testpath);
    assert(refind_conf_content == sample_conf_content);

    // Cleanup.
    fs::remove(file_testpath);

    // test empty file
    std::ofstream test_file{file_testpath.data()};
    assert(test_file.is_open());

    // setup file
    test_file << "";
    test_file.close();

    assert(!gucc::bootloader::refind_write_extra_kern_strings(file_testpath, extra_kernel_strings));

    // Cleanup.
    fs::remove(file_testpath);
}
