#include "definitions.hpp"
#include "utils.hpp"

// import gucc
#include "gucc/io_utils.hpp"

int main() {
    output_inter("\n\n------ TEST BASH LAUNCH BEGIN ------\n\n");
    gucc::utils::exec("bash", true);
    output_inter("\n\n------ TEST BASH LAUNCH END ------\n\n");
}
