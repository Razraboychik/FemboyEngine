// Copyright (c) 2026 Razraboychik. All rights reserved.
// Catch2 amalgamated build provides main().
#include "catch_amalgamated.hpp"
#include "FemboyEngine/FastRandom.hpp"
#include "FemboyEngine/FemboyScript.hpp"
#include "FemboyEngine/Memory.hpp"
#include "FemboyEngine/Pathfinding.hpp"
#include "FemboyEngine/StateTree.hpp"
#include "FemboyEngine/Replication.hpp"
#include "FemboyEngine/Shapes.hpp"
#include "FemboyEngine/Physics.hpp"
#include "FemboyEngine/Input.hpp"
#include "FemboyEngine/Skirt.hpp"
#include "FemboyEngine/SoftBody.hpp"
#include "FemboyEngine/Wind.hpp"
#include "FemboyEngine/Heels.hpp"
#include "FemboyEngine/Mirror.hpp"
#include "FemboyEngine/Scene.hpp"
#include "FemboyEngine/Prefab.hpp"
#include "FemboyEngine/Profiler.hpp"
#include "FemboyEngine/ResourcePack.hpp"
#include "FemboyEngine/Hosting.hpp"
#include "FemboyEngine/Vanity.hpp"
#include "FemboyEngine/UI.hpp"
#include "FemboyEngine/Tilemap.hpp"
#include "FemboyEngine/Assets.hpp"
#include "FemboyEngine/AssetPipeline.hpp"
#include "FemboyEngine/HotSwap.hpp"
#include "FemboyEngine/VoiceChat.hpp"
#include "FemboyEngine/Networking.hpp"
#ifdef FEMBOY_HAS_ENET
#include <enet/enet.h>
#endif
#include "FemboyEngine/Jobs.hpp"
#include "FemboyEngine/Simd.hpp"
#include "FemboyEngine/Sparkles.hpp"
#include "FemboyEngine/Tags.hpp"
#include "FemboyEngine/Skills.hpp"
#include "FemboyEngine/VisualScript.hpp"
#include "FemboyEngine/AntiAliasing.hpp"
#include "FemboyEngine/Accessibility.hpp"
#include "FemboyEngine/Display.hpp"
#include "FemboyEngine/Engine.hpp"
#include "FemboyEngine/SettingsRegistry.hpp"
#include "FemboyEngine/AudioOcclusion.hpp"
#include "FemboyEngine/ChunkedTilemap.hpp"
#include "FemboyEngine/ECS.hpp"
#include "FemboyEngine/Lighting.hpp"
#include "FemboyEngine/Macros.hpp"
#include "FemboyEngine/NativeModule.hpp"
#include "FemboyEngine/PhysicsSystem.hpp"
#include "FemboyEngine/RichText.hpp"
#include "FemboyEngine/Rollback.hpp"
#include "FemboyEngine/RotationAtlas.hpp"
#include "FemboyEngine/Sdf.hpp"
#include "FemboyEngine/Sockets.hpp"
#include "FemboyEngine/Visibility.hpp"
#include "FemboyEngine/Wfc.hpp"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

// ENet pulls in the Windows compatibility headers on MSVC, which define these
// as macros. They are ancient calling-convention keywords and collide with
// ordinary identifiers used by the tests (`TagContainer small`, `WindSample far`).
#ifdef small
#undef small
#endif
#ifdef far
#undef far
#endif
#ifdef near
#undef near
#endif
#include <string>
#include <vector>

using namespace Fem;

TEST_CASE("FastRandom produces values in range", "[random]")
{
    FastRandom rng(42);
    for (int i = 0; i < 1000; ++i)
    {
        float f = rng.NextFloat();
        REQUIRE(f >= 0.f);
        REQUIRE(f < 1.f);
    }
}

TEST_CASE("FastRandom nextInt in range", "[random]")
{
    FastRandom rng(42);
    for (int i = 0; i < 1000; ++i)
    {
        int v = rng.NextInt(0, 10);
        REQUIRE(v >= 0);
        REQUIRE(v <= 10);
    }
}

TEST_CASE("FastRandom nextInt handles inverted range", "[random]")
{
    FastRandom rng(7);
    for (int i = 0; i < 100; ++i)
    {
        const int v = rng.NextInt(10, 0);
        REQUIRE(v >= 0);
        REQUIRE(v <= 10);
    }
}

TEST_CASE("FemboyScript basic arithmetic", "[script]")
{
    FemboyScript fs;
    fs.RegisterFunction("print", [](const std::vector<FSValue>&) -> FSValue { return FSValue{}; });
    auto r = fs.Run("int x = 5;\nint y = 3;\nint z = x + y;");
    REQUIRE(r.ok);
}

TEST_CASE("FemboyScript strict typing", "[script]")
{
    FemboyScript fs;
    fs.RegisterFunction("print", [](const std::vector<FSValue>&) -> FSValue { return FSValue{}; });
    auto r = fs.Run("string s = \"hello\";\ns = 42;");
    REQUIRE(!r.ok);
    REQUIRE(r.error.find("type mismatch") != std::string::npos);
}

TEST_CASE("FemboyScript function call", "[script]")
{
    FemboyScript fs;
    fs.RegisterFunction("print", [](const std::vector<FSValue>&) -> FSValue { return FSValue{}; });
    auto r = fs.Run("int x = 0;\nvoid Foo(int a) { x = a; }\nFoo(42);");
    REQUIRE(r.ok);
}

TEST_CASE("FemboyScript for loop", "[script]")
{
    FemboyScript fs;
    fs.RegisterFunction("print", [](const std::vector<FSValue>&) -> FSValue { return FSValue{}; });
    auto r = fs.Run("int sum = 0;\nfor (int i = 0; i < 5; i++) { sum = sum + i; }");
    REQUIRE(r.ok);
}

TEST_CASE("FemboyScript switch", "[script]")
{
    FemboyScript fs;
    fs.RegisterFunction("print", [](const std::vector<FSValue>&) -> FSValue { return FSValue{}; });
    auto r = fs.Run("int result = 0;\nswitch (2) { case 1: result = 1; break; case 2: result = 2; break; default: "
                    "result = 0; break; }");
    REQUIRE(r.ok);
}

TEST_CASE("FemboyScript rejects infinite loops", "[script]")
{
    FemboyScript fs;
    auto r = fs.Run("while (true) { }");
    REQUIRE(!r.ok);
}

TEST_CASE("FemboyScript reports unterminated strings", "[script]")
{
    FemboyScript fs;
    auto r = fs.Run("string s = \"oops;");
    REQUIRE(!r.ok);
    REQUIRE(r.error.find("lex error") != std::string::npos);
}

TEST_CASE("FemboyScript const cannot be reassigned", "[script]")
{
    FemboyScript fs;
    auto r = fs.Run("const int x = 5;\nx = 6;");
    REQUIRE(!r.ok);
}

TEST_CASE("FemboyScript division by zero is an error", "[script]")
{
    FemboyScript fs;
    auto r = fs.Run("int a = 4;\nint b = 0;\nint c = a / b;");
    REQUIRE(!r.ok);
}

TEST_CASE("Shapes overlap uses centers", "[shapes]")
{
    ColliderComponent a;
    a.type = ShapeType::Rectangle;
    a.size = {10.f, 10.f};
    ColliderComponent b = a;

    TransformComponent ta;
    ta.x = 0.f;
    ta.y = 0.f;
    TransformComponent tb;
    tb.x = 5.f;
    tb.y = 0.f;

    // Two 10x10 boxes whose top-left corners are 5px apart overlap by half.
    REQUIRE(ShapeRenderer::TestOverlap(a, ta, b, tb));

    tb.x = 20.f;
    REQUIRE_FALSE(ShapeRenderer::TestOverlap(a, ta, b, tb));
}

TEST_CASE("Pathfinding finds a path around a wall", "[pathfinding]")
{
    Tilemap map;
    map.Resize(8, 8, 16);
    for (int y = 0; y < 7; ++y)
    {
        map.SetTile(4, y, 1, true);
    }

    Pathfinder pf;
    const auto path = pf.FindPath(map, 1, 1, 6, 1);
    REQUIRE_FALSE(path.empty());
    REQUIRE(path.front() == std::make_pair(1, 1));
    REQUIRE(path.back() == std::make_pair(6, 1));
}

TEST_CASE("Replication round-trips a property", "[replication]")
{
    Replication& rep = Replication::Instance();
    rep.Reset();
    const uint32_t id = rep.RegisterProperty("player", "health", 100);
    REQUIRE(id != 0);

    rep.SetProperty(id, 42);
    const auto delta = rep.BuildDelta();
    REQUIRE_FALSE(delta.is_null());

    // A second build with no changes produces no delta.
    REQUIRE(rep.BuildDelta().is_null());

    rep.Reset();
    rep.RegisterProperty("player", "health", 0);
    REQUIRE(rep.ApplyDelta(delta) == 1);
    REQUIRE(rep.GetPropertyOr(id) == 42);

    rep.Reset();
}

TEST_CASE("Replication rejects unknown RPCs", "[replication]")
{
    Replication& rep = Replication::Instance();
    rep.Reset();
    REQUIRE_FALSE(rep.DispatchRpc("missing", 0, nullptr));
    rep.RegisterRpc("ping", [](uint32_t, const nlohmann::json&) {});
    REQUIRE(rep.DispatchRpc("ping", 0, nullptr));
    rep.Reset();
}

// Rendering: layered draw list

TEST_CASE("Skirt orders commands by layer then z", "[FrameDrawList]")
{
    Skirt FrameDrawList;
    FrameDrawList.Rect(sf::FloatRect({0.f, 0.f}, {1.f, 1.f}), sf::Color::Red, sf::Color::Transparent, DrawCommand::kNoThickness,
               5, DrawLayer::UI);
    FrameDrawList.Rect(sf::FloatRect({0.f, 0.f}, {1.f, 1.f}), sf::Color::Green, sf::Color::Transparent,
               DrawCommand::kNoThickness, 0, DrawLayer::World);
    FrameDrawList.Rect(sf::FloatRect({0.f, 0.f}, {1.f, 1.f}), sf::Color::Blue, sf::Color::Transparent,
               DrawCommand::kNoThickness, 1, DrawLayer::World);
    FrameDrawList.Text("hi", "font", {0, 0}, 16, sf::Color::White, 3, DrawLayer::UI);

    FrameDrawList.Sort();
    const auto& c = FrameDrawList.Commands();
    REQUIRE(c.size() == 4);
    REQUIRE(c[0].layer == DrawLayer::World);
    REQUIRE(c[0].z == 0);
    REQUIRE(c[1].z == 1);
    REQUIRE(c[2].layer == DrawLayer::UI);
    REQUIRE(c[2].z == 3);
    REQUIRE(c[3].z == 5);
}

TEST_CASE("Skirt keeps submission order for equal depth", "[FrameDrawList]")
{
    Skirt FrameDrawList;
    FrameDrawList.Rect(sf::FloatRect({0.f, 0.f}, {1.f, 1.f}), sf::Color::Red, sf::Color::Transparent, DrawCommand::kNoThickness,
               0, DrawLayer::World);
    FrameDrawList.Rect(sf::FloatRect({0.f, 0.f}, {1.f, 1.f}), sf::Color::Green, sf::Color::Transparent,
               DrawCommand::kNoThickness, 0, DrawLayer::World);
    FrameDrawList.Sort();
    REQUIRE(FrameDrawList.Commands()[0].color == sf::Color::Red);
    REQUIRE(FrameDrawList.Commands()[1].color == sf::Color::Green);

    FrameDrawList.Clear();
    REQUIRE(FrameDrawList.Empty());
}

// Scheduler

TEST_CASE("Heels runs phases in order and shuts down in reverse", "[heels]")
{
    Heels heels;
    std::vector<std::string> log;
    bool initialized = false;

    heels.Add({"preB", Heels::Phase::PreUpdate, 20,
               [&]()
               {
                   initialized = true;
                   return true;
               },
               [&](float) { log.push_back("preB"); }, [&]() { log.push_back("downB"); }});
    heels.Add({"fixed", Heels::Phase::FixedUpdate, 10, nullptr, [&](float) { log.push_back("fixed"); }, nullptr});
    heels.Add({"preA", Heels::Phase::PreUpdate, 10, nullptr, [&](float) { log.push_back("preA"); },
               [&]() { log.push_back("downA"); }});
    heels.Add({"post", Heels::Phase::PostUpdate, 10, nullptr, [&](float) { log.push_back("post"); }, nullptr});

    REQUIRE(heels.InitializeAll());
    REQUIRE(initialized);

    heels.Tick(0.016f);
    heels.FixedTick(0.02f);
    heels.FixedTick(0.02f);
    heels.PostTick(0.016f);
    REQUIRE(log == std::vector<std::string>{"preA", "preB", "fixed", "fixed", "post"});

    log.clear();
    heels.ShutdownAll();
    REQUIRE(log == std::vector<std::string>{"downB", "downA"});
}

TEST_CASE("Heels rolls back systems when initialization fails", "[heels]")
{
    Heels heels;
    bool startedShutDown = false;
    heels.Add({"ok", Heels::Phase::PreUpdate, 0, []() { return true; }, nullptr, [&]() { startedShutDown = true; }});
    heels.Add({"bad", Heels::Phase::PreUpdate, 1, []() { return false; }, nullptr, nullptr});

    REQUIRE_FALSE(heels.InitializeAll());
    REQUIRE(startedShutDown);
}

// File layer

TEST_CASE("Vanity writes and reads JSON atomically", "[vanity]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_vanity_test";
    fs::create_directories(dir);
    const std::string path = (dir / "data.json").string();

    const nlohmann::json data = {{"name", "scene"}, {"value", 42}};
    REQUIRE(Vanity::WriteJson(path, data));

    auto read = Vanity::ReadJson(path);
    REQUIRE(read);
    REQUIRE(read.Value()["value"] == 42);
    REQUIRE(read.Value()["name"] == "scene");

    // No temp file is left behind after a successful write.
    REQUIRE_FALSE(Vanity::Exists(path + ".tmp"));

    // A malformed file is reported, not thrown.
    REQUIRE(Vanity::WriteText(path, "{not json"));
    auto broken = Vanity::ReadJson(path);
    REQUIRE_FALSE(broken);
    REQUIRE_FALSE(broken.Error().empty());

    REQUIRE_FALSE(Vanity::ReadJson((dir / "missing.json").string()));

    fs::remove_all(dir);
}

TEST_CASE("Vanity sanitizes file names from user data", "[vanity]")
{
    const std::string traversal = Vanity::SanitizeFileName("../escape", "slot");
    REQUIRE(traversal.find('/') == std::string::npos);
    REQUIRE(traversal.find('\\') == std::string::npos);
    REQUIRE(traversal.front() != '.');

    REQUIRE(Vanity::SanitizeFileName("..", "slot") == "slot");
    REQUIRE(Vanity::SanitizeFileName("", "slot") == "slot");
    REQUIRE(Vanity::SanitizeFileName("save-01_ok", "slot") == "save-01_ok");
    REQUIRE(Vanity::SanitizeFileName("C:\\Windows\\system32", "slot").find(':') == std::string::npos);
}

// Reflection

namespace
{
struct TestHealthComponent
{
    int hp = 100;
    std::string ownerName;
    bool alive = true;
    float regen = 0.f;
};

struct TestTaggedComponent
{
    GameplayTags tags;
};
} // namespace

TEST_CASE("Mirror drives Scene serialization generically", "[mirror]")
{
    // A component the engine does not know about is registered once and then
    // round-trips through Scene with no serialization code of its own.
    Mirror::Instance()
        .Component<TestHealthComponent>("TestHealthComponent", "testHealth")
        .AddField("hp", &TestHealthComponent::hp)
        .AddField("ownerName", &TestHealthComponent::ownerName)
        .AddField("alive", &TestHealthComponent::alive)
        .AddField("regen", &TestHealthComponent::regen)
        .Done();

    Registry reg;
    const Entity e = reg.create();
    reg.emplace<TransformComponent>(e, TransformComponent{1.f, 2.f, 3.f, 4.f, 45.f});
    SpriteComponent sprite;
    sprite.textureId = "hero";
    sprite.z = 7;
    sprite.tint = sf::Color(10, 20, 30, 40);
    sprite.srcX = 16;
    sprite.srcY = 32;
    sprite.srcW = 48;
    sprite.srcH = 64;
    sprite.originX = 0.f;
    sprite.originY = 1.f;
    sprite.flipX = true;
    sprite.flipY = false;
    reg.emplace<SpriteComponent>(e, sprite);
    TagComponent tag;
    tag.tag = "player";
    reg.emplace<TagComponent>(e, tag);
    TestHealthComponent health;
    health.hp = 3;
    health.ownerName = "razraboychik";
    health.alive = false;
    health.regen = 1.5f;
    reg.emplace<TestHealthComponent>(e, health);

    Scene scene;
    scene.CaptureFrom(reg);
    const auto& entities = scene.Data()["entities"];
    REQUIRE(entities.size() == 1);
    const auto& je = entities[0];

    REQUIRE(je["transform"]["x"].get<float>() == Catch::Approx(1.f));
    REQUIRE(je["transform"]["rot"].get<float>() == Catch::Approx(45.f));
    // Colour keeps the historic flat r/g/b/a layout.
    REQUIRE(je["sprite"]["r"].get<int>() == 10);
    REQUIRE(je["sprite"]["a"].get<int>() == 40);
    REQUIRE_FALSE(je["sprite"].contains("tint"));
    REQUIRE(je["sprite"]["srcX"].get<int>() == 16);
    REQUIRE(je["sprite"]["srcW"].get<int>() == 48);
    REQUIRE(je["sprite"]["originX"].get<float>() == Catch::Approx(0.f));
    REQUIRE(je["sprite"]["flipX"].get<bool>() == true);
    REQUIRE(je["tag"].get<std::string>() == "player");
    REQUIRE(je["testHealth"]["hp"].get<int>() == 3);
    REQUIRE(je["testHealth"]["alive"].get<bool>() == false);
    REQUIRE(je["testHealth"]["regen"].get<float>() == Catch::Approx(1.5f));

    Registry loaded;
    scene.ApplyTo(loaded);
    auto view = loaded.view<TransformComponent, SpriteComponent, TagComponent, TestHealthComponent>();
    REQUIRE(static_cast<size_t>(std::distance(view.begin(), view.end())) == 1);
    const Entity restored = *view.begin();
    REQUIRE(view.get<TransformComponent>(restored).rotation == Catch::Approx(45.f));
    REQUIRE(view.get<SpriteComponent>(restored).textureId == "hero");
    REQUIRE(view.get<SpriteComponent>(restored).tint == sf::Color(10, 20, 30, 40));
    REQUIRE(view.get<SpriteComponent>(restored).srcX == 16);
    REQUIRE(view.get<SpriteComponent>(restored).srcH == 64);
    REQUIRE(view.get<SpriteComponent>(restored).originY == Catch::Approx(1.f));
    REQUIRE(view.get<SpriteComponent>(restored).flipX == true);
    REQUIRE(view.get<TagComponent>(restored).tag == "player");
    REQUIRE(view.get<TestHealthComponent>(restored).hp == 3);
    REQUIRE(view.get<TestHealthComponent>(restored).ownerName == "razraboychik");
    REQUIRE(view.get<TestHealthComponent>(restored).alive == false);
}

TEST_CASE("Mirror reflects GameplayTags fields", "[mirror][tags]")
{
    Mirror::Instance()
        .Component<TestTaggedComponent>("TestTaggedComponent", "testTagged")
        .AddField("tags", &TestTaggedComponent::tags)
        .Done();

    const TagId tag = TagRegistry::Instance().RegisterTag("UnitTest.Mirror.Tag");
    REQUIRE(TagRegistry::Instance().IsValid(tag));

    Registry reg;
    const Entity e = reg.create();
    TestTaggedComponent authored;
    authored.tags.tags.Add(tag);
    reg.emplace<TestTaggedComponent>(e, authored);

    const ComponentReflection* reflection = Mirror::Instance().Find("TestTaggedComponent");
    REQUIRE(reflection != nullptr);
    nlohmann::json document = nlohmann::json::object();
    reflection->capture(reg, e, document);
    REQUIRE(document["testTagged"]["tags"].is_array());
    REQUIRE(document["testTagged"]["tags"].size() == 1);

    Registry loaded;
    const Entity restored = loaded.create();
    loaded.emplace<TestTaggedComponent>(restored);
    reflection->apply(loaded, restored, document["testTagged"]);
    CHECK(loaded.get<TestTaggedComponent>(restored).tags.Has(tag));
}

TEST_CASE("Scene ignores malformed component values", "[mirror]")
{
    Registry reg;
    const Entity e = reg.create();
    reg.emplace<TransformComponent>(e);
    Scene scene;
    scene.CaptureFrom(reg);
    auto& entities = scene.Data()["entities"];
    entities[0]["transform"]["x"] = "not a number";
    entities[0]["transform"]["y"] = nullptr;
    entities[0]["sprite"] = 5; // wrong type entirely

    Registry loaded;
    scene.ApplyTo(loaded);
    auto view = loaded.view<TransformComponent>();
    REQUIRE(static_cast<size_t>(std::distance(view.begin(), view.end())) == 1);
    REQUIRE(view.get<TransformComponent>(*view.begin()).x == 0.f);
}

