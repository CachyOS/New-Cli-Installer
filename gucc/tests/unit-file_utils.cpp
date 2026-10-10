#include "doctest_compatibility.h"
#include "test_temp_root.hpp"

#include "gucc/file_utils.hpp"
#include "gucc/logger.hpp"

#include <memory>
#include <string>
#include <string_view>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
using gucc::tests::TempRoot;

TEST_CASE("read_whole_file")
{
  auto sink   = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) { });
  auto logger = std::make_shared<spdlog::logger>("default", sink);
  spdlog::set_default_logger(logger);
  gucc::logger::set_logger(logger);

  using gucc::file_utils::read_whole_file;

  TempRoot root{"gucc-file-utils"};
  const auto path = (root.path() / "file").string();

  SECTION("regular file")
  {
    REQUIRE(gucc::file_utils::write_file(path, "line1\nline2\n"sv));
    REQUIRE_EQ(read_whole_file(path), "line1\nline2\n");
  }
  SECTION("file larger than the initial buffer, with embedded NULs")
  {
    auto content    = std::string(100'000, 'x');
    content[50'000] = '\0';
    REQUIRE(gucc::file_utils::write_file(path, content));
    REQUIRE_EQ(read_whole_file(path), content);
  }
  SECTION("empty file")
  {
    REQUIRE(gucc::file_utils::write_file(path, ""sv));
    REQUIRE(read_whole_file(path).empty());
  }
  SECTION("missing file")
  {
    REQUIRE(read_whole_file((root.path() / "missing").string()).empty());
  }
  SECTION("directory")
  {
    REQUIRE(read_whole_file(root.path().string()).empty());
  }
  SECTION("procfs reports size 0 but has content")
  {
    const auto status = read_whole_file("/proc/self/status");
    REQUIRE_FALSE(status.empty());
    REQUIRE(status.contains("Name:"sv));
  }
  SECTION("path slice without a null terminator")
  {
    REQUIRE(gucc::file_utils::write_file(path, "slice"sv));
    const auto padded = path + "-not-part-of-the-path";
    REQUIRE_EQ(read_whole_file(std::string_view{padded}.substr(0, path.size())), "slice");
  }
  SECTION("write helpers honour a slice too")
  {
    const auto padded = path + "-suffix";
    const auto slice  = std::string_view{padded}.substr(0, path.size());
    REQUIRE(gucc::file_utils::create_file_for_overwrite(slice, "a"sv));
    REQUIRE(gucc::file_utils::write_to_file("b"sv, slice));
    REQUIRE_EQ(read_whole_file(path), "b");
  }
}
