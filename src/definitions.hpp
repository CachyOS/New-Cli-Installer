#ifndef DEFINITIONS_HPP
#define DEFINITIONS_HPP

#include <cstdio>  // for stderr

#include <fmt/color.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

inline constexpr auto RESET       = "\033[0m";
inline constexpr auto BLACK       = "\033[30m";        /* Black */
inline constexpr auto RED         = "\033[31m";        /* Red */
inline constexpr auto GREEN       = "\033[32m";        /* Green */
inline constexpr auto YELLOW      = "\033[33m";        /* Yellow */
inline constexpr auto BLUE        = "\033[34m";        /* Blue */
inline constexpr auto MAGENTA     = "\033[35m";        /* Magenta */
inline constexpr auto CYAN        = "\033[36m";        /* Cyan */
inline constexpr auto WHITE       = "\033[37m";        /* White */
inline constexpr auto BOLDBLACK   = "\033[1m\033[30m"; /* Bold Black */
inline constexpr auto BOLDRED     = "\033[1m\033[31m"; /* Bold Red */
inline constexpr auto BOLDGREEN   = "\033[1m\033[32m"; /* Bold Green */
inline constexpr auto BOLDYELLOW  = "\033[1m\033[33m"; /* Bold Yellow */
inline constexpr auto BOLDBLUE    = "\033[1m\033[34m"; /* Bold Blue */
inline constexpr auto BOLDMAGENTA = "\033[1m\033[35m"; /* Bold Magenta */
inline constexpr auto BOLDCYAN    = "\033[1m\033[36m"; /* Bold Cyan */
inline constexpr auto BOLDWHITE   = "\033[1m\033[37m"; /* Bold White */

#define output_inter(...)  fmt::print(__VA_ARGS__)
#define error_inter(...)   fmt::print(stderr, fmt::fg(fmt::color::red), __VA_ARGS__)
#define warning_inter(...) fmt::print(fmt::fg(fmt::color::yellow), __VA_ARGS__)
#define info_inter(...)    fmt::print(fmt::fg(fmt::color::cyan), __VA_ARGS__)
#define success_inter(...) fmt::print(fmt::fg(fmt::color::green), __VA_ARGS__)

#endif