TEST_CASE("Scene migrates legacy files and loads additively", "[scene][prefab]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_scene_additive";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "legacy.scene";
    nlohmann::json legacy = nlohmann::json::object();
    legacy["name"] = "legacy";
    legacy["objects"] = nlohmann::json::array();
    legacy["objects"].push_back({{"tag", "loaded"}});
    legacy["objects"].push_back({{"transform", {{"x", 4.f}, {"y", 5.f}}}});
    REQUIRE(Vanity::WriteJson(path.string(), legacy));

    Registry reg;
    const Entity existing = reg.create();
    reg.emplace<TagComponent>(existing).tag = "existing";

    Scene scene;
    REQUIRE(scene.Load(path.string()));
    REQUIRE(scene.Data()["version"].get<int>() == 1);
    REQUIRE(scene.Data().contains("entities"));
    REQUIRE_FALSE(scene.Data().contains("objects"));

    REQUIRE(scene.LoadAdditive(path.string(), reg));
    REQUIRE(reg.valid(existing));
    REQUIRE(reg.get<TagComponent>(existing).tag == "existing");

    std::size_t loaded = 0;
    auto tags = reg.view<TagComponent>();
    for (const Entity e : tags)
    {
        loaded += reg.get<TagComponent>(e).tag == "loaded" ? 1u : 0u;
    }
    REQUIRE(loaded == 1);

    // Saving uses the current schema and preserves the same entity count.
    scene.CaptureFrom(reg);
    REQUIRE(scene.Data()["version"].get<int>() == Scene::CurrentVersion());
    const fs::path out = dir / "saved.scene";
    REQUIRE(scene.Save(out.string()));
    Scene reread;
    REQUIRE(reread.Load(out.string()));
    REQUIRE(reread.Data()["entities"].size() == 3);

    fs::remove_all(dir);
}

TEST_CASE("Prefab inherits parents, applies overrides and spawns children", "[prefab][scene]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_prefab_nested";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto write = [&dir](const char* name, const nlohmann::json& data)
    { REQUIRE(Vanity::WriteJson((dir / (std::string(name) + ".prefab")).string(), data)); };

    write("base", {{"name", "base"}, {"tag", "base"}, {"transform", {{"x", 10.f}, {"y", 20.f}}}});
    write("bullet", {{"name", "bullet"}, {"tag", "bullet"}, {"transform", {{"x", 0.f}, {"y", 0.f}}}});
    write("derived", {{"name", "derived"},
                      {"parent", "base"},
                      {"overrides", {{"transform", {{"y", 40.f}}}}},
                      {"children", nlohmann::json::array(
                                       {{{"prefab", "bullet"}, {"tag", "child"}, {"offset", {{"x", 5.f}, {"y", 5.f}}}},
                                        {{"prefab", "bullet"}, {"tag", "childzero"}}})}});

    Prefab base;
    Prefab bullet;
    Prefab derived;
    REQUIRE(base.Load((dir / "base.prefab").string()));
    REQUIRE(bullet.Load((dir / "bullet.prefab").string()));
    REQUIRE(derived.Load((dir / "derived.prefab").string()));
    REQUIRE(derived.HasParent());
    REQUIRE(derived.ParentName() == "base");

    PrefabLibrary& library = PrefabLibrary::Instance();
    library.Clear();
    library.Add("base", base);
    library.Add("bullet", bullet);
    library.Add("derived", derived);

    std::vector<std::string> errors;
    const nlohmann::json resolved = derived.ResolvedData(&errors);
    CHECK(errors.empty());
    CHECK(resolved["tag"].get<std::string>() == "base");
    CHECK(resolved["transform"]["x"].get<float>() == Catch::Approx(10.f));
    CHECK(resolved["transform"]["y"].get<float>() == Catch::Approx(40.f));

    Registry reg;
    const Entity root = library.Instantiate("derived", reg);
    REQUIRE(root != entt::null);
    REQUIRE(reg.get<TagComponent>(root).tag == "base");
    REQUIRE(reg.get<TransformComponent>(root).x == Catch::Approx(10.f));
    REQUIRE(reg.get<TransformComponent>(root).y == Catch::Approx(40.f));
    REQUIRE(reg.storage<entt::entity>().size() == 3);

    bool foundChild = false;
    bool foundZeroOffsetChild = false;
    auto tags = reg.view<TagComponent>();
    for (const Entity e : tags)
    {
        const std::string& tag = reg.get<TagComponent>(e).tag;
        if (tag == "child")
        {
            foundChild = true;
            const TransformComponent& transform = reg.get<TransformComponent>(e);
            CHECK(transform.x == Catch::Approx(15.f));
            CHECK(transform.y == Catch::Approx(45.f));
        }
        else if (tag == "childzero")
        {
            foundZeroOffsetChild = true;
            const TransformComponent& transform = reg.get<TransformComponent>(e);
            CHECK(transform.x == Catch::Approx(10.f));
            CHECK(transform.y == Catch::Approx(40.f));
        }
    }
    CHECK(foundChild);
    CHECK(foundZeroOffsetChild);

    // A parent cycle is reported and does not recurse forever.
    write("cycle_a", {{"name", "cycle_a"}, {"parent", "cycle_b"}});
    write("cycle_b", {{"name", "cycle_b"}, {"parent", "cycle_a"}});
    Prefab cycleA;
    Prefab cycleB;
    REQUIRE(cycleA.Load((dir / "cycle_a.prefab").string()));
    REQUIRE(cycleB.Load((dir / "cycle_b.prefab").string()));
    library.Add("cycle_a", cycleA);
    library.Add("cycle_b", cycleB);
    std::vector<std::string> cycleErrors;
    const nlohmann::json cycle = cycleA.ResolvedData(&cycleErrors);
    CHECK(cycle.is_object());
    REQUIRE_FALSE(cycleErrors.empty());
    const bool mentionsCycle = std::any_of(cycleErrors.begin(), cycleErrors.end(), [](const std::string& error)
                                           { return error.find("cycle") != std::string::npos; });
    CHECK(mentionsCycle);

    library.Clear();
    fs::remove_all(dir);
}

TEST_CASE("Profiler keeps a bounded frame history and aggregates scopes", "[profiler]")
{
    Profiler& profiler = Profiler::Instance();
    const bool wasEnabled = profiler.Enabled();
    profiler.SetEnabled(true);
    profiler.Reset();
    profiler.SetHistorySize(3);

    profiler.BeginFrame();
    {
        ProfileScope scope("Gameplay");
        CHECK(profiler.FrameOpen());
    }
    profiler.Record("PreUpdate", 1.5);
    profiler.Record("FixedUpdate", 2.0);
    profiler.Record("Render", 3.0);
    profiler.SetRenderStats(12, 7, 4, 900);
    profiler.SetRegistryStats(42, 9);
    profiler.EndFrame();

    REQUIRE(profiler.HistorySize() == 1);
    const FrameProfile first = profiler.LastFrame();
    CHECK(first.frame >= 1);
    CHECK(first.preUpdateMs == Catch::Approx(1.5));
    CHECK(first.fixedUpdateMs == Catch::Approx(2.0));
    CHECK(first.renderMs == Catch::Approx(3.0));
    CHECK(first.commands == 12);
    CHECK(first.drawCalls == 7);
    CHECK(first.textureBinds == 4);
    CHECK(first.vertices == 900);
    CHECK(first.entities == 42);
    CHECK(first.systems == 9);
    REQUIRE_FALSE(first.scopes.empty());

    profiler.BeginFrame();
    profiler.EndFrame();
    profiler.BeginFrame();
    profiler.EndFrame();
    CHECK(profiler.HistorySize() == 3);

    const std::vector<ProfileSample> average = profiler.AverageScopes(3);
    const auto gameplay = std::find_if(average.begin(), average.end(),
                                       [](const ProfileSample& sample) { return sample.name == "Gameplay"; });
    REQUIRE(gameplay != average.end());
    CHECK(gameplay->milliseconds >= 0.0);
    CHECK(profiler.AverageFrameMs(3) > 0.0);
    CHECK(profiler.AverageFps(3) > 0.0);

    const nlohmann::json report = profiler.ToJson(2);
    CHECK(report["frames"].size() == 2);
    CHECK(report["frames"][0].contains("scopes"));

    // A disabled profiler keeps its history and records nothing new.
    profiler.SetEnabled(false);
    profiler.BeginFrame();
    profiler.Record("Ignored", 1.0);
    profiler.EndFrame();
    CHECK(profiler.HistorySize() == 3);
    profiler.Reset();
    profiler.SetHistorySize(300);
    profiler.SetEnabled(wasEnabled);
}

// Networking: stable property keys

TEST_CASE("Replication property ids are stable and order independent", "[replication]")
{
    Replication& rep = Replication::Instance();
    rep.Reset();

    const uint32_t health = rep.RegisterProperty("player", "health", 100);
    const uint32_t name = rep.RegisterProperty("player", "name", "hero");
    const uint32_t score = rep.RegisterProperty("world", "score", 0);
    REQUIRE(health != 0);
    REQUIRE(name != 0);
    REQUIRE(health != name);
    REQUIRE(score != health);

    // Same names on a fresh session, registered in the opposite order: a peer
    // that loaded differently must still address the same ids.
    rep.Reset();
    const uint32_t score2 = rep.RegisterProperty("world", "score", 0);
    const uint32_t name2 = rep.RegisterProperty("player", "name", "hero");
    const uint32_t health2 = rep.RegisterProperty("player", "health", 100);
    REQUIRE(score2 == score);
    REQUIRE(name2 == name);
    REQUIRE(health2 == health);

    rep.Reset();
}

// Input

namespace
{
sf::Event KeyPressed(sf::Keyboard::Key key)
{
    return sf::Event(sf::Event::KeyPressed{key, sf::Keyboard::Scancode::Unknown, false, false, false, false});
}

sf::Event KeyReleased(sf::Keyboard::Key key)
{
    return sf::Event(sf::Event::KeyReleased{key, sf::Keyboard::Scancode::Unknown, false, false, false, false});
}
} // namespace

TEST_CASE("Input reports press and release edges", "[input]")
{
    Input& in = Input::Instance();
    in.Initialize();
    in.CreateAction("TestJump", InputValueType::Bool);

    InputMappingContext ctx;
    ctx.name = "test";
    InputMapping mapping;
    mapping.actionName = "TestJump";
    mapping.keyCode = static_cast<int>(sf::Keyboard::Key::Space);
    ctx.mappings.push_back(mapping);
    in.AddContext(ctx);

    in.Update(KeyPressed(sf::Keyboard::Key::Space));
    in.Tick(0.016f);
    REQUIRE(in.GetBool("TestJump"));
    REQUIRE(in.WasPressed("TestJump"));
    REQUIRE(in.IsHeld("TestJump"));

    in.Update(KeyReleased(sf::Keyboard::Key::Space));
    in.Tick(0.016f);
    REQUIRE_FALSE(in.GetBool("TestJump"));
    REQUIRE(in.WasReleased("TestJump"));
    REQUIRE_FALSE(in.IsHeld("TestJump"));

    in.Initialize();
}

TEST_CASE("Input buffers a press once per action, not once per binding", "[input]")
{
    Input& in = Input::Instance();
    in.Initialize();
    in.CreateAction("TestFire", InputValueType::Bool);
    in.SetBufferSize(4);

    InputTrigger trigger;
    trigger.type = InputTriggerType::Pressed;

    InputMappingContext ctx;
    ctx.name = "test";
    for (int i = 0; i < 3; ++i)
    {
        InputMapping mapping;
        mapping.actionName = "TestFire";
        mapping.keyCode = static_cast<int>(sf::Keyboard::Key::Space);
        mapping.triggers.push_back(trigger);
        ctx.mappings.push_back(mapping);
    }
    in.AddContext(ctx);

    in.Update(KeyPressed(sf::Keyboard::Key::Space));
    in.Tick(0.016f);

    REQUIRE(in.WasTriggered("TestFire", InputTriggerType::Pressed));
    REQUIRE(in.ConsumeBufferedInput("TestFire"));
    REQUIRE_FALSE(in.ConsumeBufferedInput("TestFire"));

    in.Initialize();
}

TEST_CASE("Input accumulates opposing float bindings to zero", "[input]")
{
    Input& in = Input::Instance();
    in.Initialize();
    in.CreateAction("TestAxis", InputValueType::Float);

    InputMappingContext ctx;
    ctx.name = "test";
    InputMapping positive;
    positive.actionName = "TestAxis";
    positive.keyCode = static_cast<int>(sf::Keyboard::Key::D);
    InputMapping negative;
    negative.actionName = "TestAxis";
    negative.keyCode = static_cast<int>(sf::Keyboard::Key::A);
    InputModifier negate;
    negate.type = InputModifierType::Negate;
    negate.negateX = true;
    negative.modifiers.push_back(negate);
    ctx.mappings.push_back(positive);
    ctx.mappings.push_back(negative);
    in.AddContext(ctx);

    in.Update(KeyPressed(sf::Keyboard::Key::D));
    in.Update(KeyPressed(sf::Keyboard::Key::A));
    in.Tick(0.016f);
    REQUIRE(in.GetFloat("TestAxis") == Catch::Approx(0.f).margin(0.0001f));

    in.Update(KeyReleased(sf::Keyboard::Key::A));
    in.Tick(0.016f);
    REQUIRE(in.GetFloat("TestAxis") == Catch::Approx(1.f));

    in.Initialize();
}

// Engine improvements: draw list, scheduler time, UI, jobs

TEST_CASE("Skirt carries sprite-sheet metadata", "[FrameDrawList]")
{
    Skirt FrameDrawList;
    FrameDrawList.Sprite("atlas", {10.f, 20.f}, {2.f, 2.f}, 0.f, sf::Color::White, 3, DrawLayer::Entities,
                 sf::FloatRect({16.f, 32.f}, {48.f, 64.f}), sf::Vector2f(0.f, 1.f), true, false);
    REQUIRE(FrameDrawList.Size() == 1);
    const DrawCommand& c = FrameDrawList.Commands().front();
    REQUIRE(c.source.position.x == Catch::Approx(16.f));
    REQUIRE(c.source.size.y == Catch::Approx(64.f));
    REQUIRE(c.origin.y == Catch::Approx(1.f));
    REQUIRE(c.flipX);
    REQUIRE_FALSE(c.flipY);
}

TEST_CASE("Heels keeps unscaled systems on real time", "[heels]")
{
    Heels heels;
    float gameDt = 0.f;
    float netDt = 0.f;
    heels.Add({"game", Heels::Phase::PreUpdate, 0, nullptr, [&](float dt) { gameDt = dt; }, nullptr});
    heels.Add({"net", Heels::Phase::PreUpdate, 1, nullptr, [&](float dt) { netDt = dt; }, nullptr, true});
    REQUIRE(heels.InitializeAll());
    heels.Tick(0.016f, 0.25f);
    REQUIRE(gameDt == Catch::Approx(0.016f));
    REQUIRE(netDt == Catch::Approx(0.25f));
    heels.ShutdownAll();
}

TEST_CASE("Replication keeps dirty state until a successful commit", "[replication]")
{
    Replication& rep = Replication::Instance();
    rep.Reset();
    const uint32_t id = rep.RegisterProperty("net", "position", 1);
    REQUIRE(id != 0);

    rep.SetProperty(id, 2);
    const auto first = rep.PeekDelta();
    REQUIRE_FALSE(first.is_null());
    // Peeking is non-destructive: a failed send can simply try again.
    REQUIRE_FALSE(rep.PeekDelta().is_null());
    REQUIRE(rep.DirtyCount() == 1);

    rep.CommitDelta(first);
    REQUIRE(rep.PeekDelta().is_null());
    REQUIRE(rep.DirtyCount() == 0);

    // A value that changed after the peek must survive the commit.
    rep.SetProperty(id, 3);
    const auto stale = rep.PeekDelta();
    rep.SetProperty(id, 4);
    rep.CommitDelta(stale);
    REQUIRE(rep.DirtyCount() == 1);
    const auto latest = rep.PeekDelta();
    REQUIRE_FALSE(latest.is_null());
    REQUIRE(latest["props"][0]["v"].get<int>() == 4);

    // A full snapshot includes clean properties and leaves their flags alone.
    rep.CommitDelta(latest);
    const auto snapshot = rep.SnapshotAll();
    REQUIRE(snapshot["props"].size() == 1);
    REQUIRE(snapshot["props"][0]["v"].get<int>() == 4);
    REQUIRE(rep.DirtyCount() == 0);

    rep.Reset();
}

TEST_CASE("Mirror can add and remove components generically", "[mirror]")
{
    const ComponentReflection* sprite = Mirror::Instance().Find("SpriteComponent");
    REQUIRE(sprite != nullptr);

    Registry reg;
    const Entity e = reg.create();
    REQUIRE_FALSE(sprite->has(reg, e));
    sprite->apply(reg, e, nlohmann::json::object());
    REQUIRE(sprite->has(reg, e));
    REQUIRE(sprite->getConst(reg, e) != nullptr);
    REQUIRE(sprite->remove);
    sprite->remove(reg, e);
    REQUIRE_FALSE(sprite->has(reg, e));
}

TEST_CASE("PinkUI scales layouts once and reflows on resize", "[ui]")
{
    auto root = std::make_shared<UIWidget>();
    root->type = "Panel";
    auto child = std::make_shared<UIWidget>();
    child->type = "Text";
    child->rect = sf::FloatRect({10.f, 20.f}, {100.f, 30.f});
    root->children.push_back(child);

    PinkUI& ui = PinkUI::Instance();
    ui.SetScale(2.f);
    ui.UpdateLayout(root, sf::FloatRect({0.f, 0.f}, {800.f, 600.f}));

    REQUIRE(root->screenRect.size.x == Catch::Approx(800.f));
    // Offsets and sizes use the same scale, so nothing drifts at scale != 1.
    REQUIRE(child->screenRect.position.x == Catch::Approx(20.f));
    REQUIRE(child->screenRect.position.y == Catch::Approx(40.f));
    REQUIRE(child->screenRect.size.x == Catch::Approx(200.f));
    REQUIRE(child->screenRect.size.y == Catch::Approx(60.f));

    ui.SetViewport(sf::FloatRect({0.f, 0.f}, {1600.f, 1200.f}));
    REQUIRE(root->screenRect.size.x == Catch::Approx(1600.f));
    REQUIRE(child->screenRect.position.x == Catch::Approx(20.f));

    Skirt FrameDrawList;
    ui.Render(root, FrameDrawList);
    REQUIRE_FALSE(FrameDrawList.Empty());
    FrameDrawList.Sort();
    for (const auto& c : FrameDrawList.Commands())
    {
        REQUIRE(c.layer == DrawLayer::UI);
    }

    ui.SetScale(1.f);
    ui.SetViewport(sf::FloatRect({0.f, 0.f}, {1280.f, 720.f}));
}

TEST_CASE("Layer render hook receives the shared draw list", "[core]")
{
    struct RenderLayer : Layer
    {
        int rendered = 0;
        int detached = 0;
        void OnRender(Skirt& FrameDrawList) override
        {
            ++rendered;
            FrameDrawList.Rect(sf::FloatRect({0.f, 0.f}, {4.f, 4.f}), sf::Color::White);
        }
        void OnDetach() override
        {
            ++detached;
        }
        const char* Name() const override
        {
            return "RenderLayer";
        }
    };

    auto layer = std::make_shared<RenderLayer>();
    Application::Instance().PushLayer(layer);
    Skirt FrameDrawList;
    Application::Instance().Render(FrameDrawList);
    REQUIRE(layer->rendered == 1);
    REQUIRE(FrameDrawList.Size() == 1);

    Application::Instance().Shutdown();
    REQUIRE(layer->detached == 1);
    REQUIRE(Application::Instance().Layers().Layers().empty());
}

TEST_CASE("Pathfinding uses octile distance and smooths diagonals", "[pathfinding]")
{
    Tilemap open;
    open.Resize(8, 8, 16);
    Pathfinder pf;

    const auto straight = pf.FindPath(open, 0, 0, 4, 4);
    REQUIRE(straight.size() == 9); // 4 + 4 steps plus the start cell

    const auto diagonal = pf.FindPathDiagonal(open, 0, 0, 4, 4);
    REQUIRE(diagonal.size() == 2); // start and goal: the line is clear
    REQUIRE(diagonal.front() == std::make_pair(0, 0));
    REQUIRE(diagonal.back() == std::make_pair(4, 4));

    // A wall with a gap: the diagonal path must still be walkable.
    Tilemap walled;
    walled.Resize(8, 8, 16);
    for (int y = 0; y < 5; ++y)
    {
        walled.SetTile(3, y, 1, true);
    }
    const auto around = pf.FindPathDiagonal(walled, 1, 1, 6, 6);
    REQUIRE_FALSE(around.empty());
    REQUIRE(around.front() == std::make_pair(1, 1));
    REQUIRE(around.back() == std::make_pair(6, 6));
    for (const auto& [x, y] : around)
    {
        REQUIRE_FALSE(walled.IsSolid(x, y));
    }
}

TEST_CASE("Vanity creates parents and rejects reserved names", "[vanity]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_vanity_durable";
    fs::remove_all(dir);
    const fs::path nested = dir / "saves" / "slot1" / "data.json";

    REQUIRE(Vanity::WriteJson(nested.string(), {{"ok", true}}));
    REQUIRE(Vanity::Exists(nested.string()));
    auto read = Vanity::ReadJson(nested.string());
    REQUIRE(read);
    REQUIRE(read.Value()["ok"].get<bool>());

    // No temporary siblings survive a successful write.
    for (const auto& entry : fs::directory_iterator(nested.parent_path()))
    {
        REQUIRE(entry.path().filename().string().find("data.json.tmp.") == std::string::npos);
    }

    REQUIRE(Vanity::SanitizeFileName("CON", "slot") == "_CON");
    REQUIRE(Vanity::SanitizeFileName("nul.json", "slot") == "_nul.json");
    REQUIRE(Vanity::SanitizeFileName("save.", "slot") == "save");
    REQUIRE(Vanity::SanitizeFileName(std::string(300, 'a'), "slot").size() <= 180);

    fs::remove_all(dir);
}

