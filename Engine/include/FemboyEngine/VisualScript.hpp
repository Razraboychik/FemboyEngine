// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <nlohmann/json.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/FemboyScript.hpp"

namespace Fem
{

// Visual Scripting: node-based graph that compiles to FemboyScript.
// Used for materials, widgets, gameplay logic - no code required.

enum class VSNodeType
{
    // Literals
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BoolLiteral,
    // Math
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    // Logic
    And,
    Or,
    Not,
    Compare,
    Branch,
    // Variables
    GetVar,
    SetVar,
    // Functions
    CallFunction,
    Print,
    // Material-specific
    Color,
    TextureSample,
    Lerp,
    Clamp,
    Time,
    // Widget-specific
    SetWidgetProp,
    GetWidgetProp,
    OnClick,
    OnHover,
    // Control
    Sequence,
    Delay,
    ForLoop,
    // Events
    OnStart,
    OnUpdate,
    OnCollision,
    // I/O
    Output,
    // Tags. Appended at the end on purpose: `type` is serialized as its integer
    // value, so inserting a node above these would silently reinterpret every
    // graph saved before the change.
    TagLiteral,
    HasTag,
    AddTag,
    RemoveTag,
    TagQuery,
    OnTagAdded,
    OnTagRemoved,

    // Language completion pass (1.3)
    // Appended after the tag block for the same reason: `type` is serialized
    // as its integer value, so existing graphs keep their meaning.
    //
    // These nodes close the gap between visual and text scripts: local
    // variables, structured control flow, user functions, arrays and the
    // scalar math that otherwise forced a graph to call a host function.
    DeclareVar,
    While,
    Break,
    Continue,
    Return,
    FunctionDefinition,
    Select,
    ArrayLiteral,
    ArrayGet,
    ArrayLength,
    Min,
    Max,
    Abs,
    Floor,
    Ceil,
    Round,
    Sqrt,
    Pow,
    Sin,
    Cos,
    Tan,
    Atan2,
    ToString,
    ToNumber,
    OnSignal,
    // Text-language constructs that had no visual form yet. Null matters
    // because it is a legal value in FemboyScript; do/while and switch are
    // statements the parser already understands.
    NullLiteral,
    DoWhile,
    Switch,
    // Reflection nodes. Appended after every existing value for the same
    // compatibility reason as the blocks above: the enum's integer value is
    // what a saved graph stores.
    EnumLiteral,
    StructLiteral,
    StructGet,
    StructSet
};

// One entry in the reflection-aware node catalog. The editor offers an
// "EnumLiteral" entry per enum value and a "StructLiteral" entry per registered
// struct, so adding FEMENUM/FEMSTRUCT is enough for the node picker to know it.
struct VSReflectionNode
{
    VSNodeType type = VSNodeType::EnumLiteral;
    std::string label;
    std::string typeName;
    std::string valueName;
};

const std::vector<VSReflectionNode>& VsReflectionCatalog();

// Every node type, in enum order, and its display name. The editor's node picker
// reads these instead of keeping a parallel array, so a new node can never be
// added to the enum and forgotten in the UI.
const std::vector<VSNodeType>& VsNodeTypes();
const char* VsNodeTypeName(VSNodeType type);

struct VSNode
{
    int id = 0;
    VSNodeType type = VSNodeType::IntLiteral;
    std::string label;
    float posX = 0, posY = 0;
    std::map<std::string, nlohmann::json> params;
    std::vector<int> inputPins;
    std::vector<int> outputPins;
    // runtime
    nlohmann::json cachedValue;
};

struct VSConnection
{
    int fromNode = 0;
    int fromPin = 0;
    int toNode = 0;
    int toPin = 0;
};

class VisualScriptGraph
{
public:
    int AddNode(VSNodeType type, float x = 0, float y = 0);
    void RemoveNode(int id);

    // Connections are validated: both endpoints must exist and a connection is
    // never duplicated. Returns false when the connection is rejected.
    bool Connect(int fromNode, int fromPin, int toNode, int toPin);
    void Disconnect(int fromNode, int fromPin, int toNode, int toPin);
    void Clear();

    VSNode* Node(int id);
    const VSNode* Node(int id) const;
    const std::vector<VSNode>& Nodes() const
    {
        return nodes_;
    }
    const std::vector<VSConnection>& Connections() const
    {
        return connections_;
    }

