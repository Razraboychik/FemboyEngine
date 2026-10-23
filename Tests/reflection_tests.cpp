// Copyright (c) 2026 Razraboychik. All rights reserved.
//
// Reflection, randomness and typed-value tests. The registry types are
// registered at namespace scope, exactly as a game would register its own
// enums and structs, so the tests also document the macro API.

#include "catch_amalgamated.hpp"

#include "FemboyEngine/Camera2D.hpp"
#include "FemboyEngine/ClassDefaults.hpp"
#include "FemboyEngine/CryptoRandom.hpp"
#include "FemboyEngine/FemboyScript.hpp"
#include "FemboyEngine/Mirror.hpp"
#include "FemboyEngine/InstancedStruct.hpp"
#include "FemboyEngine/RandomSystem.hpp"
#include "FemboyEngine/Reflection.hpp"
#include "FemboyEngine/VisualScript.hpp"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace
{
enum class ReflectState
{
    Idle = 0,
    Running = 7,
    Dead = 9
};

struct ReflectStats
{
    int health = 100;
    double speed = 2.5;
    std::string name = "player";
    bool alive = true;
};

struct UnregisteredStats
{
    int value = 1;
};

struct ClassDefaultStats
{
    int health = 100;
    float speed = 2.5f;
    std::string name = "player";
};

class ReflectActor
{
public:
    int health = 50;

    int GetHealth() const
    {
        return health;
    }
};
} // namespace

FEMENUM(ReflectState, meta = (category="Gameplay", display="Reflect State", description="Test state"));
FEMENUM_VALUE(ReflectState, Idle, ReflectState::Idle, meta = ());
FEMENUM_VALUE(ReflectState, Running, ReflectState::Running, meta = (display="Running"));
FEMENUM_VALUE(ReflectState, Dead, ReflectState::Dead, meta = ());

FEMSTRUCT(ReflectStats, meta = (category="Stats", display="Reflect Stats"));
FEMSTRUCT_FIELD(ReflectStats, health, 100, meta = (category="Stats", display="Health", min=0, max=100, units=hp));
FEMSTRUCT_FIELD(ReflectStats, speed, 2.5, meta = (min=0.5, max=10.0));
FEMSTRUCT_FIELD(ReflectStats, name, std::string("player"), meta = ());
FEMSTRUCT_FIELD(ReflectStats, alive, true, meta = ());

FEMPROPERTY(ReflectActor, int, health, meta = (category="Stats", display="Health", min=0, max=200));
FEMFUNCTION(ReflectActor, int, GetHealth, meta = (category="Stats", display="Get Health"));

TEST_CASE("CryptoRandom stays in range and produces distinct buffers", "[crypto]")
{
    Fem::CryptoRandom random;

    for (int i = 0; i < 2000; ++i)
    {
        const int value = random.NextInt(-5, 5);
        REQUIRE(value >= -5);
        REQUIRE(value <= 5);
    }

    for (int i = 0; i < 100; ++i)
    {
        const float value = random.NextFloat();
        REQUIRE(value >= 0.0f);
        REQUIRE(value < 1.0f);
    }

    REQUIRE(random.NextBool(1.0f));
    REQUIRE_FALSE(random.NextBool(0.0f));

    std::uint8_t first[256] = {};
    std::uint8_t second[256] = {};
    random.Fill(first, sizeof(first));
    random.Fill(second, sizeof(second));
    REQUIRE(std::memcmp(first, second, sizeof(first)) != 0);

    // A zero-length fill must be a no-op rather than touching the pointer.
    random.Fill(nullptr, 0);
}

TEST_CASE("RandomSystem network state round-trips", "[random]")
{
    using Fem::RandomSystem;

    RandomSystem::SeedNetwork(0x123456789abcdef0ULL);
    REQUIRE(RandomSystem::NetworkSeed() == 0x123456789abcdef0ULL);

    std::vector<std::uint64_t> first;
    for (int i = 0; i < 32; ++i)
    {
        first.push_back(RandomSystem::Network()());
    }

    const std::string state = RandomSystem::CaptureNetworkState();
    REQUIRE_FALSE(state.empty());

    std::vector<std::uint64_t> second;
    for (int i = 0; i < 32; ++i)
    {
        second.push_back(RandomSystem::Network()());
    }
    REQUIRE(first != second);

    REQUIRE(RandomSystem::RestoreNetworkState(state));
    for (int i = 0; i < 32; ++i)
    {
        REQUIRE(RandomSystem::Network()() == second[static_cast<std::size_t>(i)]);
    }

    const std::uint64_t hostSeed = RandomSystem::SeedNetworkFromHost();
    REQUIRE(hostSeed != 0);
    REQUIRE(RandomSystem::NetworkSeed() == hostSeed);
}

