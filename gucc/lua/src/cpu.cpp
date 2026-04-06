#include "gucc/lua/bindings.hpp"

#include "gucc/cpu.hpp"

namespace gucc::lua {

void register_cpu(sol::table& gucc_table) {
    auto cpu = make_subtable(gucc_table, "cpu");

    cpu["get_vendor"] = []() -> std::string {
        auto vendor = gucc::cpu::get_cpu_vendor();
        switch (vendor) {
        case gucc::cpu::CpuVendor::Intel:
            return "Intel";
        case gucc::cpu::CpuVendor::AMD:
            return "AMD";
        default:
            return "Unknown";
        }
    };

    cpu["get_isa_levels"] = []() {
        auto levels = gucc::cpu::get_isa_levels();
        return sol::as_table(std::move(levels));
    };
}

}  // namespace gucc::lua
