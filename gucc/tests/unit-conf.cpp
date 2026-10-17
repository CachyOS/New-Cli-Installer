#include "doctest_compatibility.h"

#include "gucc/conf.hpp"

#include <string_view>

using namespace std::string_view_literals;

namespace conf = gucc::conf;
using conf::KeyStyle;

static_assert(!conf::classify(""sv));
static_assert(!conf::classify(" \t\r"sv));
static_assert(conf::classify("  key=1"sv)->body == "key=1"sv);
static_assert(!conf::classify("key=1"sv)->commented);
static_assert(conf::classify("#  en_US UTF-8"sv)->body == "  en_US UTF-8"sv);
static_assert(conf::classify("; x"sv, "#;"sv)->commented);
static_assert(!conf::classify("; x"sv)->commented);

static_assert(conf::section_name("[core]"sv) == "core"sv);
static_assert(conf::section_name("  [Seat:*]  # main\r"sv) == "Seat:*"sv);
static_assert(!conf::section_name("#[core]"sv));
static_assert(!conf::section_name("[]"sv));
static_assert(!conf::section_name("[broken"sv));

static_assert(conf::key_of(" User = neo"sv, KeyStyle::Assign) == "User"sv);
static_assert(conf::key_of("no assignment"sv, KeyStyle::Assign).empty());
static_assert(conf::key_of("  extra_kernel_version_strings a,b"sv, KeyStyle::Word) == "extra_kernel_version_strings"sv);
static_assert(conf::key_of("en_US.UTF-8\r"sv, KeyStyle::Word) == "en_US.UTF-8"sv);

static_assert(conf::is_directive("#AutomaticLogin=x"sv, "AutomaticLogin"sv, KeyStyle::Assign));
static_assert(!conf::is_directive("AutomaticLoginEnable=True"sv, "AutomaticLogin"sv, KeyStyle::Assign));
static_assert(!conf::is_directive("# Set User=x to enable"sv, "User"sv, KeyStyle::Assign));

TEST_CASE("conf")
{
    SECTION("replaces")
    {
        REQUIRE_EQ(conf::set_directive("a 1\n#key old\nkey older\n"sv, "key"sv, "key new"sv, KeyStyle::Word), "a 1\nkey new\nkey new\n");
        REQUIRE_EQ(conf::set_directive("# key=1\n"sv, "key"sv, "key=2"sv, KeyStyle::Assign), "key=2\n");
    }
    SECTION("appends missing directive")
    {
        REQUIRE_EQ(conf::set_directive(""sv, "key"sv, "key v"sv, KeyStyle::Word), "key v\n");
        REQUIRE_EQ(conf::set_directive("a 1"sv, "key"sv, "key v"sv, KeyStyle::Word), "a 1\nkey v\n");
        REQUIRE_EQ(conf::set_directive("a 1\n"sv, "key"sv, "key v"sv, KeyStyle::Word), "a 1\nkey v\n");
    }
    SECTION("simple uncomment")
    {
        const auto is_b = [](const conf::Line& line) { return conf::key_of(line.body, KeyStyle::Word) == "b"sv; };
        REQUIRE_EQ(conf::uncomment_where("#a\n  #b 1\n#b\n"sv, is_b), "#a\n  b 1\nb\n");
        REQUIRE_EQ(conf::uncomment_where("b\n"sv, is_b), "b\n");
    }
    SECTION("preserves comments and blank lines")
    {
        const auto is_b = [](const conf::Line& line) { return conf::key_of(line.body, KeyStyle::Word) == "b"sv; };
        REQUIRE_EQ(conf::comment_out_where("a\nb 1\n#b 2\n\n"sv, is_b), "a\n#b 1\n#b 2\n\n");
    }
}

TEST_CASE("conf set_value")
{
    using gucc::conf::set_value;

    SECTION("replaces an existing assignment, keeping the rest")
    {
        static constexpr auto conf = "[Autologin]\nUser=old\nSession=plasma\n"sv;
        REQUIRE_EQ(set_value(conf, "Autologin"sv, "User"sv, "neo"sv), "[Autologin]\nUser=neo\nSession=plasma\n");
    }
    SECTION("uncomments a commented-out default")
    {
        static constexpr auto lightdm = "[LightDM]\n#autologin-user=\n[Seat:*]\n#greeter-session=\n#autologin-user=\n#autologin-user-timeout=0\n"sv;
        REQUIRE_EQ(set_value(lightdm, "Seat:*"sv, "autologin-user"sv, "neo"sv),
            "[LightDM]\n#autologin-user=\n[Seat:*]\n#greeter-session=\nautologin-user=neo\n#autologin-user-timeout=0\n");
    }
    SECTION("commented with a space, as lxdm ships it")
    {
        static constexpr auto lxdm = "[base]\n# autologin=dgod\n# session=/usr/bin/startlxde\n"sv;
        REQUIRE_EQ(set_value(lxdm, "base"sv, "autologin"sv, "neo"sv), "[base]\nautologin=neo\n# session=/usr/bin/startlxde\n");
    }
    SECTION("similar key names do not match")
    {
        static constexpr auto conf = "[daemon]\nAutomaticLoginEnable=True\n"sv;
        REQUIRE_EQ(set_value(conf, "daemon"sv, "AutomaticLogin"sv, "neo"sv), "[daemon]\nAutomaticLoginEnable=True\nAutomaticLogin=neo\n");
    }
    SECTION("appends to the end of its section, not the file")
    {
        static constexpr auto gdm = "[daemon]\n# Uncomment the line below to force the login screen to use Xorg\n#WaylandEnable=false\n\n[security]\n\n[debug]\n"sv;
        REQUIRE_EQ(set_value(gdm, "daemon"sv, "AutomaticLoginEnable"sv, "True"sv),
            "[daemon]\n# Uncomment the line below to force the login screen to use Xorg\n#WaylandEnable=false\nAutomaticLoginEnable=True\n\n[security]\n\n[debug]\n");
    }
    SECTION("adds a missing section")
    {
        REQUIRE_EQ(set_value(""sv, "Autologin"sv, "User"sv, "neo"sv), "[Autologin]\nUser=neo\n");
        REQUIRE_EQ(set_value("[General]\nNumlock=on\n"sv, "Autologin"sv, "User"sv, "neo"sv), "[General]\nNumlock=on\n\n[Autologin]\nUser=neo\n");
    }
    SECTION("a key in another section is left alone")
    {
        static constexpr auto conf = "[Other]\nUser=keep\n"sv;
        REQUIRE_EQ(set_value(conf, "Autologin"sv, "User"sv, "neo"sv), "[Other]\nUser=keep\n\n[Autologin]\nUser=neo\n");
    }
}
