#include "gucc/kmod.hpp"
#include "gucc/io_utils.hpp"

#include <initializer_list>  // for initializer_list
#include <string>            // for string
#include <utility>           // for move
#include <vector>            // for vector

using namespace std::string_view_literals;

namespace {

auto modprobe(std::vector<std::string> argv, std::initializer_list<std::string_view> modules, std::string_view what) noexcept -> gucc::Result<void> {
    // modprobe -a without names is a usage error
    if (modules.size() == 0) {
        return {};
    }
    argv.insert(argv.end(), modules.begin(), modules.end());
    return gucc::utils::exec_or_error(argv, what);
}

}  // namespace

namespace gucc::kmod {

auto load(std::initializer_list<std::string_view> modules) noexcept -> Result<void> {
    return modprobe({"/usr/bin/modprobe", "-q", "-a"}, modules, "load kernel modules"sv);
}

auto unload(std::initializer_list<std::string_view> modules) noexcept -> Result<void> {
    return modprobe({"/usr/bin/modprobe", "-r", "-q", "-a"}, modules, "unload kernel modules"sv);
}

}  // namespace gucc::kmod