TEST_CASE("Vanity finds a writable data directory", "[vanity]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_vanity_writable";
    fs::remove_all(dir);

    // A directory that does not exist yet cannot receive a save.
    REQUIRE_FALSE(Vanity::IsDirectoryWritable(dir.string()));

    REQUIRE(Vanity::EnsureDirectory(dir.string()));
    REQUIRE(Vanity::IsDirectoryWritable(dir.string()));

    // The probe file must not survive the check.
    size_t entries = 0;
    for (const auto& entry : fs::directory_iterator(dir))
    {
        ++entries;
        REQUIRE(entry.path().filename().string().find(".write_probe.") == std::string::npos);
    }
    REQUIRE(entries == 0);

    // The per-user directory is either unavailable or an absolute path; an
    // empty string is a legal answer on a stripped-down environment.
    const std::string user = Vanity::UserDataDirectory("FemboyEngineTest");
    REQUIRE((user.empty() || fs::path(user).is_absolute()));

    fs::remove_all(dir);
}

TEST_CASE("JobSystem ParallelFor covers the range", "[jobs]")
{
    JobSystem& jobs = JobSystem::Instance();
    jobs.Initialize(4);

    constexpr size_t kCount = 2000;
    std::atomic<size_t> visited{0};
    std::atomic<long long> sum{0};
    jobs.ParallelFor(0, kCount,
                     [&](size_t i)
                     {
                         visited.fetch_add(1, std::memory_order_relaxed);
                         sum.fetch_add(static_cast<long long>(i), std::memory_order_relaxed);
                     });
    REQUIRE(visited.load() == kCount);
    REQUIRE(sum.load() == static_cast<long long>(kCount * (kCount - 1) / 2));

    // Calling ParallelFor from a worker must not deadlock.
    auto nested = jobs.Submit(
        [&]()
        {
            jobs.ParallelFor(0, 100,
                             [&](size_t i) { sum.fetch_add(static_cast<long long>(i), std::memory_order_relaxed); });
        });
    nested.get();
    REQUIRE(sum.load() == static_cast<long long>(kCount * (kCount - 1) / 2) + 4950);

    jobs.Shutdown();
}

TEST_CASE("Tilemap submits batched geometry to the shared draw list", "[tilemap]")
{
    Tilemap map;
    map.Resize(1, 1, 16);
    map.SetTile(0, 0, 0, false);

    Skirt FrameDrawList;
    // A missing id resolves to the 16x16 placeholder, which is enough to prove
    // the path: one command, tile layer, six vertices.
    map.Render(FrameDrawList, "definitely_missing_texture", 1);
    REQUIRE(FrameDrawList.Size() == 1);

    const DrawCommand& c = FrameDrawList.Commands().front();
    REQUIRE(c.kind == DrawKind::Vertices);
    REQUIRE(c.layer == DrawLayer::Tiles);
    REQUIRE(c.vertices.getVertexCount() == 6);
}

TEST_CASE("Asset manifest reload drops removed entries", "[assets]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_asset_reload";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const fs::path texA = dir / "a.png";
    const fs::path texB = dir / "b.png";
    sf::Image img({16, 16}, sf::Color::Red);
    REQUIRE(img.saveToFile(texA.string()));
    REQUIRE(img.saveToFile(texB.string()));

    AssetManager& assets = AssetManager::Instance();
    assets.Clear();

    const fs::path manifestA = dir / "manifest_a.json";
    const fs::path manifestB = dir / "manifest_b.json";
    REQUIRE(Vanity::WriteJson(manifestA.string(), {{"textures", {{"a", texA.generic_string()}}}}));
    REQUIRE(Vanity::WriteJson(manifestB.string(), {{"textures", {{"b", texB.generic_string()}}}}));

    REQUIRE(assets.LoadManifest(manifestA.string()));
    REQUIRE(assets.HasTexture("a"));
    REQUIRE_FALSE(assets.HasTexture("b"));

    // The second manifest is the complete asset set: "a" must not linger.
    REQUIRE(assets.LoadManifest(manifestB.string()));
    REQUIRE_FALSE(assets.HasTexture("a"));
    REQUIRE(assets.HasTexture("b"));

    assets.Clear();
    fs::remove_all(dir);
}

TEST_CASE("Asset hot reload swaps a changed texture", "[assets][hotswap]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_asset_hotswap";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const fs::path texturePath = dir / "hot.png";
    sf::Image red({8, 8}, sf::Color::Red);
    REQUIRE(red.saveToFile(texturePath.string()));

    const fs::path manifest = dir / "manifest.json";
    REQUIRE(Vanity::WriteJson(manifest.string(), {{"textures", {{"hot", texturePath.generic_string()}}}}));

    AssetManager& assets = AssetManager::Instance();
    assets.Clear();
    REQUIRE(assets.LoadManifest(manifest.string()));
    REQUIRE(assets.HasTexture("hot"));
    REQUIRE(assets.Texture("hot").copyToImage().getPixel({0, 0}).r > 200);

    sf::Image blue({8, 8}, sf::Color::Blue);
    REQUIRE(blue.saveToFile(texturePath.string()));
    // NTFS timestamps are fine-grained, but forcing a newer value keeps the
    // test deterministic on filesystems with coarse timestamp resolution.
    std::error_code ec;
    const auto previous = fs::last_write_time(texturePath, ec);
    REQUIRE_FALSE(ec);
    fs::last_write_time(texturePath, previous + std::chrono::seconds(2), ec);
    REQUIRE_FALSE(ec);

    HotSwap& watch = HotSwap::Instance();
    watch.Poll();
    REQUIRE_FALSE(watch.ChangedFiles().empty());
    assets.HotReload();
    watch.ClearChanged();

    REQUIRE(assets.Texture("hot").copyToImage().getPixel({0, 0}).b > 200);

    assets.Clear();
    fs::remove_all(dir);
}

TEST_CASE("Asset database reads legacy manifests and dependency order", "[assets][pipeline]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_asset_db";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const fs::path a = dir / "a.png";
    const fs::path b = dir / "b.png";
    sf::Image red({16, 16}, sf::Color::Red);
    sf::Image blue({8, 8}, sf::Color::Blue);
    REQUIRE(red.saveToFile(a.string()));
    REQUIRE(blue.saveToFile(b.string()));

    const fs::path manifest = dir / "manifest.json";
    REQUIRE(Vanity::WriteJson(manifest.string(), {
                                                     {"textures", {{"a", "a.png"}, {"b", "b.png"}}},
                                                     {"import", {{"a", {{"atlas", "ui"}, {"smooth", false}}}}},
                                                     {"dependencies", {{"b", {"a"}}}},
                                                 }));

    AssetDatabase database;
    REQUIRE(database.Load(manifest.string()));
    std::vector<std::string> errors;
    REQUIRE(database.Validate(&errors));
    REQUIRE(errors.empty());
    REQUIRE(database.Assets().size() == 2);

    const AssetImportSettings* settings = database.Find("a");
    REQUIRE(settings != nullptr);
    REQUIRE(settings->kind == AssetKind::Texture);
    REQUIRE(settings->atlas == "ui");
    REQUIRE_FALSE(settings->smooth);
    REQUIRE(database.DependsOn("b", "a"));

    const std::vector<std::string> order = database.TopologicalOrder(&errors);
    REQUIRE(errors.empty());
    REQUIRE(order.size() == 2);
    REQUIRE(std::find(order.begin(), order.end(), "a") < std::find(order.begin(), order.end(), "b"));
    REQUIRE(database.SourceHash("a") != 0);
    REQUIRE(AssetDatabase::HashHex(database.SourceHash("a")).size() == 16);

    fs::remove_all(dir);
}

TEST_CASE("Asset database validation rejects dependency cycles", "[assets][pipeline]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_asset_cycle";
    fs::remove_all(dir);
    fs::create_directories(dir);

    REQUIRE(sf::Image({4, 4}, sf::Color::Red).saveToFile((dir / "a.png").string()));
    REQUIRE(sf::Image({4, 4}, sf::Color::Blue).saveToFile((dir / "b.png").string()));
    const fs::path manifest = dir / "manifest.json";
    REQUIRE(Vanity::WriteJson(manifest.string(), {
                                                     {"textures", {{"a", "a.png"}, {"b", "b.png"}}},
                                                     {"dependencies", {{"a", {"b"}}, {"b", {"a"}}}},
                                                 }));

    AssetDatabase database;
    REQUIRE(database.Load(manifest.string()));
    std::vector<std::string> errors;
    REQUIRE_FALSE(database.Validate(&errors));
    const bool mentionsCycle = std::any_of(errors.begin(), errors.end(), [](const std::string& error)
                                           { return error.find("cycle") != std::string::npos; });
    REQUIRE(mentionsCycle);
    fs::remove_all(dir);
}

TEST_CASE("Atlas packer keeps every region inside its page", "[assets][pipeline]")
{
    std::vector<AtlasInput> inputs;
    inputs.push_back({"wide", sf::Image({40, 20}, sf::Color::Red)});
    inputs.push_back({"tall", sf::Image({20, 50}, sf::Color::Green)});
    inputs.push_back({"square", sf::Image({16, 16}, sf::Color::Blue)});

    AtlasOptions options;
    options.maxSize = 128;
    options.padding = 2;
    options.powerOfTwo = true;
    const AtlasResult packed = AtlasPacker::Pack(inputs, options);
    REQUIRE(packed.ok);
    REQUIRE(packed.regions.size() == 3);
    REQUIRE_FALSE(packed.pages.empty());

    for (const AtlasRegion& region : packed.regions)
    {
        REQUIRE(region.page >= 0);
        REQUIRE(region.page < static_cast<int>(packed.pages.size()));
        const AtlasPage& page = packed.pages[static_cast<std::size_t>(region.page)];
        REQUIRE(region.x >= 0);
        REQUIRE(region.y >= 0);
        REQUIRE(region.x + region.width <= page.width);
        REQUIRE(region.y + region.height <= page.height);
    }
}

TEST_CASE("Asset cooker writes a shared atlas and a loadable manifest", "[assets][pipeline]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "femboyengine_asset_cook";
    fs::remove_all(dir);
    fs::create_directories(dir);

    REQUIRE(sf::Image({32, 32}, sf::Color::Red).saveToFile((dir / "a.png").string()));
    REQUIRE(sf::Image({16, 16}, sf::Color::Blue).saveToFile((dir / "b.png").string()));

    const fs::path manifest = dir / "manifest.json";
    REQUIRE(
        Vanity::WriteJson(manifest.string(), {
                                                 {"assets",
                                                  {{"a", {{"type", "texture"}, {"path", "a.png"}, {"atlas", "ui"}}},
                                                   {"b", {{"type", "texture"}, {"path", "b.png"}, {"atlas", "ui"}}}}},
                                                 {"dependencies", {{"b", {"a"}}}},
                                             }));

    AssetDatabase database;
    REQUIRE(database.Load(manifest.string()));
    std::vector<std::string> errors;
    REQUIRE(database.Validate(&errors));
    REQUIRE(database.Stale((dir / "cooked").string()).size() == 2);

    CookOptions options;
    options.outputDir = (dir / "cooked").string();
    options.createPack = true;
    const CookResult cooked = AssetCooker::Cook(database, options);
    REQUIRE(cooked.ok);
    REQUIRE(cooked.errors.empty());
    REQUIRE(cooked.textures == 2);
    REQUIRE(cooked.atlases == 1);
    REQUIRE(fs::exists(dir / "cooked" / "manifest.json"));
    REQUIRE(fs::exists(dir / "cooked" / "assets.fempak"));

    AssetManager& assets = AssetManager::Instance();
    assets.Clear();
    REQUIRE(assets.LoadManifest((dir / "cooked" / "manifest.json").string()));
    REQUIRE(assets.HasTexture("a"));
    REQUIRE(assets.HasTexture("b"));
    const sf::IntRect rectA = assets.TextureRect("a");
    const sf::IntRect rectB = assets.TextureRect("b");
    REQUIRE(rectA.size.x > 0);
    REQUIRE(rectB.size.x > 0);
    REQUIRE(assets.Texture("a").getSize().x >= static_cast<unsigned>(rectA.size.x));
    REQUIRE(assets.Texture("b").getSize().x >= static_cast<unsigned>(rectB.size.x));
    assets.Clear();

    // The cooked pack carries the manifest too and extracts into the same tree
    // the manifest describes, so a runtime can mount one archive.
    ResourcePack packed;
    REQUIRE(packed.Load((dir / "cooked" / "assets.fempak").string()));
    REQUIRE(packed.Has("manifest.json"));
    REQUIRE(packed.Has("cooked.json"));
    REQUIRE(packed.Has("atlas:ui:0"));
    for (const ResourcePack::Entry& entry : packed.Entries())
    {
        CHECK_FALSE(std::filesystem::path(entry.path).is_absolute());
        CHECK(entry.path.find("..") == std::string::npos);
    }
    REQUIRE(packed.Extract((dir / "unpacked").string()));
    REQUIRE(fs::exists(dir / "unpacked" / "manifest.json"));
    REQUIRE(fs::exists(dir / "unpacked" / "atlases" / "ui_0.png"));

    assets.Clear();
    REQUIRE(assets.LoadPackedManifest((dir / "cooked" / "assets.fempak").string(), (dir / "pack-cache").string()));
    REQUIRE(assets.HasTexture("a"));
    REQUIRE(assets.HasTexture("b"));
    CHECK(assets.Texture("a").getSize().x >= static_cast<unsigned>(rectA.size.x));
    assets.Clear();

    // The source hash cache makes the second cook report a clean tree.
    REQUIRE(database.Stale((dir / "cooked").string()).empty());
    REQUIRE(sf::Image({32, 32}, sf::Color::Green).saveToFile((dir / "a.png").string()));
    REQUIRE(database.Stale((dir / "cooked").string()).size() == 1);

    fs::remove_all(dir);
}

TEST_CASE("Voice: Opus round-trips a 20 ms frame and settings are clamped", "[voice]")
{
    std::string error;
#ifdef FEMBOY_HAS_VOICE
    REQUIRE(VoiceChat::CodecSelfTest(&error));
#else
    REQUIRE_FALSE(VoiceChat::CodecSelfTest(&error));
#endif

    VoiceChat& voice = VoiceChat::Instance();
    CHECK_FALSE(voice.Enabled());
    voice.SetInputGain(9.f);
    CHECK(voice.InputGain() == Catch::Approx(4.f));
    voice.SetOutputVolume(-1.f);
    CHECK(voice.OutputVolume() == Catch::Approx(0.f));
    voice.SetVoiceActivationThreshold(5.f);
    CHECK(voice.VoiceActivationThreshold() == Catch::Approx(1.f));
    voice.SetPushToTalk(false);
    CHECK_FALSE(voice.PushToTalk());
    voice.SetPushToTalk(true);
    CHECK(voice.PushToTalk());

    VoiceSettings settings;
    REQUIRE(settings.FromJson(
        {{"inputGain", 9.f}, {"outputVolume", -2.f}, {"voiceActivationThreshold", 5.f}, {"pushToTalk", false}}));
    CHECK(settings.inputGain == Catch::Approx(4.f));
    CHECK(settings.outputVolume == Catch::Approx(0.f));
    CHECK(settings.voiceActivationThreshold == Catch::Approx(1.f));
    CHECK_FALSE(settings.pushToTalk);
    const nlohmann::json serialized = settings.ToJson();
    REQUIRE(serialized.is_object());
    CHECK(serialized["inputGain"].get<float>() == Catch::Approx(4.f));
    VoiceSettings roundTrip;
    REQUIRE(roundTrip.FromJson(serialized));
    CHECK(roundTrip.inputGain == Catch::Approx(4.f));
}