    // Compile to FemboyScript source. Cycles are detected and compiled as 0.
    //
    // Pin model, deliberately small enough to author by hand and to draw:
    //   * an event's output pin 0 is its execution output;
    //   * a statement's input pin 0 is execution input, output pin 0 is the
    //     next statement;
    //   * a value node's output pin 0 is its value;
    //   * statement data starts at input pin 1. The older `in0` parameter is
    //     still accepted as the first argument so graphs saved by 1.2 load and
    //     behave the same way.
    // Branch uses output pin 0 for true and pin 1 for false. Sequence, ForLoop,
    // While, DoWhile and Delay put their body on output pin 0; DoWhile reads
    // its condition from data pin 1. Switch reads its value from data pin 1,
    // puts case bodies on output pins 10+index and the default on pin 99.
    //
    // The error overload reports invalid identifiers/operators instead of
    // silently emitting code that only fails when the runtime parses it.
    std::string Compile() const;
    std::string Compile(std::string* error) const;
    // Serialize/deserialize. All entry points are validated.
    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& j);
    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path);

    // Real-time evaluation (for preview). Cycles are detected and yield null.
    nlohmann::json Evaluate(int outputNodeId) const;

private:
    std::vector<VSNode> nodes_;
    std::vector<VSConnection> connections_;
    int nextId_ = 1;

    std::string CompileNode(int nodeId, std::map<int, std::string>& cache, std::set<int>& visiting,
                            std::string* error) const;
    bool HasCycleFrom(int nodeId) const;
};

// Runtime bridge between a graph and FemboyScript.
//
// Before this class, Compile() produced source text and Evaluate() existed only
// for the editor preview: a graph could be inspected but not actually drive a
// game. VisualScriptRuntime compiles the graph once, parses it once through the
// FemboyScript cache, and then dispatches OnStart/OnUpdate/OnTagAdded by name.
// No lexer, parser or allocation is involved in the per-frame path.
class VisualScriptRuntime
{
public:
    // Owns a private script instance. Use the explicit constructor to share the
    // host's FemboyScript, its native functions and its injected globals.
    VisualScriptRuntime();
    explicit VisualScriptRuntime(FemboyScript& script);
    ~VisualScriptRuntime();

    VisualScriptRuntime(const VisualScriptRuntime&) = delete;
    VisualScriptRuntime& operator=(const VisualScriptRuntime&) = delete;

    bool Load(const VisualScriptGraph& graph, std::string* error = nullptr);
    bool LoadSource(const std::string& source, std::string* error = nullptr);
    void Unload();

    bool Ready() const
    {
        return loaded_;
    }
    const std::string& Source() const
    {
        return source_;
    }
    FemboyScript& Script()
    {
        return *script_;
    }

    FemboyScript::Result Start();
    FemboyScript::Result Update(float dt);
    FemboyScript::Result TagAdded(const std::string& tag);
    FemboyScript::Result TagRemoved(const std::string& tag);
    // The remaining graph events. They used to exist as node types with no
    // dispatcher, so a graph could be authored and compiled but never fire.
    FemboyScript::Result Collision(const std::string& other);
    FemboyScript::Result Click();
    FemboyScript::Result Hover(bool active);
    FemboyScript::Result Signal(const std::string& name);
    FemboyScript::Result Call(const std::string& function, const std::vector<FSValue>& args = {});

private:
    FemboyScript* script_ = nullptr;
    std::unique_ptr<FemboyScript> owned_;
    FemboyScript::ProgramPtr program_;
    std::string source_;
    bool loaded_ = false;
    float time_ = 0.f;
};

// Visual Script Editor (ImGui-based, real-time preview)
class VisualScriptEditor
{
public:
    static VisualScriptEditor& Instance();
    bool Open(const std::string& graphPath);
    void Render(); // call each frame in editor
    void Close();
    bool IsOpen() const
    {
        return open_;
    }
    VisualScriptGraph& Graph()
    {
        return graph_;
    }
    const std::string& Path() const
    {
        return path_;
    }

private:
    VisualScriptEditor() = default;
    bool open_ = false;
    std::string path_;
    VisualScriptGraph graph_;
    // preview
    bool previewEnabled_ = true;
    nlohmann::json previewResult_;
    float previewTimer_ = 0;
};

} // namespace Fem
