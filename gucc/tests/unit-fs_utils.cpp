#include "doctest_compatibility.h"

#include "gucc/fs_utils.hpp"

#include <string_view>

using namespace std::string_view_literals;

using gucc::fs::utils::detail::parse_lsblk_pairs;

TEST_CASE("parse_lsblk_pairs")
{
  SECTION("fstype and uuid")
  {
    const auto pairs = parse_lsblk_pairs(R"(FSTYPE="vfat" UUID="FB54-6E42")"sv);
    REQUIRE_EQ(pairs.size(), 2u);
    CHECK_EQ(pairs.at("FSTYPE"), "vfat"sv);
    CHECK_EQ(pairs.at("UUID"), "FB54-6E42"sv);
  }
  SECTION("empty values are kept")
  {
    const auto pairs = parse_lsblk_pairs("FSTYPE=\"\" UUID=\"\"\n"sv);
    REQUIRE_EQ(pairs.size(), 2u);
    CHECK(pairs.at("FSTYPE").empty());
    CHECK(pairs.at("UUID").empty());
  }
  SECTION("UUID key does not match inside PARTUUID")
  {
    const auto pairs = parse_lsblk_pairs(R"(PARTUUID="1b8b6bd1-02" UUID="5d2f")"sv);
    CHECK_EQ(pairs.at("PARTUUID"), "1b8b6bd1-02"sv);
    CHECK_EQ(pairs.at("UUID"), "5d2f"sv);

    const auto only_partuuid = parse_lsblk_pairs(R"(PARTUUID="1b8b6bd1-02")"sv);
    CHECK_FALSE(only_partuuid.contains("UUID"));
  }
  SECTION("hex escapes are decoded")
  {
    const auto pairs = parse_lsblk_pairs(R"(LABEL="my\x20disk\x22q\x5c" FSTYPE="ext4")"sv);
    CHECK_EQ(pairs.at("LABEL"), "my disk\"q\\"sv);
    CHECK_EQ(pairs.at("FSTYPE"), "ext4"sv);
  }
  SECTION("truncated or invalid escapes stay literal")
  {
    CHECK_EQ(parse_lsblk_pairs(R"(LABEL="a\x2")"sv).at("LABEL"), R"(a\x2)"sv);
    CHECK_EQ(parse_lsblk_pairs(R"(LABEL="a\xZZ")"sv).at("LABEL"), R"(a\xZZ)"sv);
    CHECK_EQ(parse_lsblk_pairs(R"(LABEL="a\x")"sv).at("LABEL"), R"(a\x)"sv);
    CHECK_EQ(parse_lsblk_pairs(R"(LABEL="a\")"sv).at("LABEL"), R"(a\)"sv);
  }
  SECTION("stderr lines before the row are skipped, only the first row is read")
  {
    const auto pairs = parse_lsblk_pairs("lsblk: /dev/sdz: not a block device\nFSTYPE=\"btrfs\" UUID=\"aaaa\"\nFSTYPE=\"ext4\" UUID=\"bbbb\"\n"sv);
    CHECK_EQ(pairs.at("FSTYPE"), "btrfs"sv);
    CHECK_EQ(pairs.at("UUID"), "aaaa"sv);
  }
  SECTION("duplicate key")
  {
    CHECK_EQ(parse_lsblk_pairs(R"(A="1" A="2")"sv).at("A"), "1"sv);
  }
  SECTION("malformed input")
  {
    CHECK(parse_lsblk_pairs(""sv).empty());
    CHECK(parse_lsblk_pairs("\n\n"sv).empty());
    CHECK(parse_lsblk_pairs("FSTYPE=vfat"sv).empty());
    CHECK(parse_lsblk_pairs(R"(="x")"sv).empty());
    const auto pairs = parse_lsblk_pairs(R"(FSTYPE="vfat" UUID="FB54)"sv);
    REQUIRE_EQ(pairs.size(), 1u);
    CHECK_EQ(pairs.at("FSTYPE"), "vfat"sv);
  }
}