TEST_CASE("Network: binary packets use channel 1, not the JSON parser", "[network][binary]")
{
#ifdef FEMBOY_HAS_ENET
    Network& server = Network::Instance();
    server.Shutdown();

    unsigned short port = 0;
    for (unsigned short candidate = 47811; candidate < 47821; ++candidate)
    {
        if (server.StartServer(candidate, 4))
        {
            port = candidate;
            break;
        }
    }
    REQUIRE(port != 0);

    ENetHost* client = nullptr;
    struct Guard
    {
        Network& network;
        ENetHost*& host;
        ~Guard()
        {
            network.SetBinaryReceiveCallback({});
            network.SetPeerConnectedCallback({});
            if (host)
            {
                enet_host_destroy(host);
                host = nullptr;
            }
            network.StopServer();
            network.Shutdown();
        }
    } guard{server, client};

    uint32_t serverPeerId = 0;
    server.SetPeerConnectedCallback([&](uint32_t id) { serverPeerId = id; });

    std::vector<std::uint8_t> serverReceived;
    server.SetBinaryReceiveCallback(
        [&](uint32_t, std::uint8_t channel, const std::uint8_t* data, std::size_t size)
        {
            if (channel == Network::kVoiceChannel)
            {
                serverReceived.assign(data, data + size);
            }
        });

    client = enet_host_create(nullptr, 1, Network::kChannelCount, 0, 0);
    REQUIRE(client != nullptr);

    ENetAddress address{};
    REQUIRE(enet_address_set_host(&address, "127.0.0.1") == 0);
    address.port = port;
    ENetPeer* clientPeer = enet_host_connect(client, &address, Network::kChannelCount, 0);
    REQUIRE(clientPeer != nullptr);

    bool clientConnected = false;
    std::vector<std::uint8_t> clientReceived;
    std::uint8_t clientChannel = 0xff;
    auto pumpClient = [&]()
    {
        ENetEvent event{};
        while (enet_host_service(client, &event, 0) > 0)
        {
            if (event.type == ENET_EVENT_TYPE_CONNECT)
            {
                clientConnected = true;
            }
            else if (event.type == ENET_EVENT_TYPE_RECEIVE)
            {
                clientChannel = event.channelID;
                clientReceived.assign(event.packet->data, event.packet->data + event.packet->dataLength);
                enet_packet_destroy(event.packet);
                event.packet = nullptr;
            }
        }
    };

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((!clientConnected || serverPeerId == 0) && std::chrono::steady_clock::now() < deadline)
    {
        server.Poll();
        pumpClient();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(clientConnected);
    REQUIRE(serverPeerId != 0);

    const std::vector<std::uint8_t> toClient{0x46, 0x42, 0x56, 0x31, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0x7f};
    REQUIRE(server.SendBinaryTo(serverPeerId, Network::kVoiceChannel, toClient.data(), toClient.size(), false));
    while (clientReceived.empty() && std::chrono::steady_clock::now() < deadline)
    {
        pumpClient();
        server.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(clientChannel == Network::kVoiceChannel);
    REQUIRE(clientReceived == toClient);

    const std::vector<std::uint8_t> toServer{0x46, 0x42, 0x56, 0x31, 1, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0x42};
    ENetPacket* packet = enet_packet_create(toServer.data(), toServer.size(), 0);
    REQUIRE(packet != nullptr);
    REQUIRE(enet_peer_send(clientPeer, Network::kVoiceChannel, packet) == 0);
    enet_host_flush(client);
    while (serverReceived.empty() && std::chrono::steady_clock::now() < deadline)
    {
        server.Poll();
        pumpClient();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(serverReceived == toServer);
#else
    SUCCEED("ENet is not available in this build");
#endif
}

TEST_CASE("Packet rate limiter enforces its burst budget", "[network]")
{
    PacketRateLimiter limiter;
    CHECK_FALSE(limiter.Enabled());
    CHECK(limiter.Allow());

    limiter.Configure(10.0, 2.0);
    CHECK(limiter.Enabled());
    CHECK(limiter.Allow());
    CHECK(limiter.Allow());
    // The bucket is empty immediately after the burst; refill needs real time.
    CHECK_FALSE(limiter.Allow());

    limiter.Reset();
    CHECK(limiter.Allow());

    limiter.Configure(0.0, 10.0);
    CHECK_FALSE(limiter.Enabled());
    CHECK(limiter.Allow());
}

TEST_CASE("Reconnect policy rejects unsafe timings", "[hosting][network]")
{
    GameHost& host = GameHost::Instance();
    const GameHost::ReconnectPolicy previous = host.GetReconnectPolicy();

    GameHost::ReconnectPolicy policy;
    policy.enabled = true;
    policy.initialDelay = 0.f;
    policy.maxDelay = -5.f;
    policy.backoffMultiplier = 0.25f;
    policy.maxAttempts = -4;
    host.SetReconnectPolicy(policy);

    CHECK(host.GetReconnectPolicy().enabled);
    CHECK(host.GetReconnectPolicy().initialDelay == Catch::Approx(0.1f));
    CHECK(host.GetReconnectPolicy().maxDelay >= host.GetReconnectPolicy().initialDelay);
    CHECK(host.GetReconnectPolicy().backoffMultiplier == Catch::Approx(1.f));
    CHECK(host.GetReconnectPolicy().maxAttempts == 0);
    CHECK_FALSE(host.AllPlayersReady());
    CHECK_FALSE(host.SetReady(true));
    CHECK(host.ReadyCount() == 0);

    host.SetReconnectPolicy(previous);
}

TEST_CASE("Hosting: lobby readiness tracks the listen server", "[hosting][network]")
{
#ifdef FEMBOY_HAS_ENET
    GameHost& host = GameHost::Instance();
    host.StopHost();
    host.Leave();

    unsigned short port = 0;
    for (unsigned short candidate = 47931; candidate < 47941; ++candidate)
    {
        if (host.Host(candidate, 4, "TestServer"))
        {
            port = candidate;
            break;
        }
    }
    REQUIRE(port != 0);
    REQUIRE(host.IsHosting());
    REQUIRE(host.PlayerCount() == 1);
    REQUIRE(host.IsReady());
    REQUIRE(host.AllPlayersReady());
    REQUIRE(host.ReadyCount() == 1);

    REQUIRE(host.SetReady(false));
    CHECK_FALSE(host.IsReady());
    CHECK_FALSE(host.AllPlayersReady());
    CHECK(host.ReadyCount() == 0);

    REQUIRE(host.SetReady(true));
    CHECK(host.IsReady());
    CHECK(host.AllPlayersReady());
    CHECK(host.ReadyCount() == 1);

    host.StopHost();
    CHECK_FALSE(host.IsHosting());
    CHECK_FALSE(host.AllPlayersReady());
#else
    SUCCEED("ENet is not available in this build");
#endif
}

// Gameplay tags

// Declared the way game code declares one: a macro, one translation unit, no
// central list to keep in sync.
namespace
{
FEMBOY_DEFINE_TAG(UnitTestMacroTag, "UnitTest.Macro.Declared");
}

TEST_CASE("Tags: hierarchy is derived from the name", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();

    const TagId enemy = registry.RegisterTag("UnitTest.Enemy");
    const TagId mine = registry.RegisterTag("UnitTest.Enemy.Mine");
    const TagId buried = registry.RegisterTag("UnitTest.Enemy.Mine.Buried");
    const TagId root = registry.Find("UnitTest");

    REQUIRE(enemy != kInvalidTag);
    REQUIRE(mine != kInvalidTag);
    REQUIRE(root != kInvalidTag);

    // Registering the same name twice is free and returns the same id.
    REQUIRE(registry.RegisterTag("UnitTest.Enemy") == enemy);
    REQUIRE(registry.RegisterTag("UnitTest.Enemy.Mine") == mine);

    // The parent falls out of the dots, in both directions.
    REQUIRE(registry.Parent(mine) == enemy);
    REQUIRE(registry.Parent(enemy) == root);
    REQUIRE(registry.Parent(root) == kInvalidTag);
    REQUIRE(registry.IsDescendantOf(mine, enemy));
    REQUIRE(registry.IsDescendantOf(buried, enemy));
    REQUIRE_FALSE(registry.IsDescendantOf(enemy, mine));
    REQUIRE_FALSE(registry.IsDescendantOf(enemy, enemy));

    REQUIRE(registry.Name(mine) == "UnitTest.Enemy.Mine");
    REQUIRE(registry.Find("UnitTest.Enemy") == enemy);
    REQUIRE(registry.Find("UnitTest.DoesNotExist") == kInvalidTag);
    REQUIRE_FALSE(registry.IsValid(kInvalidTag));

    const std::vector<TagId> kids = registry.Children(enemy);
    REQUIRE(std::find(kids.begin(), kids.end(), mine) != kids.end());

    // Ids are hashes of the name, so they are stable and never a table index.
    REQUIRE(registry.IsValid(enemy));
    REQUIRE(enemy != 1u);
}

TEST_CASE("Tags: invalid names are rejected, not rewritten", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();

    REQUIRE(registry.RegisterTag("") == kInvalidTag);
    REQUIRE(registry.RegisterTag("UnitTest Bad") == kInvalidTag);
    REQUIRE(registry.RegisterTag("UnitTest..Double") == kInvalidTag);
    REQUIRE(registry.RegisterTag(".UnitTest.Leading") == kInvalidTag);
    REQUIRE(registry.RegisterTag("UnitTest.Trailing.") == kInvalidTag);
    REQUIRE(registry.Find("UnitTest Bad") == kInvalidTag);
}

TEST_CASE("Tags: the macro registers on first use", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();

    const TagId id = UnitTestMacroTag();
    REQUIRE(id != kInvalidTag);
    REQUIRE(registry.Name(id) == "UnitTest.Macro.Declared");
    REQUIRE(UnitTestMacroTag() == id);
    REQUIRE(registry.Parent(id) == registry.Find("UnitTest.Macro"));
}

TEST_CASE("Tags: container matching walks the hierarchy", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();

    const TagId creature = registry.RegisterTag("UnitTest.Creature");
    const TagId mine = registry.RegisterTag("UnitTest.Creature.Mine");
    const TagId boss = registry.RegisterTag("UnitTest.Creature.Boss");
    const TagId weak = registry.RegisterTag("UnitTest.Weak");

    TagContainer c;
    c.Add(mine);
    c.Add(weak);

    // Sorted and deduplicated, whatever the insertion order.
    REQUIRE(c.Size() == 2);
    c.Add(mine);
    REQUIRE(c.Size() == 2);
    REQUIRE(std::is_sorted(c.Ids().begin(), c.Ids().end()));
    c.Add(kInvalidTag);
    REQUIRE(c.Size() == 2);

    REQUIRE(c.Has(mine));
    REQUIRE_FALSE(c.Has(creature));
    // A mine is a creature: matching walks up, exactness does not.
    REQUIRE(c.Matches(creature));
    REQUIRE(c.Matches(mine));
    REQUIRE_FALSE(c.Matches(boss));

    TagContainer parentQuery;
    parentQuery.Add(creature);
    REQUIRE(c.MatchesAny(parentQuery));
    REQUIRE_FALSE(c.HasAny(parentQuery));

    TagContainer everything;
    everything.Add(creature);
    everything.Add(weak);
    REQUIRE(c.MatchesAll(everything));
    REQUIRE_FALSE(c.HasAll(everything));

    TagContainer exact;
    exact.Add(mine);
    exact.Add(weak);
    REQUIRE(c.HasAll(exact));
    REQUIRE(c.HasAny(exact));

    REQUIRE(c.ToString().find("UnitTest.Creature.Mine") != std::string::npos);

    c.Remove(mine);
    REQUIRE_FALSE(c.Has(mine));
    REQUIRE(c.Size() == 1);

    // An unknown name is refused, never invented: a typo has to be visible.
    REQUIRE_FALSE(c.Add("UnitTest.NotRegistered"));
    REQUIRE(c.Size() == 1);

    const TagContainer made = MakeTags({"UnitTest.Weak", "UnitTest.Creature.Mine"});
    REQUIRE(made.Size() == 2);
    REQUIRE(made.Matches(creature));
}

TEST_CASE("Tags: json is a flat list", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();

    const nlohmann::json doc = {
        {"version", 1},
        {"tags", nlohmann::json::array({
                     "UnitTest.Json",
                     "UnitTest.Json.Child",
                     {{"name", "UnitTest.Json.Commented"}, {"comment", "hello"}, {"color", "#ff00ff"}},
                     "UnitTest Bad Name",
                     {"UnitTest.Json"},
                     42,
                 })},
    };

    REQUIRE(registry.LoadFromJson(doc));

    const TagId child = registry.Find("UnitTest.Json.Child");
    REQUIRE(child != kInvalidTag);
    REQUIRE(registry.Parent(child) == registry.Find("UnitTest.Json"));

    const TagId commented = registry.Find("UnitTest.Json.Commented");
    REQUIRE(commented != kInvalidTag);
    REQUIRE(registry.Comment(commented) == "hello");
    REQUIRE(registry.Color(commented) == "#ff00ff");

    // Invalid entries are skipped rather than mangled into something valid.
    REQUIRE(registry.Find("UnitTest Bad Name") == kInvalidTag);

    // A bare array is the shape a hand-written file usually takes.
    REQUIRE(registry.LoadFromJson(nlohmann::json::array({"UnitTest.Bare"})));
    REQUIRE(registry.Find("UnitTest.Bare") != kInvalidTag);

    // Something that is neither an array nor an object with "tags" is an error.
    REQUIRE_FALSE(registry.LoadFromJson(nlohmann::json::object()));
    REQUIRE_FALSE(registry.LoadFromJson(nlohmann::json(7)));

    // Reloading what was saved changes nothing, and ids survive the round trip
    // because they are hashes of the name rather than table positions.
    const nlohmann::json saved = registry.ToJson();
    REQUIRE(saved["tags"].is_array());
    REQUIRE(registry.LoadFromJson(saved));
    REQUIRE(registry.Find("UnitTest.Json.Child") == child);
    REQUIRE(registry.Comment(registry.Find("UnitTest.Json.Commented")) == "hello");

    // The text form of a container is the names, so a log line is readable.
    TagContainer c;
    REQUIRE(c.Add("UnitTest.Json.Child"));
    REQUIRE(c.Add("UnitTest.Json"));
    REQUIRE(c.Size() == 2);
    REQUIRE(c.Has(child));
    REQUIRE(c.ToJson().is_array());
    REQUIRE(c.ToJson().size() == 2);

    TagContainer reloaded;
    REQUIRE(reloaded.FromJson(c.ToJson()));
    REQUIRE(reloaded.Has(registry.Find("UnitTest.Json")));
}

TEST_CASE("Tags: visual script nodes compile to runtime calls", "[tags][visualscript]")
{
    VisualScriptGraph graph;

    const int onStart = graph.AddNode(VSNodeType::OnStart, 0.f, 0.f);
    const int has = graph.AddNode(VSNodeType::HasTag, 0.f, 100.f);
    const int print = graph.AddNode(VSNodeType::Print, 0.f, 200.f);

    // The tag lives as a default on the input pin, so a literal needs no extra
    // node and a connected tag still wins.
    VSNode* hasNode = graph.Node(has);
    REQUIRE(hasNode != nullptr);
    hasNode->params["in1"] = "UnitTest.Creature";

    REQUIRE(graph.Connect(onStart, 0, has, 0));
    REQUIRE(graph.Connect(has, 0, print, 0));

    const std::string code = graph.Compile();
    REQUIRE(code.find("hasTag(") != std::string::npos);
    REQUIRE(code.find("\"UnitTest.Creature\"") != std::string::npos);

    // A tag event that names a tag guards on it; an unnamed one hears everything.
    VisualScriptGraph events;
    const int added = events.AddNode(VSNodeType::OnTagAdded, 0.f, 0.f);
    const int printNode = events.AddNode(VSNodeType::Print, 0.f, 100.f);
    VSNode* addedNode = events.Node(added);
    REQUIRE(addedNode != nullptr);
    addedNode->params["tag"] = "Enemy.Mine";
    REQUIRE(events.Connect(added, 0, printNode, 0));

    const std::string eventCode = events.Compile();
    REQUIRE(eventCode.find("void OnTagAdded(string tag)") != std::string::npos);
    // A named tag event guards its body instead of returning early, so several
    // OnTagAdded nodes can share one generated function.
    REQUIRE(eventCode.find("if (tag == \"Enemy.Mine\")") != std::string::npos);

    // Every node type has a name, and the editor's picker is driven by the same
    // list, so the two can never drift apart.
    for (VSNodeType type : VsNodeTypes())
    {
        REQUIRE(VsNodeTypeName(type) != nullptr);
        REQUIRE(std::string(VsNodeTypeName(type)) != "Unknown");
    }
    REQUIRE(std::string(VsNodeTypeName(VSNodeType::HasTag)) == "HasTag");
}

TEST_CASE("Tags: replace rebuilds the table without moving ids", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();

    const TagId child = registry.RegisterTag("UnitTest.Json.Child");
    registry.RegisterTag("UnitTest.Json.Doomed");

    const nlohmann::json replacement = {
        {"tags", nlohmann::json::array({"UnitTest.Json", "UnitTest.Json.Child", "UnitTest.Json.Kept"})}};
    REQUIRE(registry.LoadFromJson(replacement, true));

    // The name that is still in the document keeps its id; the one that is gone
    // is gone.
    REQUIRE(registry.Find("UnitTest.Json.Child") == child);
    REQUIRE(registry.Find("UnitTest.Json.Kept") != kInvalidTag);
    REQUIRE(registry.Find("UnitTest.Json.Doomed") == kInvalidTag);
}

TEST_CASE("Tags: a rename leaves an alias that keeps resolving", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();

    const TagId original = registry.RegisterTag("UnitTest.Legacy.Enemy");
    registry.SetMeta(original, "was here first", "#abc");

    REQUIRE(registry.RenameTag("UnitTest.Legacy.Enemy", "UnitTest.Modern.Enemy"));

    // The id is the old name hashed, so the old id is gone and cannot answer.
    REQUIRE(registry.IsValid(original) == false);

    const TagId modern = registry.Find("UnitTest.Modern.Enemy");
    REQUIRE(modern != kInvalidTag);
    REQUIRE(modern != original);

    // A save written before the rename still resolves to the new tag.
    REQUIRE(registry.Find("UnitTest.Legacy.Enemy") == modern);
    REQUIRE(registry.Find("UnitTest.Modern.Enemy") == modern);
    REQUIRE(registry.Comment(modern) == "was here first");

    // Renaming something that was never registered is a refusal, not a crash.
    REQUIRE(registry.RenameTag("UnitTest.Legacy.Missing", "UnitTest.Modern.Other") == false);

    // A chained rename follows the chain to the end.
    REQUIRE(registry.RenameTag("UnitTest.Modern.Enemy", "UnitTest.Final.Enemy"));
    REQUIRE(registry.Find("UnitTest.Legacy.Enemy") == registry.Find("UnitTest.Final.Enemy"));
}

TEST_CASE("Tags: replication carries names, not ids", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();
    const TagId weak = registry.RegisterTag("UnitTest.Rep.Weak");
    const TagId boss = registry.RegisterTag("UnitTest.Rep.Boss");

    TagContainer previous;
    previous.Add(weak);

    TagContainer current;
    current.Add(boss);

    const nlohmann::json delta = TagDelta(previous, current);
    REQUIRE(delta.is_object());
    REQUIRE(delta.contains("add"));
    REQUIRE(delta.contains("remove"));
    REQUIRE(delta["add"].size() == 1);
    REQUIRE(delta["add"][0] == "UnitTest.Rep.Boss");
    REQUIRE(delta["remove"][0] == "UnitTest.Rep.Weak");

    // The receiving side rebuilds exactly the same set from names alone.
    TagContainer applied;
    applied.Add(weak);
    std::vector<std::string> unknown;
    REQUIRE(ApplyTagDelta(applied, delta, &unknown));
    REQUIRE(unknown.empty());
    REQUIRE(applied.Has(boss));
    REQUIRE(applied.Has(weak) == false);

    // An unchanged set serializes to an empty object.
    REQUIRE(TagDelta(current, current).empty());

    // A name this build does not know is reported and skipped, never guessed at.
    TagContainer small;
    const nlohmann::json foreign = {{"add", nlohmann::json::array({"UnitTest.Rep.Weak", "Vendor.Dlc.Tag"})}};
    REQUIRE(ApplyTagDelta(small, foreign, &unknown) == false);
    REQUIRE(unknown.size() == 1);
    REQUIRE(unknown[0] == "Vendor.Dlc.Tag");
    REQUIRE(small.Has(weak));
}

TEST_CASE("Tags: a query rejects the impossible in one AND", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();
    registry.RegisterTag("UnitTest.Q.Enemy");
    const TagId mine = registry.RegisterTag("UnitTest.Q.Enemy.Mine");
    const TagId boss = registry.RegisterTag("UnitTest.Q.Enemy.Boss");
    const TagId stunned = registry.RegisterTag("UnitTest.Q.Effect.Stunned");

    TagContainer enemy;
    enemy.Add("UnitTest.Q.Enemy");
    TagContainer mineEnemy = enemy;
    mineEnemy.Add(mine);
    TagContainer stunnedEnemy = enemy;
    stunnedEnemy.Add(stunned);

    TagContainer required;
    required.Add("UnitTest.Q.Enemy.Mine");
    TagContainer blocked;
    blocked.Add(stunned);

    const TagQuery query(required, blocked);
    REQUIRE(query.Empty() == false);
    // Held through the parent tag as well: that is what makes the query
    // hierarchy-aware rather than an exact-set test.
    REQUIRE(query.Matches(mineEnemy));
    REQUIRE(query.Matches(enemy) == false);
    REQUIRE(query.Matches(stunnedEnemy) == false);

    // A query that cannot possibly match is rejected before any walk happens.
    TagContainer unrelated;
    unrelated.Add(boss);
    REQUIRE(query.Matches(unrelated) == false);

    // The hint is the union of a tag and its ancestors.
    REQUIRE(mineEnemy.Hint() != 0);
    REQUIRE((mineEnemy.Hint() & required.Hint()) == required.Hint());
    REQUIRE((unrelated.Hint() & required.Hint()) != required.Hint());
    REQUIRE((stunnedEnemy.Hint() & blocked.Hint()) != 0);
}

TEST_CASE("Tags: a state-tree transition can ask a tag question", "[tags]")
{
    TagRegistry& registry = TagRegistry::Instance();
    const TagId stunned = registry.RegisterTag("UnitTest.ST.Effect.Stunned");

    TagContainer tags;
    auto condition = STHasTag(stunned, &tags);
    REQUIRE(condition(nullptr) == false);

    tags.Add(stunned);
    REQUIRE(condition(nullptr) == true);

    // The getter form survives the container moving, and fails instead of
    // dereferencing a null one.
    const TagContainer* current = nullptr;
    auto viaGetter = STHasTag(stunned, [&current]() { return current; });
    REQUIRE(viaGetter(nullptr) == false);
    current = &tags;
    REQUIRE(viaGetter(nullptr) == true);
}

TEST_CASE("Skills: a duration effect changes current, not base", "[skills]")
{
    const TagId health = TagRegistry::Instance().RegisterTag("UnitTest.Skills.Health");
    const TagId speed = TagRegistry::Instance().RegisterTag("UnitTest.Skills.Speed");
    const TagId slow = TagRegistry::Instance().RegisterTag("UnitTest.Skills.Effect.Slow");

    EffectDef def;
    def.id = slow;
    def.duration = 2.f;
    AttributeModifier mod;
    mod.attribute = speed;
    mod.op = ModifierOp::Multiply;
    mod.magnitude = 0.5f;
    def.modifiers.push_back(mod);
    SkillLibrary::Instance().DefineEffect(def);

    SkillSet set;
    set.DefineAttribute(health, 100.f, 0.f, 100.f);
    set.DefineAttribute(speed, 200.f, 0.f, 1000.f);
    REQUIRE(set.Attributes().Current(health) == Catch::Approx(100.f));

    // An untouched attribute is recomputed lazily: before any change, current is
    // what was defined.
    set.ApplyEffect(slow);
    set.Tick(0.016f);
    REQUIRE(set.Attributes().Base(speed) == Catch::Approx(200.f));
    REQUIRE(set.Attributes().Current(speed) == Catch::Approx(100.f));
    REQUIRE(set.HasEffect(slow));

    // Expiry restores the value exactly, because it was never written to base.
    set.Tick(2.5f);
    REQUIRE(set.HasEffect(slow) == false);
    REQUIRE(set.Attributes().Current(speed) == Catch::Approx(200.f));
}

TEST_CASE("Skills: an instant effect folds into the base", "[skills]")
{
    const TagId health = TagRegistry::Instance().RegisterTag("UnitTest.Skills.Instant.Health");
    const TagId heal = TagRegistry::Instance().RegisterTag("UnitTest.Skills.Effect.Heal");

    EffectDef def;
    def.id = heal;
    AttributeModifier mod;
    mod.attribute = health;
    mod.op = ModifierOp::Add;
    mod.magnitude = 25.f;
    def.modifiers.push_back(mod);
    SkillLibrary::Instance().DefineEffect(def);

    SkillSet set;
    set.DefineAttribute(health, 50.f, 0.f, 100.f);
    REQUIRE(set.ApplyEffect(heal) == 0);
    REQUIRE(set.HasEffect(heal) == false);
    REQUIRE(set.Attributes().Current(health) == Catch::Approx(75.f));

    // Clamped to the declared maximum, and permanent: nothing to expire.
    set.ApplyEffect(heal);
    REQUIRE(set.Attributes().Current(health) == Catch::Approx(100.f));
    set.Tick(5.f);
    REQUIRE(set.Attributes().Current(health) == Catch::Approx(100.f));
}

TEST_CASE("Skills: a periodic effect damages the base every period", "[skills]")
{
    const TagId health = TagRegistry::Instance().RegisterTag("UnitTest.Skills.Burn.Health");
    const TagId burn = TagRegistry::Instance().RegisterTag("UnitTest.Skills.Effect.Burn");

    EffectDef def;
    def.id = burn;
    def.duration = 3.f;
    def.period = 1.f;
    def.periodAttribute = health;
    def.periodMagnitude = -5.f;
    SkillLibrary::Instance().DefineEffect(def);

    SkillSet set;
    set.DefineAttribute(health, 100.f, 0.f, 100.f);
    set.ApplyEffect(burn);

    set.Tick(1.01f);
    REQUIRE(set.Attributes().Current(health) == Catch::Approx(95.f).margin(0.01f));
    set.Tick(1.0f);
    REQUIRE(set.Attributes().Current(health) == Catch::Approx(90.f).margin(0.01f));

    // Past the duration the effect is gone and stops ticking entirely.
    set.Tick(2.f);
    REQUIRE(set.HasEffect(burn) == false);
    REQUIRE(set.Attributes().Current(health) == Catch::Approx(85.f).margin(0.01f));
    set.Tick(3.f);
    REQUIRE(set.Attributes().Current(health) == Catch::Approx(85.f).margin(0.01f));
}

TEST_CASE("Skills: requirements and blocking tags decide activation", "[skills]")
{
    TagRegistry& registry = TagRegistry::Instance();
    const TagId player = registry.RegisterTag("UnitTest.Skills.Player");
    const TagId stamina = registry.RegisterTag("UnitTest.Skills.Stamina");
    const TagId stunnedTag = registry.RegisterTag("UnitTest.Skills.Effect.Stunned");
    const TagId dash = registry.RegisterTag("UnitTest.Skills.Skill.Dash");
    const TagId dashTag = registry.RegisterTag("UnitTest.Skills.Skill.Dashing");

    SkillDef def;
    def.id = dash;
    def.cooldown = 2.f;
    def.duration = 0.5f;
    def.costAttribute = stamina;
    def.cost = 10.f;
    def.blocked.Add(stunnedTag);
    def.grantedTags.Add(dashTag);
    SkillLibrary::Instance().DefineSkill(def);

    // The stun is an effect that grants the tag the dash is blocked by. An
    // effect's own id is not a tag on the set: only grantedTags are.
    EffectDef stun;
    stun.id = stunnedTag;
    stun.duration = 1.f;
    stun.grantedTags.Add(stunnedTag);
    SkillLibrary::Instance().DefineEffect(stun);

    SkillSet set;
    set.DefineAttribute(stamina, 30.f, 0.f, 100.f);
    set.AddPersistentTag(player);

    // Not granted yet, so it cannot be activated even though everything else fits.
    REQUIRE(set.CanActivate(dash) == false);
    REQUIRE(set.Activate(dash) == false);

    REQUIRE(set.Grant(dash));
    REQUIRE(set.Grant(dash) == false); // already granted
    REQUIRE(set.CanActivate(dash));

    REQUIRE(set.Activate(dash));
    REQUIRE(set.Attributes().Current(stamina) == Catch::Approx(20.f));
    REQUIRE(set.IsActive(dash));
    REQUIRE(set.Cooldown(dash) == Catch::Approx(2.f));
    // The skill's own tag is granted for as long as it runs.
    REQUIRE(set.Tags().Matches(dashTag));

    // Running and on cooldown both refuse a second activation.
    REQUIRE(set.Activate(dash) == false);

    set.Cancel(dash);
    REQUIRE(set.IsActive(dash) == false);
    REQUIRE(set.Tags().Matches(dashTag) == false);

    // A blocked tag refuses activation outright, and only for that set.
    SkillSet blocked;
    blocked.DefineAttribute(stamina, 30.f, 0.f, 100.f);
    blocked.AddPersistentTag(player);
    blocked.Grant(dash);
    REQUIRE(blocked.CanActivate(dash));

    blocked.ApplyEffect(stunnedTag);
    REQUIRE(blocked.Tags().Has(stunnedTag));
    REQUIRE(blocked.CanActivate(dash) == false);
    REQUIRE(blocked.Activate(dash) == false);

    // The stun expires and the skill becomes available again.
    blocked.Tick(1.5f);
    REQUIRE(blocked.HasEffect(stunnedTag) == false);
    REQUIRE(blocked.CanActivate(dash));

    // Revoking takes the skill away, drops its live instance and its cooldown.
    REQUIRE(set.Revoke(dash));
    REQUIRE(set.HasSkill(dash) == false);
    REQUIRE(set.CanActivate(dash) == false);
    REQUIRE(set.Cooldown(dash) == Catch::Approx(0.f));
}

TEST_CASE("Skills: a set survives a JSON round trip", "[skills]")
{
    TagRegistry& registry = TagRegistry::Instance();
    const TagId health = registry.RegisterTag("UnitTest.Skills.Save.Health");
    const TagId shield = registry.RegisterTag("UnitTest.Skills.Effect.Shield");
    const TagId dodge = registry.RegisterTag("UnitTest.Skills.Skill.Dodge");
    const TagId player = registry.RegisterTag("UnitTest.Skills.Save.Player");

    EffectDef effect;
    effect.id = shield;
    effect.duration = 5.f;
    AttributeModifier mod;
    mod.attribute = health;
    mod.op = ModifierOp::Add;
    mod.magnitude = 10.f;
    effect.modifiers.push_back(mod);
    SkillLibrary::Instance().DefineEffect(effect);

    SkillDef skill;
    skill.id = dodge;
    skill.cooldown = 4.f;
    SkillLibrary::Instance().DefineSkill(skill);

    SkillSet original;
    original.DefineAttribute(health, 80.f, 0.f, 100.f);
    original.AddPersistentTag(player);
    original.Grant(dodge);
    original.ApplyEffect(shield);
    original.Tick(1.f);
    original.Activate(dodge);

    const nlohmann::json saved = original.ToJson();

    SkillSet loaded;
    REQUIRE(loaded.FromJson(saved));
    REQUIRE(loaded.Tags().Has(player));
    REQUIRE(loaded.HasSkill(dodge));
    REQUIRE(loaded.HasEffect(shield));
    // The boosted value comes back through the recompute, not from the file.
    REQUIRE(loaded.Attributes().Current(health) == Catch::Approx(90.f));
    REQUIRE(loaded.Attributes().Base(health) == Catch::Approx(80.f));
    REQUIRE(loaded.Cooldown(dodge) > 0.f);
}

TEST_CASE("Skills: the library round trips through JSON", "[skills]")
{
    const nlohmann::json data = {
        {"effects", nlohmann::json::array({{{"id", "UnitTest.Lib.Effect.Wet"},
                                            {"duration", 6.f},
                                            {"granted", nlohmann::json::array({"UnitTest.Lib.Wet"})},
                                            {"modifiers", nlohmann::json::array({{{"attribute", "UnitTest.Lib.Speed"},
                                                                                  {"op", "multiply"},
                                                                                  {"magnitude", 0.75f}}})}}})},
        {"skills", nlohmann::json::array({{{"id", "UnitTest.Lib.Skill.Splash"},
                                           {"cooldown", 3.f},
                                           {"cost", "UnitTest.Lib.Stamina"},
                                           {"costAmount", 12.f},
                                           {"blocked", nlohmann::json::array({"UnitTest.Lib.Effect.Wet"})}}})}};

    // Tags named only in a data file are registered by the load itself.
    REQUIRE(SkillLibrary::Instance().LoadFromJson(data));

    const TagId wet = TagRegistry::Instance().Find("UnitTest.Lib.Effect.Wet");
    const TagId splash = TagRegistry::Instance().Find("UnitTest.Lib.Skill.Splash");
    REQUIRE(wet != kInvalidTag);
    REQUIRE(splash != kInvalidTag);

    const EffectDef* effect = SkillLibrary::Instance().Effect(wet);
    REQUIRE(effect != nullptr);
    REQUIRE(effect->duration == Catch::Approx(6.f));
    REQUIRE(effect->modifiers.size() == 1);
    REQUIRE(effect->modifiers[0].op == ModifierOp::Multiply);
    REQUIRE(effect->grantedTags.Has(TagRegistry::Instance().Find("UnitTest.Lib.Wet")));

    const SkillDef* skill = SkillLibrary::Instance().Skill(splash);
    REQUIRE(skill != nullptr);
    REQUIRE(skill->cooldown == Catch::Approx(3.f));
    REQUIRE(skill->cost == Catch::Approx(12.f));
    REQUIRE(skill->blocked.Has(wet));

    // A reload through toJson/loadFromJson keeps the data, and behaviour attached
    // in code survives the reload.
    SkillDef* editable = SkillLibrary::Instance().EditSkill(splash);
    REQUIRE(editable != nullptr);
    editable->onActivate = [](SkillSet&, TagId) {};

    const nlohmann::json dumped = SkillLibrary::Instance().ToJson();
    REQUIRE(SkillLibrary::Instance().LoadFromJson(dumped));
    const SkillDef* reloaded = SkillLibrary::Instance().Skill(splash);
    REQUIRE(reloaded != nullptr);
    REQUIRE(reloaded->onActivate != nullptr);
    REQUIRE(reloaded->cooldown == Catch::Approx(3.f));
}

TEST_CASE("Skills: damage rules pick a multiplier by tag", "[skills]")
{
    TagRegistry& registry = TagRegistry::Instance();
    const TagId electric = registry.RegisterTag("UnitTest.Dmg.Electric");
    const TagId physical = registry.RegisterTag("UnitTest.Dmg.Physical");
    const TagId wet = registry.RegisterTag("UnitTest.Dmg.Wet");
    const TagId armour = registry.RegisterTag("UnitTest.Dmg.Armour");

    DamageRules& rules = DamageRules::Instance();
    rules.Clear();
    rules.AddRule(electric, TagContainer(), 1.f);
    // A later, more specific rule overrides the general one.
    rules.AddRule(electric, MakeTags({"UnitTest.Dmg.Wet"}), 2.f);
    rules.AddRule(physical, MakeTags({"UnitTest.Dmg.Armour"}), 0.5f);

    TagContainer wetTarget;
    wetTarget.Add(wet);
    TagContainer dryArmoured;
    dryArmoured.Add(armour);

    REQUIRE(rules.Multiplier(electric, wetTarget) == Catch::Approx(2.f));
    REQUIRE(rules.Multiplier(electric, dryArmoured) == Catch::Approx(1.f));
    REQUIRE(rules.Multiplier(physical, dryArmoured) == Catch::Approx(0.5f));
    // Unknown type: the default, not zero.
    REQUIRE(rules.Multiplier(registry.RegisterTag("UnitTest.Dmg.Fire"), dryArmoured) == Catch::Approx(1.f));

    const nlohmann::json dumped = rules.ToJson();
    rules.Clear();
    REQUIRE(rules.LoadFromJson(dumped));
    REQUIRE(rules.Multiplier(electric, wetTarget) == Catch::Approx(2.f));

    rules.Clear();
    REQUIRE(rules.Multiplier(electric, wetTarget) == Catch::Approx(1.f));
}

TEST_CASE("Skills: the script bridge wires the tag nodes to a live set", "[skills]")
{
    TagRegistry& registry = TagRegistry::Instance();
    const TagId creature = registry.RegisterTag("UnitTest.Bridge.Creature");
    const TagId health = registry.RegisterTag("UnitTest.Bridge.Health");

    TagContainer tags;
    AttributeSet attributes;
    attributes.Define(health, 40.f, 0.f, 100.f);

    ScriptBridge::Instance().Bind(&tags, &attributes, nullptr);
    REQUIRE(ScriptBridge::Instance().Bound());

    FemboyScript script;
    std::vector<std::string> printed;
    // `print` is a language keyword handled by the interpreter itself, so the
    // test registers its own sink under a name the language does not own.
    script.RegisterFunction("record",
                            [&printed](const std::vector<FSValue>& args) -> FSValue
                            {
                                if (!args.empty())
                                {
                                    if (const std::string* text = std::get_if<std::string>(&args[0]))
                                    {
                                        printed.push_back(*text);
                                    }
                                    else if (const bool* flag = std::get_if<bool>(&args[0]))
                                    {
                                        printed.push_back(*flag ? "true" : "false");
                                    }
                                    else if (const double* number = std::get_if<double>(&args[0]))
                                    {
                                        printed.push_back(std::to_string(static_cast<int>(*number)));
                                    }
                                }
                                return FSValue{};
                            });
    ScriptBridge::Instance().Install(script);

    // The tag is the last argument and the object in front of it is ignored: a 2D
    // game means "the bound actor", so the graph keeps working unchanged.
    const auto result = script.Run("addTag(0, \"UnitTest.Bridge.Creature\");\n"
                                   "record(hasTag(0, \"UnitTest.Bridge.Creature\"));\n"
                                   "record(matchesTag(0, \"UnitTest.Bridge\"));\n"
                                   "record(attribute(0, \"UnitTest.Bridge.Health\"));\n"
                                   "setAttribute(0, \"UnitTest.Bridge.Health\", 55);\n"
                                   "record(removeTag(0, \"UnitTest.Bridge.Creature\"));\n");
    REQUIRE(result.ok);
    REQUIRE(printed.size() == 4);
    REQUIRE(printed[0] == "true");
    REQUIRE(printed[1] == "true");
    REQUIRE(printed[2] == "40");
    REQUIRE(printed[3] == "true");

    REQUIRE(tags.Has(creature) == false);
    REQUIRE(attributes.Current(health) == Catch::Approx(55.f));

    // An unbound bridge refuses instead of crashing.
    ScriptBridge::Instance().Unbind();
    REQUIRE(ScriptBridge::Instance().Bound() == false);
    REQUIRE(script.Run("record(hasTag(0, \"UnitTest.Bridge.Creature\"));").ok);
    REQUIRE(printed.back() == "false");
}

TEST_CASE("Skills: granted tags are refcounted", "[skills]")
{
    TagRegistry& registry = TagRegistry::Instance();
    const TagId shared = registry.RegisterTag("UnitTest.Ref.Tag");
    const TagId shortEffect = registry.RegisterTag("UnitTest.Ref.Effect.Short");
    const TagId longEffect = registry.RegisterTag("UnitTest.Ref.Effect.Long");

    EffectDef first;
    first.id = shortEffect;
    first.duration = 1.f;
    first.grantedTags.Add(shared);
    SkillLibrary::Instance().DefineEffect(first);

    EffectDef second;
    second.id = longEffect;
    second.duration = 5.f;
    second.grantedTags.Add(shared);
    SkillLibrary::Instance().DefineEffect(second);

    SkillSet set;
    set.ApplyEffect(shortEffect);
    set.ApplyEffect(longEffect);
    REQUIRE(set.Tags().Has(shared));

    // The short effect ends; the tag stays because the long one still grants it.
    set.Tick(1.2f);
    REQUIRE(set.HasEffect(shortEffect) == false);
    REQUIRE(set.Tags().Has(shared));

    // Removing a persistent tag cannot drop a tag an effect is still granting.
    set.AddPersistentTag(shared);
    REQUIRE(set.RemovePersistentTag(shared));
    REQUIRE(set.Tags().Has(shared));

    // Once the last grant is gone, so is the tag.
    set.Tick(4.f);
    REQUIRE(set.HasEffect(longEffect) == false);
    REQUIRE(set.Tags().Has(shared) == false);
}

TEST_CASE("Tags: a watched file reloads when its write stamp moves", "[tags]")
{
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "femboy_tags_watch_test.json";
    fs::remove(path);

    const auto write = [&path](const char* body)
    {
        std::ofstream out(path, std::ios::trunc);
        out << body;
    };

    write("{\"tags\":[\"UnitTest.Watch.First\"]}");
    TagRegistry& registry = TagRegistry::Instance();
    REQUIRE(registry.WatchFile(path.string()));
    REQUIRE(registry.Find("UnitTest.Watch.First") != kInvalidTag);

    // Nothing was written since the load, so a poll is a stat and a compare.
    REQUIRE(registry.PollReload() == false);

    // A second write can land inside the same clock tick on a coarse filesystem,
    // so the stamp is moved forward explicitly instead of sleeping.
    write("{\"tags\":[\"UnitTest.Watch.First\",\"UnitTest.Watch.Second\"]}");
    fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(5));

    REQUIRE(registry.PollReload());
    REQUIRE(registry.Find("UnitTest.Watch.Second") != kInvalidTag);

    // The same stamp again: no reload, no work.
    REQUIRE(registry.PollReload() == false);

    registry.UnwatchFile();
    REQUIRE(registry.PollReload() == false);

    fs::remove(path);
}

TEST_CASE("PinkUI containers lay out flow, size, switch and scroll", "[ui]")
{
    PinkUI& ui = PinkUI::Instance();
    ui.SetScale(1.f);

    SECTION("vertical flow shares the leftover space")
    {
        auto root = std::make_shared<UIWidget>();
        root->type = "VBox";
        auto fixed = std::make_shared<UIWidget>();
        fixed->type = "Text";
        fixed->rect = sf::FloatRect({0.f, 0.f}, {100.f, 50.f});
        auto flexible = std::make_shared<UIWidget>();
        flexible->type = "Text";
        root->children = {fixed, flexible};

        ui.UpdateLayout(root, sf::FloatRect({0.f, 0.f}, {400.f, 300.f}));
        REQUIRE(fixed->screenRect.size.y == Catch::Approx(50.f));
        REQUIRE(flexible->screenRect.position.y == Catch::Approx(50.f));
        REQUIRE(flexible->screenRect.size.y == Catch::Approx(250.f));
    }

    SECTION("horizontal flow splits evenly")
    {
        auto root = std::make_shared<UIWidget>();
        root->type = "HBox";
        std::vector<std::shared_ptr<UIWidget>> kids;
        for (int i = 0; i < 3; ++i)
        {
            auto child = std::make_shared<UIWidget>();
            child->type = "Text";
            kids.push_back(child);
        }
        root->children = kids;

        ui.UpdateLayout(root, sf::FloatRect({0.f, 0.f}, {300.f, 100.f}));
        REQUIRE(kids[1]->screenRect.position.x == Catch::Approx(100.f));
        REQUIRE(kids[2]->screenRect.size.x == Catch::Approx(100.f));
    }

    SECTION("SizeBox keeps its own size and its child fills it")
    {
        auto root = std::make_shared<UIWidget>();
        root->type = "Canvas";
        auto box = std::make_shared<UIWidget>();
        box->type = "SizeBox";
        box->rect = sf::FloatRect({10.f, 20.f}, {120.f, 60.f});
        auto inner = std::make_shared<UIWidget>();
        inner->type = "Text";
        box->children = {inner};
        root->children = {box};

        ui.UpdateLayout(root, sf::FloatRect({0.f, 0.f}, {800.f, 600.f}));
        REQUIRE(box->screenRect.size.x == Catch::Approx(120.f));
        REQUIRE(inner->screenRect.size.x == Catch::Approx(120.f));
        REQUIRE(inner->screenRect.size.y == Catch::Approx(60.f));
    }

    SECTION("WidgetSwitcher lays out only the active child")
    {
        auto root = std::make_shared<UIWidget>();
        root->type = "WidgetSwitcher";
        root->id = "switch";
        auto first = std::make_shared<UIWidget>();
        first->type = "Text";
        auto second = std::make_shared<UIWidget>();
        second->type = "Text";
        root->children = {first, second};

        ui.UpdateLayout(root, sf::FloatRect({0.f, 0.f}, {200.f, 100.f}));
        REQUIRE(first->screenRect.size.x == Catch::Approx(200.f));
        REQUIRE(second->screenRect.size.x == Catch::Approx(0.f));

        REQUIRE(ui.SetSwitch(root, "switch", 1));
        REQUIRE(second->screenRect.size.x == Catch::Approx(200.f));
    }

    SECTION("ScrollBox clamps its offset to the content")
    {
        auto root = std::make_shared<UIWidget>();
        root->type = "ScrollBox";
        root->id = "list";
        root->props["itemHeight"] = 100.f;
        for (int i = 0; i < 10; ++i)
        {
            auto child = std::make_shared<UIWidget>();
            child->type = "Text";
            root->children.push_back(child);
        }

        ui.UpdateLayout(root, sf::FloatRect({0.f, 0.f}, {400.f, 300.f}));
        REQUIRE(root->children.front()->screenRect.position.y == Catch::Approx(0.f));

        REQUIRE(ui.ScrollBy(root, "list", 1000.f));
        // Ten 100px rows in a 300px viewport leave 700px of travel.
        REQUIRE(root->scroll == Catch::Approx(700.f));
        REQUIRE(root->children.front()->screenRect.position.y == Catch::Approx(-700.f));

        REQUIRE(ui.ScrollTo(root, "list", 0.f));
        REQUIRE(root->children.front()->screenRect.position.y == Catch::Approx(0.f));
    }

    ui.SetScale(1.f);
}

TEST_CASE("Rope keeps its length and hangs under gravity", "[physics]")
{
    Rope rope;
    rope.Reset({0.f, 0.f}, {100.f, 0.f}, 10, 1.f);
    rope.Pin(0);

    for (int i = 0; i < 240; ++i)
    {
        rope.Update(1.f / 60.f);
    }

    REQUIRE(rope.Point(0).y == Catch::Approx(0.f).margin(0.5f));
    REQUIRE(rope.Point(0).x == Catch::Approx(0.f).margin(0.5f));
    REQUIRE(rope.Point(10).y > 40.f);

    float total = 0.f;
    for (int i = 0; i < rope.SegmentCount(); ++i)
    {
        const sf::Vector2f a = rope.Point(i);
        const sf::Vector2f b = rope.Point(i + 1);
        total += std::hypot(b.x - a.x, b.y - a.y);
    }
    REQUIRE(total == Catch::Approx(100.f).epsilon(0.05f));

    // An un-pinned rope has no anchor at all and falls as one piece.
    Rope free;
    free.Reset({0.f, 0.f}, {50.f, 0.f}, 5, 1.f);
    for (int i = 0; i < 60; ++i)
    {
        free.Update(1.f / 60.f);
    }
    REQUIRE(free.Point(0).y > 5.f);
}

TEST_CASE("Cloth keeps pinned points fixed and sags below them", "[physics]")
{
    Cloth cloth;
    cloth.Reset({0.f, 0.f}, 8, 6, 10.f);
    cloth.PinRow(0);

    for (int i = 0; i < 240; ++i)
    {
        cloth.Update(1.f / 60.f);
    }

    REQUIRE(cloth.Point(0, 0).x == Catch::Approx(0.f).margin(0.5f));
    REQUIRE(cloth.Point(0, 0).y == Catch::Approx(0.f).margin(0.5f));
    REQUIRE(cloth.Point(7, 0).x == Catch::Approx(70.f).margin(1.f));
    REQUIRE(cloth.Point(4, 5).y > 30.f);
    REQUIRE(cloth.Pinned(4, 0));
    REQUIRE_FALSE(cloth.Pinned(4, 5));
}

TEST_CASE("Wind: gusts sway the direction and never reverse the base flow", "[wind]")
{
    GustWind::Settings settings;
    settings.direction = {1.f, 0.f};
    settings.speed = 200.f;
    settings.gustStrength = 0.f;
    settings.swayDegrees = 30.f;
    settings.swayPeriod = 4.f;
    GustWind wind(settings);

    const WindSample atStart = wind.Sample({0.f, 0.f}, 0.f);
    REQUIRE(atStart.Speed() == Catch::Approx(200.f).margin(1.f));
    REQUIRE(atStart.velocity.x > 0.f);

    // A quarter period later the sway has turned the flow by 30 degrees.
    const WindSample quarter = wind.Sample({0.f, 0.f}, 1.f);
    REQUIRE(quarter.velocity.x > 0.f);
    REQUIRE(std::abs(quarter.velocity.y) > 20.f);

    // Half a period later the sway is on the other side: the "side to side"
    // motion a hurricane has, not a constant diagonal.
    const WindSample half = wind.Sample({0.f, 0.f}, 2.f);
    REQUIRE(half.velocity.y * quarter.velocity.y < 0.f);
}

TEST_CASE("Wind: a vortex turns around its core and the hurricane eye is calm", "[wind]")
{
    VortexWind::Settings v;
    v.center = {0.f, 0.f};
    v.strength = 300.f;
    v.coreRadius = 100.f;
    v.outerRadius = 800.f;
    v.inward = 0.f;
    v.drift = {0.f, 0.f};
    VortexWind vortex(v);

    const WindSample east = vortex.Sample({50.f, 0.f}, 0.f);
    REQUIRE(east.velocity.x == Catch::Approx(0.f).margin(1.f));
    REQUIRE(east.velocity.y > 100.f); // counter-clockwise

    const WindSample far = vortex.Sample({400.f, 0.f}, 0.f);
    REQUIRE(far.Speed() < east.Speed());

    HurricaneWind::Settings h;
    h.center = {0.f, 0.f};
    h.drift = {0.f, 0.f};
    h.eyeRadius = 100.f;
    h.eyewallRadius = 250.f;
    h.maxSpeed = 400.f;
    HurricaneWind hurricane(h);

    REQUIRE(hurricane.Sample({0.f, 0.f}, 0.f).Speed() < 20.f);
    REQUIRE(hurricane.Sample({250.f, 0.f}, 0.f).Speed() > hurricane.Sample({600.f, 0.f}, 0.f).Speed());
}

TEST_CASE("Wind: zones fade out and composites add their layers", "[wind]")
{
    auto inner = std::make_shared<ConstantWind>(sf::Vector2f{1.f, 0.f}, 100.f);
    WindZone zone(sf::FloatRect({0.f, 0.f}, {200.f, 200.f}), inner, 50.f);

    REQUIRE(zone.Sample({100.f, 100.f}, 0.f).Speed() == Catch::Approx(100.f));
    REQUIRE(zone.Sample({1000.f, 1000.f}, 0.f).Speed() == Catch::Approx(0.f));
    REQUIRE(zone.Sample({225.f, 100.f}, 0.f).Speed() < 100.f);

    CompositeWind composite;
    composite.Add(inner);
    composite.Add(std::make_shared<ConstantWind>(sf::Vector2f{0.f, 1.f}, 50.f));
    const WindSample sum = composite.Sample({0.f, 0.f}, 0.f);
    REQUIRE(sum.velocity.x == Catch::Approx(100.f).margin(1.f));
    REQUIRE(sum.velocity.y == Catch::Approx(50.f).margin(1.f));
}

TEST_CASE("Weather presets scale from calm to hurricane", "[wind]")
{
    const Weather calm = MakeWeather(WeatherKind::Calm);
    const Weather breeze = MakeWeather(WeatherKind::Breeze);
    const Weather storm = MakeWeather(WeatherKind::Storm);
    const Weather hurricane = MakeWeather(WeatherKind::Hurricane);

    REQUIRE(calm.wind->Sample({0.f, 0.f}, 0.f).Speed() == Catch::Approx(0.f));
    REQUIRE(breeze.wind->Sample({0.f, 0.f}, 0.f).Speed() > 0.f);
    REQUIRE(storm.wind->Sample({0.f, 0.f}, 0.f).Speed() > breeze.wind->Sample({0.f, 0.f}, 0.f).Speed());
    REQUIRE(hurricane.rain > storm.rain);
    REQUIRE(hurricane.wind->Sample({-80.f, 0.f}, 1.f).Speed() > 0.f);
    REQUIRE(std::string(WeatherName(WeatherKind::Tornado)) == "Tornado");
}

TEST_CASE("Rope: wind pushes it sideways, bending stiffens it and links can snap", "[physics][wind]")
{
    ConstantWind wind(sf::Vector2f{1.f, 0.f}, 300.f);

    Rope rope;
    rope.Reset({0.f, 0.f}, {0.f, 200.f}, 10, 1.f);
    rope.Pin(0);
    rope.SetWindField(&wind);

    for (int i = 0; i < 120; ++i)
    {
        rope.Update(1.f / 60.f);
    }

    REQUIRE(rope.Point(10).x > 10.f);

    // A stiff chain resists folding: its tip stays closer to the anchor line.
    Rope chain;
    chain.Reset({0.f, 0.f}, {0.f, 80.f}, 4, 1.f);
    chain.Pin(0);
    chain.SetWindField(&wind);
    Rope::Settings stiff = chain.GetSettings();
    stiff.bendStiffness = 0.9f;
    chain.SetSettings(stiff);
    for (int i = 0; i < 60; ++i)
    {
        chain.Update(1.f / 60.f);
    }
    REQUIRE(chain.Point(4).x < rope.Point(10).x);

    Rope breaking;
    breaking.Reset({0.f, 0.f}, {0.f, 100.f}, 4, 1.f);
    breaking.Pin(0);
    breaking.Pin(4);
    breaking.MovePin(4, {400.f, 100.f}); // instantly impossible stretch
    Rope::Settings snap = breaking.GetSettings();
    snap.breakStretch = 1.2f;
    snap.stiffness = 0.f;
    breaking.SetSettings(snap);
    for (int i = 0; i < 5; ++i)
    {
        breaking.Update(1.f / 60.f);
    }
    REQUIRE(breaking.BrokenLinks() > 0);
}

TEST_CASE("Cloth: wind inflates it and tearing removes links", "[physics][wind]")
{
    ConstantWind wind(sf::Vector2f{1.f, 0.f}, 400.f);

    Cloth cloth;
    cloth.Reset({0.f, 0.f}, 8, 6, 10.f);
    cloth.PinRow(0);
    cloth.SetWindField(&wind);
    Cloth::Settings settings = cloth.GetSettings();
    settings.tearStretch = 1.15f;
    cloth.SetSettings(settings);

    for (int i = 0; i < 120; ++i)
    {
        cloth.Update(1.f / 60.f);
    }

    REQUIRE(cloth.Point(7, 5).x > 70.f);

    // A hard, instantaneous pull tears the sheet; the count is stable because a
    // broken link is never solved again.
    Cloth torn;
    torn.Reset({0.f, 0.f}, 6, 6, 10.f);
    torn.PinRow(0);
    torn.Pin(5, 5);
    Cloth::Settings tearSettings = torn.GetSettings();
    tearSettings.tearStretch = 1.05f;
    torn.SetSettings(tearSettings);
    torn.MovePin(5, 5, {400.f, 400.f});
    for (int i = 0; i < 5; ++i)
    {
        torn.Update(1.f / 60.f);
    }
    REQUIRE(torn.TornLinks() > 0);
}

TEST_CASE("Ribbon: rest twist lays out a helix and wind torque spins it", "[physics][wind]")
{
    Ribbon ribbon;
    ribbon.ResetHelix({0.f, 0.f}, 40.f, 120.f, 48, 10.f, 3.f);

    const sf::Vector2f first = ribbon.Point(0);
    const sf::Vector2f last = ribbon.Point(48);
    REQUIRE(first.x == Catch::Approx(40.f).margin(0.5f));
    REQUIRE(last.x == Catch::Approx(40.f).margin(0.5f));
    REQUIRE(last.y > first.y);
    REQUIRE(std::abs(ribbon.Twist(48) - ribbon.Twist(0)) > 6.f);

    Ribbon strip;
    strip.Reset({0.f, 0.f}, {0.f, 200.f}, 10, 12.f);
    strip.Pin(0);
    strip.SetRestTwist(0.f);
    const float before = strip.Twist(5);
    for (int i = 0; i < 30; ++i)
    {
        strip.Update(1.f / 60.f);
    }
    REQUIRE(strip.Twist(5) == Catch::Approx(before).margin(0.01f));

    ConstantWind wind(sf::Vector2f{1.f, 0.f}, 300.f);
    strip.SetWindField(&wind);
    for (int i = 0; i < 60; ++i)
    {
        strip.Update(1.f / 60.f);
    }
    REQUIRE(std::abs(strip.Twist(5)) > 0.001f);
}

TEST_CASE("StateTree: JSON trees transition on events and conditions", "[statetree]")
{
    StateTree tree;
    int enteredIdle = 0;
    int enteredChase = 0;

    tree.RegisterAction("idle", [](float, void*) { return STStatus::Succeeded; });
    tree.RegisterAction("chase", [](float, void*) { return STStatus::Running; });
    tree.RegisterAction("enterIdle",
                        [&enteredIdle](float, void*)
                        {
                            ++enteredIdle;
                            return STStatus::Succeeded;
                        });
    tree.RegisterAction("enterChase",
                        [&enteredChase](float, void*)
                        {
                            ++enteredChase;
                            return STStatus::Succeeded;
                        });

    const nlohmann::json treeJson = {
        {"root", "Idle"},
        {"states",
         nlohmann::json::array({
             {{"name", "Idle"},
              {"actions", {"idle"}},
              {"transitions", nlohmann::json::array({{{"to", "Chase"}, {"event", "spotted"}, {"priority", 2}}})}},
             {{"name", "Chase"},
              {"actions", {"chase"}},
              {"transitions", nlohmann::json::array({{{"to", "Idle"}, {"event", "lost"}}})}},
         })}};

    REQUIRE(tree.LoadFromJson(treeJson));
    REQUIRE(tree.CurrentState() == "Idle");

    tree.Tick(1.f / 60.f);
    REQUIRE(tree.CurrentState() == "Idle");

    tree.SendEvent("spotted");
    tree.Tick(1.f / 60.f);
    REQUIRE(tree.CurrentState() == "Chase");

    // Events are one-shot: the next tick without sendEvent keeps the state.
    tree.Tick(1.f / 60.f);
    REQUIRE(tree.CurrentState() == "Chase");

    tree.SendEvent("lost");
    tree.Tick(1.f / 60.f);
    REQUIRE(tree.CurrentState() == "Idle");
    REQUIRE(tree.StateHistory().size() >= 3);
}

// Benchmarks. Hidden tag ("[.]") so the normal suite stays fast; run them with
//   femboy_tests.exe "[benchmark]"
// to see the numbers the threading and SIMD work is justified by.
TEST_CASE("Benchmark: job pool speedup", "[.][benchmark][jobs]")
{
    JobSystem& jobs = JobSystem::Instance();
    jobs.Initialize();
    const size_t workers = jobs.ThreadCount();

    constexpr size_t kItems = 120000;
    std::vector<float> serialResult(kItems);
    std::vector<float> parallelResult(kItems);

    const auto work = [](float seed)
    {
        float x = seed;
        for (int k = 0; k < 96; ++k)
        {
            x = std::sin(x) * 1.0001f + 0.5f;
        }
        return x;
    };

    const auto measure = [](auto&& body)
    {
        const auto start = std::chrono::steady_clock::now();
        body();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };

    const auto runSerial = [&]
    {
        for (size_t i = 0; i < kItems; ++i)
        {
            serialResult[i] = work(static_cast<float>(i) * 0.001f);
        }
    };
    const auto runParallel = [&]
    { jobs.ParallelFor(0, kItems, [&](size_t i) { parallelResult[i] = work(static_cast<float>(i) * 0.001f); }, 256); };

    runSerial();
    runParallel();
    const double serialMs = measure(runSerial);
    const double parallelMs = measure(runParallel);

    for (size_t i = 0; i < kItems; ++i)
    {
        REQUIRE(parallelResult[i] == Catch::Approx(serialResult[i]));
    }

    WARN("job pool: " << workers << " workers, " << kItems << " items (" << kItems * 96 << " sin ops): serial "
                      << serialMs << " ms, parallel " << parallelMs << " ms, speedup " << serialMs / parallelMs << "x");

    jobs.Shutdown();
}

TEST_CASE("Benchmark: sprite corner rotation, scalar vs SIMD", "[.][benchmark][simd]")
{
    constexpr int kSprites = 500000;
    std::vector<sf::Vector2f> corners(static_cast<size_t>(kSprites) * 4);
    for (int i = 0; i < kSprites; ++i)
    {
        const float x = static_cast<float>(i % 1000) * 3.f;
        const float y = static_cast<float>(i / 1000) * 3.f;
        corners[static_cast<size_t>(i) * 4 + 0] = {x, y};
        corners[static_cast<size_t>(i) * 4 + 1] = {x + 32.f, y};
        corners[static_cast<size_t>(i) * 4 + 2] = {x + 32.f, y + 32.f};
        corners[static_cast<size_t>(i) * 4 + 3] = {x, y + 32.f};
    }

    const sf::Vector2f pivot{100.f, 100.f};
    const float radians = 0.37f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);

    auto scalar = corners;
    auto vector = corners;

    const auto measure = [](auto&& body)
    {
        const auto start = std::chrono::steady_clock::now();
        body();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };

    const double scalarMs = measure(
        [&]
        {
            for (int i = 0; i < kSprites; ++i)
            {
                for (int k = 0; k < 4; ++k)
                {
                    sf::Vector2f& p = scalar[static_cast<size_t>(i) * 4 + static_cast<size_t>(k)];
                    const float dx = p.x - pivot.x;
                    const float dy = p.y - pivot.y;
                    p = {pivot.x + dx * cosine - dy * sine, pivot.y + dx * sine + dy * cosine};
                }
            }
        });

    const double vectorMs = measure(
        [&]
        {
            for (int i = 0; i < kSprites; ++i)
            {
                Simd::RotatePoints4(&vector[static_cast<size_t>(i) * 4], pivot, cosine, sine);
            }
        });

    for (size_t i = 0; i < vector.size(); ++i)
    {
        REQUIRE(vector[i].x == Catch::Approx(scalar[i].x).margin(0.001f));
        REQUIRE(vector[i].y == Catch::Approx(scalar[i].y).margin(0.001f));
    }

    WARN("sprite rotation, " << kSprites << " sprites: scalar " << scalarMs << " ms, SIMD " << vectorMs
                             << " ms, speedup " << scalarMs / vectorMs << "x");
}

TEST_CASE("Benchmark: particle update, inline vs job pool", "[.][benchmark][jobs]")
{
    JobSystem& jobs = JobSystem::Instance();
    jobs.Initialize();
    const size_t workers = jobs.ThreadCount();
    Sparkles& sparkles = Sparkles::Instance();

    const auto run = [&sparkles](bool threaded, int count)
    {
        EmitterConfig cfg;
        cfg.spawnRate = 0.f;
        cfg.burstCount = count;
        cfg.lifeMin = 1000.f;
        cfg.lifeMax = 1000.f;
        cfg.velocityMin = {0.f, 0.f};
        cfg.velocityMax = {0.f, 0.f};
        cfg.gravity = 0.f;
        cfg.sizeMin = 2.f;
        cfg.sizeMax = 2.f;
        cfg.sizeEnd = 2.f;
        sparkles.CreateEmitter(cfg);
        sparkles.SetMultithreaded(threaded);

        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 60; ++frame)
        {
            sparkles.Update(1.f / 60.f);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        sparkles.Clear();
        return ms;
    };

    for (const int count : {20000, 100000})
    {
        const double inlineMs = run(false, count);
        const double poolMs = run(true, count);
        WARN("particles x" << count << ", 60 frames (" << workers << " workers): inline " << inlineMs
                           << " ms, job pool " << poolMs << " ms, speedup " << inlineMs / poolMs << "x");
    }

    sparkles.SetMultithreaded(true);
    jobs.Shutdown();
}

TEST_CASE("RichText: tags split styled runs and strip cleanly", "[richtext]")
{
    const RichText parsed = ParseRichText("Hello {b}bold{/b} {color=#ff0000}red{/color}", TextStyle{});
    REQUIRE(parsed.hasMarkup);
    REQUIRE(parsed.runs.size() == 4);
    REQUIRE(parsed.runs[1].style.bold);
    REQUIRE_FALSE(parsed.runs[0].style.bold);
    REQUIRE(parsed.runs[3].style.color == sf::Color(255, 0, 0, 255));
    REQUIRE(parsed.plain == "Hello bold red");
    REQUIRE(StripRichText("a {b}b{/b}") == "a b");
}

TEST_CASE("RichText: doubled braces are literal and unknown tags survive", "[richtext]")
{
    REQUIRE(StripRichText("{{x}}") == "{x}");
    REQUIRE(StripRichText("{n} not a tag") == "{n} not a tag");
    REQUIRE_FALSE(HasRichTextMarkup("plain"));
    REQUIRE(HasRichTextMarkup("{i}yes{/i}"));
}

TEST_CASE("RichText: nested styles restore the outer style", "[richtext]")
{
    const RichText parsed = ParseRichText("{color=#00ff00}green {b}bold{/b} green again{/color}", TextStyle{});
    REQUIRE(parsed.runs.size() == 3);
    REQUIRE(parsed.runs[0].style.color == sf::Color(0, 255, 0, 255));
    REQUIRE(parsed.runs[1].style.bold);
    REQUIRE_FALSE(parsed.runs[2].style.bold);
    REQUIRE(parsed.runs[2].style.color == sf::Color(0, 255, 0, 255));
}

TEST_CASE("AntiAliasing: names, sample counts and indices agree", "[antialiasing]")
{
    REQUIRE(std::string(AntiAliasingName(AntiAliasingMode::Off)) == "Off");
    REQUIRE(AntiAliasingSampleCount(AntiAliasingMode::Msaa4) == 4);
    REQUIRE(AntiAliasingSampleCount(AntiAliasingMode::Fxaa) == 0);
    REQUIRE(AntiAliasingFromIndex(1) == AntiAliasingMode::Fxaa);
    REQUIRE(AntiAliasingFromIndex(-1) == AntiAliasingMode::Off);
    REQUIRE(AntiAliasingFromIndex(99) == AntiAliasingMode::Off);
}

TEST_CASE("RotationAtlas: angle index wraps and snaps to the nearest frame", "[atlas]")
{
    REQUIRE(RotationAtlas::FrameIndex(0.f, 16) == 0);
    REQUIRE(RotationAtlas::FrameIndex(359.f, 16) == 0);
    REQUIRE(RotationAtlas::FrameIndex(-1.f, 16) == 0);
    REQUIRE(RotationAtlas::FrameIndex(180.f, 8) == 4);
    REQUIRE(RotationAtlas::FrameIndex(23.f, 8) == 1);
}

TEST_CASE("Macros: array count and stringify are usable in constant expressions", "[macros]")
{
    const int values[] = {1, 2, 3, 4};
    STATIC_REQUIRE(FB_ARRAY_COUNT(values) == 4);
    REQUIRE(std::string(FB_STRINGIFY(values)) == "values");

    int branch = 0;
    if (true)
    {
        FB_LIKELY
        {
            branch = 1;
        }
    }
    REQUIRE(branch == 1);
}

TEST_CASE("NativeModule: reports the platform extension and refuses a late reload", "[hotreload]")
{
    NativeModule module;
    REQUIRE(NativeModule::PlatformExtension() == ".dll");
    REQUIRE_FALSE(module.Loaded());
    REQUIRE_FALSE(module.Reload());
    REQUIRE_FALSE(module.LastError().empty());
    module.Unload();
}

TEST_CASE("Visibility: raycast hits a wall and blocks line of sight", "[visibility]")
{
    VisibilityWorld world;
    world.AddSegment({100.f, -100.f}, {100.f, 100.f}, 1.f, 7);
    world.Build();
    REQUIRE(world.SegmentCount() == 1);

    const RayHit hit = world.Raycast({0.f, 0.f}, {1.f, 0.f}, 1000.f);
    REQUIRE(hit.hit);
    REQUIRE(hit.distance == Catch::Approx(100.f).margin(0.01f));
    REQUIRE(hit.material == 7);

    REQUIRE_FALSE(world.LineOfSight({0.f, 0.f}, {200.f, 0.f}));
    REQUIRE(world.LineOfSight({0.f, 0.f}, {50.f, 0.f}));

    const std::vector<sf::Vector2f> polygon = world.VisibilityPolygon({0.f, 0.f}, 150.f);
    REQUIRE(polygon.size() >= 3);
}

TEST_CASE("SDF: distance grows away from a wall and AO darkens near it", "[sdf]")
{
    DistanceField field;
    const bool built = field.Build(32, 32, 10.f, [](int x, int) { return x < 2; });
    REQUIRE(built);
    REQUIRE(field.SolidAt(5.f, 50.f));
    REQUIRE_FALSE(field.SolidAt(55.f, 50.f));
    REQUIRE(field.DistanceAt(65.f, 50.f) > field.DistanceAt(25.f, 50.f));
    REQUIRE(field.AoAt(85.f, 50.f, 64.f) > field.AoAt(25.f, 50.f, 64.f));
}

TEST_CASE("AudioOcclusion: a wall between listener and source muffles it", "[audio][occlusion]")
{
    VisibilityWorld visibility;
    visibility.AddSegment({100.f, -50.f}, {100.f, 50.f}, 1.f, 3);

    AudioOcclusion& occlusion = AudioOcclusion::Instance();
    occlusion.SetVisibility(&visibility);
    occlusion.SetListener({0.f, 0.f});
    occlusion.SetMaxDistance(1000.f);

    const OcclusionResult clear = occlusion.Compute({-50.f, 0.f});
    REQUIRE_FALSE(clear.blocked);

    const OcclusionResult blocked = occlusion.Compute({200.f, 0.f});
    REQUIRE(blocked.blocked);
    REQUIRE(blocked.gain < clear.gain);
    REQUIRE(blocked.pitch < 1.f);

    occlusion.SetVisibility(nullptr);
}

TEST_CASE("WFC: the solver only places neighbours the rules allow", "[wfc]")
{
    Wfc wfc;
    wfc.SetSize(12, 12);
    REQUIRE(wfc.AddTile(0, 1.f));
    REQUIRE(wfc.AddTile(1, 1.f));
    wfc.AddPair(0, 0);
    wfc.AddPair(1, 1);

    REQUIRE(wfc.Solve(4242, 4));
    for (int y = 0; y < wfc.Height(); ++y)
    {
        for (int x = 0; x < wfc.Width(); ++x)
        {
            if (x + 1 < wfc.Width())
            {
                REQUIRE(wfc.At(x, y) == wfc.At(x + 1, y));
            }
            if (y + 1 < wfc.Height())
            {
                REQUIRE(wfc.At(x, y) == wfc.At(x, y + 1));
            }
        }
    }
}

TEST_CASE("WFC: an empty rule set with the open fallback disabled fails", "[wfc]")
{
    Wfc wfc;
    wfc.SetSize(4, 4);
    REQUIRE(wfc.AddTile(0));
    REQUIRE(wfc.AddTile(1));
    wfc.SetOpenWhenNoRules(false);
    REQUIRE_FALSE(wfc.Solve(1, 1));
}

TEST_CASE("WFC: rules survive a JSON round trip", "[wfc]")
{
    Wfc wfc;
    wfc.SetSize(8, 8);
    wfc.AddTile(0, 2.f);
    wfc.AddTile(3, 1.f);
    wfc.AddPair(0, 3);

    Wfc restored;
    REQUIRE(restored.FromJson(wfc.ToJson()));
    REQUIRE(restored.TileCount() == 4);
    REQUIRE(restored.Solve(7, 4));
    REQUIRE(restored.Grid().size() == 64);
}

TEST_CASE("Rollback: a late input rewinds and re-simulates the world", "[rollback]")
{
    int state = 0;
    RollbackSession session;
    session.Configure(2, 0, 8, 0);
    session.SetCallbacks(
        [&state](std::uint32_t, const std::vector<PlayerInput>& inputs)
        {
            for (const PlayerInput& input : inputs)
            {
                state += static_cast<int>(input.input.buttons);
            }
        },
        [&state]()
        {
            std::vector<std::uint8_t> bytes(sizeof(int));
            std::memcpy(bytes.data(), &state, sizeof(int));
            return bytes;
        },
        [&state](const std::vector<std::uint8_t>& bytes)
        {
            if (bytes.size() < sizeof(int))
            {
                return false;
            }
            std::memcpy(&state, bytes.data(), sizeof(int));
            return true;
        },
        [&state]() { return static_cast<std::uint64_t>(state); });

    for (int frame = 1; frame <= 3; ++frame)
    {
        session.SubmitLocalInput(1);
        REQUIRE(session.Advance());
    }
    REQUIRE(state == 3); // local input only; remote input is predicted as zero

    for (int frame = 1; frame <= 3; ++frame)
    {
        InputFrame input;
        input.frame = static_cast<std::uint32_t>(frame);
        input.buttons = 10;
        session.ReceiveRemoteInput(1, input);
    }

    REQUIRE(state == 33);
    REQUIRE(session.LastChecksum() == 33);
    REQUIRE(session.ResimulatedFrames() >= 3);
}

TEST_CASE("ChunkedTilemap: tiles round trip, streaming generates and unloads", "[chunked]")
{
    ChunkedTilemap map;
    map.Configure(8, 16);
    REQUIRE(map.ChunkSize() == 8);
    REQUIRE(map.TileSize() == 16);

    map.SetTile(3, 2, 5, true);
    map.SetTile(20, 20, 7, false);
    REQUIRE(map.TileAt(3, 2).id == 5);
    REQUIRE(map.IsSolid(3, 2));
    REQUIRE(map.TileAt(20, 20).id == 7);
    REQUIRE(map.TileAt(-1, -1).id == -1);

    map.SetGenerator(
        [](TileChunk& chunk)
        {
            for (int y = 0; y < chunk.size; ++y)
            {
                for (int x = 0; x < chunk.size; ++x)
                {
                    chunk.At(x, y).id = 1;
                }
            }
        });
    map.UnloadAll();

    // Ensure() runs the generator inline; StreamAsync() only schedules, so
    // asserting the generated tile uses Ensure() and the scheduling behaviour is
    // checked separately.
    map.Ensure(ChunkCoord{0, 0});
    REQUIRE(map.Find(ChunkCoord{0, 0}) != nullptr);
    REQUIRE(map.TileAt(1, 1).id == 1);

    const int scheduled = map.StreamAsync(sf::FloatRect({0.f, 0.f}, {200.f, 32.f}), 4, 0, 1);
    REQUIRE(scheduled >= 1);

    map.SetWorldTileBounds(64, 64);
    REQUIRE(map.HasWorldBounds());
    REQUIRE(map.WorldWidth() == 64);

    map.SetTile(5, 5, 9);
    ChunkedTilemap target;
    target.Configure(8, 16);
    REQUIRE(target.FromJson(map.ToJson()));
    REQUIRE(target.TileAt(5, 5).id == 9);
}

TEST_CASE("Sockets: an attachment follows the parent offset and rotation", "[sockets]")
{
    Registry registry;
    const Entity parent = registry.create();
    const Entity child = registry.create();
    registry.emplace<TransformComponent>(parent);
    registry.emplace<TransformComponent>(child);

    SocketHost& host = registry.emplace<SocketHost>(parent);
    host.Add("muzzle", {10.f, 0.f});

    REQUIRE(SocketSystem::Attach(registry, child, parent, "muzzle"));
    SocketSystem::Update(registry);

    TransformComponent& childTransform = registry.get<TransformComponent>(child);
    REQUIRE(childTransform.x == Catch::Approx(10.f));
    REQUIRE(childTransform.y == Catch::Approx(0.f).margin(0.01f));

    registry.get<TransformComponent>(parent).rotation = 90.f;
    SocketSystem::Update(registry);
    REQUIRE(childTransform.x == Catch::Approx(0.f).margin(0.01f));
    REQUIRE(childTransform.y == Catch::Approx(10.f).margin(0.01f));
    REQUIRE(registry.get<SocketAttachment>(child).resolved);

    const SocketWorld missing = SocketSystem::Evaluate(registry, parent, "does-not-exist");
    REQUIRE(missing.position.x == Catch::Approx(0.f));
    REQUIRE(missing.position.y == Catch::Approx(0.f));

    SocketSystem::Detach(registry, child);
    REQUIRE_FALSE(registry.all_of<SocketAttachment>(child));
}

TEST_CASE("Lighting: the CPU lightmap brightens near a light and keeps AO in mind", "[lighting]")
{
    LightingSystem lighting;
    lighting.SetResolution(1.f);
    lighting.SetAmbient(sf::Color(20, 20, 20));
    lighting.SetAmbientOcclusion(0.f);

    Light2D light;
    light.position = {100.f, 100.f};
    light.radius = 120.f;
    light.color = sf::Color::White;
    light.intensity = 1.f;
    light.castsShadows = false;
    lighting.AddLight(light);

    const sf::FloatRect bounds({0.f, 0.f}, {200.f, 200.f});
    REQUIRE(lighting.Update(0.f, bounds));
    REQUIRE(lighting.Valid());

    const sf::Color centre = lighting.SampleAt({100.f, 100.f});
    const sf::Color corner = lighting.SampleAt({10.f, 10.f});
    REQUIRE(centre.r > corner.r);
    REQUIRE(corner.r >= 20); // ambient is never darkened when AO is off
}

TEST_CASE("Physics: a revolute hinge pins two bodies at a fixed distance", "[physics][joints]")
{
    PhysicsWorld world;
    REQUIRE(world.Available());

    Registry registry;
    PhysicsSystem physics;
    const Entity left = registry.create();
    const Entity right = registry.create();
    TransformComponent& leftTransform = registry.emplace<TransformComponent>(left);
    leftTransform.x = 100.f;
    leftTransform.y = 100.f;
    TransformComponent& rightTransform = registry.emplace<TransformComponent>(right);
    rightTransform.x = 200.f;
    rightTransform.y = 100.f;

    REQUIRE(physics.CreateBody(registry, world, left, 16.f, 16.f));
    REQUIRE(physics.CreateBody(registry, world, right, 16.f, 16.f));

    JointHandle joint = world.CreateRevoluteJoint(registry.get<PhysicsBodyComponent>(left).bodyId,
                                                  registry.get<PhysicsBodyComponent>(right).bodyId, {150.f, 100.f});
    REQUIRE(joint.Valid());

    for (int step = 0; step < 60; ++step)
    {
        world.Step(1.f / 60.f);
        physics.Update(registry, world, 1.f / 60.f);
    }

    const TransformComponent& a = registry.get<TransformComponent>(left);
    const TransformComponent& b = registry.get<TransformComponent>(right);
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    REQUIRE(std::sqrt(dx * dx + dy * dy) == Catch::Approx(100.f).margin(6.f));

    world.DestroyJoint(joint);
    REQUIRE_FALSE(joint.Valid());
}

TEST_CASE("Physics: Box2D worker count uses the built-in scheduler", "[physics][threads]")
{
    PhysicsWorld world;
    REQUIRE(world.Available());
    REQUIRE(world.WorkerCount() == 1);

    // Box2D reports 1 for its serial path, including when the caller asks for 0.
    world.SetWorkerCount(0);
    REQUIRE(world.WorkerCount() == 1);

    world.SetWorkerCount(3);
    REQUIRE(world.WorkerCount() == 3);

    // Large values are clamped by Box2D; the engine only validates the range.
    world.SetWorkerCount(1000);
    REQUIRE(world.WorkerCount() >= 3);
}

// Memory: generational handles

TEST_CASE("SlotMap validates handles against generations", "[memory][slotmap]")
{
    SlotMap<int> map;
    SlotMap<int>::Handle handle = map.Create(7);
    REQUIRE(handle.Valid());
    REQUIRE(map.Size() == 1);
    REQUIRE(map.Get(handle) != nullptr);
    REQUIRE(*map.Get(handle) == 7);

    REQUIRE(map.Destroy(handle));
    // The object is logically dead immediately; the bytes are still there
    // until Collect(), but no handle can reach them any more.
    REQUIRE(map.Get(handle) == nullptr);
    REQUIRE(map.Pending() == 1);
    REQUIRE(map.Size() == 0);

    REQUIRE(map.Collect(1) == 1);
    REQUIRE(map.Pending() == 0);
    REQUIRE(map.Get(handle) == nullptr);

    // The slot is reused, but the old handle must not resurrect.
    SlotMap<int>::Handle reused = map.Create(9);
    REQUIRE(reused.index == handle.index);
    REQUIRE(reused.generation != handle.generation);
    REQUIRE(map.Get(handle) == nullptr);
    REQUIRE(map.Get(reused) != nullptr);
    REQUIRE(*map.Get(reused) == 9);
}

TEST_CASE("SlotMap collects destruction with a per-frame budget", "[memory][slotmap]")
{
    SlotMap<int> map;
    std::vector<SlotMap<int>::Handle> handles;
    for (int i = 0; i < 5; ++i)
    {
        handles.push_back(map.Create(i));
    }

    for (const auto& handle : handles)
    {
        REQUIRE(map.Destroy(handle));
    }

    REQUIRE(map.Size() == 0);
    REQUIRE(map.Pending() == 5);
    REQUIRE(map.Collect(2) == 2);
    REQUIRE(map.Pending() == 3);
    REQUIRE(map.Collect(2) == 2);
    REQUIRE(map.Pending() == 1);
    REQUIRE(map.Collect(0) == 1);
    REQUIRE(map.Pending() == 0);
    REQUIRE(map.Capacity() == 5);
}

TEST_CASE("SlotMap destroy is idempotent and rejects stale handles", "[memory][slotmap]")
{
    SlotMap<int> map;
    SlotMap<int>::Handle handle = map.Create(1);
    REQUIRE(map.Destroy(handle));
    REQUIRE_FALSE(map.Destroy(handle));
    REQUIRE(map.Get(handle) == nullptr);
    REQUIRE_FALSE(map.Destroy(SlotMap<int>::Handle{}));
    REQUIRE_FALSE(map.Alive(SlotMap<int>::Handle{}));

    map.Collect();
    REQUIRE_FALSE(map.Destroy(handle));
}

// FemboyScript: program cache, external calls, print sink

TEST_CASE("FemboyScript compiles a source once and caches it", "[script][cache]")
{
    FemboyScript script;
    const std::string source = "int x = 1;";

    std::string error;
    FemboyScript::ProgramPtr first = script.Compile(source, &error);
    REQUIRE(first != nullptr);
    REQUIRE(error.empty());
    REQUIRE(first->Valid());
    REQUIRE(first->Source() == source);

    FemboyScript::ProgramPtr second = script.Compile(source, &error);
    REQUIRE(second == first);
    REQUIRE(script.CacheSize() == 1);
    REQUIRE(script.CacheHits() == 1);
    REQUIRE(script.CacheMisses() == 1);

    std::string badError;
    REQUIRE(script.Compile("int = ;", &badError) == nullptr);
    REQUIRE_FALSE(badError.empty());
}

TEST_CASE("FemboyScript runs a cached program repeatedly", "[script][cache]")
{
    FemboyScript script;
    const std::string source = "int x = 0; int add(int n) { x = x + n; return x; }";

    FemboyScript::ProgramPtr program = script.Compile(source);
    REQUIRE(program != nullptr);

    REQUIRE(script.Run(program).ok);
    REQUIRE(script.HasFunction("add"));

    FemboyScript::Result first = script.Call("add", {2.0});
    REQUIRE(first.ok);
    REQUIRE(std::get<double>(first.value) == Catch::Approx(2.0));

    FemboyScript::Result second = script.Call("add", {3.0});
    REQUIRE(second.ok);
    REQUIRE(std::get<double>(second.value) == Catch::Approx(5.0));

    FemboyScript::Result missing = script.Call("notThere");
    REQUIRE_FALSE(missing.ok);
}

TEST_CASE("FemboyScript captures print output through a sink", "[script]")
{
    FemboyScript script;
    std::vector<std::string> lines;
    script.SetPrintHandler([&](const std::string& line) { lines.push_back(line); });

    REQUIRE(script.Run("print(\"hello\"); print(1 + 2);").ok);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "hello");
    REQUIRE(lines[1] == "3");
}

