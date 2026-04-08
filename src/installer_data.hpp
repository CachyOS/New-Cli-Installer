#ifndef INSTALLER_DATA_HPP
#define INSTALLER_DATA_HPP

// Moved to installer-lib. This header provides backward-compatible aliases.
#include "cachyos/installer_data.hpp"

namespace installer::data {

using cachyos::installer::data::available_desktops;
using cachyos::installer::data::available_filesystems;
using cachyos::installer::data::available_kernels;
using cachyos::installer::data::available_shells;
using cachyos::installer::data::get_device_list;
using cachyos::installer::data::parse_device_name;
using cachyos::installer::data::to_vec;

}  // namespace installer::data

#endif  // INSTALLER_DATA_HPP