TEST_CASE("RandomSystem frame and call streams are reproducible", "[random]")
{
    using Fem::RandomSystem;

    // The base seed is process-wide. Seeding the network stream does not touch
    // it; BeginFrame() draws one on first use.
    RandomSystem::BeginFrame(100);
    const std::uint64_t frameFirst = RandomSystem::Frame().NextU64();
    const std::uint64_t frameSecond = RandomSystem::Frame().NextU64();

    RandomSystem::BeginFrame(100);
    REQUIRE(RandomSystem::Frame().NextU64() == frameFirst);
    REQUIRE(RandomSystem::Frame().NextU64() == frameSecond);

    RandomSystem::BeginFrame(100);
    const std::uint64_t callFirst = RandomSystem::Call().NextU64();
    RandomSystem::BeginFrame(100);
    REQUIRE(RandomSystem::Call().NextU64() == callFirst);
}

TEST_CASE("Enums register values and names", "[reflection]")
{
    const Fem::FemEnumType* state = Fem::FemEnumRegistry::Instance().Find("ReflectState");
    REQUIRE(state != nullptr);
    REQUIRE(state->values.size() == 3);

    const Fem::FemEnumEntry* running = state->FindName("Running");
    REQUIRE(running != nullptr);
    REQUIRE(running->value == 7);
    REQUIRE(state->meta.category == "Gameplay");
    REQUIRE(state->meta.displayName == "Reflect State");
    REQUIRE(running->meta.displayName == "Running");

    const Fem::FemEnumEntry* dead = state->FindValue(9);
    REQUIRE(dead != nullptr);
    REQUIRE(dead->name == "Dead");
}

TEST_CASE("meta spec parses UE-style keys and constraints", "[reflection][meta]")
{
    const Fem::FemMeta meta = Fem::FemMetaFromSpec(
        "meta = (Category = \"Combat\", ClampMin = 1.5, ClampMax = 9, Units = \"s\", "
        "Tooltip = \"delay, seconds\", Advanced = true)");

    REQUIRE(meta.category == "Combat");
    REQUIRE(meta.description == "delay, seconds");
    REQUIRE(meta.Min().value_or(-1.0) == 1.5);
    REQUIRE(meta.Max().value_or(-1.0) == 9.0);
    REQUIRE(meta.Get("units") == "s");
    REQUIRE(meta.Get("advanced") == "true");
    REQUIRE(meta.AllowsNumber(5.0));
    REQUIRE_FALSE(meta.AllowsNumber(0.5));
    REQUIRE_FALSE(meta.AllowsNumber(10.0));
    REQUIRE(meta.ClampNumber(0.5) == 1.5);
    REQUIRE(meta.ClampNumber(10.0) == 9.0);

    REQUIRE(Fem::FemMetaFromSpec("").Empty());
    REQUIRE(Fem::FemMetaFromSpec("meta = ()").Empty());
}

TEST_CASE("Structs register fields, defaults and JSON", "[reflection]")
{
    using Fem::FemStructRegistry;

    const Fem::FemStructType* type = FemStructRegistry::Instance().Find("ReflectStats");
    REQUIRE(type != nullptr);
    REQUIRE(type->fields.size() == 4);
    REQUIRE(type->FindField("health") != nullptr);
    REQUIRE(type->meta.category == "Stats");

    const Fem::FemStructField* healthField = type->FindField("health");
    REQUIRE(healthField != nullptr);
    REQUIRE(healthField->meta.Min().value_or(-1.0) == 0.0);
    REQUIRE(healthField->meta.Max().value_or(-1.0) == 100.0);
    REQUIRE(healthField->meta.Get("units") == "hp");
    REQUIRE(healthField->meta.AllowsNumber(50.0));
    REQUIRE_FALSE(healthField->meta.AllowsNumber(150.0));
    REQUIRE(healthField->meta.ClampNumber(150.0) == 100.0);

    ReflectStats stats;
    stats.health = 42;
    stats.speed = 9.5;
    stats.name = "mage";
    stats.alive = false;

    const nlohmann::json json = Fem::FemReflect::ToJson(stats);
    REQUIRE(json["$type"] == "struct");
    REQUIRE(json["name"] == "ReflectStats");
    REQUIRE(json["fields"]["health"] == 42);

    ReflectStats restored;
    REQUIRE(Fem::FemReflect::FromJson(json, restored));
    REQUIRE(restored.health == 42);
    REQUIRE(restored.speed == 9.5);
    REQUIRE(restored.name == "mage");
    REQUIRE_FALSE(restored.alive);

    void* defaults = FemStructRegistry::Instance().CreateDefault("ReflectStats");
    REQUIRE(defaults != nullptr);
    const auto* defaultStats = static_cast<const ReflectStats*>(defaults);
    REQUIRE(defaultStats->health == 100);
    REQUIRE(defaultStats->name == "player");
    REQUIRE(defaultStats->alive);
    type->destroy(defaults);
}