TEST_CASE("FemboyScript reset clears script state but keeps natives", "[script]")
{
    FemboyScript script;
    bool nativeCalled = false;
    script.RegisterFunction("native",
                            [&](const std::vector<FSValue>&)
                            {
                                nativeCalled = true;
                                return FSValue{};
                            });

    REQUIRE(script.Run("void tick() { native(); }").ok);
    REQUIRE(script.HasFunction("tick"));
    REQUIRE(script.Call("tick").ok);
    REQUIRE(nativeCalled);

    script.Reset();
    REQUIRE_FALSE(script.HasFunction("tick"));
    // Native functions survive a reset: they are host configuration.
    REQUIRE(script.HasFunction("native"));
}

TEST_CASE("FemboyScript enforces a step budget", "[script]")
{
    FemboyScript script;
    script.SetExecutionBudget(100);
    const FemboyScript::Result result = script.Run("int i = 0; while (i < 1000000) { i = i + 1; }");
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("budget") != std::string::npos);
}

// VisualScript runtime dispatches compiled graphs

TEST_CASE("VisualScriptRuntime dispatches OnStart without reparsing", "[script][visualscript]")
{
    FemboyScript script;
    std::vector<double> recorded;
    script.RegisterFunction("record",
                            [&](const std::vector<FSValue>& args)
                            {
                                if (!args.empty() && std::holds_alternative<double>(args[0]))
                                {
                                    recorded.push_back(std::get<double>(args[0]));
                                }
                                return FSValue{};
                            });

    VisualScriptGraph graph;
    const int call = graph.AddNode(VSNodeType::CallFunction);
    graph.Node(call)->params["name"] = "record";
    // The call's argument is the node's own default input; the event drives the
    // statement's execution pin. This is the model the compiler emits today.
    graph.Node(call)->params["in0"] = 42;
    const int start = graph.AddNode(VSNodeType::OnStart);
    REQUIRE(graph.Connect(start, 0, call, 0));

    VisualScriptRuntime runtime(script);
    REQUIRE(runtime.Load(graph));
    REQUIRE(runtime.Ready());
    REQUIRE_FALSE(runtime.Source().empty());

    REQUIRE(runtime.Start().ok);
    REQUIRE(recorded.size() == 1);
    REQUIRE(recorded[0] == Catch::Approx(42.0));

    // Nothing else is pending: update is a cheap named call.
    REQUIRE(runtime.Update(0.016f).ok);
    REQUIRE(runtime.TagAdded("Enemy").ok);
    REQUIRE(runtime.TagRemoved("Enemy").ok);
}

