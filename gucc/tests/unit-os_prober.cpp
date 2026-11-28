#include "doctest_compatibility.h"

#include "gucc/os_prober.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST_CASE("os-prober output parse")
{
    using gucc::os_prober::detail::parse;

    SECTION("efi loader and bios linux")
    {
        const auto& entries = parse("/dev/nvme0n1p1@/EFI/Microsoft/Boot/bootmgfw.efi:Windows Boot Manager:Windows:efi\n"
                                    "/dev/sda3:Arch Linux:Arch:linux\n"sv);
        REQUIRE_EQ(entries.size(), 2);
        CHECK_EQ(entries[0].device, "/dev/nvme0n1p1"s);
        CHECK_EQ(entries[0].efi_path, std::optional{"/EFI/Microsoft/Boot/bootmgfw.efi"s});
        CHECK_EQ(entries[0].pretty_name, "Windows Boot Manager"s);
        CHECK_EQ(entries[1].device, "/dev/sda3"s);
        CHECK_FALSE(entries[1].efi_path.has_value());
        CHECK_EQ(entries[1].pretty_name, "Arch Linux"s);
    }
    SECTION("blank long name falls back to short name")
    {
        const auto& entries = parse("/dev/sda1: :Windows:chain\n"sv);
        REQUIRE_EQ(entries.size(), 1);
        CHECK_EQ(entries[0].pretty_name, "Windows"s);
    }
    SECTION("junk is skipped")
    {
        CHECK(parse(""sv).empty());
        CHECK(parse("\n   \nnot-a-device:Foo:Foo:linux\nlogging noise\n"sv).empty());
    }
}