TEST_CASE("Properties and functions register metadata", "[reflection]")
{
    const Fem::FemProperty* property = Fem::FemPropertyRegistry::Instance().Find("ReflectActor", "health");
    REQUIRE(property != nullptr);
    REQUIRE(property->type == "int");
    REQUIRE(property->meta.category == "Stats");
    REQUIRE(property->meta.Get("max") == "200");

    ReflectActor actor;
    actor.health = 77;
    REQUIRE(property->get(&actor) == 77);
    REQUIRE(property->set(&actor, 12));
    REQUIRE(actor.health == 12);

    const Fem::FemFunction* function = Fem::FemFunctionRegistry::Instance().Find("ReflectActor", "GetHealth");
    REQUIRE(function != nullptr);
    REQUIRE(function->returnType == "int");
    REQUIRE(function->className == "ReflectActor");
    REQUIRE(function->meta.category == "Stats");
    REQUIRE(function->meta.displayName == "Get Health");
}

TEST_CASE("FemboyScript enum and struct values round-trip", "[script][reflection]")
{
    using Fem::FemboyScript;

    const Fem::FSValue enumValue = Fem::FEnumValue{"ReflectState", 7};
    const nlohmann::json enumJson = FemboyScript::ToJson(enumValue);
    REQUIRE(enumJson["$type"] == "enum");
    REQUIRE(enumJson["name"] == "ReflectState");
    REQUIRE(enumJson["value"] == 7);

    const Fem::FSValue enumRoundTrip = FemboyScript::FromJson(enumJson);
    REQUIRE(std::holds_alternative<Fem::FEnumValue>(enumRoundTrip));
    REQUIRE(std::get<Fem::FEnumValue>(enumRoundTrip).value == 7);

    Fem::FStructValue stats;
    stats.type = "ReflectStats";
    auto health = std::make_shared<Fem::FSValueHolder>();
    health->value = 55.0;
    stats.fields["health"] = health;
    auto name = std::make_shared<Fem::FSValueHolder>();
    name->value = std::string("rogue");
    stats.fields["name"] = name;

    const Fem::FSValue structValue = stats;
    const nlohmann::json structJson = FemboyScript::ToJson(structValue);
    REQUIRE(structJson["$type"] == "struct");
    REQUIRE(structJson["fields"]["health"] == 55.0);

    const Fem::FSValue structRoundTrip = FemboyScript::FromJson(structJson);
    REQUIRE(std::holds_alternative<Fem::FStructValue>(structRoundTrip));
    REQUIRE(std::get<Fem::FStructValue>(structRoundTrip) == stats);
}

TEST_CASE("FemboyScript exposes reflection natives", "[script][reflection]")
{
    Fem::FemboyScript script;
    std::vector<std::string> lines;
    script.SetPrintHandler([&lines](const std::string& line) { lines.push_back(line); });

    const auto result = script.Run(
        "print(structType(structNew(\"ReflectStats\")));"
        "print(structGet(structNew(\"ReflectStats\"), \"health\"));"
        "print(structGet(structSet(structNew(\"ReflectStats\"), \"health\", 77), \"health\"));"
        "print(enumName(enumValue(\"ReflectState\", \"Running\")));"
        "print(enumInt(enumValue(\"ReflectState\", \"Dead\")));");
    INFO(result.error);
    REQUIRE(result.ok);

    REQUIRE(lines.size() >= 5);
    REQUIRE(lines[0] == "ReflectStats");
    REQUIRE(lines[1] == "100");
    REQUIRE(lines[2] == "77");
    REQUIRE(lines[3] == "Running");
    REQUIRE(lines[4] == "9");
}

