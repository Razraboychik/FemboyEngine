// Copyright (c) 2026 Razraboychik. All rights reserved.
#include "catch_amalgamated.hpp"
#include "FemMath/Math.hpp"

TEST_CASE("FemMath vector helpers preserve screen angle convention", "[femmath]")
{
    using namespace FemMath;

    const Vec2 right = FromAngle(0.f);
    const Vec2 down = FromAngle(90.f);
    REQUIRE(right.x == Catch::Approx(1.f));
    REQUIRE(right.y == Catch::Approx(0.f));
    REQUIRE(down.x == Catch::Approx(0.f).margin(1e-5f));
    REQUIRE(down.y == Catch::Approx(1.f));
    REQUIRE(AngleOf(down) == Catch::Approx(90.f));

    const Vec2 rotated = Rotated({1.f, 0.f}, 90.f);
    REQUIRE(rotated.x == Catch::Approx(0.f).margin(1e-5f));
    REQUIRE(rotated.y == Catch::Approx(1.f));
    REQUIRE(Length({3.f, 4.f}) == Catch::Approx(5.f));
    REQUIRE(Dot({1.f, 2.f}, {3.f, 4.f}) == Catch::Approx(11.f));
    REQUIRE(Cross({1.f, 0.f}, {0.f, 1.f}) == Catch::Approx(1.f));
}

TEST_CASE("FemMath normalisation, lerp and distance are stable", "[femmath]")
{
    using namespace FemMath;

    const Vec2 normalized = Normalized({0.f, 5.f});
    REQUIRE(normalized.x == Catch::Approx(0.f));
    REQUIRE(normalized.y == Catch::Approx(1.f));
    REQUIRE(Normalized({0.f, 0.f}) == Vec2{0.f, 0.f});

    REQUIRE(Lerp(10.f, 20.f, 0.25f) == Catch::Approx(12.5f));
    REQUIRE(Lerp(10.f, 20.f, 2.f) == Catch::Approx(20.f));

    const Vec2 point = Lerp(Vec2{0.f, 0.f}, Vec2{10.f, 20.f}, 0.5f);
    REQUIRE(point.x == Catch::Approx(5.f));
    REQUIRE(point.y == Catch::Approx(10.f));

    const Color color = Lerp(Color(0, 0, 0, 0), Color(255, 255, 255, 255), 0.5f);
    REQUIRE(color.r == 127);
    REQUIRE(color.g == 127);
    REQUIRE(color.b == 127);
    REQUIRE(color.a == 127);
    REQUIRE(WithAlpha(Color::White, 0.5f).a == 127);

    REQUIRE(Distance({0.f, 0.f}, {3.f, 4.f}) == Catch::Approx(5.f));
    REQUIRE(DistanceToSegment({5.f, 5.f}, {0.f, 0.f}, {10.f, 0.f}) == Catch::Approx(5.f));
    REQUIRE(DistanceToSegment({5.f, 5.f}, {5.f, 5.f}, {5.f, 5.f}) == Catch::Approx(0.f));
}

TEST_CASE("FemMath Rect supports scalar layout and containment", "[femmath]")
{
    using namespace FemMath;

    const Rect rectangle(10.f, 20.f, 100.f, 50.f);
    REQUIRE(rectangle.position.x == Catch::Approx(10.f));
    REQUIRE(rectangle.size.y == Catch::Approx(50.f));
    REQUIRE(RectContains(rectangle, {50.f, 40.f}));
    REQUIRE_FALSE(RectContains(rectangle, {5.f, 40.f}));
    REQUIRE_FALSE(RectContains(rectangle, {50.f, 80.f}));

    const sf::FloatRect converted = rectangle;
    REQUIRE(converted.size.x == Catch::Approx(100.f));
}
