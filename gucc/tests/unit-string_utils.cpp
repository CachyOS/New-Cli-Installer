#include "doctest_compatibility.h"

#include "gucc/string_utils.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

TEST_CASE("Split test")
{
  SECTION("empty string view")
  {
    static constexpr auto input = ""sv;
    const auto tokens = gucc::utils::make_multiline_view(input, false, ',');
    REQUIRE_EQ(tokens.size(), 0);
  }
  SECTION("single delim string view")
  {
    static constexpr auto input = ","sv;
    const auto tokens = gucc::utils::make_multiline_view(input, false, ',');
    REQUIRE_EQ(tokens.size(), 0);
  }
  SECTION("short string view")
  {
    static constexpr auto input = "1,22,333"sv;
    const auto tokens = gucc::utils::make_multiline_view(input, false, ',');
    REQUIRE_EQ(input.data(), tokens[0].data());
    REQUIRE_EQ(tokens.size(), 3);
    REQUIRE_EQ(tokens[0], "1");
    REQUIRE_EQ(tokens[1], "22");
    REQUIRE_EQ(tokens[2], "333");
  }
  SECTION("short string")
  {
    static constexpr std::string input = "1,22,333";
    const auto tokens = gucc::utils::make_multiline_view(input, false, ',');
    REQUIRE_EQ(input.data(), tokens[0].data());
    REQUIRE_EQ(tokens.size(), 3);
    REQUIRE_EQ(tokens[0], "1");
    REQUIRE_EQ(tokens[1], "22");
    REQUIRE_EQ(tokens[2], "333");
  }
  SECTION("long string view")
  {
    static constexpr auto input = "this will not fit into small string optimization"sv;
    const auto tokens = gucc::utils::make_multiline_view(input, false, ' ');
    REQUIRE_EQ(input.data(), tokens[0].data());
    REQUIRE_EQ(tokens.size(), 8);
    REQUIRE_EQ(tokens[0], "this");
    REQUIRE_EQ(tokens[1], "will");
    REQUIRE_EQ(tokens[2], "not");
    REQUIRE_EQ(tokens[3], "fit");
    REQUIRE_EQ(tokens[4], "into");
    REQUIRE_EQ(tokens[5], "small");
    REQUIRE_EQ(tokens[6], "string");
    REQUIRE_EQ(tokens[7], "optimization");
  }
  SECTION("long string")
  {
    const std::string input = "this will not fit into small string optimization";
    const auto tokens = gucc::utils::make_multiline_view(input, false, ' ');
    REQUIRE_EQ(input.data(), tokens[0].data());
    REQUIRE_EQ(tokens.size(), 8);
    REQUIRE_EQ(tokens[0], "this");
    REQUIRE_EQ(tokens[1], "will");
    REQUIRE_EQ(tokens[2], "not");
    REQUIRE_EQ(tokens[3], "fit");
    REQUIRE_EQ(tokens[4], "into");
    REQUIRE_EQ(tokens[5], "small");
    REQUIRE_EQ(tokens[6], "string");
    REQUIRE_EQ(tokens[7], "optimization");
  }
}

TEST_CASE("join test")
{
  SECTION("empty vector")
  {
    const std::vector<std::string> input{};
    const auto joined_str = gucc::utils::join(input, ',');
    REQUIRE_EQ(joined_str.empty(), true);
  }
  SECTION("single delim")
  {
    const std::vector<std::string> input{","};
    const auto joined_str = gucc::utils::join(input, ',');
    REQUIRE_EQ(joined_str.size(), 1);
    REQUIRE_EQ(joined_str, ",");
  }
  SECTION("short vector")
  {
    const std::vector<std::string> input{"1", "22", "333"};
    const auto joined_str = gucc::utils::join(input, ',');
    REQUIRE_EQ(joined_str.size(), 8);
    REQUIRE_EQ(joined_str, "1,22,333");
  }
}

TEST_CASE("trim test")
{
  SECTION("empty string view")
  {
    static constexpr auto input = ""sv;
    const auto trimmed_str = gucc::utils::trim(input);
    REQUIRE_EQ(trimmed_str.size(), 0);
    REQUIRE_EQ(trimmed_str, input);
  }
  SECTION("only chars to trim")
  {
    static constexpr auto input = "\n\t \t\v\f"sv;
    const auto trimmed_str = gucc::utils::trim(input);
    REQUIRE_EQ(trimmed_str.size(), 0);
    REQUIRE_EQ(trimmed_str, ""sv);
  }
  SECTION("short string view")
  {
    static constexpr auto input = "\n\t 2\t\v\f"sv;
    const auto trimmed_str = gucc::utils::trim(input);
    REQUIRE_EQ(trimmed_str.size(), 1);
    REQUIRE_EQ(trimmed_str, "2"sv);
    REQUIRE_EQ(gucc::utils::ltrim(input), "2\t\v\f"sv);
    REQUIRE_EQ(gucc::utils::rtrim(input), "\n\t 2"sv);
  }
  SECTION("without any trim chars")
  {
    static constexpr auto input = "this will not fit into small string optimization"sv;
    const auto trimmed_str = gucc::utils::trim(input);
    REQUIRE_EQ(trimmed_str, input);
  }
}