TEST_CASE("VisualScript compiles reflection nodes", "[visualscript][reflection]")
{
    using Fem::VSNodeType;

    {
        Fem::VisualScriptGraph graph;
        const int start = graph.AddNode(VSNodeType::OnStart);
        const int print = graph.AddNode(VSNodeType::Print);
        const int enumNode = graph.AddNode(VSNodeType::EnumLiteral);
        Fem::VSNode* node = graph.Node(enumNode);
        REQUIRE(node != nullptr);
        node->params["enumType"] = "ReflectState";
        node->params["value"] = "Running";
        REQUIRE(graph.Connect(start, 0, print, 0));
        REQUIRE(graph.Connect(enumNode, 0, print, 1));

        const std::string source = graph.Compile();
        REQUIRE(source.find("enumValue(\"ReflectState\", \"Running\")") != std::string::npos);
    }

    {
        Fem::VisualScriptGraph graph;
        const int start = graph.AddNode(VSNodeType::OnStart);
        const int print = graph.AddNode(VSNodeType::Print);
        const int structNode = graph.AddNode(VSNodeType::StructLiteral);
        Fem::VSNode* node = graph.Node(structNode);
        REQUIRE(node != nullptr);
        node->params["structType"] = "ReflectStats";
        REQUIRE(graph.Connect(start, 0, print, 0));
        REQUIRE(graph.Connect(structNode, 0, print, 1));

        const std::string source = graph.Compile();
        REQUIRE(source.find("structNew(\"ReflectStats\")") != std::string::npos);
    }

    {
        Fem::VisualScriptGraph graph;
        const int start = graph.AddNode(VSNodeType::OnStart);
        const int print = graph.AddNode(VSNodeType::Print);
        const int get = graph.AddNode(VSNodeType::StructGet);
        const int literal = graph.AddNode(VSNodeType::StructLiteral);
        graph.Node(get)->params["field"] = "health";
        graph.Node(literal)->params["structType"] = "ReflectStats";
        REQUIRE(graph.Connect(literal, 0, get, 0));
        REQUIRE(graph.Connect(get, 0, print, 1));
        REQUIRE(graph.Connect(start, 0, print, 0));

        const std::string source = graph.Compile();
        REQUIRE(source.find("structGet(structNew(\"ReflectStats\"), \"health\")") != std::string::npos);
    }
}

TEST_CASE("VisualScript catalog exposes registered types", "[visualscript][reflection]")
{
    const auto& catalog = Fem::VsReflectionCatalog();
    bool hasEnum = false;
    bool hasStruct = false;
    for (const auto& entry : catalog)
    {
        if (entry.typeName == "ReflectState" && entry.valueName == "Running")
        {
            hasEnum = true;
        }
        if (entry.typeName == "ReflectStats" && entry.type == Fem::VSNodeType::StructLiteral)
        {
            hasStruct = true;
        }
    }
    REQUIRE(hasEnum);
    REQUIRE(hasStruct);
}

TEST_CASE("FemInstancedStruct owns, validates and serialises a value", "[reflection][instanced]")
{
    ReflectStats stats;
    stats.health = 33;
    stats.name = "knight";

    Fem::FemInstancedStruct instance;
    REQUIRE(instance.Set(stats));
    REQUIRE(instance.IsValid());
    REQUIRE(instance.GetType() == "ReflectStats");
    REQUIRE(instance.IsType<ReflectStats>());
    REQUIRE_FALSE(instance.IsType<UnregisteredStats>());
    REQUIRE(instance.Get<ReflectStats>() != nullptr);
    REQUIRE(instance.Get<ReflectStats>()->health == 33);

    Fem::FemInstancedStruct copy = instance;
    REQUIRE(copy.IsType<ReflectStats>());
    REQUIRE(copy.Get<ReflectStats>()->health == 33);
    copy.Get<ReflectStats>()->health = 44;
    REQUIRE(instance.Get<ReflectStats>()->health == 33);

    const nlohmann::json json = instance.ToJson();
    Fem::FemInstancedStruct restored;
    REQUIRE(restored.FromJson(json));
    REQUIRE(restored.GetType() == "ReflectStats");
    REQUIRE(restored.Get<ReflectStats>()->name == "knight");

    Fem::FemInstancedStruct defaults;
    REQUIRE(defaults.SetDefault("ReflectStats"));
    REQUIRE(defaults.Get<ReflectStats>()->health == 100);

    Fem::FemInstancedStruct rejected;
    REQUIRE_FALSE(rejected.Set(UnregisteredStats{}));
    REQUIRE_FALSE(rejected.IsValid());
}

