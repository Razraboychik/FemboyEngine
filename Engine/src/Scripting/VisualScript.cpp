// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/VisualScript.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Reflection.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>

namespace Fem
{

namespace
{
bool IsEventType(VSNodeType type)
{
    switch (type)
    {
        case VSNodeType::OnStart:
        case VSNodeType::OnUpdate:
        case VSNodeType::OnCollision:
        case VSNodeType::OnClick:
        case VSNodeType::OnHover:
        case VSNodeType::OnTagAdded:
        case VSNodeType::OnTagRemoved:
        case VSNodeType::OnSignal:
            return true;
        default:
            return false;
    }
}

// Nodes that evaluate to a value and may therefore sit on a data pin. A call
// can also drive execution, but its result is useful as an expression too.
bool IsValueType(VSNodeType type)
{
    switch (type)
    {
        case VSNodeType::IntLiteral:
        case VSNodeType::FloatLiteral:
        case VSNodeType::StringLiteral:
        case VSNodeType::BoolLiteral:
        case VSNodeType::Add:
        case VSNodeType::Subtract:
        case VSNodeType::Multiply:
        case VSNodeType::Divide:
        case VSNodeType::Modulo:
        case VSNodeType::And:
        case VSNodeType::Or:
        case VSNodeType::Not:
        case VSNodeType::Compare:
        case VSNodeType::GetVar:
        case VSNodeType::CallFunction:
        case VSNodeType::Color:
        case VSNodeType::TextureSample:
        case VSNodeType::Lerp:
        case VSNodeType::Clamp:
        case VSNodeType::Time:
        case VSNodeType::GetWidgetProp:
        case VSNodeType::Output:
        case VSNodeType::TagLiteral:
        case VSNodeType::HasTag:
        case VSNodeType::TagQuery:
        case VSNodeType::Select:
        case VSNodeType::ArrayLiteral:
        case VSNodeType::ArrayGet:
        case VSNodeType::ArrayLength:
        case VSNodeType::Min:
        case VSNodeType::Max:
        case VSNodeType::Abs:
        case VSNodeType::Floor:
        case VSNodeType::Ceil:
        case VSNodeType::Round:
        case VSNodeType::Sqrt:
        case VSNodeType::Pow:
        case VSNodeType::Sin:
        case VSNodeType::Cos:
        case VSNodeType::Tan:
        case VSNodeType::Atan2:
        case VSNodeType::ToString:
        case VSNodeType::ToNumber:
        case VSNodeType::NullLiteral:
        case VSNodeType::EnumLiteral:
        case VSNodeType::StructLiteral:
        case VSNodeType::StructGet:
        case VSNodeType::StructSet:
            return true;
        default:
            return false;
    }
}

// A statement participates in execution flow. Some of these (a call) can also
// be used as a value; `isValueType` lists those separately.
bool IsStatementType(VSNodeType type)
{
    switch (type)
    {
        case VSNodeType::Branch:
        case VSNodeType::SetVar:
        case VSNodeType::Print:
        case VSNodeType::SetWidgetProp:
        case VSNodeType::Sequence:
        case VSNodeType::Delay:
        case VSNodeType::ForLoop:
        case VSNodeType::AddTag:
        case VSNodeType::RemoveTag:
        case VSNodeType::CallFunction:
        case VSNodeType::DeclareVar:
        case VSNodeType::While:
        case VSNodeType::DoWhile:
        case VSNodeType::Switch:
        case VSNodeType::Break:
        case VSNodeType::Continue:
        case VSNodeType::Return:
            return true;
        default:
            return false;
    }
}

bool ValidSegment(const std::string& s)
{
    if (s.empty())
    {
        return false;
    }
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
    {
        return false;
    }
    for (char ch : s)
    {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'))
        {
            return false;
        }
    }
    return true;
}

bool ValidIdentifier(const std::string& s)
{
    if (s.empty())
    {
        return false;
    }
    std::size_t start = 0;
    while (start <= s.size())
    {
        const std::size_t dot = s.find('.', start);
        const std::string part = s.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!ValidSegment(part))
        {
            return false;
        }
        if (dot == std::string::npos)
        {
            break;
        }
        start = dot + 1;
    }
    return true;
}

bool ValidBaseType(const std::string& s)
{
    static const std::set<std::string> types = {"int", "float", "double", "string", "bool"};
    return types.count(s) != 0;
}

bool ValidTypeName(const std::string& s)
{
    if (s == "void")
    {
        return true;
    }
    if (s.size() > 2 && s.compare(s.size() - 2, 2, "[]") == 0)
    {
        return ValidBaseType(s.substr(0, s.size() - 2));
    }
    return ValidBaseType(s);
}

std::string EscapeString(const std::string& text)
{
    std::string out;
    for (char ch : text)
    {
        if (ch == '"' || ch == '\\')
        {
            out.push_back('\\');
        }
        if (ch == '\n')
        {
            out += "\\n";
            continue;
        }
        if (ch == '\t')
        {
            out += "\\t";
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

std::string StringLiteral(const std::string& text)
{
    return "\"" + EscapeString(text) + "\"";
}

bool KnownNodeType(int raw)
{
    for (VSNodeType type : VsNodeTypes())
    {
        if (static_cast<int>(type) == raw)
        {
            return true;
        }
    }
    return false;
}
} // namespace

// The single source of truth for node names. The editor's node picker reads this
// instead of keeping its own array, which is the difference between "the picker
// is out of date" being impossible and it being a bug someone finds later.
const std::vector<VSNodeType>& VsNodeTypes()
{
    static const std::vector<VSNodeType> types = {
        VSNodeType::IntLiteral,
        VSNodeType::FloatLiteral,
        VSNodeType::StringLiteral,
        VSNodeType::BoolLiteral,
        VSNodeType::Add,
        VSNodeType::Subtract,
        VSNodeType::Multiply,
        VSNodeType::Divide,
        VSNodeType::Modulo,
        VSNodeType::And,
        VSNodeType::Or,
        VSNodeType::Not,
        VSNodeType::Compare,
        VSNodeType::Branch,
        VSNodeType::GetVar,
        VSNodeType::SetVar,
        VSNodeType::CallFunction,
        VSNodeType::Print,
        VSNodeType::Color,
        VSNodeType::TextureSample,
        VSNodeType::Lerp,
        VSNodeType::Clamp,
        VSNodeType::Time,
        VSNodeType::SetWidgetProp,
        VSNodeType::GetWidgetProp,
        VSNodeType::OnClick,
        VSNodeType::OnHover,
        VSNodeType::Sequence,
        VSNodeType::Delay,
        VSNodeType::ForLoop,
        VSNodeType::OnStart,
        VSNodeType::OnUpdate,
        VSNodeType::OnCollision,
        VSNodeType::Output,
        VSNodeType::TagLiteral,
        VSNodeType::HasTag,
        VSNodeType::AddTag,
        VSNodeType::RemoveTag,
        VSNodeType::TagQuery,
        VSNodeType::OnTagAdded,
        VSNodeType::OnTagRemoved,
        VSNodeType::DeclareVar,
        VSNodeType::While,
        VSNodeType::Break,
        VSNodeType::Continue,
        VSNodeType::Return,
        VSNodeType::FunctionDefinition,
        VSNodeType::Select,
        VSNodeType::ArrayLiteral,
        VSNodeType::ArrayGet,
        VSNodeType::ArrayLength,
        VSNodeType::Min,
        VSNodeType::Max,
        VSNodeType::Abs,
        VSNodeType::Floor,
        VSNodeType::Ceil,
        VSNodeType::Round,
        VSNodeType::Sqrt,
        VSNodeType::Pow,
        VSNodeType::Sin,
        VSNodeType::Cos,
        VSNodeType::Tan,
        VSNodeType::Atan2,
        VSNodeType::ToString,
        VSNodeType::ToNumber,
        VSNodeType::OnSignal,
        VSNodeType::NullLiteral,
        VSNodeType::DoWhile,
        VSNodeType::Switch,
        VSNodeType::EnumLiteral,
        VSNodeType::StructLiteral,
        VSNodeType::StructGet,
        VSNodeType::StructSet,
    };
    return types;
}

const std::vector<VSReflectionNode>& VsReflectionCatalog()
{
    static std::vector<VSReflectionNode> catalog;
    static std::size_t signature = static_cast<std::size_t>(-1);

    // Rebuild when a registration happened after the previous call (a plugin
    // loading its own enums, for example). Cheap enough for a UI picker.
    std::size_t current = FemStructRegistry::Instance().Size();
    for (const auto& type : FemEnumRegistry::Instance().All())
    {
        current += 1 + type.values.size();
    }
    if (current == signature)
    {
        return catalog;
    }

    catalog.clear();
    for (const auto& type : FemEnumRegistry::Instance().All())
    {
        const std::string enumLabel = type.meta.displayName.empty() ? type.name : type.meta.displayName;
        for (const auto& value : type.values)
        {
            const std::string valueLabel = value.meta.displayName.empty() ? value.name : value.meta.displayName;
            catalog.push_back(VSReflectionNode{VSNodeType::EnumLiteral, "Enum " + enumLabel + "." + valueLabel,
                                               type.name, value.name});
        }
    }
    for (const FemStructType* type : FemStructRegistry::Instance().All())
    {
        if (type)
        {
            const std::string label = type->meta.displayName.empty() ? type->name : type->meta.displayName;
            catalog.push_back(VSReflectionNode{VSNodeType::StructLiteral, "Struct " + label, type->name, {}});
        }
    }
    signature = current;
    return catalog;
}

const char* VsNodeTypeName(VSNodeType type)
{
    switch (type)
    {
        case VSNodeType::IntLiteral:
            return "IntLiteral";
        case VSNodeType::FloatLiteral:
            return "FloatLiteral";
        case VSNodeType::StringLiteral:
            return "StringLiteral";
        case VSNodeType::BoolLiteral:
            return "BoolLiteral";
        case VSNodeType::Add:
            return "Add";
        case VSNodeType::Subtract:
            return "Subtract";
        case VSNodeType::Multiply:
            return "Multiply";
        case VSNodeType::Divide:
            return "Divide";
        case VSNodeType::Modulo:
            return "Modulo";
        case VSNodeType::And:
            return "And";
        case VSNodeType::Or:
            return "Or";
        case VSNodeType::Not:
            return "Not";
        case VSNodeType::Compare:
            return "Compare";
        case VSNodeType::Branch:
            return "Branch";
        case VSNodeType::GetVar:
            return "GetVar";
        case VSNodeType::SetVar:
            return "SetVar";
        case VSNodeType::CallFunction:
            return "CallFunction";
        case VSNodeType::Print:
            return "Print";
        case VSNodeType::Color:
            return "Color";
        case VSNodeType::TextureSample:
            return "TextureSample";
        case VSNodeType::Lerp:
            return "Lerp";
        case VSNodeType::Clamp:
            return "Clamp";
        case VSNodeType::Time:
            return "Time";
        case VSNodeType::SetWidgetProp:
            return "SetWidgetProp";
        case VSNodeType::GetWidgetProp:
            return "GetWidgetProp";
        case VSNodeType::OnClick:
            return "OnClick";
        case VSNodeType::OnHover:
            return "OnHover";
        case VSNodeType::Sequence:
            return "Sequence";
        case VSNodeType::Delay:
            return "Delay";
        case VSNodeType::ForLoop:
            return "ForLoop";
        case VSNodeType::OnStart:
            return "OnStart";
        case VSNodeType::OnUpdate:
            return "OnUpdate";
        case VSNodeType::OnCollision:
            return "OnCollision";
        case VSNodeType::Output:
            return "Output";
        case VSNodeType::TagLiteral:
            return "TagLiteral";
        case VSNodeType::HasTag:
            return "HasTag";
        case VSNodeType::AddTag:
            return "AddTag";
        case VSNodeType::RemoveTag:
            return "RemoveTag";
        case VSNodeType::TagQuery:
            return "TagQuery";
        case VSNodeType::OnTagAdded:
            return "OnTagAdded";
        case VSNodeType::OnTagRemoved:
            return "OnTagRemoved";
        case VSNodeType::DeclareVar:
            return "DeclareVar";
        case VSNodeType::While:
            return "While";
        case VSNodeType::Break:
            return "Break";
        case VSNodeType::Continue:
            return "Continue";
        case VSNodeType::Return:
            return "Return";
        case VSNodeType::FunctionDefinition:
            return "FunctionDefinition";
        case VSNodeType::Select:
            return "Select";
        case VSNodeType::ArrayLiteral:
            return "ArrayLiteral";
        case VSNodeType::ArrayGet:
            return "ArrayGet";
        case VSNodeType::ArrayLength:
            return "ArrayLength";
        case VSNodeType::Min:
            return "Min";
        case VSNodeType::Max:
            return "Max";
        case VSNodeType::Abs:
            return "Abs";
        case VSNodeType::Floor:
            return "Floor";
        case VSNodeType::Ceil:
            return "Ceil";
        case VSNodeType::Round:
            return "Round";
        case VSNodeType::Sqrt:
            return "Sqrt";
        case VSNodeType::Pow:
            return "Pow";
        case VSNodeType::Sin:
            return "Sin";
        case VSNodeType::Cos:
            return "Cos";
        case VSNodeType::Tan:
            return "Tan";
        case VSNodeType::Atan2:
            return "Atan2";
        case VSNodeType::ToString:
            return "ToString";
        case VSNodeType::ToNumber:
            return "ToNumber";
        case VSNodeType::OnSignal:
            return "OnSignal";
        case VSNodeType::NullLiteral:
            return "NullLiteral";
        case VSNodeType::DoWhile:
            return "DoWhile";
        case VSNodeType::Switch:
            return "Switch";
        case VSNodeType::EnumLiteral:
            return "EnumLiteral";
        case VSNodeType::StructLiteral:
            return "StructLiteral";
        case VSNodeType::StructGet:
            return "StructGet";
        case VSNodeType::StructSet:
            return "StructSet";
    }
    return "Unknown";
}

VisualScriptEditor& VisualScriptEditor::Instance()
{
    static VisualScriptEditor e;
    return e;
}

bool VisualScriptEditor::Open(const std::string& graphPath)
{
    if (!graph_.LoadFromFile(graphPath))
    {
        BOY_WARN("VisualScript", "could not open graph: " + graphPath);
        return false;
    }
    path_ = graphPath;
    open_ = true;
    return true;
}

void VisualScriptEditor::Close()
{
    open_ = false;
}

void VisualScriptEditor::Render()
{
    if (!open_)
    {
        return;
    }

    // The ImGui canvas itself is drawn by the editor; here we only advance the
    // preview timer and refresh the cached evaluation.
    if (previewEnabled_)
    {
        previewTimer_ += 0.016f;
        if (previewTimer_ >= 0.1f)
        {
            previewTimer_ = 0.f;
            for (const auto& n : graph_.Nodes())
            {
                if (n.type == VSNodeType::Output)
                {
                    previewResult_ = graph_.Evaluate(n.id);
                    break;
                }
            }
        }
    }
}

int VisualScriptGraph::AddNode(VSNodeType type, float x, float y)
{
    VSNode n;
    n.id = nextId_++;
    n.type = type;
    n.posX = x;
    n.posY = y;

    switch (type)
    {
        case VSNodeType::IntLiteral:
            n.label = "Int";
            n.params["value"] = 0;
            break;
        case VSNodeType::FloatLiteral:
            n.label = "Float";
            n.params["value"] = 0.0f;
            break;
        case VSNodeType::StringLiteral:
            n.label = "String";
            n.params["value"] = "";
            break;
        case VSNodeType::BoolLiteral:
            n.label = "Bool";
            n.params["value"] = false;
            break;
        case VSNodeType::Add:
            n.label = "Add";
            break;
        case VSNodeType::Subtract:
            n.label = "Subtract";
            break;
        case VSNodeType::Multiply:
            n.label = "Multiply";
            break;
        case VSNodeType::Divide:
            n.label = "Divide";
            break;
        case VSNodeType::Modulo:
            n.label = "Modulo";
            break;
        case VSNodeType::And:
            n.label = "And";
            break;
        case VSNodeType::Or:
            n.label = "Or";
            break;
        case VSNodeType::Not:
            n.label = "Not";
            break;
        case VSNodeType::Compare:
            n.label = "Compare";
            n.params["op"] = "==";
            break;
        case VSNodeType::Branch:
            n.label = "Branch";
            break;
        case VSNodeType::GetVar:
            n.label = "GetVar";
            n.params["name"] = "x";
            break;
        case VSNodeType::SetVar:
            n.label = "SetVar";
            n.params["name"] = "x";
            break;
        case VSNodeType::CallFunction:
            n.label = "Call";
            n.params["name"] = "";
            break;
        case VSNodeType::Print:
            n.label = "Print";
            break;
        case VSNodeType::Color:
            n.label = "Color";
            n.params["r"] = 1.0f;
            n.params["g"] = 1.0f;
            n.params["b"] = 1.0f;
            n.params["a"] = 1.0f;
            break;
        case VSNodeType::TextureSample:
            n.label = "TextureSample";
            break;
        case VSNodeType::Lerp:
            n.label = "Lerp";
            break;
        case VSNodeType::Clamp:
            n.label = "Clamp";
            break;
        case VSNodeType::Time:
            n.label = "Time";
            break;
        case VSNodeType::SetWidgetProp:
            n.label = "SetWidgetProp";
            n.params["prop"] = "text";
            break;
        case VSNodeType::GetWidgetProp:
            n.label = "GetWidgetProp";
            n.params["prop"] = "text";
            break;
        case VSNodeType::OnClick:
            n.label = "OnClick";
            break;
        case VSNodeType::OnHover:
            n.label = "OnHover";
            break;
        case VSNodeType::Sequence:
            n.label = "Sequence";
            break;
        case VSNodeType::Delay:
            n.label = "Delay";
            n.params["seconds"] = 1.0f;
            break;
        case VSNodeType::ForLoop:
            n.label = "ForLoop";
            break;
        case VSNodeType::OnStart:
            n.label = "OnStart";
            break;
        case VSNodeType::OnUpdate:
            n.label = "OnUpdate";
            break;
        case VSNodeType::OnCollision:
            n.label = "OnCollision";
            break;
        case VSNodeType::Output:
            n.label = "Output";
            break;
        case VSNodeType::TagLiteral:
            n.label = "Tag";
            n.params["value"] = "Enemy";
            break;
        case VSNodeType::HasTag:
            n.label = "HasTag";
            // The default lives on the input pin, so a literal tag works with no
            // extra node and a connected tag still wins.
            n.params["in1"] = "Enemy";
            break;
        case VSNodeType::AddTag:
            n.label = "AddTag";
            n.params["in1"] = "Enemy";
            break;
        case VSNodeType::RemoveTag:
            n.label = "RemoveTag";
            n.params["in1"] = "Enemy";
            break;
        case VSNodeType::TagQuery:
            n.label = "TagQuery";
            n.params["mode"] = "any";
            n.params["in1"] = "Enemy";
            break;
        case VSNodeType::OnTagAdded:
            n.label = "OnTagAdded";
            // Empty means "every tag"; naming one narrows the event.
            n.params["tag"] = "";
            break;
        case VSNodeType::OnTagRemoved:
            n.label = "OnTagRemoved";
            n.params["tag"] = "";
            break;
        case VSNodeType::DeclareVar:
            n.label = "Declare";
            n.params["type"] = "float";
            n.params["name"] = "value";
            break;
        case VSNodeType::While:
            n.label = "While";
            break;
        case VSNodeType::Break:
            n.label = "Break";
            break;
        case VSNodeType::Continue:
            n.label = "Continue";
            break;
        case VSNodeType::Return:
            n.label = "Return";
            break;
        case VSNodeType::FunctionDefinition:
            n.label = "Function";
            n.params["name"] = "MyFunction";
            n.params["returnType"] = "void";
            n.params["params"] = "";
            break;
        case VSNodeType::Select:
            n.label = "Select";
            break;
        case VSNodeType::ArrayLiteral:
            n.label = "Array";
            n.params["items"] = nlohmann::json::array();
            break;
        case VSNodeType::ArrayGet:
            n.label = "ArrayGet";
            break;
        case VSNodeType::ArrayLength:
            n.label = "ArrayLength";
            break;
        case VSNodeType::Min:
            n.label = "Min";
            break;
        case VSNodeType::Max:
            n.label = "Max";
            break;
        case VSNodeType::Abs:
            n.label = "Abs";
            break;
        case VSNodeType::Floor:
            n.label = "Floor";
            break;
        case VSNodeType::Ceil:
            n.label = "Ceil";
            break;
        case VSNodeType::Round:
            n.label = "Round";
            break;
        case VSNodeType::Sqrt:
            n.label = "Sqrt";
            break;
        case VSNodeType::Pow:
            n.label = "Pow";
            break;
        case VSNodeType::Sin:
            n.label = "Sin";
            break;
        case VSNodeType::Cos:
            n.label = "Cos";
            break;
        case VSNodeType::Tan:
            n.label = "Tan";
            break;
        case VSNodeType::Atan2:
            n.label = "Atan2";
            break;
        case VSNodeType::ToString:
            n.label = "ToString";
            break;
        case VSNodeType::ToNumber:
            n.label = "ToNumber";
            break;
        case VSNodeType::OnSignal:
            n.label = "OnSignal";
            n.params["signal"] = "";
            break;
        case VSNodeType::NullLiteral:
            n.label = "Null";
            break;
        case VSNodeType::DoWhile:
            n.label = "DoWhile";
            break;
        case VSNodeType::Switch:
            n.label = "Switch";
            n.params["cases"] = nlohmann::json::array({"0", "1"});
            n.params["default"] = true;
            break;
        case VSNodeType::EnumLiteral:
        {
            n.label = "Enum";
            const auto& enums = FemEnumRegistry::Instance().All();
            n.params["enumType"] = enums.empty() ? "" : enums.front().name;
            n.params["value"] = (enums.empty() || enums.front().values.empty()) ? "" : enums.front().values.front().name;
            break;
        }
        case VSNodeType::StructLiteral:
        {
            n.label = "Struct";
            const auto structs = FemStructRegistry::Instance().All();
            n.params["structType"] = structs.empty() ? "" : structs.front()->name;
            break;
        }
        case VSNodeType::StructGet:
            n.label = "StructGet";
            n.params["field"] = "";
            break;
        case VSNodeType::StructSet:
            n.label = "StructSet";
            n.params["field"] = "";
            break;
        default:
            n.label = "Node";
            break;
    }

    nodes_.push_back(n);
    return n.id;
}

void VisualScriptGraph::RemoveNode(int id)
{
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(),
                                      [id](const VSConnection& c) { return c.fromNode == id || c.toNode == id; }),
                       connections_.end());

    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [id](const VSNode& n) { return n.id == id; }),
                 nodes_.end());
}

