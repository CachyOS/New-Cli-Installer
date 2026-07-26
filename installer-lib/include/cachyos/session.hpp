#pragma once

#include "cachyos/types.hpp"

// import gucc
#include "gucc/process.hpp"

namespace cachyos::installer {

struct InstallSession {
    gucc::utils::ProcessRunner& runner;
    ProgressCallback on_progress;
};

}  // namespace cachyos::installer