TEST_CASE("VisualScript compiles control flow, variables and calls", "[visualscript]")
{
    FemboyScript script;
    std::vector<double> recorded;
    script.RegisterFunction("record",
                            [&](const std::vector<FSValue>& args)
                            {
                                if (!args.empty() && std::holds_alternative<double>(args[0]))
                                {
                                    recorded.push_back(std::get<double>(args[0]));
                                }
                                return FSValue{};
                            });

    VisualScriptGraph graph;
    const int start = graph.AddNode(VSNodeType::OnStart);
    const int total = graph.AddNode(VSNodeType::DeclareVar);
    graph.Node(total)->params["type"] = "float";
    graph.Node(total)->params["name"] = "total";
    graph.Node(total)->params["in1"] = 0;
    const int loop = graph.AddNode(VSNodeType::ForLoop);
    graph.Node(loop)->params["index"] = "i";
    graph.Node(loop)->params["in1"] = 0;
    graph.Node(loop)->params["in2"] = 3;
    graph.Node(loop)->params["in3"] = 1;
    const int set = graph.AddNode(VSNodeType::SetVar);
    graph.Node(set)->params["name"] = "total";
    const int add = graph.AddNode(VSNodeType::Add);
    const int getTotal = graph.AddNode(VSNodeType::GetVar);
    graph.Node(getTotal)->params["name"] = "total";
    const int getI = graph.AddNode(VSNodeType::GetVar);
    graph.Node(getI)->params["name"] = "i";
    const int record = graph.AddNode(VSNodeType::CallFunction);
    graph.Node(record)->params["name"] = "record";

    REQUIRE(graph.Connect(start, 0, total, 0));
    REQUIRE(graph.Connect(total, 0, loop, 0));
    REQUIRE(graph.Connect(loop, 0, set, 0));
    REQUIRE(graph.Connect(set, 0, record, 0));
    REQUIRE(graph.Connect(getTotal, 0, add, 0));
    REQUIRE(graph.Connect(getI, 0, add, 1));
    REQUIRE(graph.Connect(add, 0, set, 1));
    REQUIRE(graph.Connect(getTotal, 0, record, 1));

    VisualScriptRuntime runtime(script);
    REQUIRE(runtime.Load(graph));
    REQUIRE(runtime.Start().ok);
    REQUIRE(recorded.size() == 3);
    REQUIRE(recorded[0] == Catch::Approx(0.0));
    REQUIRE(recorded[1] == Catch::Approx(1.0));
    REQUIRE(recorded[2] == Catch::Approx(3.0));
}