TEST_CASE("extract_after test")
{
  SECTION("empty string")
  {
    static constexpr auto input = ""sv;
    const auto result = gucc::utils::extract_after(input, "prefix"sv);
    REQUIRE(!result.has_value());
  }
  SECTION("prefix not found")
  {
    static constexpr auto input = "hello world"sv;
    const auto result = gucc::utils::extract_after(input, "missing"sv);
    REQUIRE(!result.has_value());
  }
  SECTION("basic extraction with space delimiter")
  {
    static constexpr auto input = "ID 256 gen 100 path @home"sv;
    const auto id_val = gucc::utils::extract_after(input, "ID "sv);
    REQUIRE(id_val.has_value());
    REQUIRE_EQ(*id_val, "256"sv);

    const auto gen_val = gucc::utils::extract_after(input, "gen "sv);
    REQUIRE(gen_val.has_value());
    REQUIRE_EQ(*gen_val, "100"sv);
  }
  SECTION("extraction at end of string")
  {
    static constexpr auto input = "path @home"sv;
    const auto result = gucc::utils::extract_after(input, "path "sv, '\0');
    REQUIRE(result.has_value());
    REQUIRE_EQ(*result, "@home"sv);
  }
  SECTION("extraction with custom delimiter")
  {
    static constexpr auto input = "key:value:rest"sv;
    const auto result = gucc::utils::extract_after(input, "key"sv, ':');
    REQUIRE(result.has_value());
    REQUIRE_EQ(*result, ""sv);
  }
  SECTION("extraction after colon")
  {
    static constexpr auto input = "Device size: 12345678"sv;
    const auto result = gucc::utils::extract_after(input, "Device size:"sv, '\n');
    REQUIRE(result.has_value());
    REQUIRE_EQ(*result, " 12345678"sv);
  }
  SECTION("btrfs subvolume list format")
  {
    static constexpr auto input = "ID 257 gen 1234 parent 5 top level 5 parent_uuid - uuid abc-123 path @snapshots"sv;

    const auto id = gucc::utils::extract_after(input, "ID "sv);
    REQUIRE(id.has_value());
    REQUIRE_EQ(*id, "257"sv);

    const auto gen = gucc::utils::extract_after(input, "gen "sv);
    REQUIRE(gen.has_value());
    REQUIRE_EQ(*gen, "1234"sv);

    const auto parent = gucc::utils::extract_after(input, "parent "sv);
    REQUIRE(parent.has_value());
    REQUIRE_EQ(*parent, "5"sv);

    const auto top_level = gucc::utils::extract_after(input, "top level "sv);
    REQUIRE(top_level.has_value());
    REQUIRE_EQ(*top_level, "5"sv);

    const auto parent_uuid = gucc::utils::extract_after(input, "parent_uuid "sv);
    REQUIRE(parent_uuid.has_value());
    REQUIRE_EQ(*parent_uuid, "-"sv);

    const auto uuid = gucc::utils::extract_after(input, " uuid "sv);
    REQUIRE(uuid.has_value());
    REQUIRE_EQ(*uuid, "abc-123"sv);

    const auto path = gucc::utils::extract_after(input, "path "sv, '\0');
    REQUIRE(path.has_value());
    REQUIRE_EQ(*path, "@snapshots"sv);
  }
}

TEST_CASE("parse_uint test")
{
  SECTION("empty string")
  {
    static constexpr auto input = ""sv;
    const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
    REQUIRE(!result.has_value());
  }
  SECTION("valid uint32")
  {
    static constexpr auto input = "12345"sv;
    const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
    REQUIRE(result.has_value());
    REQUIRE_EQ(*result, 12345u);
  }
  SECTION("valid uint64")
  {
    static constexpr auto input = "9999999999"sv;
    const auto result = gucc::utils::parse_uint<std::uint64_t>(input);
    REQUIRE(result.has_value());
    REQUIRE_EQ(*result, 9999999999ULL);
  }
  SECTION("zero")
  {
    static constexpr auto input = "0"sv;
    const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
    REQUIRE(result.has_value());
    REQUIRE_EQ(*result, 0u);
  }
  SECTION("invalid - contains letters")
  {
    static constexpr auto input = "123abc"sv;
    const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
    if (result.has_value()) {
      REQUIRE_EQ(*result, 123u);
    }
  }
  SECTION("invalid - negative number")
  {
    static constexpr auto input = "-123"sv;
    const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
    REQUIRE(!result.has_value());
  }
  SECTION("combined with extract_after")
  {
    static constexpr auto input = "ID 256 gen 100"sv;
    const auto id_str = gucc::utils::extract_after(input, "ID "sv);
    REQUIRE(id_str.has_value());
    const auto id = gucc::utils::parse_uint<std::uint64_t>(*id_str);
    REQUIRE(id.has_value());
    REQUIRE_EQ(*id, 256ULL);
  }
}
