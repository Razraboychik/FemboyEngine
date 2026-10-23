// Copyright (c) 2026 Razraboychik. All rights reserved.
#include "catch_amalgamated.hpp"
#include "FemboyEngine/Console.hpp"

TEST_CASE("Console registers and executes commands", "[console]")
{
    Fem::Console& Console = Fem::Console::Instance();
    Console.Clear();

    bool Called = false;
    Console.RegisterCommand("test.console", "test command",
                            [&Called](const std::vector<std::string>& Args)
                            {
                                Called = true;
                                REQUIRE(Args.size() == 2);
                                REQUIRE(Args[1] == "value");
                            });

    REQUIRE(Console.Execute("test.console value"));
    REQUIRE(Called);
    REQUIRE(Console.Execute("missing.command") == false);

    bool Found = false;
    for (const std::string& Line : Console.Output())
    {
        if (Line.find("unknown command") != std::string::npos)
        {
            Found = true;
        }
    }
    REQUIRE(Found);
    Console.Clear();
    REQUIRE(Console.Output().empty());
}
