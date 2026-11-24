#include <string>

#include "core/games/launch_options.h"
#include "ef_test.h"

using namespace ef::core::games;

EF_TEST(launch_options_parses_apps_block_values) {
    const std::string vdf = R"("UserLocalConfigStore"
{
    "Software"
    {
        "Valve"
        {
            "Steam"
            {
                "apps"
                {
                    "730"
                    {
                        "LaunchOptions"        "-novid -console"
                    }
                    "1172470"
                    {
                        "LaunchOptions"        ""
                        "LastPlayed"           "1700000000"
                    }
                }
            }
        }
    }
})";
    auto m = parse_launch_options(vdf);
    EF_CHECK(m.size() == 2);
    EF_CHECK(m["730"] == "-novid -console");
    EF_CHECK(m.at("1172470").empty());
}

EF_TEST(launch_options_ignores_nested_blocks_and_other_keys) {
    const std::string vdf = R"("apps" { "730" { "Cloud" { "LaunchOptions" "youtube.com" } } })";
    auto m = parse_launch_options(vdf);
    EF_CHECK(m.empty());  // app bloğunun doğrudan çocuğu olmayan LaunchOptions alınmaz
}

EF_TEST(launch_options_broken_input_returns_empty) {
    EF_CHECK(parse_launch_options("").empty());
    EF_CHECK(parse_launch_options(R"("apps" { "730" { "LaunchOptions" )").empty());
    EF_CHECK(parse_launch_options("not quoted").empty());
}

EF_TEST(launch_options_escape_and_comments) {
    const std::string vdf = "// yorum\n\"apps\" { \"730\" { \"LaunchOptions\" \"-novid \\\"x\\\"\" } }";
    auto m = parse_launch_options(vdf);
    EF_CHECK(m.size() == 1);
    EF_CHECK(m["730"] == "-novid \"x\"");
}
