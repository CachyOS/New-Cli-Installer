#include "utils.hpp"

#include <iostream>
#include <string>

#include <netdb.h>

namespace utils {
bool is_connected() noexcept {
    return gethostbyname("google.com");
}

std::string exec(const std::string_view& command) noexcept {
    char buffer[128];
    std::string result;

    FILE* pipe = popen(command.data(), "r");

    if (!pipe) {
        return "popen failed!";
    }

    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr) {
            result += buffer;
        }
    }

    pclose(pipe);

    return result;
}

bool prompt_char(const char* prompt, char& read, const char* color) noexcept {
    fmt::print("{}{}{}\n", color, prompt, RESET);

    std::string tmp;
    if (std::getline(std::cin, tmp)) {
        if (tmp.length() == 1) {
            read = tmp[0];
        } else {
            read = '\0';
        }

        return true;
    }

    return false;
}
}  // namespace utils
