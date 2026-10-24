#include "doctest_compatibility.h"

#include "gucc/string_utils.hpp"

#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

static_assert(utils::to_lower('Q') == 'q' && utils::to_lower('1') == '1' && utils::to_upper('k') == 'K');
static_assert(utils::to_lower(static_cast<char>(0xC4)) == static_cast<char>(0xC4));
static_assert(utils::to_lower("VFAT-Ünit"sv) == "vfat-Ünit");

TEST_CASE("string_utils")
{
  SECTION("Split test")
  {
    SECTION("empty string view")
    {
      const auto input  = ""sv;
      const auto tokens = gucc::utils::make_multiline_view(input, false, ',');
      REQUIRE_EQ(tokens.size(), 0);
    }
    SECTION("single delim string view")
    {
      const auto input  = ","sv;
      const auto tokens = gucc::utils::make_multiline_view(input, false, ',');
      REQUIRE_EQ(tokens.size(), 0);
    }
    SECTION("short string view")
    {
      const auto input  = "1,22,333"sv;
      const auto tokens = gucc::utils::make_multiline_view(input, false, ',');
      REQUIRE_EQ(input.data(), tokens[0].data());
      REQUIRE_EQ(tokens.size(), 3);
      REQUIRE_EQ(tokens[0], "1");
      REQUIRE_EQ(tokens[1], "22");
      REQUIRE_EQ(tokens[2], "333");
    }
    SECTION("short string")
    {
      const std::string input = "1,22,333";
      const auto tokens       = gucc::utils::make_multiline_view(input, false, ',');
      REQUIRE_EQ(input.data(), tokens[0].data());
      REQUIRE_EQ(tokens.size(), 3);
      REQUIRE_EQ(tokens[0], "1");
      REQUIRE_EQ(tokens[1], "22");
      REQUIRE_EQ(tokens[2], "333");
    }
    SECTION("long string view")
    {
      const auto input  = "this will not fit into small string optimization"sv;
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
      const auto tokens       = gucc::utils::make_multiline_view(input, false, ' ');
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
  SECTION("join test")
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
  SECTION("make_multiline join test")
  {
    const std::vector<std::string> input{"1", "22", "333"};
    REQUIRE_EQ(gucc::utils::make_multiline(std::vector<std::string>{}), "");
    REQUIRE_EQ(gucc::utils::make_multiline(input), "1\n22\n333\n");
    REQUIRE_EQ(gucc::utils::make_multiline(input, true, ", "), "333, 22, 1, ");
  }
  SECTION("trim test")
  {
    SECTION("empty string view")
    {
      const auto input       = ""sv;
      const auto trimmed_str = gucc::utils::trim(input);
      REQUIRE_EQ(trimmed_str.size(), 0);
      REQUIRE_EQ(trimmed_str, input);
    }
    SECTION("only chars to trim")
    {
      const auto input       = "\n\t \t\v\f"sv;
      const auto trimmed_str = gucc::utils::trim(input);
      REQUIRE_EQ(trimmed_str.size(), 0);
      REQUIRE_EQ(trimmed_str, ""sv);
    }
    SECTION("short string view")
    {
      const auto input       = "\n\t 2\t\v\f"sv;
      const auto trimmed_str = gucc::utils::trim(input);
      REQUIRE_EQ(trimmed_str.size(), 1);
      REQUIRE_EQ(trimmed_str, "2"sv);
      REQUIRE_EQ(gucc::utils::ltrim(input), "2\t\v\f"sv);
      REQUIRE_EQ(gucc::utils::rtrim(input), "\n\t 2"sv);
    }
    SECTION("without any trim chars")
    {
      const auto input       = "this will not fit into small string optimization"sv;
      const auto trimmed_str = gucc::utils::trim(input);
      REQUIRE_EQ(trimmed_str, input);
    }
  }
  SECTION("extract_after test")
  {
    SECTION("empty string")
    {
      const auto input  = ""sv;
      const auto result = gucc::utils::extract_after(input, "prefix"sv);
      REQUIRE(!result.has_value());
    }
    SECTION("prefix not found")
    {
      const auto input  = "hello world"sv;
      const auto result = gucc::utils::extract_after(input, "missing"sv);
      REQUIRE(!result.has_value());
    }
    SECTION("basic extraction with space delimiter")
    {
      const auto input  = "ID 256 gen 100 path @home"sv;
      const auto id_val = gucc::utils::extract_after(input, "ID "sv);
      REQUIRE(id_val.has_value());
      REQUIRE_EQ(*id_val, "256"sv);

      const auto gen_val = gucc::utils::extract_after(input, "gen "sv);
      REQUIRE(gen_val.has_value());
      REQUIRE_EQ(*gen_val, "100"sv);
    }
    SECTION("extraction at end of string")
    {
      const auto input  = "path @home"sv;
      const auto result = gucc::utils::extract_after(input, "path "sv, '\0');
      REQUIRE(result.has_value());
      REQUIRE_EQ(*result, "@home"sv);
    }
    SECTION("extraction with custom delimiter")
    {
      const auto input  = "key:value:rest"sv;
      const auto result = gucc::utils::extract_after(input, "key"sv, ':');
      REQUIRE(result.has_value());
      REQUIRE_EQ(*result, ""sv);
    }
    SECTION("extraction after colon")
    {
      const auto input  = "Device size: 12345678"sv;
      const auto result = gucc::utils::extract_after(input, "Device size:"sv, '\n');
      REQUIRE(result.has_value());
      REQUIRE_EQ(*result, " 12345678"sv);
    }
    SECTION("btrfs subvolume list format")
    {
      const auto input = "ID 257 gen 1234 parent 5 top level 5 parent_uuid - uuid abc-123 path @snapshots"sv;

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
  SECTION("parse_uint test")
  {
    SECTION("empty string")
    {
      const auto input  = ""sv;
      const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
      REQUIRE(!result.has_value());
    }
    SECTION("valid uint32")
    {
      const auto input  = "12345"sv;
      const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
      REQUIRE(result.has_value());
      REQUIRE_EQ(*result, 12345u);
    }
    SECTION("valid uint64")
    {
      const auto input  = "9999999999"sv;
      const auto result = gucc::utils::parse_uint<std::uint64_t>(input);
      REQUIRE(result.has_value());
      REQUIRE_EQ(*result, 9999999999ULL);
    }
    SECTION("zero")
    {
      const auto input  = "0"sv;
      const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
      REQUIRE(result.has_value());
      REQUIRE_EQ(*result, 0u);
    }
    SECTION("invalid contains letters")
    {
      const auto input  = "123abc"sv;
      const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
      // hmm
      if (result.has_value()) {
        REQUIRE_EQ(*result, 123u);
      }
    }
    SECTION("invalid negative number")
    {
      const auto input  = "-123"sv;
      const auto result = gucc::utils::parse_uint<std::uint32_t>(input);
      REQUIRE(!result.has_value());
    }
    SECTION("combined with extract_after")
    {
      const auto input  = "ID 256 gen 100"sv;
      const auto id_str = gucc::utils::extract_after(input, "ID "sv);
      REQUIRE(id_str.has_value());
      const auto id = gucc::utils::parse_uint<std::uint64_t>(*id_str);
      REQUIRE(id.has_value());
      REQUIRE_EQ(*id, 256ULL);
    }
  }
  SECTION("first_field")
  {
    REQUIRE(gucc::utils::first_field(""sv).empty());
    REQUIRE(gucc::utils::first_field(" \t "sv).empty());
    REQUIRE_EQ(gucc::utils::first_field("\t    /dev/sda1  ONLINE\t0 0 0  "sv), "/dev/sda1"sv);
    REQUIRE_EQ(gucc::utils::first_field("zpcachyos/ROOT\t/"sv), "zpcachyos/ROOT"sv);
  }
  SECTION("nth_field")
  {
    using gucc::utils::nth_field;
    REQUIRE_EQ(nth_field("/dev/sda2 none swap defaults 0 0"sv, 0), "/dev/sda2"sv);
    REQUIRE_EQ(nth_field("  /dev/sda2\t\tnone  swap"sv, 2), "swap"sv);
    REQUIRE_EQ(nth_field("/dev/sda2 none"sv, 2), ""sv);
    REQUIRE_EQ(nth_field("/dev/sda2 none "sv, 2), ""sv);
    REQUIRE_EQ(nth_field(""sv, 0), ""sv);
    REQUIRE_EQ(nth_field("a b c"sv, 5), ""sv);
    REQUIRE_EQ(nth_field(" \t "sv, 0), ""sv);
  }
  SECTION("nth_field with custom delimiters")
  {
    using gucc::utils::nth_field;
    REQUIRE_EQ(nth_field("root:x:0:0::/root:/bin/bash"sv, 0, ":"sv), "root"sv);
    REQUIRE_EQ(nth_field("root:x:0:0::/root:/bin/bash"sv, 4, ":"sv), "/root"sv);
    REQUIRE_EQ(nth_field("::a::b::"sv, 1, ":"sv), "b"sv);
    REQUIRE_EQ(nth_field("::a::b::"sv, 2, ":"sv), ""sv);
    REQUIRE_EQ(nth_field("my label\tvalue two"sv, 0, "\t"sv), "my label"sv);
    REQUIRE_EQ(nth_field("my label\tvalue two"sv, 1, "\t"sv), "value two"sv);
    REQUIRE_EQ(nth_field("a=b;c=d"sv, 3, "=;"sv), "d"sv);
    REQUIRE_EQ(nth_field("abc"sv, 0, ""sv), "abc"sv);
    static_assert(nth_field("x:y"sv, 1, ":"sv) == "y"sv);
  }
  SECTION("starts_with_word")
  {
    using gucc::utils::starts_with_word;
    REQUIRE(starts_with_word("swap /dev/sda3"sv, "swap"sv));
    REQUIRE(starts_with_word("swap\t/dev/sda3"sv, "swap"sv));
    REQUIRE(starts_with_word("swap"sv, "swap"sv));
    REQUIRE_FALSE(starts_with_word("swapfoo /dev/sda3"sv, "swap"sv));
    REQUIRE_FALSE(starts_with_word(" swap"sv, "swap"sv));
    REQUIRE_FALSE(starts_with_word("sw"sv, "swap"sv));
    REQUIRE_FALSE(starts_with_word("en_US.UTF-8\r"sv, "en_US.UTF-8"sv));
    REQUIRE(starts_with_word("en_US.UTF-8\r"sv, "en_US.UTF-8"sv, " \t\r"sv));
    REQUIRE(starts_with_word("key=value"sv, "key"sv, "="sv));
    REQUIRE(starts_with_word(""sv, ""sv));
    REQUIRE_FALSE(starts_with_word("x"sv, ""sv));
  }
  SECTION("decode_escapes")
  {
    using gucc::utils::decode_escapes;
    REQUIRE_EQ(decode_escapes("/mnt/my\\040disk"sv, "\\"sv, 8, 3), "/mnt/my disk");
    REQUIRE_EQ(decode_escapes("a\\134b"sv, "\\"sv, 8, 3), "a\\b");
    REQUIRE_EQ(decode_escapes("trunc\\04"sv, "\\"sv, 8, 3), "trunc\\04");
    REQUIRE_EQ(decode_escapes("bad\\089"sv, "\\"sv, 8, 3), "bad\\089");
    REQUIRE_EQ(decode_escapes("big\\777"sv, "\\"sv, 8, 3), "big\\777");
    REQUIRE_EQ(decode_escapes("end\\"sv, "\\"sv, 8, 3), "end\\");
    REQUIRE_EQ(decode_escapes("\\\\040"sv, "\\"sv, 8, 3), "\\ ");
    REQUIRE_EQ(decode_escapes("My\\x20Disk"sv, "\\x"sv, 16, 2), "My Disk");
    REQUIRE_EQ(decode_escapes("q\\x22"sv, "\\x"sv, 16, 2), "q\"");
    REQUIRE_EQ(decode_escapes("t\\x2"sv, "\\x"sv, 16, 2), "t\\x2");
    REQUIRE_EQ(decode_escapes("g\\xzz"sv, "\\x"sv, 16, 2), "g\\xzz");
    REQUIRE_EQ(decode_escapes("plain \\y"sv, "\\x"sv, 16, 2), "plain \\y");
    REQUIRE_EQ(decode_escapes("same"sv, ""sv, 16, 2), "same");
  }
  SECTION("split_lines delimiter")
  {
    const auto fields = gucc::utils::split_lines("a,,b,"sv, ',') | std::ranges::to<std::vector<std::string_view>>();
    REQUIRE_EQ(fields, std::vector{"a"sv, ""sv, "b"sv, ""sv});
    const auto lines = gucc::utils::split_lines("x\n\ny"sv) | std::ranges::to<std::vector<std::string_view>>();
    REQUIRE_EQ(lines, std::vector{"x"sv, ""sv, "y"sv});
  }
  SECTION("parse_uint_exact")
  {
    using gucc::utils::parse_uint_exact;
    REQUIRE_EQ(parse_uint_exact<std::uint32_t>("42"sv), 42U);
    REQUIRE_FALSE(parse_uint_exact<std::uint32_t>("42abc"sv).has_value());
    REQUIRE_FALSE(parse_uint_exact<std::uint32_t>("42 "sv).has_value());
    REQUIRE_FALSE(parse_uint_exact<std::uint32_t>(" 42"sv).has_value());
    REQUIRE_FALSE(parse_uint_exact<std::uint32_t>(""sv).has_value());
    REQUIRE_FALSE(parse_uint_exact<std::uint8_t>("256"sv).has_value());
    REQUIRE_EQ(gucc::utils::parse_uint<std::uint32_t>("42abc"sv), 42U);
  }
}
