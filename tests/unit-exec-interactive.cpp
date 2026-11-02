#include "definitions.hpp"
#include "utils.hpp"

// import gucc
#include "gucc/process.hpp"

#include <string>  // for string

int main() {
    using namespace std::string_literals;
    output_inter("\n\n------ TEST BASH LAUNCH BEGIN ------\n\n");
    gucc::utils::run_interactive({"/usr/bin/bash"s});
    output_inter("\n\n------ TEST BASH LAUNCH END ------\n\n");
}
