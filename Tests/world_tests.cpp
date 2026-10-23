// Copyright (c) 2026 Razraboychik. All rights reserved.
#include "catch_amalgamated.hpp"
#include "FemboyEngine/World.hpp"

TEST_CASE("World owns entity lifecycle", "[world][ecs]")
{
    Fem::Registry registry;
    Fem::World world(registry);

    const Fem::Entity first = world.Spawn();
    REQUIRE(world.Valid(first));
    REQUIRE(world.Count() == 1);

    Fem::TransformComponent& transform = world.Emplace<Fem::TransformComponent>(first);
    transform.x = 12.f;
    REQUIRE(world.Get<Fem::TransformComponent>(first).x == Catch::Approx(12.f));
    REQUIRE(world.Has<Fem::TransformComponent>(first));

    const Fem::Entity second = world.SpawnWith(Fem::TransformComponent{3.f, 4.f});
    REQUIRE(world.Count() == 2);
    REQUIRE(world.Get<Fem::TransformComponent>(second).x == Catch::Approx(3.f));

    std::size_t visited = 0;
    auto view = world.View<Fem::TransformComponent>();
    for (const Fem::Entity entity : view)
    {
        (void)entity;
        ++visited;
    }
    REQUIRE(visited == 2);

    REQUIRE(world.Destroy(first));
    REQUIRE_FALSE(world.Destroy(first));
    REQUIRE(world.Count() == 1);

    world.Clear();
    REQUIRE(world.Count() == 0);
    REQUIRE_FALSE(world.Valid(second));
}
