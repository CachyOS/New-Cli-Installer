#include "definitions.hpp"
#include "utils.hpp"

int main() {
    output_inter("\n\n------ TEST BASH LAUNCH BEGIN ------\n\n");
    utils::exec("bash", true);
    output_inter("\n\n------ TEST BASH LAUNCH END ------\n\n");
}