TEST_CASE("VisualScript branch selects one execution path", "[visualscript]")
{
    FemboyScript script;
    std::vector<double> recorded;
    script.RegisterFunction("record",
                            [&](const std::vector<FSValue>& args)
                            {
                                if (!args.empty() && std::holds_alternative<double>(args[0]))
                                {
                                    recorded.push_back(std::get<double>(args[0]));
                                }
                                return FSValue{};
                            });

    VisualScriptGraph graph;
    const int start = graph.AddNode(VSNodeType::OnStart);
    const int branch = graph.AddNode(VSNodeType::Branch);
    const int left = graph.AddNode(VSNodeType::IntLiteral);
    graph.Node(left)->params["value"] = 1;
    const int right = graph.AddNode(VSNodeType::IntLiteral);
    graph.Node(right)->params["value"] = 1;
    const int compare = graph.AddNode(VSNodeType::Compare);
    const int yes = graph.AddNode(VSNodeType::CallFunction);
    graph.Node(yes)->params["name"] = "record";
    graph.Node(yes)->params["in0"] = 10;
    const int no = graph.AddNode(VSNodeType::CallFunction);
    graph.Node(no)->params["name"] = "record";
    graph.Node(no)->params["in0"] = 20;

    REQUIRE(graph.Connect(start, 0, branch, 0));
    REQUIRE(graph.Connect(left, 0, compare, 0));
    REQUIRE(graph.Connect(right, 0, compare, 1));
    REQUIRE(graph.Connect(compare, 0, branch, 1));
    REQUIRE(graph.Connect(branch, 0, yes, 0));
    REQUIRE(graph.Connect(branch, 1, no, 0));

    VisualScriptRuntime runtime(script);
    REQUIRE(runtime.Load(graph));
    REQUIRE(runtime.Start().ok);
    REQUIRE(recorded.size() == 1);
    REQUIRE(recorded[0] == Catch::Approx(10.0));
}