void VisualScriptGraph::Clear()
{
    nodes_.clear();
    connections_.clear();
    nextId_ = 1;
}

bool VisualScriptGraph::Connect(int fromNode, int fromPin, int toNode, int toPin)
{
    if (fromNode == toNode)
    {
        BOY_WARN("VisualScript", "connect: refusing self-connection");
        return false;
    }
    if (fromPin < 0 || toPin < 0)
    {
        BOY_WARN("VisualScript", "connect: pin numbers must be non-negative");
        return false;
    }
    if (!Node(fromNode) || !Node(toNode))
    {
        BOY_WARN("VisualScript", "connect: unknown node");
        return false;
    }

    // One connection per input pin keeps evaluation unambiguous.
    for (const auto& c : connections_)
    {
        if (c.toNode == toNode && c.toPin == toPin)
        {
            BOY_WARN("VisualScript", "connect: input pin already connected");
            return false;
        }
    }

    connections_.push_back({fromNode, fromPin, toNode, toPin});
    return true;
}

void VisualScriptGraph::Disconnect(int fromNode, int fromPin, int toNode, int toPin)
{
    connections_.erase(
        std::remove_if(
            connections_.begin(), connections_.end(), [&](const VSConnection& c)
            { return c.fromNode == fromNode && c.fromPin == fromPin && c.toNode == toNode && c.toPin == toPin; }),
        connections_.end());
}