TEST_CASE("ClassDefaults compose field, project and instance values", "[mirror][defaults]")
{
    using Fem::ClassDefaults;

    Fem::Mirror::Instance()
        .Component<ClassDefaultStats>("ClassDefaultStats", "class_default_stats")
        .AddField("health", &ClassDefaultStats::health, Fem::FieldOptions().Range(0, 1000).Category("Stats"))
        .AddField("speed", &ClassDefaultStats::speed)
        .AddField("name", &ClassDefaultStats::name)
        .Done();

    const Fem::ComponentReflection* component = Fem::Mirror::Instance().Find("class_default_stats");
    REQUIRE(component != nullptr);
    REQUIRE(component->fields != nullptr);
    REQUIRE(component->fields->size() == 3);
    REQUIRE(component->fields->at(0).defaultValue == 100);
    REQUIRE(component->fields->at(0).category == "Stats");

    ClassDefaults& defaults = ClassDefaults::Instance();
    defaults.Clear();

    Fem::Registry registry;
    const Fem::Entity entity = registry.create();
    REQUIRE(defaults.ApplyTo(registry, entity, *component));
    REQUIRE(registry.get<ClassDefaultStats>(entity).health == 100);

    defaults.For("class_default_stats")["health"] = 250;
    defaults.For("class_default_stats")["name"] = "tank";
    REQUIRE(defaults.ApplyTo(registry, entity, *component));
    REQUIRE(registry.get<ClassDefaultStats>(entity).health == 250);
    REQUIRE(registry.get<ClassDefaultStats>(entity).name == "tank");

    // Instance data wins over the class default, and missing fields still get
    // the project default.
    REQUIRE(defaults.ApplyTo(registry, entity, *component, {{"health", 10}}));
    REQUIRE(registry.get<ClassDefaultStats>(entity).health == 10);
    REQUIRE(registry.get<ClassDefaultStats>(entity).name == "tank");

    REQUIRE(defaults.ResetField(registry, entity, *component, "health"));
    REQUIRE(registry.get<ClassDefaultStats>(entity).health == 100);

    const nlohmann::json saved = defaults.ToJson();
    defaults.Clear();
    REQUIRE(defaults.FromJson(saved));
    REQUIRE(defaults.Find("class_default_stats") != nullptr);
    defaults.Clear();
}

TEST_CASE("ViewProjection is an invertible affine isometric map", "[camera][projection]")
{
    Fem::ViewProjection projection;
    projection.mode = Fem::ProjectionMode::Isometric;
    projection.isoAxisX = 1.f;
    projection.isoAxisY = 0.5f;

    const sf::Vector2f east = projection.Project({1.f, 0.f});
    const sf::Vector2f north = projection.Project({0.f, 1.f});
    REQUIRE(east.x == Catch::Approx(1.f));
    REQUIRE(east.y == Catch::Approx(0.5f));
    REQUIRE(north.x == Catch::Approx(-1.f));
    REQUIRE(north.y == Catch::Approx(0.5f));

    const sf::Vector2f world{17.f, -9.f};
    const sf::Vector2f roundTrip = projection.Unproject(projection.Project(world));
    REQUIRE(roundTrip.x == Catch::Approx(world.x));
    REQUIRE(roundTrip.y == Catch::Approx(world.y));

    Fem::Camera2D camera;
    camera.SetViewportSize(800.f, 600.f);
    camera.SetPosition(100.f, 50.f);
    camera.SetProjection(Fem::ProjectionMode::Isometric);
    Fem::Registry registry;
    camera.Update(0.f, registry);

    const sf::Vector2f screen = camera.WorldToScreen({100.f, 50.f});
    REQUIRE(screen.x == Catch::Approx(400.f));
    REQUIRE(screen.y == Catch::Approx(300.f));
    const sf::Vector2f restored = camera.ScreenToWorld(screen);
    REQUIRE(restored.x == Catch::Approx(100.f));
    REQUIRE(restored.y == Catch::Approx(50.f));
}