TEST_CASE("VisualScript defines and calls a typed function", "[visualscript]")
{
    FemboyScript script;
    std::vector<double> recorded;
    script.RegisterFunction("record",
                            [&](const std::vector<FSValue>& args)
                            {
                                if (!args.empty() && std::holds_alternative<double>(args[0]))
                                {
                                    recorded.push_back(std::get<double>(args[0]));
                                }
                                return FSValue{};
                            });

    VisualScriptGraph graph;
    const int start = graph.AddNode(VSNodeType::OnStart);
    const int fn = graph.AddNode(VSNodeType::FunctionDefinition);
    graph.Node(fn)->params["name"] = "AddTwo";
    graph.Node(fn)->params["returnType"] = "int";
    graph.Node(fn)->params["params"] = "int a, int b";
    const int add = graph.AddNode(VSNodeType::Add);
    const int a = graph.AddNode(VSNodeType::GetVar);
    graph.Node(a)->params["name"] = "a";
    const int b = graph.AddNode(VSNodeType::GetVar);
    graph.Node(b)->params["name"] = "b";
    const int ret = graph.AddNode(VSNodeType::Return);
    const int two = graph.AddNode(VSNodeType::IntLiteral);
    graph.Node(two)->params["value"] = 2;
    const int three = graph.AddNode(VSNodeType::IntLiteral);
    graph.Node(three)->params["value"] = 3;
    const int addTwo = graph.AddNode(VSNodeType::CallFunction);
    graph.Node(addTwo)->params["name"] = "AddTwo";
    const int record = graph.AddNode(VSNodeType::CallFunction);
    graph.Node(record)->params["name"] = "record";

    REQUIRE(graph.Connect(fn, 0, ret, 0));
    REQUIRE(graph.Connect(a, 0, add, 0));
    REQUIRE(graph.Connect(b, 0, add, 1));
    REQUIRE(graph.Connect(add, 0, ret, 1));
    REQUIRE(graph.Connect(start, 0, record, 0));
    REQUIRE(graph.Connect(two, 0, addTwo, 0));
    REQUIRE(graph.Connect(three, 0, addTwo, 1));
    REQUIRE(graph.Connect(addTwo, 0, record, 1));

    VisualScriptRuntime runtime(script);
    REQUIRE(runtime.Load(graph));
    REQUIRE(runtime.Start().ok);
    REQUIRE(recorded.size() == 1);
    REQUIRE(recorded[0] == Catch::Approx(5.0));
}

TEST_CASE("VisualScriptRuntime dispatches every event kind", "[visualscript]")
{
    FemboyScript script;
    std::vector<double> recorded;
    script.RegisterFunction("record",
                            [&](const std::vector<FSValue>& args)
                            {
                                if (!args.empty() && std::holds_alternative<double>(args[0]))
                                {
                                    recorded.push_back(std::get<double>(args[0]));
                                }
                                return FSValue{};
                            });

    VisualScriptGraph graph;
    const VSNodeType events[] = {VSNodeType::OnClick, VSNodeType::OnHover, VSNodeType::OnCollision,
                                 VSNodeType::OnSignal, VSNodeType::OnTagAdded};
    for (int i = 0; i < 5; ++i)
    {
        const int event = graph.AddNode(events[i]);
        const int call = graph.AddNode(VSNodeType::CallFunction);
        graph.Node(call)->params["name"] = "record";
        graph.Node(call)->params["in0"] = i + 1;
        REQUIRE(graph.Connect(event, 0, call, 0));
    }

    VisualScriptRuntime runtime(script);
    REQUIRE(runtime.Load(graph));
    REQUIRE(runtime.Click().ok);
    REQUIRE(runtime.Hover(true).ok);
    REQUIRE(runtime.Collision("wall").ok);
    REQUIRE(runtime.Signal("ready").ok);
    REQUIRE(runtime.TagAdded("Enemy").ok);
    REQUIRE(recorded.size() == 5);
    REQUIRE(recorded[0] == Catch::Approx(1.0));
    REQUIRE(recorded[4] == Catch::Approx(5.0));
}

TEST_CASE("VisualScript rejects code injection in identifiers", "[visualscript]")
{
    VisualScriptGraph graph;
    const int set = graph.AddNode(VSNodeType::SetVar);
    graph.Node(set)->params["name"] = "x; print(\"injected\")";
    graph.Node(set)->params["in0"] = 1;

    std::string error;
    const std::string source = graph.Compile(&error);
    REQUIRE_FALSE(error.empty());
    REQUIRE(source.find("injected") == std::string::npos);

    FemboyScript script;
    VisualScriptRuntime runtime(script);
    REQUIRE_FALSE(runtime.Load(graph, &error));
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("VisualScript fromJson rejects unknown types and duplicate pins", "[visualscript]")
{
    VisualScriptGraph graph;
    nlohmann::json json;
    json["nodes"] = nlohmann::json::array();
    json["nodes"].push_back({{"id", 1}, {"type", 9999}});
    json["nodes"].push_back({{"id", 2}, {"type", static_cast<int>(VSNodeType::Print)}});
    json["nodes"].push_back({{"id", 3}, {"type", static_cast<int>(VSNodeType::IntLiteral)}});
    json["connections"] = nlohmann::json::array();
    json["connections"].push_back({{"fromNode", 3}, {"fromPin", 0}, {"toNode", 2}, {"toPin", 0}});
    json["connections"].push_back({{"fromNode", 3}, {"fromPin", 1}, {"toNode", 2}, {"toPin", 0}});
    REQUIRE(graph.FromJson(json));
    REQUIRE(graph.Nodes().size() == 2);
    REQUIRE(graph.Connections().size() == 1);
}

TEST_CASE("FemboyScript postfix, arity, return type and default math", "[script]")
{
    FemboyScript script;
    std::vector<std::string> lines;
    script.SetPrintHandler([&](const std::string& line) { lines.push_back(line); });

    const FemboyScript::Result postfix = script.Run("int i = 1; int old = i++; print(old); print(i);");
    REQUIRE(postfix.ok);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "1");
    REQUIRE(lines[1] == "2");

    lines.clear();
    const FemboyScript::Result few = script.Run("void f(int a) { } f();");
    REQUIRE_FALSE(few.ok);
    REQUIRE(few.error.find("expects 1") != std::string::npos);

    const FemboyScript::Result many = script.Run("void f(int a) { } f(1, 2);");
    REQUIRE_FALSE(many.ok);

    const FemboyScript::Result badReturn = script.Run("int f() { return \"text\"; } print(f());");
    REQUIRE_FALSE(badReturn.ok);

    lines.clear();
    const FemboyScript::Result math =
        script.Run("print(clamp(5, 0, 3)); print(lerp(0, 10, 0.5)); print(arrayLength([1, 2, 3]));");
    REQUIRE(math.ok);
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0] == "3");
    REQUIRE(lines[1] == "5");
    REQUIRE(lines[2] == "3");

    // break/continue used to parse into statements that execStmt silently
    // ignored, which also made generated switch cases fall through.
    lines.clear();
    const FemboyScript::Result control = script.Run(
        "int i = 0; while (i < 10) { i++; if (i == 3) { break; } } print(i);"
        "int sum = 0; for (int j = 0; j < 4; j++) { if (j == 1) { continue; } sum = sum + j; } print(sum);"
        "switch (2) { case 1: print(\"one\"); break; case 2: print(\"two\"); break; default: print(\"other\"); }"
        "int n = 0; do { n++; } while (n < 2); print(n);");
    INFO(control.error);
    REQUIRE(control.ok);
    REQUIRE(lines.size() == 4);
    REQUIRE(lines[0] == "3");
    REQUIRE(lines[1] == "5");
    REQUIRE(lines[2] == "two");
    REQUIRE(lines[3] == "2");

    const FemboyScript::Result strayBreak = script.Run("break;");
    REQUIRE_FALSE(strayBreak.ok);
    REQUIRE(strayBreak.error.find("break outside") != std::string::npos);

    const FemboyScript::Result functionBreak = script.Run("void f() { break; } int i = 0; while (i < 2) { i++; f(); }");
    REQUIRE_FALSE(functionBreak.ok);
    REQUIRE(functionBreak.error.find("break outside") != std::string::npos);
}
TEST_CASE("VisualScript covers do/while, switch, null and trig", "[visualscript]")
{
    FemboyScript script;
    std::vector<double> recorded;
    script.RegisterFunction("record",
                            [&](const std::vector<FSValue>& args)
                            {
                                if (!args.empty() && std::holds_alternative<double>(args[0]))
                                {
                                    recorded.push_back(std::get<double>(args[0]));
                                }
                                return FSValue{};
                            });

    VisualScriptGraph graph;
    const int start = graph.AddNode(VSNodeType::OnStart);
    const int declare = graph.AddNode(VSNodeType::DeclareVar);
    graph.Node(declare)->params["type"] = "int";
    graph.Node(declare)->params["name"] = "n";
    graph.Node(declare)->params["in1"] = 0;
    const int loop = graph.AddNode(VSNodeType::DoWhile);
    const int set = graph.AddNode(VSNodeType::SetVar);
    graph.Node(set)->params["name"] = "n";
    const int add = graph.AddNode(VSNodeType::Add);
    const int getN = graph.AddNode(VSNodeType::GetVar);
    graph.Node(getN)->params["name"] = "n";
    const int one = graph.AddNode(VSNodeType::IntLiteral);
    graph.Node(one)->params["value"] = 1;
    const int compare = graph.AddNode(VSNodeType::Compare);
    graph.Node(compare)->params["op"] = "<";
    const int two = graph.AddNode(VSNodeType::IntLiteral);
    graph.Node(two)->params["value"] = 2;
    const int loopRecord = graph.AddNode(VSNodeType::CallFunction);
    graph.Node(loopRecord)->params["name"] = "record";

    REQUIRE(graph.Connect(start, 0, declare, 0));
    REQUIRE(graph.Connect(declare, 0, loop, 0));
    REQUIRE(graph.Connect(loop, 0, set, 0));
    REQUIRE(graph.Connect(set, 0, loopRecord, 0));
    REQUIRE(graph.Connect(getN, 0, add, 0));
    REQUIRE(graph.Connect(one, 0, add, 1));
    REQUIRE(graph.Connect(add, 0, set, 1));
    REQUIRE(graph.Connect(getN, 0, loopRecord, 1));
    REQUIRE(graph.Connect(getN, 0, compare, 0));
    REQUIRE(graph.Connect(two, 0, compare, 1));
    REQUIRE(graph.Connect(compare, 0, loop, 1));

    // A second graph keeps the switch test independent of the loop above.
    VisualScriptGraph switchGraph;
    const int switchStart = switchGraph.AddNode(VSNodeType::OnStart);
    const int sw = switchGraph.AddNode(VSNodeType::Switch);
    switchGraph.Node(sw)->params["cases"] = nlohmann::json::array({1, 2});
    const int value = switchGraph.AddNode(VSNodeType::IntLiteral);
    switchGraph.Node(value)->params["value"] = 2;
    const int caseTwo = switchGraph.AddNode(VSNodeType::CallFunction);
    switchGraph.Node(caseTwo)->params["name"] = "record";
    switchGraph.Node(caseTwo)->params["in0"] = 20;
    const int fallback = switchGraph.AddNode(VSNodeType::CallFunction);
    switchGraph.Node(fallback)->params["name"] = "record";
    switchGraph.Node(fallback)->params["in0"] = 30;
    REQUIRE(switchGraph.Connect(switchStart, 0, sw, 0));
    REQUIRE(switchGraph.Connect(value, 0, sw, 1));
    REQUIRE(switchGraph.Connect(sw, 11, caseTwo, 0));
    REQUIRE(switchGraph.Connect(sw, 99, fallback, 0));

    VisualScriptRuntime loopRuntime(script);
    REQUIRE(loopRuntime.Load(graph));
    REQUIRE(loopRuntime.Start().ok);
    REQUIRE(recorded.size() == 2);
    REQUIRE(recorded[0] == Catch::Approx(1.0));
    REQUIRE(recorded[1] == Catch::Approx(2.0));

    VisualScriptRuntime switchRuntime(script);
    REQUIRE(switchRuntime.Load(switchGraph));
    REQUIRE(switchRuntime.Start().ok);
    REQUIRE(recorded.size() == 3);
    REQUIRE(recorded[2] == Catch::Approx(20.0));

    VisualScriptGraph nullGraph;
    const int printNull = nullGraph.AddNode(VSNodeType::Print);
    const int nullNode = nullGraph.AddNode(VSNodeType::NullLiteral);
    REQUIRE(nullGraph.Connect(nullNode, 0, printNull, 1));
    REQUIRE(nullGraph.Compile().find("print(null)") != std::string::npos);
}

// Settings registry: schema, validation, extension keys and round-trip

TEST_CASE("FemObject is the engine root", "[core][femobject]")
{
    Fem::Engine& engine = Fem::Engine::Instance();
    CHECK(std::string(engine.GetClass()->GetName()) == "Engine");
    CHECK(engine.IsA<Fem::FemObject>());

    Fem::Skirt FrameDrawList;
    CHECK(std::string(FrameDrawList.GetClass()->GetName()) == "DrawList");
    CHECK(FrameDrawList.IsA<Fem::FemObject>());
}

TEST_CASE("SettingsRegistry validates types, clamps ranges and round-trips", "[settings]")
{
    SettingsRegistry& registry = SettingsRegistry::Instance();

    SettingDef volume;
    volume.key = "unit.settings.volume";
    volume.category = "Unit";
    volume.label = "Volume";
    volume.type = SettingType::Float;
    volume.defaultValue = 0.5f;
    volume.minValue = 0.f;
    volume.maxValue = 1.f;
    REQUIRE(registry.Add(volume));

    SettingDef count;
    count.key = "unit.settings.count";
    count.category = "Unit";
    count.label = "Count";
    count.type = SettingType::Int;
    count.defaultValue = 2;
    count.minValue = 0;
    count.maxValue = 10;
    REQUIRE(registry.Add(count));

    SettingDef mode;
    mode.key = "unit.settings.mode";
    mode.category = "Unit";
    mode.label = "Mode";
    mode.type = SettingType::Enum;
    mode.defaultValue = 0;
    mode.enumLabels = {"A", "B", "C"};
    REQUIRE(registry.Add(mode));

    // Duplicate keys are rejected rather than silently shadowing a descriptor.
    REQUIRE_FALSE(registry.Add(volume));

    REQUIRE(registry.Set("unit.settings.volume", 1.7f));
    CHECK(registry.Get<float>("unit.settings.volume") == Catch::Approx(1.f));
    REQUIRE(registry.Set("unit.settings.count", -4));
    CHECK(registry.Get<int>("unit.settings.count") == 0);
    REQUIRE(registry.Set("unit.settings.mode", 9));
    CHECK(registry.Get<int>("unit.settings.mode") == 2);

    // Wrong JSON types are rejected and leave the old value in place.
    REQUIRE_FALSE(registry.Set("unit.settings.volume", "loud"));
    CHECK(registry.Get<float>("unit.settings.volume") == Catch::Approx(1.f));

    const nlohmann::json saved = registry.ToJson();
    REQUIRE(saved.contains("values"));
    CHECK(saved["values"]["unit.settings.volume"].get<float>() == Catch::Approx(1.f));

    // A newer build may own keys this build does not know. Loading must keep
    // them instead of deleting them on the next save.
    nlohmann::json incoming = saved;
    incoming["values"]["unit.settings.futureKey"] = 42;
    incoming["values"]["unit.settings.volume"] = 0.25f;
    REQUIRE(registry.LoadJson(incoming));
    CHECK(registry.Get<float>("unit.settings.volume") == Catch::Approx(0.25f));
    CHECK(registry.ToJson()["values"]["unit.settings.futureKey"].get<int>() == 42);

    REQUIRE(registry.Reset("unit.settings.volume"));
    CHECK(registry.Get<float>("unit.settings.volume") == Catch::Approx(0.5f));
    CHECK_FALSE(registry.Set("unit.settings.unknown", 1));
    CHECK(registry.Definition("unit.settings.unknown") == nullptr);
}

TEST_CASE("DisplaySettings keeps OLED shift bounded and switchable", "[settings][display]")
{
    DisplaySettings& display = DisplaySettings::Instance();
    display.uiPixelShiftEnabled = true;
    display.uiPixelShiftStrength = 3;
    display.uiPixelShiftInterval = 15.f;

    display.Update(16.f);
    const sf::Vector2i first = display.UiPixelShift();
    CHECK(std::abs(first.x) <= 3);
    CHECK(std::abs(first.y) <= 3);

    display.uiPixelShiftEnabled = false;
    const sf::Vector2i off = display.UiPixelShift();
    CHECK(off.x == 0);
    CHECK(off.y == 0);

    display.ResetToDefaults();
    CHECK(display.uiPixelShiftEnabled);
    CHECK(display.uiPixelShiftStrength == 1);

    // OLED noise and the idle dissolve cycle survive a JSON round-trip, so a
    // hand-edited or older settings file can be migrated without losing them.
    display.noise = 0.04f;
    display.uiDissolveEnabled = true;
    display.uiDissolveInterval = 30.f;
    display.uiDissolveDuration = 1.f;
    display.uiDissolveStrength = 0.5f;
    display.NotifyActivity();
    display.Update(31.f);
    CHECK(display.UiDissolve() == Catch::Approx(0.5f));
    display.NotifyActivity();
    CHECK(display.UiDissolve() == 0.f);

    const nlohmann::json saved = display.ToJson();
    display.noise = 0.f;
    display.uiDissolveEnabled = false;
    display.FromJson(saved);
    CHECK(display.noise == Catch::Approx(0.04f));
    CHECK(display.uiDissolveEnabled);
    CHECK(display.uiDissolveInterval == Catch::Approx(30.f));
    CHECK(display.uiDissolveDuration == Catch::Approx(1.f));
    CHECK(display.uiDissolveStrength == Catch::Approx(0.5f));

    display.ResetToDefaults();
    CHECK(display.noise == Catch::Approx(0.f));
    CHECK_FALSE(display.uiDissolveEnabled);
}

TEST_CASE("AccessibilitySettings mirrors subtitle and flash comfort values", "[settings][accessibility]")
{
    AccessibilitySettings& settings = AccessibilitySettings::Instance();
    settings.ResetToDefaults();

    settings.subtitleScale = 1.4f;
    settings.reduceMotion = true;
    settings.reduceFlash = true;
    settings.WriteToRegistry();
    settings.Apply();

    CHECK(Accessibility::Instance().SubtitleScale() == Catch::Approx(1.4f));
    CHECK(Accessibility::Instance().ReduceMotion());
    CHECK(Accessibility::Instance().ReduceFlash());

    const nlohmann::json saved = settings.ToJson();
    settings.ResetToDefaults();
    settings.FromJson(saved);

    CHECK(settings.subtitleScale == Catch::Approx(1.4f));
    CHECK(settings.reduceFlash);
    CHECK(Accessibility::Instance().SubtitleScale() == Catch::Approx(1.4f));
    CHECK(Accessibility::Instance().ReduceFlash());

    settings.ResetToDefaults();
    CHECK_FALSE(Accessibility::Instance().ReduceFlash());
}

TEST_CASE("Photosensitivity mode forces motion and flash comfort", "[settings][accessibility]")
{
    AccessibilitySettings& settings = AccessibilitySettings::Instance();
    settings.ResetToDefaults();

    settings.photosensitivity = true;
    settings.reduceMotion = false;
    settings.reduceFlash = false;
    settings.WriteToRegistry();
    settings.Apply();

    CHECK(Accessibility::Instance().Photosensitivity());
    CHECK(Accessibility::Instance().ReduceMotion());
    CHECK(Accessibility::Instance().ReduceFlash());

    const nlohmann::json saved = settings.ToJson();
    settings.ResetToDefaults();
    settings.FromJson(saved);
    CHECK(settings.photosensitivity);
    CHECK(Accessibility::Instance().Photosensitivity());
    CHECK(Accessibility::Instance().ReduceMotion());
    CHECK(Accessibility::Instance().ReduceFlash());

    settings.ResetToDefaults();
    CHECK_FALSE(Accessibility::Instance().Photosensitivity());
    CHECK_FALSE(Accessibility::Instance().ReduceFlash());
}