VSNode* VisualScriptGraph::Node(int id)
{
    for (auto& n : nodes_)
    {
        if (n.id == id)
        {
            return &n;
        }
    }
    return nullptr;
}

const VSNode* VisualScriptGraph::Node(int id) const
{
    for (const auto& n : nodes_)
    {
        if (n.id == id)
        {
            return &n;
        }
    }
    return nullptr;
}

bool VisualScriptGraph::HasCycleFrom(int nodeId) const
{
    std::set<int> visited;
    std::function<bool(int)> dfs = [&](int id) -> bool
    {
        if (!visited.insert(id).second)
        {
            return true; // revisited on the same path
        }
        for (const auto& c : connections_)
        {
            if (c.fromNode == id && dfs(c.toNode))
            {
                return true;
            }
        }
        visited.erase(id); // allow diamond graphs; only true cycles are rejected
        return false;
    };
    return dfs(nodeId);
}

std::string VisualScriptGraph::CompileNode(int nodeId, std::map<int, std::string>& cache, std::set<int>& visiting,
                                           std::string* error) const
{
    if (visiting.count(nodeId))
    {
        if (error && error->empty())
        {
            *error = "cycle detected in data pins at node " + std::to_string(nodeId);
        }
        BOY_WARN("VisualScript", "compile: cycle detected at node " + std::to_string(nodeId));
        return "0";
    }

    auto it = cache.find(nodeId);
    if (it != cache.end())
    {
        return it->second;
    }

    const VSNode* n = Node(nodeId);
    if (!n)
    {
        return "0";
    }

    if (!IsValueType(n->type))
    {
        if (error && error->empty() && !IsStatementType(n->type) && !IsEventType(n->type) &&
            n->type != VSNodeType::FunctionDefinition)
        {
            *error = "node " + std::to_string(n->id) + " (" + VsNodeTypeName(n->type) + ") has no value output";
        }
        return "null";
    }

    visiting.insert(nodeId);

    auto paramExpr = [&](const std::string& key) -> std::optional<std::string>
    {
        auto p = n->params.find(key);
        if (p == n->params.end())
        {
            return std::nullopt;
        }
        const auto& v = p->second;
        if (v.is_number())
        {
            return v.dump();
        }
        if (v.is_boolean())
        {
            return v.get<bool>() ? std::string("true") : std::string("false");
        }
        if (v.is_string())
        {
            return StringLiteral(v.get<std::string>());
        }
        if (v.is_null())
        {
            return std::string("null");
        }
        return std::nullopt;
    };

    auto sourceIsValue = [&](int sourceId) -> bool
    {
        const VSNode* source = Node(sourceId);
        return source && IsValueType(source->type);
    };

    auto hasInput = [&](int pin) -> bool
    {
        for (const auto& c : connections_)
        {
            if (c.toNode == nodeId && c.toPin == pin && sourceIsValue(c.fromNode))
            {
                return true;
            }
        }
        return false;
    };

    auto getInput = [&](int pin) -> std::string
    {
        // Data pins only accept value-producing nodes. An event or an unrelated
        // executable node connected to the same number is an execution edge,
        // not an argument, which is the distinction the 1.2 compiler missed.
        for (const auto& c : connections_)
        {
            if (c.toNode == nodeId && c.toPin == pin && sourceIsValue(c.fromNode))
            {
                return CompileNode(c.fromNode, cache, visiting, error);
            }
        }

        const std::string key = "in" + std::to_string(pin);
        if (auto value = paramExpr(key))
        {
            return *value;
        }
        return "0";
    };

    std::function<std::string(const nlohmann::json&)> jsonExpr;
    jsonExpr = [&](const nlohmann::json& value) -> std::string
    {
        if (value.is_number())
        {
            return value.dump();
        }
        if (value.is_boolean())
        {
            return value.get<bool>() ? "true" : "false";
        }
        if (value.is_string())
        {
            return StringLiteral(value.get<std::string>());
        }
        if (value.is_null())
        {
            return "null";
        }
        if (value.is_array())
        {
            std::string out = "[";
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                if (i)
                {
                    out += ", ";
                }
                out += jsonExpr(value[i]);
            }
            out += "]";
            return out;
        }
        return "null";
    };

    auto callArgs = [&](int firstPin) -> std::string
    {
        int maxArg = -1;
        for (const auto& c : connections_)
        {
            if (c.toNode == nodeId && c.toPin >= firstPin && sourceIsValue(c.fromNode))
            {
                maxArg = std::max(maxArg, c.toPin - firstPin);
            }
        }
        for (const auto& [key, value] : n->params)
        {
            if (key.size() > 2 && key.rfind("in", 0) == 0 && std::isdigit(static_cast<unsigned char>(key[2])))
            {
                try
                {
                    maxArg = std::max(maxArg, std::stoi(key.substr(2)) - firstPin);
                }
                catch (const std::exception&)
                {
                }
            }
        }
        auto argc = n->params.find("argc");
        if (argc != n->params.end() && argc->second.is_number_integer())
        {
            maxArg = std::max(maxArg, argc->second.get<int>() - 1);
        }

        std::string out;
        for (int i = 0; i <= maxArg; ++i)
        {
            if (i)
            {
                out += ", ";
            }
            const int pin = firstPin + i;
            if (hasInput(pin))
            {
                out += getInput(pin);
            }
            else if (auto value = paramExpr("in" + std::to_string(pin)))
            {
                out += *value;
            }
            else if (auto value = paramExpr("in" + std::to_string(i)))
            {
                out += *value; // legacy in0/in1 numbering on statement calls
            }
            else
            {
                out += "0";
            }
        }
        return out;
    };

    std::string result = "null";

    switch (n->type)
    {
        case VSNodeType::IntLiteral:
        case VSNodeType::FloatLiteral:
        {
            auto p = n->params.find("value");
            result = (p != n->params.end() && p->second.is_number()) ? p->second.dump() : "0";
            break;
        }
        case VSNodeType::StringLiteral:
        {
            auto p = n->params.find("value");
            result = StringLiteral((p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>()
                                                                                   : std::string());
            break;
        }
        case VSNodeType::BoolLiteral:
        {
            auto p = n->params.find("value");
            result = (p != n->params.end() && p->second.is_boolean() && p->second.get<bool>()) ? "true" : "false";
            break;
        }
        case VSNodeType::Add:
            result = "(" + getInput(0) + " + " + getInput(1) + ")";
            break;
        case VSNodeType::Subtract:
            result = "(" + getInput(0) + " - " + getInput(1) + ")";
            break;
        case VSNodeType::Multiply:
            result = "(" + getInput(0) + " * " + getInput(1) + ")";
            break;
        case VSNodeType::Divide:
            result = "(" + getInput(0) + " / " + getInput(1) + ")";
            break;
        case VSNodeType::Modulo:
            result = "(" + getInput(0) + " % " + getInput(1) + ")";
            break;
        case VSNodeType::And:
            result = "(" + getInput(0) + " && " + getInput(1) + ")";
            break;
        case VSNodeType::Or:
            result = "(" + getInput(0) + " || " + getInput(1) + ")";
            break;
        case VSNodeType::Not:
            result = "!(" + getInput(0) + ")";
            break;
        case VSNodeType::Compare:
        {
            auto p = n->params.find("op");
            const std::string op =
                (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : "==";
            static const std::set<std::string> allowed = {"==", "!=", "<", "<=", ">", ">="};
            if (!allowed.count(op) && error && error->empty())
            {
                *error = "invalid comparison operator: " + op;
            }
            const std::string safeOp = allowed.count(op) ? op : "==";
            result = "(" + getInput(0) + " " + safeOp + " " + getInput(1) + ")";
            break;
        }
        case VSNodeType::Select:
            result = "(" + getInput(0) + " ? " + getInput(1) + " : " + getInput(2) + ")";
            break;
        case VSNodeType::GetVar:
        {
            auto p = n->params.find("name");
            const std::string name =
                (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string("x");
            if (!ValidIdentifier(name))
            {
                if (error && error->empty())
                {
                    *error = "invalid variable name: " + name;
                }
                result = "null";
            }
            else
            {
                result = name;
            }
            break;
        }
        case VSNodeType::SetVar:
        {
            auto p = n->params.find("name");
            const std::string name =
                (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string("x");
            if (!ValidIdentifier(name))
            {
                if (error && error->empty())
                {
                    *error = "invalid variable name: " + name;
                }
                result = "null";
            }
            else
            {
                result = name + " = " + getInput(0);
            }
            break;
        }
        case VSNodeType::Print:
            result = "print(" + getInput(0) + ")";
            break;
        case VSNodeType::CallFunction:
        {
            auto p = n->params.find("name");
            const std::string name =
                (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string();
            if (!ValidIdentifier(name))
            {
                if (error && error->empty())
                {
                    *error = "invalid function name: " + name;
                }
                result = "null";
            }
            else
            {
                result = name + "(" + callArgs(0) + ")";
            }
            break;
        }
        case VSNodeType::Color:
        {
            auto getf = [&](const std::string& key, int pin, float fallback)
            {
                if (hasInput(pin))
                {
                    return getInput(pin);
                }
                auto p = n->params.find(key);
                if (p != n->params.end() && p->second.is_number())
                {
                    return p->second.dump();
                }
                return std::to_string(fallback);
            };
            result = "Color(" + getf("r", 0, 1.f) + ", " + getf("g", 1, 1.f) + ", " + getf("b", 2, 1.f) + ", " +
                     getf("a", 3, 1.f) + ")";
            break;
        }
        case VSNodeType::TextureSample:
            result = "sampleTexture(" + getInput(0) + ", " + getInput(1) + ")";
            break;
        case VSNodeType::Lerp:
            result = "lerp(" + getInput(0) + ", " + getInput(1) + ", " + getInput(2) + ")";
            break;
        case VSNodeType::Clamp:
            result = "clamp(" + getInput(0) + ", " + getInput(1) + ", " + getInput(2) + ")";
            break;
        case VSNodeType::Time:
            result = "time";
            break;
        case VSNodeType::GetWidgetProp:
        {
            auto p = n->params.find("prop");
            const std::string prop =
                (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string("text");
            result = "getWidgetProp(" + StringLiteral(prop) + ")";
            break;
        }
        case VSNodeType::Output:
            result = getInput(0);
            break;
        case VSNodeType::TagLiteral:
        {
            auto p = n->params.find("value");
            result = StringLiteral((p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>()
                                                                                   : std::string());
            break;
        }
        case VSNodeType::HasTag:
            result = "hasTag(" + getInput(0) + ", " + getInput(1) + ")";
            break;
        case VSNodeType::TagQuery:
        {
            auto p = n->params.find("mode");
            const std::string mode =
                (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string("any");
            static const std::set<std::string> allowed = {"any", "all", "exact", "none"};
            if (!allowed.count(mode) && error && error->empty())
            {
                *error = "invalid tag query mode: " + mode;
            }
            result = "tagQuery(" + StringLiteral(allowed.count(mode) ? mode : std::string("any")) + ", " + getInput(0) +
                     ", " + getInput(1) + ")";
            break;
        }
        case VSNodeType::ArrayLiteral:
        {
            auto p = n->params.find("items");
            result = (p != n->params.end() && p->second.is_array()) ? jsonExpr(p->second) : std::string("[ ]");
            break;
        }
        case VSNodeType::ArrayGet:
            result = "((" + getInput(0) + ")[" + getInput(1) + "])";
            break;
        case VSNodeType::ArrayLength:
            result = "arrayLength(" + getInput(0) + ")";
            break;
        case VSNodeType::Min:
            result = "min(" + getInput(0) + ", " + getInput(1) + ")";
            break;
        case VSNodeType::Max:
            result = "max(" + getInput(0) + ", " + getInput(1) + ")";
            break;
        case VSNodeType::Abs:
            result = "abs(" + getInput(0) + ")";
            break;
        case VSNodeType::Floor:
            result = "floor(" + getInput(0) + ")";
            break;
        case VSNodeType::Ceil:
            result = "ceil(" + getInput(0) + ")";
            break;
        case VSNodeType::Round:
            result = "round(" + getInput(0) + ")";
            break;
        case VSNodeType::Sqrt:
            result = "sqrt(" + getInput(0) + ")";
            break;
        case VSNodeType::Pow:
            result = "pow(" + getInput(0) + ", " + getInput(1) + ")";
            break;
        case VSNodeType::Sin:
            result = "sin(" + getInput(0) + ")";
            break;
        case VSNodeType::Cos:
            result = "cos(" + getInput(0) + ")";
            break;
        case VSNodeType::Tan:
            result = "tan(" + getInput(0) + ")";
            break;
        case VSNodeType::Atan2:
            result = "atan2(" + getInput(0) + ", " + getInput(1) + ")";
            break;
        case VSNodeType::ToString:
            result = "toString(" + getInput(0) + ")";
            break;
        case VSNodeType::ToNumber:
            result = "toNumber(" + getInput(0) + ")";
            break;
        case VSNodeType::NullLiteral:
            result = "null";
            break;
        case VSNodeType::EnumLiteral:
        {
            auto stringParam = [&](const char* key) -> std::string
            {
                auto p = n->params.find(key);
                return (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string();
            };
            const std::string typeName = stringParam("enumType");
            auto value = n->params.find("value");
            std::string valueCode = StringLiteral("");
            if (value != n->params.end())
            {
                if (value->second.is_string())
                {
                    valueCode = StringLiteral(value->second.get<std::string>());
                }
                else if (value->second.is_number())
                {
                    valueCode = value->second.dump();
                }
            }
            result = "enumValue(" + StringLiteral(typeName) + ", " + valueCode + ")";
            break;
        }
        case VSNodeType::StructLiteral:
        {
            auto stringParam = [&](const char* key) -> std::string
            {
                auto p = n->params.find(key);
                return (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string();
            };
            const std::string typeName = stringParam("structType");
            result = "structNew(" + StringLiteral(typeName) + ")";
            if (const FemStructType* type = FemStructRegistry::Instance().Find(typeName))
            {
                for (std::size_t i = 0; i < type->fields.size(); ++i)
                {
                    const int pin = static_cast<int>(i);
                    if (hasInput(pin))
                    {
                        result = "structSet(" + result + ", " + StringLiteral(type->fields[i].name) + ", " +
                                 getInput(pin) + ")";
                    }
                }
            }
            break;
        }
        case VSNodeType::StructGet:
        {
            auto p = n->params.find("field");
            const std::string field = (p != n->params.end() && p->second.is_string())
                                          ? p->second.get<std::string>()
                                          : std::string();
            if (field.empty() && error && error->empty())
            {
                *error = "StructGet node " + std::to_string(n->id) + " has no field";
            }
            result = "structGet(" + getInput(0) + ", " + StringLiteral(field) + ")";
            break;
        }
        case VSNodeType::StructSet:
        {
            auto p = n->params.find("field");
            const std::string field = (p != n->params.end() && p->second.is_string())
                                          ? p->second.get<std::string>()
                                          : std::string();
            if (field.empty() && error && error->empty())
            {
                *error = "StructSet node " + std::to_string(n->id) + " has no field";
            }
            result = "structSet(" + getInput(0) + ", " + StringLiteral(field) + ", " + getInput(1) + ")";
            break;
        }
        default:
            result = "null";
            break;
    }

    visiting.erase(nodeId);
    cache[nodeId] = result;
    return result;
}

std::string VisualScriptGraph::Compile() const
{
    std::string error;
    std::string source = Compile(&error);
    if (!error.empty())
    {
        BOY_WARN("VisualScript", "compile: " + error);
    }
    return source;
}

std::string VisualScriptGraph::Compile(std::string* error) const
{
    if (error)
    {
        error->clear();
    }

    std::map<int, std::string> cache;
    std::set<std::string> globals;
    std::set<int> flowStack;
    std::set<int> expressionStack;
    std::ostringstream code;

    auto report = [&](const std::string& text)
    {
        if (error && error->empty())
        {
            *error = text;
        }
    };

    auto paramCode = [&](const VSNode& n, const std::string& key) -> std::optional<std::string>
    {
        auto p = n.params.find(key);
        if (p == n.params.end())
        {
            return std::nullopt;
        }
        const auto& value = p->second;
        if (value.is_number())
        {
            return value.dump();
        }
        if (value.is_boolean())
        {
            return value.get<bool>() ? std::string("true") : std::string("false");
        }
        if (value.is_string())
        {
            return StringLiteral(value.get<std::string>());
        }
        if (value.is_null())
        {
            return std::string("null");
        }
        return std::nullopt;
    };

    auto data = [&](const VSNode& n, int pin) -> std::optional<std::string>
    {
        for (const auto& c : connections_)
        {
            if (c.toNode != n.id || c.toPin != pin)
            {
                continue;
            }
            const VSNode* source = Node(c.fromNode);
            if (source && IsValueType(source->type))
            {
                return CompileNode(c.fromNode, cache, expressionStack, error);
            }
        }
        return paramCode(n, "in" + std::to_string(pin));
    };

    auto dataOr = [&](const VSNode& n, int pin, const std::string& fallback) -> std::string
    {
        if (auto value = data(n, pin))
        {
            return *value;
        }
        return fallback;
    };

    auto firstValue = [&](const VSNode& n, const std::string& incoming, const std::string& fallback) -> std::string
    {
        if (!incoming.empty())
        {
            return incoming;
        }
        // New graphs put arguments on pin 1; graphs written for 1.2 used pin 0
        // as a value edge even for statement nodes.
        if (auto value = data(n, 1))
        {
            return *value;
        }
        if (auto value = data(n, 0))
        {
            return *value;
        }
        return fallback;
    };

    auto statementArgs = [&](const VSNode& n, const std::string& incoming) -> std::string
    {
        std::vector<std::string> args;
        int nextIndex = 1;

        if (!incoming.empty())
        {
            args.push_back(incoming);
        }
        else if (auto first = data(n, 1))
        {
            args.push_back(*first);
        }
        else if (auto first = paramCode(n, "in0"))
        {
            args.push_back(*first);
        }

        int maxIndex = -1;
        if (!incoming.empty())
        {
            // A value edge on pin 0 is argument 0; further arguments live on
            // pin 1, 2, ... and fall back to in1, in2, ...
            for (const auto& c : connections_)
            {
                if (c.toNode != n.id || c.toPin < 1)
                {
                    continue;
                }
                const VSNode* source = Node(c.fromNode);
                if (source && IsValueType(source->type))
                {
                    maxIndex = std::max(maxIndex, c.toPin);
                }
            }
            for (const auto& [key, value] : n.params)
            {
                if (key.size() > 2 && key.rfind("in", 0) == 0 && std::isdigit(static_cast<unsigned char>(key[2])))
                {
                    try
                    {
                        maxIndex = std::max(maxIndex, std::stoi(key.substr(2)));
                    }
                    catch (const std::exception&)
                    {
                    }
                }
            }
            for (int index = 1; index <= maxIndex; ++index)
            {
                const int pin = index;
                if (auto value = data(n, pin))
                {
                    args.push_back(*value);
                }
                else if (auto value = paramCode(n, "in" + std::to_string(index)))
                {
                    args.push_back(*value);
                }
                else
                {
                    args.push_back("0");
                }
            }
            return [&]()
            {
                std::string text;
                for (std::size_t i = 0; i < args.size(); ++i)
                {
                    if (i)
                    {
                        text += ", ";
                    }
                    text += args[i];
                }
                return text;
            }();
        }

        for (const auto& c : connections_)
        {
            if (c.toNode != n.id || c.toPin < 2)
            {
                continue;
            }
            const VSNode* source = Node(c.fromNode);
            if (source && IsValueType(source->type))
            {
                maxIndex = std::max(maxIndex, c.toPin - 2);
            }
        }
        for (const auto& [key, value] : n.params)
        {
            if (key.size() > 2 && key.rfind("in", 0) == 0 && std::isdigit(static_cast<unsigned char>(key[2])))
            {
                try
                {
                    maxIndex = std::max(maxIndex, std::stoi(key.substr(2)));
                }
                catch (const std::exception&)
                {
                }
            }
        }
        if (args.empty() && maxIndex >= 0)
        {
            args.push_back("0");
        }
        for (int index = 1; index <= maxIndex; ++index)
        {
            const int pin = index + 1;
            if (auto value = data(n, pin))
            {
                args.push_back(*value);
            }
            else if (auto value = paramCode(n, "in" + std::to_string(index)))
            {
                args.push_back(*value);
            }
            else
            {
                args.push_back("0");
            }
        }

        auto argc = n.params.find("argc");
        if (argc != n.params.end() && argc->second.is_number_integer())
        {
            const int wanted = std::max(0, argc->second.get<int>());
            while (static_cast<int>(args.size()) < wanted)
            {
                args.push_back("0");
            }
            if (static_cast<int>(args.size()) > wanted)
            {
                args.resize(static_cast<std::size_t>(wanted));
            }
        }

        std::string text;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (i)
            {
                text += ", ";
            }
            text += args[i];
        }
        return text;
    };

    std::function<void(int, int, const std::string&)> emitBody;
    std::function<void(int, const std::string&, const std::string&)> emitStatement;
    std::function<void(int, const std::string&, const std::string&)> emitValueOrStatement;

    emitBody = [&](int nodeId, int pin, const std::string& indent)
    {
        for (const auto& c : connections_)
        {
            if (c.fromNode == nodeId && c.fromPin == pin)
            {
                emitValueOrStatement(c.toNode, "", indent);
            }
        }
    };

    emitValueOrStatement = [&](int nodeId, const std::string& incoming, const std::string& indent)
    {
        const VSNode* n = Node(nodeId);
        if (!n || IsEventType(n->type) || n->type == VSNodeType::FunctionDefinition)
        {
            return;
        }

        if (IsStatementType(n->type))
        {
            emitStatement(nodeId, incoming, indent);
            return;
        }

        if (IsValueType(n->type))
        {
            const std::string value = CompileNode(nodeId, cache, expressionStack, error);
            // A value on an execution pin is the legacy way of writing
            // `print(hasTag(...))`: evaluate it, then feed it into the first
            // statement it points at. Value-to-value connections are data edges
            // and are pulled by the consuming expression instead.
            for (const auto& c : connections_)
            {
                if (c.fromNode != nodeId || c.fromPin != 0)
                {
                    continue;
                }
                const VSNode* target = Node(c.toNode);
                if (target && IsStatementType(target->type))
                {
                    emitStatement(c.toNode, value, indent);
                }
            }
        }
    };

    emitStatement = [&](int nodeId, const std::string& incoming, const std::string& indent)
    {
        const VSNode* n = Node(nodeId);
        if (!n)
        {
            return;
        }
        if (!IsStatementType(n->type))
        {
            emitValueOrStatement(nodeId, incoming, indent);
            return;
        }
        if (flowStack.count(nodeId))
        {
            report("execution cycle at node " + std::to_string(nodeId));
            return;
        }

        flowStack.insert(nodeId);
        bool followOutput = true;

        switch (n->type)
        {
            case VSNodeType::Print:
                code << indent << "print(" << firstValue(*n, incoming, "0") << ");\n";
                break;
            case VSNodeType::SetVar:
            {
                auto p = n->params.find("name");
                const std::string name =
                    (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string("x");
                if (!ValidIdentifier(name))
                {
                    report("invalid variable name: " + name);
                    break;
                }
                code << indent << name << " = " << firstValue(*n, incoming, "0") << ";\n";
                break;
            }
            case VSNodeType::CallFunction:
            {
                auto p = n->params.find("name");
                const std::string name =
                    (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string();
                if (!ValidIdentifier(name))
                {
                    report("invalid function name: " + name);
                    break;
                }
                code << indent << name << "(" << statementArgs(*n, incoming) << ");\n";
                break;
            }
            case VSNodeType::SetWidgetProp:
            {
                auto p = n->params.find("prop");
                const std::string prop = (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>()
                                                                                         : std::string("text");
                code << indent << "setWidgetProp(" << StringLiteral(prop) << ", " << firstValue(*n, incoming, "0")
                     << ");\n";
                break;
            }
            case VSNodeType::AddTag:
            case VSNodeType::RemoveTag:
            {
                std::string object = incoming;
                bool objectFromPin1 = false;
                if (object.empty())
                {
                    if (auto value = data(*n, 1))
                    {
                        object = *value;
                        objectFromPin1 = true;
                    }
                    else if (auto value = data(*n, 0))
                    {
                        object = *value;
                    }
                    else
                    {
                        object = "0";
                    }
                }

                std::optional<std::string> tag = incoming.empty() ? data(*n, 2) : data(*n, 1);
                if (!tag && !objectFromPin1)
                {
                    tag = data(*n, 1);
                }
                if (!tag)
                {
                    tag = paramCode(*n, "in1");
                }
                if (!tag)
                {
                    tag = StringLiteral("");
                }

                code << indent << (n->type == VSNodeType::AddTag ? "addTag(" : "removeTag(") << object << ", " << *tag
                     << ");\n";
                break;
            }
            case VSNodeType::Branch:
            {
                const std::string condition = dataOr(*n, 1, "false");
                code << indent << "if (" << condition << ")\n" << indent << "{\n";
                emitBody(nodeId, 0, indent + "    ");
                code << indent << "}\n" << indent << "else\n" << indent << "{\n";
                emitBody(nodeId, 1, indent + "    ");
                code << indent << "}\n";
                followOutput = false;
                break;
            }
            case VSNodeType::Sequence:
            {
                std::set<int> pins;
                for (const auto& c : connections_)
                {
                    if (c.fromNode == nodeId)
                    {
                        pins.insert(c.fromPin);
                    }
                }
                for (int pin : pins)
                {
                    emitBody(nodeId, pin, indent);
                }
                followOutput = false;
                break;
            }
            case VSNodeType::ForLoop:
            {
                auto p = n->params.find("index");
                std::string index =
                    (p != n->params.end() && p->second.is_string()) ? p->second.get<std::string>() : std::string("i");
                if (!ValidIdentifier(index))
                {
                    report("invalid loop variable name: " + index);
                    index = "i";
                }
                const std::string start = dataOr(*n, 1, "0");
                const std::string count = dataOr(*n, 2, "10");
                const std::string step = dataOr(*n, 3, "1");
                const std::string suffix = std::to_string(nodeId);
                code << indent << "int __vs_start_" << suffix << " = " << start << ";\n";
                code << indent << "int __vs_step_" << suffix << " = " << step << ";\n";
                code << indent << "int __vs_count_" << suffix << " = " << count << ";\n";
                code << indent << "for (int __vs_loop_" << suffix << " = 0; __vs_loop_" << suffix << " < __vs_count_"
                     << suffix << "; ++__vs_loop_" << suffix << ")\n";
                code << indent << "{\n";
                code << indent << "    int " << index << " = __vs_start_" << suffix << " + __vs_loop_" << suffix
                     << " * __vs_step_" << suffix << ";\n";
                emitBody(nodeId, 0, indent + "    ");
                code << indent << "}\n";
                followOutput = false;
                break;
            }
            case VSNodeType::While:
            {
                const std::string condition = dataOr(*n, 1, "false");
                code << indent << "while (" << condition << ")\n" << indent << "{\n";
                emitBody(nodeId, 0, indent + "    ");
                code << indent << "}\n";
                followOutput = false;
                break;
            }
            case VSNodeType::DoWhile:
            {
                // The body is output pin 0 and runs before the condition is read,
                // which is the one behaviour a while node cannot express.
                code << indent << "do\n" << indent << "{\n";
                emitBody(nodeId, 0, indent + "    ");
                code << indent << "}\n" << indent << "while (" << dataOr(*n, 1, "false") << ");\n";
                followOutput = false;
                break;
            }
            case VSNodeType::Switch:
            {
                const std::string value = dataOr(*n, 1, "0");
                code << indent << "switch (" << value << ")\n" << indent << "{\n";
                auto cases = n->params.find("cases");
                if (cases != n->params.end() && cases->second.is_array())
                {
                    int index = 0;
                    for (const auto& pattern : cases->second)
                    {
                        std::string literal;
                        if (pattern.is_string())
                        {
                            literal = StringLiteral(pattern.get<std::string>());
                        }
                        else if (pattern.is_number())
                        {
                            literal = pattern.dump();
                        }
                        else if (pattern.is_boolean())
                        {
                            literal = pattern.get<bool>() ? "true" : "false";
                        }
                        else
                        {
                            report("switch case " + std::to_string(index) + " is not a scalar literal");
                            ++index;
                            continue;
                        }
                        code << indent << "case " << literal << ":\n";
                        emitBody(nodeId, 10 + index, indent + "    ");
                        code << indent << "    break;\n";
                        ++index;
                    }
                }
                auto def = n->params.find("default");
                const bool hasDefault = def != n->params.end() && def->second.is_boolean() && def->second.get<bool>();
                if (hasDefault)
                {
                    code << indent << "default:\n";
                    emitBody(nodeId, 99, indent + "    ");
                    code << indent << "    break;\n";
                }
                code << indent << "}\n";
                followOutput = false;
                break;
            }
            case VSNodeType::Delay:
            {
                const std::string seconds = dataOr(*n, 1, paramCode(*n, "seconds").value_or("1.0"));
                const std::string base = "__vs_delay_" + std::to_string(nodeId);
                globals.insert("bool " + base + "_done = false;");
                globals.insert("double " + base + "_at = 0.0;");
                code << indent << "if (!" << base << "_done)\n" << indent << "{\n";
                code << indent << "    if (" << base << "_at <= 0.0)\n";
                code << indent << "        " << base << "_at = time + (" << seconds << ");\n";
                code << indent << "    if (time >= " << base << "_at)\n" << indent << "    {\n";
                code << indent << "        " << base << "_done = true;\n";
                emitBody(nodeId, 0, indent + "        ");
                code << indent << "    }\n" << indent << "}\n";
                followOutput = false;
                break;
            }
            case VSNodeType::Return:
            {
                std::optional<std::string> value;
                if (!incoming.empty())
                {
                    value = incoming;
                }
                else
                {
                    value = data(*n, 1);
                }
                if (!value)
                {
                    value = data(*n, 0);
                }
                code << indent << (value ? "return " + *value + ";\n" : "return;\n");
                followOutput = false;
                break;
            }
            case VSNodeType::Break:
                code << indent << "break;\n";
                followOutput = false;
                break;
            case VSNodeType::Continue:
                code << indent << "continue;\n";
                followOutput = false;
                break;
            case VSNodeType::DeclareVar:
            {
                auto tp = n->params.find("type");
                auto np = n->params.find("name");
                const std::string type = (tp != n->params.end() && tp->second.is_string())
                                             ? tp->second.get<std::string>()
                                             : std::string("float");
                const std::string name = (np != n->params.end() && np->second.is_string())
                                             ? np->second.get<std::string>()
                                             : std::string("value");
                if (!ValidTypeName(type) || type == "void" || !ValidIdentifier(name))
                {
                    report("invalid declaration: " + type + " " + name);
                    break;
                }
                std::optional<std::string> value =
                    !incoming.empty() ? std::optional<std::string>(incoming) : data(*n, 1);
                if (!value)
                {
                    value = data(*n, 0);
                }
                code << indent << type << " " << name;
                if (value)
                {
                    code << " = " << *value;
                }
                code << ";\n";
                break;
            }
            default:
                break;
        }

        if (followOutput)
        {
            emitBody(nodeId, 0, indent);
        }
        flowStack.erase(nodeId);
    };

    // User functions first: event bodies and top-level code may call them.
    static const std::set<std::string> reservedEvents = {"OnStart", "OnUpdate",   "OnCollision",  "OnClick",
                                                         "OnHover", "OnTagAdded", "OnTagRemoved", "OnSignal"};
    for (const auto& n : nodes_)
    {
        if (n.type != VSNodeType::FunctionDefinition)
        {
            continue;
        }

        auto np = n.params.find("name");
        auto rp = n.params.find("returnType");
        const std::string name = (np != n.params.end() && np->second.is_string()) ? np->second.get<std::string>()
                                                                                  : std::string("MyFunction");
        const std::string returnType =
            (rp != n.params.end() && rp->second.is_string()) ? rp->second.get<std::string>() : std::string("void");
        if (!ValidIdentifier(name) || !ValidTypeName(returnType) || reservedEvents.count(name))
        {
            report("invalid or reserved function definition: " + name);
            continue;
        }

        std::string params;
        auto pp = n.params.find("params");
        const std::string raw =
            (pp != n.params.end() && pp->second.is_string()) ? pp->second.get<std::string>() : std::string();
        std::size_t start = 0;
        bool paramsOk = true;
        while (start < raw.size())
        {
            const std::size_t comma = raw.find(',', start);
            std::string part = raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            const auto first = part.find_first_not_of(" \t\r\n");
            const auto last = part.find_last_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                if (comma == std::string::npos)
                {
                    break;
                }
                start = comma + 1;
                continue;
            }
            part = part.substr(first, last - first + 1);
            const auto split = part.find_first_of(" \t");
            if (split == std::string::npos)
            {
                paramsOk = false;
                break;
            }
            const std::string type = part.substr(0, split);
            std::string pname = part.substr(part.find_first_not_of(" \t", split));
            // Function parameters in FemboyScript are scalar types; arrays are
            // declared inside the body with `int[]`.
            if (!ValidBaseType(type) || !ValidIdentifier(pname))
            {
                paramsOk = false;
                break;
            }
            if (!params.empty())
            {
                params += ", ";
            }
            params += type + " " + pname;
            if (comma == std::string::npos)
            {
                break;
            }
            start = comma + 1;
        }
        if (!paramsOk)
        {
            report("invalid parameter list on function: " + name);
            continue;
        }

        code << returnType << " " << name << "(" << params << ")\n{\n";
        flowStack.clear();
        emitBody(n.id, 0, "    ");
        code << "}\n\n";
    }

    const bool hasEvent =
        std::any_of(nodes_.begin(), nodes_.end(), [](const VSNode& node) { return IsEventType(node.type); });
    if (!hasEvent)
    {
        // A graph without an event node is a top-level script. Start from every
        // statement that is not reached by another statement; data nodes are
        // pulled in by the expressions that use them.
        for (const auto& n : nodes_)
        {
            if (!IsStatementType(n.type))
            {
                continue;
            }
            bool hasExecInput = false;
            for (const auto& c : connections_)
            {
                if (c.toNode != n.id || c.toPin != 0)
                {
                    continue;
                }
                const VSNode* source = Node(c.fromNode);
                if (source && (IsStatementType(source->type) || IsEventType(source->type)))
                {
                    hasExecInput = true;
                    break;
                }
            }
            if (hasExecInput)
            {
                continue;
            }
            flowStack.clear();
            emitStatement(n.id, "", "");
        }
    }

    auto emitEvent = [&](VSNodeType type, const char* signature, const char* filterParam)
    {
        code << signature << "\n{\n";
        for (const auto& n : nodes_)
        {
            if (n.type != type)
            {
                continue;
            }

            std::string wanted;
            if (filterParam)
            {
                auto p = n.params.find(filterParam);
                if (p != n.params.end() && p->second.is_string())
                {
                    wanted = p->second.get<std::string>();
                }
            }

            const std::string indent = wanted.empty() ? "    " : "        ";
            if (!wanted.empty())
            {
                code << "    if (" << filterParam << " == " << StringLiteral(wanted) << ")\n    {\n";
            }

            flowStack.clear();
            for (const auto& c : connections_)
            {
                if (c.fromNode == n.id)
                {
                    emitValueOrStatement(c.toNode, "", indent);
                }
            }

            if (!wanted.empty())
            {
                code << "    }\n";
            }
        }
        code << "}\n\n";
    };

    // Every event function is emitted, even when the graph has no node of that
    // kind. VisualScriptRuntime can share one host FemboyScript with several
    // runtimes; without the empty definitions a reloaded graph would keep the
    // previous graph's OnClick/OnCollision body and fire code that no longer
    // exists in the source.
    emitEvent(VSNodeType::OnStart, "void OnStart()", nullptr);
    emitEvent(VSNodeType::OnUpdate, "void OnUpdate(float dt)", nullptr);
    emitEvent(VSNodeType::OnCollision, "void OnCollision(string other)", nullptr);
    emitEvent(VSNodeType::OnClick, "void OnClick()", nullptr);
    emitEvent(VSNodeType::OnHover, "void OnHover(bool active)", nullptr);
    emitEvent(VSNodeType::OnTagAdded, "void OnTagAdded(string tag)", "tag");
    emitEvent(VSNodeType::OnTagRemoved, "void OnTagRemoved(string tag)", "tag");
    emitEvent(VSNodeType::OnSignal, "void OnSignal(string signal)", "signal");

    std::ostringstream out;
    out << "// Auto-generated by Visual Script\n";
    for (const auto& declaration : globals)
    {
        out << declaration << "\n";
    }
    if (!globals.empty())
    {
        out << "\n";
    }
    out << code.str();
    return out.str();
}

nlohmann::json VisualScriptGraph::ToJson() const
{
    nlohmann::json j;
    j["nodes"] = nlohmann::json::array();
    for (const auto& n : nodes_)
    {
        nlohmann::json nj;
        nj["id"] = n.id;
        nj["type"] = static_cast<int>(n.type);
        nj["label"] = n.label;
        nj["posX"] = n.posX;
        nj["posY"] = n.posY;
        nj["params"] = n.params;
        j["nodes"].push_back(nj);
    }

    j["connections"] = nlohmann::json::array();
    for (const auto& c : connections_)
    {
        j["connections"].push_back(
            {{"fromNode", c.fromNode}, {"fromPin", c.fromPin}, {"toNode", c.toNode}, {"toPin", c.toPin}});
    }
    j["nextId"] = nextId_;
    return j;
}

bool VisualScriptGraph::FromJson(const nlohmann::json& j)
{
    if (!j.is_object())
    {
        BOY_ERROR("VisualScript", "fromJson: root must be an object");
        return false;
    }

    std::vector<VSNode> newNodes;
    std::vector<VSConnection> newConnections;
    int newNextId = 1;

    if (j.contains("nodes"))
    {
        if (!j["nodes"].is_array())
        {
            BOY_ERROR("VisualScript", "fromJson: nodes must be an array");
            return false;
        }

        for (const auto& nj : j["nodes"])
        {
            if (!nj.is_object() || !nj.contains("id") || !nj["id"].is_number_integer())
            {
                BOY_WARN("VisualScript", "fromJson: skipping malformed node");
                continue;
            }

            VSNode n;
            n.id = nj["id"].get<int>();
            if (n.id < 0)
            {
                BOY_WARN("VisualScript", "fromJson: dropping node with negative id");
                continue;
            }
            for (const auto& existing : newNodes)
            {
                if (existing.id == n.id)
                {
                    BOY_WARN("VisualScript", "fromJson: dropping duplicate node id " + std::to_string(n.id));
                    n.id = -1;
                    break;
                }
            }
            if (n.id < 0)
            {
                continue;
            }

            // json::value() throws a type_error on a mismatch, which used to
            // escape FromJson() and crash the editor when a graph was edited by
            // hand or written by an older version. An unknown enum value is
            // worse than a bad type: casting it produced undefined behaviour in
            // every switch that did not have a default. Reject it here.
            if (!nj.contains("type") || !nj["type"].is_number_integer() || !KnownNodeType(nj["type"].get<int>()))
            {
                BOY_WARN("VisualScript", "fromJson: dropping node with unknown type");
                continue;
            }
            n.type = static_cast<VSNodeType>(nj["type"].get<int>());
            if (nj.contains("label") && nj["label"].is_string())
            {
                n.label = nj["label"].get<std::string>();
            }
            if (nj.contains("posX") && nj["posX"].is_number())
            {
                n.posX = nj["posX"].get<float>();
            }
            if (nj.contains("posY") && nj["posY"].is_number())
            {
                n.posY = nj["posY"].get<float>();
            }

            if (nj.contains("params") && nj["params"].is_object())
            {
                try
                {
                    n.params = nj["params"].get<std::map<std::string, nlohmann::json>>();
                }
                catch (const std::exception& e)
                {
                    BOY_WARN("VisualScript", std::string("params parse error: ") + e.what());
                }
            }

            newNodes.push_back(std::move(n));
            newNextId = std::max(newNextId, newNodes.back().id + 1);
        }
    }

    auto nodeExists = [&](int id)
    {
        for (const auto& n : newNodes)
        {
            if (n.id == id)
            {
                return true;
            }
        }
        return false;
    };

    if (j.contains("connections"))
    {
        if (!j["connections"].is_array())
        {
            BOY_ERROR("VisualScript", "fromJson: connections must be an array");
            return false;
        }

        for (const auto& cj : j["connections"])
        {
            if (!cj.is_object())
            {
                continue;
            }
            if (!cj.contains("fromNode") || !cj.contains("toNode"))
            {
                continue;
            }

            VSConnection c;
            if (cj.contains("fromNode") && cj["fromNode"].is_number_integer())
            {
                c.fromNode = cj["fromNode"].get<int>();
            }
            if (cj.contains("fromPin") && cj["fromPin"].is_number_integer())
            {
                c.fromPin = cj["fromPin"].get<int>();
            }
            if (cj.contains("toNode") && cj["toNode"].is_number_integer())
            {
                c.toNode = cj["toNode"].get<int>();
            }
            if (cj.contains("toPin") && cj["toPin"].is_number_integer())
            {
                c.toPin = cj["toPin"].get<int>();
            }

            // Drop dangling, self-referencing, negative-pin or duplicate input
            // connections instead of keeping them around to make evaluation
            // depend on file order.
            if (!nodeExists(c.fromNode) || !nodeExists(c.toNode) || c.fromNode == c.toNode || c.fromPin < 0 ||
                c.toPin < 0)
            {
                BOY_WARN("VisualScript", "fromJson: dropping invalid connection");
                continue;
            }
            bool duplicate = false;
            for (const auto& existing : newConnections)
            {
                if (existing.toNode == c.toNode && existing.toPin == c.toPin)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                BOY_WARN("VisualScript", "fromJson: dropping duplicate input connection");
                continue;
            }
            newConnections.push_back(c);
        }
    }

    nodes_ = std::move(newNodes);
    connections_ = std::move(newConnections);
    nextId_ = newNextId;
    if (j.contains("nextId") && j["nextId"].is_number_integer())
    {
        nextId_ = std::max(newNextId, j["nextId"].get<int>());
    }
    return true;
}

bool VisualScriptGraph::SaveToFile(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open())
    {
        BOY_ERROR("VisualScript", "cannot write: " + path);
        return false;
    }
    f << ToJson().dump(4);
    f.flush();
    if (!f)
    {
        BOY_ERROR("VisualScript", "write failed: " + path);
        return false;
    }
    BOY_INFO("VisualScript", "saved graph: " + path);
    return true;
}

bool VisualScriptGraph::LoadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        BOY_ERROR("VisualScript", "cannot open: " + path);
        return false;
    }

    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        BOY_ERROR("VisualScript", std::string("parse error: ") + e.what());
        return false;
    }

    if (!FromJson(j))
    {
        return false;
    }
    BOY_INFO("VisualScript", "loaded graph: " + path);
    return true;
}

nlohmann::json VisualScriptGraph::Evaluate(int outputNodeId) const
{
    std::map<int, nlohmann::json> cache;
    std::set<int> visiting;

    std::function<nlohmann::json(int)> eval = [&](int nodeId) -> nlohmann::json
    {
        if (visiting.count(nodeId))
        {
            BOY_WARN("VisualScript", "evaluate: cycle detected at node " + std::to_string(nodeId));
            return nullptr;
        }

        auto cached = cache.find(nodeId);
        if (cached != cache.end())
        {
            return cached->second;
        }

        const VSNode* n = Node(nodeId);
        if (!n)
        {
            return nullptr;
        }

        visiting.insert(nodeId);
        nlohmann::json result = nullptr;

        switch (n->type)
        {
            case VSNodeType::IntLiteral:
            case VSNodeType::FloatLiteral:
                result = n->params.count("value") ? n->params.at("value") : nlohmann::json(0);
                break;
            case VSNodeType::StringLiteral:
                result = n->params.count("value") ? n->params.at("value") : nlohmann::json("");
                break;
            case VSNodeType::BoolLiteral:
                result = n->params.count("value") ? n->params.at("value") : nlohmann::json(false);
                break;
            case VSNodeType::EnumLiteral:
            {
                const std::string typeName =
                    n->params.count("enumType") && n->params.at("enumType").is_string()
                        ? n->params.at("enumType").get<std::string>()
                        : std::string();
                nlohmann::json value = n->params.count("value") ? n->params.at("value") : nlohmann::json("");
                if (value.is_string())
                {
                    std::int64_t number = 0;
                    if (const FemEnumType* type = FemEnumRegistry::Instance().Find(typeName))
                    {
                        if (const FemEnumEntry* entry = type->FindName(value.get<std::string>()))
                        {
                            number = entry->value;
                        }
                    }
                    value = number;
                }
                result = {{"$type", "enum"}, {"name", typeName}, {"value", value}};
                break;
            }
            case VSNodeType::StructLiteral:
            {
                const std::string typeName =
                    n->params.count("structType") && n->params.at("structType").is_string()
                        ? n->params.at("structType").get<std::string>()
                        : std::string();
                result = {{"$type", "struct"}, {"name", typeName}, {"fields", nlohmann::json::object()}};
                break;
            }
            case VSNodeType::StructGet:
            case VSNodeType::StructSet:
                // Previewing member access would need a script interpreter; the
                // compiled graph is the source of truth for these nodes.
                result = nullptr;
                break;
            case VSNodeType::Add:
            case VSNodeType::Subtract:
            case VSNodeType::Multiply:
            case VSNodeType::Divide:
            case VSNodeType::Modulo:
            {
                nlohmann::json a = 0, b = 0;
                for (const auto& c : connections_)
                {
                    if (c.toNode == nodeId && c.toPin == 0)
                    {
                        a = eval(c.fromNode);
                    }
                }
                for (const auto& c : connections_)
                {
                    if (c.toNode == nodeId && c.toPin == 1)
                    {
                        b = eval(c.fromNode);
                    }
                }

                if (a.is_number() && b.is_number())
                {
                    const double av = a.get<double>();
                    const double bv = b.get<double>();
                    switch (n->type)
                    {
                        case VSNodeType::Add:
                            result = av + bv;
                            break;
                        case VSNodeType::Subtract:
                            result = av - bv;
                            break;
                        case VSNodeType::Multiply:
                            result = av * bv;
                            break;
                        case VSNodeType::Divide:
                            result = (bv != 0.0) ? nlohmann::json(av / bv) : nlohmann::json(0);
                            break;
                        case VSNodeType::Modulo:
                            result = (bv != 0.0) ? nlohmann::json(std::fmod(av, bv)) : nlohmann::json(0);
                            break;
                        default:
                            break;
                    }
                }
                break;
            }
            case VSNodeType::Compare:
            {
                nlohmann::json a = nullptr, b = nullptr;
                for (const auto& c : connections_)
                {
                    if (c.toNode == nodeId && c.toPin == 0)
                    {
                        a = eval(c.fromNode);
                    }
                }
                for (const auto& c : connections_)
                {
                    if (c.toNode == nodeId && c.toPin == 1)
                    {
                        b = eval(c.fromNode);
                    }
                }

                const std::string op = n->params.count("op") && n->params.at("op").is_string()
                                           ? n->params.at("op").get<std::string>()
                                           : "==";
                if (a.is_number() && b.is_number())
                {
                    const double av = a.get<double>();
                    const double bv = b.get<double>();
                    if (op == "==")
                    {
                        result = (av == bv);
                    }
                    else if (op == "!=")
                    {
                        result = (av != bv);
                    }
                    else if (op == "<")
                    {
                        result = (av < bv);
                    }
                    else if (op == "<=")
                    {
                        result = (av <= bv);
                    }
                    else if (op == ">")
                    {
                        result = (av > bv);
                    }
                    else if (op == ">=")
                    {
                        result = (av >= bv);
                    }
                }
                break;
            }
            case VSNodeType::GetVar:
            {
                const std::string name = n->params.count("name") && n->params.at("name").is_string()
                                             ? n->params.at("name").get<std::string>()
                                             : "x";
                result = n->params.count(name) ? n->params.at(name) : nlohmann::json(nullptr);
                break;
            }
            case VSNodeType::Output:
            {
                for (const auto& c : connections_)
                {
                    if (c.toNode == nodeId && c.toPin == 0)
                    {
                        result = eval(c.fromNode);
                    }
                }
                break;
            }
            default:
                result = nullptr;
                break;
        }

        visiting.erase(nodeId);
        cache[nodeId] = result;
        return result;
    };

    return eval(outputNodeId);
}

// runtime bridge

VisualScriptRuntime::VisualScriptRuntime()
{
    owned_ = std::make_unique<FemboyScript>();
    script_ = owned_.get();
}

VisualScriptRuntime::VisualScriptRuntime(FemboyScript& script) : script_(&script)
{
}

VisualScriptRuntime::~VisualScriptRuntime() = default;

bool VisualScriptRuntime::Load(const VisualScriptGraph& graph, std::string* error)
{
    std::string compileError;
    const std::string source = graph.Compile(&compileError);
    if (!compileError.empty())
    {
        if (error)
        {
            *error = compileError;
        }
        return false;
    }
    return LoadSource(source, error);
}

bool VisualScriptRuntime::LoadSource(const std::string& source, std::string* error)
{
    if (!script_)
    {
        if (error)
        {
            *error = "visual script runtime has no FemboyScript instance";
        }
        return false;
    }

    std::string compileError;
    FemboyScript::ProgramPtr program = script_->Compile(source, &compileError);
    if (!program)
    {
        if (error)
        {
            *error = compileError.empty() ? "visual script did not compile" : compileError;
        }
        return false;
    }

    // Running the program registers every generated event function. Top-level
    // side effects (graphs without event nodes) execute here exactly once.
    FemboyScript::Result result = script_->Run(program);
    if (!result.ok)
    {
        if (error)
        {
            *error = result.error;
        }
        return false;
    }

    program_ = std::move(program);
    source_ = source;
    loaded_ = true;
    time_ = 0.f;
    return true;
}

void VisualScriptRuntime::Unload()
{
    program_.reset();
    source_.clear();
    loaded_ = false;
    time_ = 0.f;
    // An owned script is private to this runtime, so clearing it also removes
    // user functions and top-level globals from the previous graph. A shared
    // host script keeps its state; every graph still emits all event functions
    // so an old handler can never survive a reload.
    if (owned_)
    {
        owned_->Reset();
    }
}

FemboyScript::Result VisualScriptRuntime::Start()
{
    if (!loaded_)
    {
        return {false, "visual script is not loaded", FSValue{}};
    }
    script_->Inject("time", static_cast<double>(time_));
    script_->Inject("dt", 0.0);
    if (!script_->HasFunction("OnStart"))
    {
        return {true, "", FSValue{}};
    }
    return script_->Call("OnStart");
}

FemboyScript::Result VisualScriptRuntime::Update(float dt)
{
    if (!loaded_)
    {
        return {false, "visual script is not loaded", FSValue{}};
    }
    time_ += std::max(0.f, dt);
    script_->Inject("time", static_cast<double>(time_));
    script_->Inject("dt", static_cast<double>(dt));
    if (!script_->HasFunction("OnUpdate"))
    {
        return {true, "", FSValue{}};
    }
    return script_->Call("OnUpdate", {static_cast<double>(dt)});
}

FemboyScript::Result VisualScriptRuntime::TagAdded(const std::string& tag)
{
    if (!loaded_)
    {
        return {false, "visual script is not loaded", FSValue{}};
    }
    if (!script_->HasFunction("OnTagAdded"))
    {
        return {true, "", FSValue{}};
    }
    return script_->Call("OnTagAdded", {tag});
}

FemboyScript::Result VisualScriptRuntime::TagRemoved(const std::string& tag)
{
    if (!loaded_)
    {
        return {false, "visual script is not loaded", FSValue{}};
    }
    if (!script_->HasFunction("OnTagRemoved"))
    {
        return {true, "", FSValue{}};
    }
    return script_->Call("OnTagRemoved", {tag});
}

FemboyScript::Result VisualScriptRuntime::Collision(const std::string& other)
{
    if (!loaded_)
    {
        return {false, "visual script is not loaded", FSValue{}};
    }
    if (!script_->HasFunction("OnCollision"))
    {
        return {true, "", FSValue{}};
    }
    return script_->Call("OnCollision", {other});
}

FemboyScript::Result VisualScriptRuntime::Click()
{
    if (!loaded_)
    {
        return {false, "visual script is not loaded", FSValue{}};
    }
    if (!script_->HasFunction("OnClick"))
    {
        return {true, "", FSValue{}};
    }
    return script_->Call("OnClick");
}

FemboyScript::Result VisualScriptRuntime::Hover(bool active)
{
    if (!loaded_)
    {
        return {false, "visual script is not loaded", FSValue{}};
    }
    if (!script_->HasFunction("OnHover"))
    {
        return {true, "", FSValue{}};
    }
    return script_->Call("OnHover", {active});
}

FemboyScript::Result VisualScriptRuntime::Signal(const std::string& name)
{
    if (!loaded_)
    {
        return {false, "visual script is not loaded", FSValue{}};
    }
    if (!script_->HasFunction("OnSignal"))
    {
        return {true, "", FSValue{}};
    }
    return script_->Call("OnSignal", {name});
}

FemboyScript::Result VisualScriptRuntime::Call(const std::string& function, const std::vector<FSValue>& args)
{
    if (!loaded_)
    {
        return {false, "visual script is not loaded", FSValue{}};
    }
    return script_->Call(function, args);
}

} // namespace Fem
