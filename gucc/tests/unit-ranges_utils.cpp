#include "doctest_compatibility.h"

#include "gucc/ranges_utils.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;
namespace utils = gucc::utils;

namespace {

struct Item {
    std::string name;
    int value{};
};

constexpr std::array kNumbers{3, 1, 4, 1, 5};

}  // namespace

static_assert(utils::index_of(kNumbers, 1) == 1U);
static_assert(!utils::index_of(kNumbers, 9));
static_assert(utils::index_if(kNumbers, [](int n) { return n > 3; }) == 2U);
static_assert(utils::find_optional(kNumbers, 4) == 4);

TEST_CASE("ranges utils")
{
    std::vector<Item> items{{.name = "a", .value = 1}, {.name = "b", .value = 2}};

    SECTION("points into the range")
    {
        auto* item = utils::find_ptr(items, "b"sv, &Item::name);
        REQUIRE(item != nullptr);
        item->value = 20;
        CHECK_EQ(items[1].value, 20);
        CHECK(utils::find_ptr(items, "zz"sv, &Item::name) == nullptr);
    }
    SECTION("const range yield")
    {
        const auto& view  = items;
        const Item* found = utils::find_if_ptr(view, [](int value) { return value == 1; }, &Item::value);
        REQUIRE(found != nullptr);
        CHECK_EQ(found->name, "a");
    }
    SECTION("index with projection")
    {
        CHECK_EQ(utils::index_of(items, "b"sv, &Item::name), 1U);
        CHECK_FALSE(utils::index_of(items, "zz"sv, &Item::name));
    }
}
