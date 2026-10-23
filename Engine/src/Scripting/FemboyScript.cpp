// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/FemboyScript.hpp"
#include "FemboyEngine/Boy.hpp"
#include "FemboyEngine/Accessibility.hpp"
#include "FemboyEngine/Display.hpp"
#include "FemboyEngine/Reflection.hpp"
#include "FemboyEngine/SettingsRegistry.hpp"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

namespace Fem
{

// Type system
enum class FSType
{
    Unknown,
    Void,
    Int,
    Float,
    Double,
    String,
    Bool,
    Array,
    Enum,
    Struct
};
static std::string TypeToString(FSType t)
{
    switch (t)
    {
        case FSType::Void:
            return "void";
        case FSType::Int:
            return "int";
        case FSType::Float:
            return "float";
        case FSType::Double:
            return "double";
        case FSType::String:
            return "string";
        case FSType::Bool:
            return "bool";
        case FSType::Array:
            return "array";
        case FSType::Enum:
            return "enum";
        case FSType::Struct:
            return "struct";
        default:
            return "unknown";
    }
}
static FSType ParseType(const std::string& s)
{
    if (s == "int")
    {
        return FSType::Int;
    }
    if (s == "float")
    {
        return FSType::Float;
    }
    if (s == "double")
    {
        return FSType::Double;
    }
    if (s == "string")
    {
        return FSType::String;
    }
    if (s == "bool")
    {
        return FSType::Bool;
    }
    if (s == "void")
    {
        return FSType::Void;
    }
    return FSType::Unknown;
}
static FSType ValueType(const FSValue& v)
{
    if (std::holds_alternative<std::monostate>(v))
    {
        return FSType::Void;
    }
    if (std::holds_alternative<bool>(v))
    {
        return FSType::Bool;
    }
    if (std::holds_alternative<double>(v))
    {
        return FSType::Double;
    }
    if (std::holds_alternative<std::string>(v))
    {
        return FSType::String;
    }
    if (std::holds_alternative<std::vector<std::shared_ptr<FSValueHolder>>>(v))
    {
        return FSType::Array;
    }
    if (std::holds_alternative<FEnumValue>(v))
    {
        return FSType::Enum;
    }
    if (std::holds_alternative<FStructValue>(v))
    {
        return FSType::Struct;
    }
    return FSType::Unknown;
}
static bool TypeCompatible(FSType declared, FSType actual)
{
    if (declared == actual)
    {
        return true;
    }
    // numeric conversions allowed
    bool declNum = (declared == FSType::Int || declared == FSType::Float || declared == FSType::Double);
    bool actNum = (actual == FSType::Int || actual == FSType::Float || actual == FSType::Double);
    if (declNum && actNum)
    {
        return true;
    }
    return false;
}
static FSValue Coerce(const FSValue& v, FSType target)
{
    if (target == FSType::Int && std::holds_alternative<double>(v))
    {
        return (double)(long long)std::get<double>(v);
    }
    if (target == FSType::Float && std::holds_alternative<double>(v))
    {
        return std::get<double>(v);
    }
    return v;
}

namespace
{
bool ValueEquals(const FSValue& a, const FSValue& b)
{
    if (a.index() != b.index())
    {
        return false;
    }
    if (const auto* value = std::get_if<bool>(&a))
    {
        return *value == std::get<bool>(b);
    }
    if (const auto* value = std::get_if<double>(&a))
    {
        return *value == std::get<double>(b);
    }
    if (const auto* value = std::get_if<std::string>(&a))
    {
        return *value == std::get<std::string>(b);
    }
    if (const auto* value = std::get_if<std::vector<std::shared_ptr<FSValueHolder>>>(&a))
    {
        const auto& other = std::get<std::vector<std::shared_ptr<FSValueHolder>>>(b);
        if (value->size() != other.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < value->size(); ++i)
        {
            if (!(*value)[i] || !other[i] || !ValueEquals((*value)[i]->value, other[i]->value))
            {
                return false;
            }
        }
        return true;
    }
    if (const auto* value = std::get_if<FEnumValue>(&a))
    {
        return *value == std::get<FEnumValue>(b);
    }
    if (const auto* value = std::get_if<FStructValue>(&a))
    {
        return *value == std::get<FStructValue>(b);
    }
    return std::holds_alternative<std::monostate>(a);
}
} // namespace

bool FStructValue::operator==(const FStructValue& other) const
{
    if (type != other.type || fields.size() != other.fields.size())
    {
        return false;
    }
    for (const auto& [name, holder] : fields)
    {
        const auto it = other.fields.find(name);
        if (it == other.fields.end() || !holder || !it->second)
        {
            return false;
        }
        if (!ValueEquals(holder->value, it->second->value))
        {
            return false;
        }
    }
    return true;
}

// Token
enum class TK
{
    End,
    Ident,
    Number,
    String,
    Bool,
    Null,
    // keywords
    Int,
    Float,
    Double,
    StringKw,
    BoolKw,
    Void,
    If,
    Else,
    For,
    While,
    Do,
    Switch,
    Case,
    Default,
    Break,
    Continue,
    Return,
    Print,
    True,
    False,
    NullKw,
    Class,
    Public,
    Private,
    New,
    Const,
    // ops
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    PlusPlus,
    MinusMinus,
    EqEq,
    BangEq,
    Lt,
    Gt,
    LtEq,
    GtEq,
    Assign,
    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    And,
    Or,
    Not,
    Amp,
    Pipe,
    Caret,
    Tilde,
    LShift,
    RShift,
    // delimiters
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Semicolon,
    Comma,
    Dot,
    Colon,
    Arrow,
    // misc
    Question,
    ColonColon
};
struct Token
{
    TK type;
    std::string text;
    double number = 0;
    FSType declaredType = FSType::Unknown;
};

static bool IsIdentStart(char c)
{
    return std::isalpha((unsigned char)c) || c == '_';
}
static bool IsIdentChar(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

// Lexer
struct LexerResult
{
    std::vector<Token> toks;
    std::string err;
};
static LexerResult Lex(const std::string& src)
{
    LexerResult r;
    size_t i = 0, n = src.size();
    auto skipWs = [&]()
    {
        while (i < n && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n'))
        {
            i++;
        }
    };
    while (true)
    {
        skipWs();
        if (i >= n)
        {
            r.toks.push_back({TK::End, ""});
            break;
        }
        char c = src[i];
        if (c == '/' && i + 1 < n && src[i + 1] == '/')
        {
            while (i < n && src[i] != '\n')
            {
                i++;
            }
            continue;
        }
        if (c == '/' && i + 1 < n && src[i + 1] == '*')
        {
            i += 2;
            bool closed = false;
            while (i + 1 < n)
            {
                if (src[i] == '*' && src[i + 1] == '/')
                {
                    closed = true;
                    i += 2;
                    break;
                }
                i++;
            }
            if (!closed)
            {
                r.err = "unterminated block comment";
                return r;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            char q = c;
            i++;
            std::string s;
            bool closed = false;
            while (i < n)
            {
                if (src[i] == q)
                {
                    closed = true;
                    i++;
                    break;
                }
                if (src[i] == '\\')
                {
                    i++;
                    if (i < n)
                    {
                        switch (src[i])
                        {
                            case 'n':
                                s += '\n';
                                break;
                            case 't':
                                s += '\t';
                                break;
                            case '\\':
                                s += '\\';
                                break;
                            default:
                                s += src[i];
                        }
                        i++;
                    }
                    continue;
                }
                s += src[i];
                i++;
            }
            if (!closed)
            {
                r.err = "unterminated string literal";
                return r;
            }
            r.toks.push_back({TK::String, s});
            continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)src[i + 1])))
        {
            size_t start = i;
            bool seenDot = false;
            while (i < n && (std::isdigit((unsigned char)src[i]) ||
                             (src[i] == '.' && !seenDot && i + 1 < n && std::isdigit((unsigned char)src[i + 1]) &&
                              !(i + 2 < n && src[i + 2] == '.'))))
            {
                if (src[i] == '.')
                {
                    seenDot = true;
                }
                i++;
            }
            std::string numstr = src.substr(start, i - start);
            Token t{TK::Number, numstr};
            try
            {
                t.number = std::stod(numstr);
            }
            catch (const std::exception&)
            {
                r.err = "invalid number literal: " + numstr;
                return r;
            }
            r.toks.push_back(t);
            continue;
        }
        if (IsIdentStart(c))
        {
            size_t start = i;
            while (i < n && IsIdentChar(src[i]))
            {
                i++;
            }
            std::string word = src.substr(start, i - start);
            TK kw = TK::Ident;
            FSType dt = FSType::Unknown;
            if (word == "int")
            {
                kw = TK::Int;
                dt = FSType::Int;
            }
            else if (word == "float")
            {
                kw = TK::Float;
                dt = FSType::Float;
            }
            else if (word == "double")
            {
                kw = TK::Double;
                dt = FSType::Double;
            }
            else if (word == "string")
            {
                kw = TK::StringKw;
                dt = FSType::String;
            }
            else if (word == "bool")
            {
                kw = TK::BoolKw;
                dt = FSType::Bool;
            }
            else if (word == "void")
            {
                kw = TK::Void;
                dt = FSType::Void;
            }
            else if (word == "if")
            {
                kw = TK::If;
            }
            else if (word == "else")
            {
                kw = TK::Else;
            }
            else if (word == "for")
            {
                kw = TK::For;
            }
            else if (word == "while")
            {
                kw = TK::While;
            }
            else if (word == "do")
            {
                kw = TK::Do;
            }
            else if (word == "switch")
            {
                kw = TK::Switch;
            }
            else if (word == "case")
            {
                kw = TK::Case;
            }
            else if (word == "default")
            {
                kw = TK::Default;
            }
            else if (word == "break")
            {
                kw = TK::Break;
            }
            else if (word == "continue")
            {
                kw = TK::Continue;
            }
            else if (word == "return")
            {
                kw = TK::Return;
            }
            else if (word == "print")
            {
                kw = TK::Print;
            }
            else if (word == "true")
            {
                kw = TK::True;
            }
            else if (word == "false")
            {
                kw = TK::False;
            }
            else if (word == "null")
            {
                kw = TK::Null;
            }
            else if (word == "class")
            {
                kw = TK::Class;
            }
            else if (word == "public")
            {
                kw = TK::Public;
            }
            else if (word == "private")
            {
                kw = TK::Private;
            }
            else if (word == "new")
            {
                kw = TK::New;
            }
            else if (word == "const")
            {
                kw = TK::Const;
            }
            Token t{kw, word};
            t.declaredType = dt;
            if (kw == TK::True || kw == TK::False)
            {
                t.type = TK::Bool;
            }
            if (kw == TK::Null)
            {
                t.type = TK::Null;
            }
            r.toks.push_back(t);
            continue;
        }
        std::string two = src.substr(i, 2);
        if (two == "==")
        {
            r.toks.push_back({TK::EqEq, two});
            i += 2;
            continue;
        }
        if (two == "!=")
        {
            r.toks.push_back({TK::BangEq, two});
            i += 2;
            continue;
        }
        if (two == "<=")
        {
            r.toks.push_back({TK::LtEq, two});
            i += 2;
            continue;
        }
        if (two == ">=")
        {
            r.toks.push_back({TK::GtEq, two});
            i += 2;
            continue;
        }
        if (two == "++")
        {
            r.toks.push_back({TK::PlusPlus, two});
            i += 2;
            continue;
        }
        if (two == "--")
        {
            r.toks.push_back({TK::MinusMinus, two});
            i += 2;
            continue;
        }
        if (two == "+=")
        {
            r.toks.push_back({TK::PlusAssign, two});
            i += 2;
            continue;
        }
        if (two == "-=")
        {
            r.toks.push_back({TK::MinusAssign, two});
            i += 2;
            continue;
        }
        if (two == "*=")
        {
            r.toks.push_back({TK::StarAssign, two});
            i += 2;
            continue;
        }
        if (two == "/=")
        {
            r.toks.push_back({TK::SlashAssign, two});
            i += 2;
            continue;
        }
        if (two == "&&")
        {
            r.toks.push_back({TK::And, two});
            i += 2;
            continue;
        }
        if (two == "||")
        {
            r.toks.push_back({TK::Or, two});
            i += 2;
            continue;
        }
        if (two == "<<")
        {
            r.toks.push_back({TK::LShift, two});
            i += 2;
            continue;
        }
        if (two == ">>")
        {
            r.toks.push_back({TK::RShift, two});
            i += 2;
            continue;
        }
        if (two == "::")
        {
            r.toks.push_back({TK::ColonColon, two});
            i += 2;
            continue;
        }
        if (two == "->")
        {
            r.toks.push_back({TK::Arrow, two});
            i += 2;
            continue;
        }
        switch (c)
        {
            case '+':
                r.toks.push_back({TK::Plus, "+"});
                break;
            case '-':
                r.toks.push_back({TK::Minus, "-"});
                break;
            case '*':
                r.toks.push_back({TK::Star, "*"});
                break;
            case '/':
                r.toks.push_back({TK::Slash, "/"});
                break;
            case '%':
                r.toks.push_back({TK::Percent, "%"});
                break;
            case '<':
                r.toks.push_back({TK::Lt, "<"});
                break;
            case '>':
                r.toks.push_back({TK::Gt, ">"});
                break;
            case '=':
                r.toks.push_back({TK::Assign, "="});
                break;
            case '!':
                r.toks.push_back({TK::Not, "!"});
                break;
            case '&':
                r.toks.push_back({TK::Amp, "&"});
                break;
            case '|':
                r.toks.push_back({TK::Pipe, "|"});
                break;
            case '^':
                r.toks.push_back({TK::Caret, "^"});
                break;
            case '~':
                r.toks.push_back({TK::Tilde, "~"});
                break;
            case '(':
                r.toks.push_back({TK::LParen, "("});
                break;
            case ')':
                r.toks.push_back({TK::RParen, ")"});
                break;
            case '{':
                r.toks.push_back({TK::LBrace, "{"});
                break;
            case '}':
                r.toks.push_back({TK::RBrace, "}"});
                break;
            case '[':
                r.toks.push_back({TK::LBracket, "["});
                break;
            case ']':
                r.toks.push_back({TK::RBracket, "]"});
                break;
            case ';':
                r.toks.push_back({TK::Semicolon, ";"});
                break;
            case ',':
                r.toks.push_back({TK::Comma, ","});
                break;
            case '.':
                r.toks.push_back({TK::Dot, "."});
                break;
            case ':':
                r.toks.push_back({TK::Colon, ":"});
                break;
            case '?':
                r.toks.push_back({TK::Question, "?"});
                break;
            default:
                r.err = "Unexpected char: " + std::string(1, c);
                return r;
        }
        i++;
    }
    return r;
}

// AST
struct Node
{
    virtual ~Node() = default;
};
struct Expr : Node
{
};
struct Stmt : Node
{
};

struct NumberLit : Expr
{
    double v;
    FSType t;
    NumberLit(double x) : v(x), t(FSType::Double)
    {
    }
};
struct StringLit : Expr
{
    std::string v;
    StringLit(std::string s) : v(std::move(s))
    {
    }
};
struct BoolLit : Expr
{
    bool v;
    BoolLit(bool b) : v(b)
    {
    }
};
struct NullLit : Expr
{
};
struct Ident : Expr
{
    std::string name;
    Ident(std::string n) : name(std::move(n))
    {
    }
};
struct Binary : Expr
{
    TK op;
    std::shared_ptr<Expr> l, r;
    Binary(TK o, std::shared_ptr<Expr> a, std::shared_ptr<Expr> b) : op(o), l(a), r(b)
    {
    }
};
struct Unary : Expr
{
    TK op;
    std::shared_ptr<Expr> e;
    // `i++` and `i--` return the value before the update; the prefix forms
    // return the updated value. The parser used to lose that distinction, so
    // an expression such as `int old = i++;` silently returned the new value.
    bool postfix = false;
    Unary(TK o, std::shared_ptr<Expr> x) : op(o), e(x)
    {
    }
};
struct Call : Expr
{
    std::string callee;
    std::vector<std::shared_ptr<Expr>> args;
};
struct IndexExpr : Expr
{
    std::shared_ptr<Expr> target;
    std::shared_ptr<Expr> index;
};
struct ArrayLit : Expr
{
    std::vector<std::shared_ptr<Expr>> items;
};
struct Assign : Expr
{
    std::string name;
    std::shared_ptr<Expr> value;
    TK op = TK::Assign;
};
struct NewExpr : Expr
{
    std::string typeName;
    std::vector<std::shared_ptr<Expr>> args;
};
struct Ternary : Expr
{
    std::shared_ptr<Expr> cond, thenE, elseE;
};

struct Block : Stmt
{
    std::vector<std::shared_ptr<Stmt>> stmts;
};
struct VarDecl : Stmt
{
    FSType type;
    std::string name;
    std::shared_ptr<Expr> init;
    bool isConst = false;
};
struct ExprStmt : Stmt
{
    std::shared_ptr<Expr> expr;
};
struct IfStmt : Stmt
{
    std::shared_ptr<Expr> cond;
    std::shared_ptr<Block> thenB;
    std::shared_ptr<Block> elseB;
};
struct ForStmt : Stmt
{
    std::shared_ptr<Stmt> init;
    std::shared_ptr<Expr> cond;
    std::shared_ptr<Expr> inc;
    std::shared_ptr<Block> body;
};
struct WhileStmt : Stmt
{
    std::shared_ptr<Expr> cond;
    std::shared_ptr<Block> body;
};
struct DoWhileStmt : Stmt
{
    std::shared_ptr<Block> body;
    std::shared_ptr<Expr> cond;
};
struct SwitchStmt : Stmt
{
    std::shared_ptr<Expr> value;
    struct Case
    {
        std::shared_ptr<Expr> pattern;
        std::shared_ptr<Block> body;
        bool isDefault = false;
    };
    std::vector<Case> cases;
};
struct ReturnStmt : Stmt
{
    std::shared_ptr<Expr> value;
};
struct BreakStmt : Stmt
{
};
struct ContinueStmt : Stmt
{
};
struct PrintStmt : Stmt
{
    std::shared_ptr<Expr> expr;
};
struct FnDecl : Stmt
{
    FSType returnType;
    std::string name;
    struct Param
    {
        FSType type;
        std::string name;
    };
    std::vector<Param> params;
    std::shared_ptr<Block> body;
};

struct Program : Node
{
    std::vector<std::shared_ptr<Stmt>> stmts;
};

// Parser
struct Parser
{
    std::vector<Token> toks;
    size_t pos = 0;
    std::string err;
    Token& Cur()
    {
        if (pos >= toks.size())
        {
            return toks.back();
        }
        return toks[pos];
    }
    void Advance()
    {
        if (pos + 1 < toks.size())
        {
            pos++;
        }
    }
    bool Check(TK t)
    {
        return Cur().type == t;
    }
    bool Match(TK t)
    {
        if (Check(t))
        {
            Advance();
            return true;
        }
        return false;
    }
    void Expect(TK t, const char* msg)
    {
        if (!Match(t))
        {
            if (err.empty())
            {
                err = msg;
            }
        }
    }

    std::shared_ptr<Program> ParseProgram()
    {
        auto p = std::make_shared<Program>();
        while (!Check(TK::End))
        {
            auto s = ParseStmt();
            if (s)
            {
                p->stmts.push_back(s);
            }
            else
            {
                if (err.empty())
                {
                    err = "bad statement";
                }
                break;
            }
        }
        return p;
    }
    std::shared_ptr<Stmt> ParseStmt()
    {
        // type declaration: int/float/double/string/bool/void followed by ident
        if (IsTypeToken(Cur().type))
        {
            FSType t = Cur().declaredType;
            Advance();
            // array type: int[]
            if (Check(TK::LBracket))
            {
                Advance();
                Expect(TK::RBracket, "expected ]");
                t = FSType::Array;
            }
            if (!Check(TK::Ident))
            {
                if (err.empty())
                {
                    err = "expected identifier after type";
                }
                return nullptr;
            }
            std::string name = Cur().text;
            // check if next is ( -> function
            // peek ahead
            size_t peek = pos + 1;
            bool isFunc = (peek < toks.size() && toks[peek].type == TK::LParen);
            if (isFunc)
            {
                // revert pos to before type? We already advanced. Parse function.
                // pos currently at ident (name). advance past name.
                Advance();
                auto f = std::make_shared<FnDecl>();
                f->returnType = t;
                f->name = name;
                Expect(TK::LParen, "expected (");
                if (!Check(TK::RParen))
                {
                    do
                    {
                        if (!IsTypeToken(Cur().type))
                        {
                            if (err.empty())
                            {
                                err = "expected type in param";
                            }
                            return nullptr;
                        }
                        FSType pt = Cur().declaredType;
                        Advance();
                        if (!Check(TK::Ident))
                        {
                            if (err.empty())
                            {
                                err = "expected param name";
                            }
                            return nullptr;
                        }
                        std::string pn = Cur().text;
                        Advance();
                        FnDecl::Param p;
                        p.type = pt;
                        p.name = pn;
                        f->params.push_back(p);
                    } while (Match(TK::Comma));
                }
                Expect(TK::RParen, "expected )");
                f->body = ParseBlock();
                return f;
            }
            // variable declaration
            auto v = std::make_shared<VarDecl>();
            v->type = t;
            v->name = name;
            Advance();
            if (Match(TK::Assign))
            {
                v->init = ParseExpr();
            }
            // optional semicolon
            Match(TK::Semicolon);
            return v;
        }
        if (Check(TK::Const))
        {
            Advance();
            if (!IsTypeToken(Cur().type))
            {
                if (err.empty())
                {
                    err = "expected type after const";
                }
                return nullptr;
            }
            FSType t = Cur().declaredType;
            Advance();
            if (!Check(TK::Ident))
            {
                if (err.empty())
                {
                    err = "expected name";
                }
                return nullptr;
            }
            std::string name = Cur().text;
            Advance();
            auto v = std::make_shared<VarDecl>();
            v->type = t;
            v->name = name;
            v->isConst = true;
            if (Match(TK::Assign))
            {
                v->init = ParseExpr();
            }
            Match(TK::Semicolon);
            return v;
        }
        if (Check(TK::If))
        {
            Advance();
            Expect(TK::LParen, "expected (");
            auto c = ParseExpr();
            Expect(TK::RParen, "expected )");
            auto s = std::make_shared<IfStmt>();
            s->cond = c;
            s->thenB = ParseBlock();
            if (Check(TK::Else))
            {
                Advance();
                if (Check(TK::If))
                {
                    auto b = std::make_shared<Block>();
                    b->stmts.push_back(ParseStmt());
                    s->elseB = b;
                }
                else
                {
                    s->elseB = ParseBlock();
                }
            }
            return s;
        }
        if (Check(TK::For))
        {
            Advance();
            Expect(TK::LParen, "expected (");
            std::shared_ptr<Stmt> init = nullptr;
            if (IsTypeToken(Cur().type))
            {
                // variable decl
                FSType t = Cur().declaredType;
                Advance();
                if (!Check(TK::Ident))
                {
                    if (err.empty())
                    {
                        err = "expected name";
                    }
                    return nullptr;
                }
                std::string nm = Cur().text;
                Advance();
                auto v = std::make_shared<VarDecl>();
                v->type = t;
                v->name = nm;
                if (Match(TK::Assign))
                {
                    v->init = ParseExpr();
                }
                init = v;
            }
            else if (!Check(TK::Semicolon))
            {
                auto e = ParseExpr();
                auto es = std::make_shared<ExprStmt>();
                es->expr = e;
                init = es;
            }
            Match(TK::Semicolon);
            std::shared_ptr<Expr> cond = nullptr;
            if (!Check(TK::Semicolon))
            {
                cond = ParseExpr();
            }
            Match(TK::Semicolon);
            std::shared_ptr<Expr> inc = nullptr;
            if (!Check(TK::RParen))
            {
                inc = ParseExpr();
            }
            Expect(TK::RParen, "expected )");
            auto s = std::make_shared<ForStmt>();
            s->init = init;
            s->cond = cond;
            s->inc = inc;
            s->body = ParseBlock();
            return s;
        }
        if (Check(TK::While))
        {
            Advance();
            Expect(TK::LParen, "expected (");
            auto c = ParseExpr();
            Expect(TK::RParen, "expected )");
            auto s = std::make_shared<WhileStmt>();
            s->cond = c;
            s->body = ParseBlock();
            return s;
        }
        if (Check(TK::Do))
        {
            Advance();
            auto s = std::make_shared<DoWhileStmt>();
            s->body = ParseBlock();
            Expect(TK::While, "expected while");
            Expect(TK::LParen, "expected (");
            s->cond = ParseExpr();
            Expect(TK::RParen, "expected )");
            Match(TK::Semicolon);
            return s;
        }
        if (Check(TK::Switch))
        {
            Advance();
            Expect(TK::LParen, "expected (");
            auto v = ParseExpr();
            Expect(TK::RParen, "expected )");
            auto s = std::make_shared<SwitchStmt>();
            s->value = v;
            Expect(TK::LBrace, "expected {");
            while (!Check(TK::RBrace) && !Check(TK::End))
            {
                if (Check(TK::Case))
                {
                    Advance();
                    auto pat = ParseExpr();
                    Expect(TK::Colon, "expected :");
                    SwitchStmt::Case c;
                    c.pattern = pat;
                    auto b = std::make_shared<Block>();
                    while (!Check(TK::Case) && !Check(TK::Default) && !Check(TK::RBrace) && !Check(TK::End))
                    {
                        auto st = ParseStmt();
                        if (st)
                        {
                            b->stmts.push_back(st);
                        }
                    }
                    c.body = b;
                    s->cases.push_back(c);
                }
                else if (Check(TK::Default))
                {
                    Advance();
                    Expect(TK::Colon, "expected :");
                    SwitchStmt::Case c;
                    c.isDefault = true;
                    auto b = std::make_shared<Block>();
                    while (!Check(TK::Case) && !Check(TK::Default) && !Check(TK::RBrace) && !Check(TK::End))
                    {
                        auto st = ParseStmt();
                        if (st)
                        {
                            b->stmts.push_back(st);
                        }
                    }
                    c.body = b;
                    s->cases.push_back(c);
                }
                else
                {
                    if (err.empty())
                    {
                        err = "expected case/default";
                    }
                    break;
                }
            }
            Expect(TK::RBrace, "expected }");
            return s;
        }
        if (Check(TK::Return))
        {
            Advance();
            auto s = std::make_shared<ReturnStmt>();
            if (!Check(TK::Semicolon))
            {
                s->value = ParseExpr();
            }
            Match(TK::Semicolon);
            return s;
        }
        if (Check(TK::Break))
        {
            Advance();
            Match(TK::Semicolon);
            return std::make_shared<BreakStmt>();
        }
        if (Check(TK::Continue))
        {
            Advance();
            Match(TK::Semicolon);
            return std::make_shared<ContinueStmt>();
        }
        if (Check(TK::Print))
        {
            Advance();
            Expect(TK::LParen, "expected (");
            auto e = ParseExpr();
            Expect(TK::RParen, "expected )");
            Match(TK::Semicolon);
            auto s = std::make_shared<PrintStmt>();
            s->expr = e;
            return s;
        }
        if (Check(TK::LBrace))
        {
            return ParseBlockStmt();
        }
        // expression statement
        auto e = ParseExpr();
        Match(TK::Semicolon);
        auto s = std::make_shared<ExprStmt>();
        s->expr = e;
        return s;
    }

    std::shared_ptr<Stmt> ParseBlockStmt()
    {
        auto b = ParseBlock();
        // wrap block? We need a Stmt. Use a dummy ExprStmt? Instead, change parseStmt callers expect Stmt.
        // We'll handle: parseBlock returns Block (Stmt subclass? Block is Stmt). Return as Stmt.
        return b;
    }

    std::shared_ptr<Block> ParseBlock()
    {
        auto b = std::make_shared<Block>();
        if (!Match(TK::LBrace))
        {
            if (err.empty())
            {
                err = "expected {";
            }
            return b;
        }
        while (!Check(TK::RBrace) && !Check(TK::End))
        {
            auto st = ParseStmt();
            if (st)
            {
                b->stmts.push_back(st);
            }
            else
            {
                if (err.empty())
                {
                    err = "bad statement";
                }
                break;
            }
        }
        Expect(TK::RBrace, "expected }");
        return b;
    }

    // expressions
    bool IsTypeToken(TK t)
    {
        return t == TK::Int || t == TK::Float || t == TK::Double || t == TK::StringKw || t == TK::BoolKw ||
               t == TK::Void;
    }

    std::shared_ptr<Expr> ParseExpr()
    {
        return ParseAssign();
    }
    std::shared_ptr<Expr> ParseAssign()
    {
        auto e = ParseTernary();
        if (Check(TK::Assign) || Check(TK::PlusAssign) || Check(TK::MinusAssign) || Check(TK::StarAssign) ||
            Check(TK::SlashAssign))
        {
            TK op = Cur().type;
            Advance();
            if (auto id = std::dynamic_pointer_cast<Ident>(e))
            {
                auto a = std::make_shared<Assign>();
                a->name = id->name;
                a->value = ParseAssign();
                a->op = op;
                return a;
            }
        }
        return e;
    }
    std::shared_ptr<Expr> ParseTernary()
    {
        auto e = ParseOr();
        if (Match(TK::Question))
        {
            auto t = ParseExpr();
            Expect(TK::Colon, "expected :");
            auto el = ParseExpr();
            auto tr = std::make_shared<Ternary>();
            tr->cond = e;
            tr->thenE = t;
            tr->elseE = el;
            return tr;
        }
        return e;
    }
    std::shared_ptr<Expr> ParseOr()
    {
        auto e = ParseAnd();
        while (Check(TK::Or))
        {
            Advance();
            auto r = ParseAnd();
            e = std::make_shared<Binary>(TK::Or, e, r);
        }
        return e;
    }
    std::shared_ptr<Expr> ParseAnd()
    {
        auto e = ParseEq();
        while (Check(TK::And))
        {
            Advance();
            auto r = ParseEq();
            e = std::make_shared<Binary>(TK::And, e, r);
        }
        return e;
    }
    std::shared_ptr<Expr> ParseEq()
    {
        auto e = ParseRel();
        while (Check(TK::EqEq) || Check(TK::BangEq))
        {
            TK op = Cur().type;
            Advance();
            auto r = ParseRel();
            e = std::make_shared<Binary>(op, e, r);
        }
        return e;
    }
    std::shared_ptr<Expr> ParseRel()
    {
        auto e = ParseShift();
        while (Check(TK::Lt) || Check(TK::Gt) || Check(TK::LtEq) || Check(TK::GtEq))
        {
            TK op = Cur().type;
            Advance();
            auto r = ParseShift();
            e = std::make_shared<Binary>(op, e, r);
        }
        return e;
    }
    std::shared_ptr<Expr> ParseShift()
    {
        auto e = ParseAdd();
        while (Check(TK::LShift) || Check(TK::RShift))
        {
            TK op = Cur().type;
            Advance();
            auto r = ParseAdd();
            e = std::make_shared<Binary>(op, e, r);
        }
        return e;
    }
    std::shared_ptr<Expr> ParseAdd()
    {
        auto e = ParseMul();
        while (Check(TK::Plus) || Check(TK::Minus))
        {
            TK op = Cur().type;
            Advance();
            auto r = ParseMul();
            e = std::make_shared<Binary>(op, e, r);
        }
        return e;
    }
    std::shared_ptr<Expr> ParseMul()
    {
        auto e = ParseUnary();
        while (Check(TK::Star) || Check(TK::Slash) || Check(TK::Percent))
        {
            TK op = Cur().type;
            Advance();
            auto r = ParseUnary();
            e = std::make_shared<Binary>(op, e, r);
        }
        return e;
    }
    std::shared_ptr<Expr> ParseUnary()
    {
        if (Check(TK::Minus) || Check(TK::Not) || Check(TK::Tilde) || Check(TK::PlusPlus) || Check(TK::MinusMinus))
        {
            TK op = Cur().type;
            Advance();
            auto e = ParseUnary();
            if (op == TK::PlusPlus || op == TK::MinusMinus)
            {
                // prefix increment handled as unary wrapper; interpreter treats as pre-inc
            }
            return std::make_shared<Unary>(op, e);
        }
        return ParsePostfix();
    }
    std::shared_ptr<Expr> ParsePostfix()
    {
        auto e = ParsePrimary();
        while (true)
        {
            if (Check(TK::PlusPlus) || Check(TK::MinusMinus))
            {
                TK op = Cur().type;
                Advance();
                auto u = std::make_shared<Unary>(op, e);
                u->postfix = true;
                e = u;
            }
            else if (Check(TK::LParen))
            {
                Advance();
                auto call = std::make_shared<Call>();
                if (auto id = std::dynamic_pointer_cast<Ident>(e))
                {
                    call->callee = id->name;
                }
                else if (err.empty())
                {
                    err = "call target must be an identifier";
                }
                if (!Check(TK::RParen))
                {
                    do
                    {
                        call->args.push_back(ParseExpr());
                    } while (Match(TK::Comma));
                }
                Expect(TK::RParen, "expected )");
                e = call;
            }
            else if (Check(TK::LBracket))
            {
                Advance();
                auto idx = ParseExpr();
                Expect(TK::RBracket, "expected ]");
                auto ie = std::make_shared<IndexExpr>();
                ie->target = e;
                ie->index = idx;
                e = ie;
            }
            else if (Check(TK::Dot))
            {
                Advance();
                std::string prop = Cur().text;
                Expect(TK::Ident, "expected prop");
                if (auto id = std::dynamic_pointer_cast<Ident>(e))
                {
                    id->name = id->name + "." + prop;
                }
            }
            else
            {
                break;
            }
        }
        return e;
    }
    std::shared_ptr<Expr> ParsePrimary()
    {
        if (Check(TK::Number))
        {
            double n = Cur().number;
            Advance();
            auto nl = std::make_shared<NumberLit>(n);
            return nl;
        }
        if (Check(TK::String))
        {
            std::string s = Cur().text;
            Advance();
            return std::make_shared<StringLit>(s);
        }
        if (Check(TK::True))
        {
            Advance();
            return std::make_shared<BoolLit>(true);
        }
        if (Check(TK::False))
        {
            Advance();
            return std::make_shared<BoolLit>(false);
        }
        if (Check(TK::Null))
        {
            Advance();
            return std::make_shared<NullLit>();
        }
        if (Check(TK::Ident))
        {
            std::string n = Cur().text;
            Advance();
            return std::make_shared<Ident>(n);
        }
        if (Check(TK::LParen))
        {
            Advance();
            auto e = ParseExpr();
            Expect(TK::RParen, "expected )");
            return e;
        }
        if (Check(TK::LBracket))
        {
            Advance();
            auto arr = std::make_shared<ArrayLit>();
            if (!Check(TK::RBracket))
            {
                do
                {
                    arr->items.push_back(ParseExpr());
                } while (Match(TK::Comma));
            }
            Expect(TK::RBracket, "expected ]");
            return arr;
        }
        if (Check(TK::New))
        {
            Advance();
            std::string tn = Cur().text;
            Expect(TK::Ident, "expected type");
            auto ne = std::make_shared<NewExpr>();
            ne->typeName = tn;
            if (Match(TK::LParen))
            {
                if (!Check(TK::RParen))
                {
                    do
                    {
                        ne->args.push_back(ParseExpr());
                    } while (Match(TK::Comma));
                }
                Expect(TK::RParen, "expected )");
            }
            return ne;
        }
        if (err.empty())
        {
            err = "unexpected token in expression";
        }
        Advance();
        return std::make_shared<NullLit>();
    }
};

// Interpreter
struct Env
{
    struct VarInfo
    {
        FSType type;
        FSValue value;
        bool isConst;
    };
    std::map<std::string, VarInfo> vars;
    std::shared_ptr<Env> parent;
    bool constViolation = false;
    std::string constName;
    FSValue Get(const std::string& n) const
    {
        auto it = vars.find(n);
        if (it != vars.end())
        {
            return it->second.value;
        }
        if (parent)
        {
            return parent->Get(n);
        }
        return FSValue{};
    }
    FSType GetType(const std::string& n) const
    {
        auto it = vars.find(n);
        if (it != vars.end())
        {
            return it->second.type;
        }
        if (parent)
        {
            return parent->GetType(n);
        }
        return FSType::Unknown;
    }
    bool Has(const std::string& n) const
    {
        if (vars.count(n))
        {
            return true;
        }
        return parent ? parent->Has(n) : false;
    }
    bool Set(const std::string& n, FSValue v, bool checkType = true, std::string* violated = nullptr)
    {
        if (vars.count(n))
        {
            if (vars[n].isConst)
            {
                constViolation = true;
                constName = n;
                if (violated)
                {
                    *violated = n;
                }
                return false;
            }
            vars[n].value = Coerce(v, vars[n].type);
            return true;
        }
        if (parent && parent->Has(n))
        {
            // Propagate both the result and the offending name from the scope
            // that actually owns the variable.
            return parent->Set(n, v, checkType, violated);
        }
        vars[n] = {FSType::Unknown, v, false};
        return true;
    }
    void Declare(const std::string& n, FSType t, FSValue v, bool isConst = false)
    {
        vars[n] = {t, Coerce(v, t), isConst};
    }
};

struct FuncObj
{
    FSType returnType;
    std::vector<std::pair<FSType, std::string>> params;
    std::shared_ptr<Block> body;
    std::shared_ptr<Env> closure;
};

struct Interpreter
{
    std::shared_ptr<Env> global;
    std::map<std::string, FuncObj> functions;
    std::map<std::string, std::function<FSValue(const std::vector<FSValue>&)>> nativeFns;
    // Optional sink for the language's own `print` statement. The editor's
    // console captures it; a game can route it to its log or an in-game console.
    // When empty, print keeps the historical BOY_INFO behaviour.
    std::function<void(const std::string&)> printSink;
    std::string error;
    FSValue returnValue = FSValue{};
    struct ReturnExc
    {
        FSValue v;
    };
    struct BreakExc
    {
    };
    struct ContinueExc
    {
    };
    struct BudgetExc
    {
    };

    // Execution budget: a malformed script (or an intentional `while(true)`)
    // must never freeze the engine.
    long long steps = 0;
    long long maxSteps = 500000;
    int depth = 0;
    int maxDepth = 128;
    int callDepth = 0;
    int maxCallDepth = 64;

    Interpreter()
    {
        global = std::make_shared<Env>();
    }

    void Tick()
    {
        if (++steps > maxSteps)
        {
            throw BudgetExc{};
        }
    }

    // type checking helper
    void CheckAssignType(FSType declared, const FSValue& v, const std::string& ctx)
    {
        if (declared == FSType::Unknown)
        {
            return;
        }
        FSType actual = ValueType(v);
        if (!TypeCompatible(declared, actual))
        {
            error = "type mismatch in " + ctx + ": expected " + TypeToString(declared) + " got " + TypeToString(actual);
        }
    }

    // Shared by evalExpr's call path and the public FemboyScript::Call().
    // Keeping the argument binding in one place means a runtime-dispatched
    // OnUpdate cannot behave differently from an in-script call.
    FSValue InvokeFunction(const std::string& name, const std::vector<FSValue>& args)
    {
        if (nativeFns.count(name))
        {
            try
            {
                return nativeFns[name](args);
            }
            catch (const std::exception& ex)
            {
                error = "native function '" + name + "' threw: " + ex.what();
                return FSValue{};
            }
            catch (...)
            {
                error = "native function '" + name + "' threw";
                return FSValue{};
            }
        }

        auto it = functions.find(name);
        if (it == functions.end())
        {
            error = "unknown function: " + name;
            return FSValue{};
        }

        FuncObj f = it->second;
        if (++callDepth > maxCallDepth)
        {
            --callDepth;
            error = "call depth exceeded: " + name;
            return FSValue{};
        }

        struct CallGuard
        {
            int& d;
            explicit CallGuard(int& v) : d(v)
            {
            }
            ~CallGuard()
            {
                --d;
            }
        } cg(callDepth);

        if (args.size() != f.params.size())
        {
            error = "function '" + name + "' expects " + std::to_string(f.params.size()) + " arguments, got " +
                    std::to_string(args.size());
            return FSValue{};
        }

        auto newEnv = std::make_shared<Env>();
        newEnv->parent = f.closure;
        for (size_t i = 0; i < f.params.size(); ++i)
        {
            CheckAssignType(f.params[i].first, args[i], "argument " + f.params[i].second);
            if (!error.empty())
            {
                return FSValue{};
            }
            newEnv->Declare(f.params[i].second, f.params[i].first, args[i]);
        }

        FSValue result;
        try
        {
            ExecBlock(f.body, newEnv);
        }
        catch (ReturnExc& re)
        {
            result = re.v;
        }
        catch (BreakExc&)
        {
            // A break inside a called function must not escape into the
            // caller's loop and break that loop instead of this function.
            error = "break outside loop or switch in function '" + name + "'";
            return FSValue{};
        }
        catch (ContinueExc&)
        {
            error = "continue outside loop in function '" + name + "'";
            return FSValue{};
        }

        // Return types were recorded but never checked, which meant a function
        // declared `int` could hand back a string and the first symptom was a
        // type error at the call site. Check it where the function ends.
        CheckAssignType(f.returnType, result, "return value of " + name);
        if (!error.empty())
        {
            return FSValue{};
        }
        return result;
    }

    FemboyScript::Result Invoke(const std::string& name, const std::vector<FSValue>& args)
    {
        error.clear();
        steps = 0;
        depth = 0;
        callDepth = 0;
        returnValue = FSValue{};
        global->constViolation = false;
        global->constName.clear();

        try
        {
            FSValue value = InvokeFunction(name, args);
            if (!error.empty())
            {
                return {false, error, FSValue{}};
            }
            return {true, "", value};
        }
        catch (ReturnExc& re)
        {
            return {true, "", re.v};
        }
        catch (BudgetExc&)
        {
            return {false, "script exceeded execution budget", FSValue{}};
        }
        catch (BreakExc&)
        {
            return {false, "break outside loop or switch", FSValue{}};
        }
        catch (ContinueExc&)
        {
            return {false, "continue outside loop", FSValue{}};
        }
        catch (const std::exception& e)
        {
            return {false, std::string("runtime error: ") + e.what(), FSValue{}};
        }
        catch (...)
        {
            return {false, "runtime error: " + error, FSValue{}};
        }
    }

    FSValue EvalExpr(std::shared_ptr<Expr> e, std::shared_ptr<Env> env)
    {
        if (!e)
        {
            return FSValue{};
        }
        Tick();
        if (++depth > maxDepth)
        {
            --depth;
            throw BudgetExc{};
        }

        struct DepthGuard
        {
            int& d;
            explicit DepthGuard(int& v) : d(v)
            {
            }
            ~DepthGuard()
            {
                --d;
            }
        } guard(depth);

        if (auto n = std::dynamic_pointer_cast<NumberLit>(e))
        {
            return n->v;
        }
        if (auto s = std::dynamic_pointer_cast<StringLit>(e))
        {
            return s->v;
        }
        if (auto b = std::dynamic_pointer_cast<BoolLit>(e))
        {
            return b->v;
        }
        if (std::dynamic_pointer_cast<NullLit>(e))
        {
            return FSValue{};
        }
        if (auto id = std::dynamic_pointer_cast<Ident>(e))
        {
            // property path
            std::string name = id->name;
            return env->Get(name);
        }
        if (auto a = std::dynamic_pointer_cast<ArrayLit>(e))
        {
            std::vector<std::shared_ptr<FSValueHolder>> vec;
            for (auto& it : a->items)
            {
                auto h = std::make_shared<FSValueHolder>();
                h->value = EvalExpr(it, env);
                vec.push_back(h);
            }
            return vec;
        }
        if (auto u = std::dynamic_pointer_cast<Unary>(e))
        {
            if (u->op == TK::PlusPlus || u->op == TK::MinusMinus)
            {
                // prefix
                if (auto id = std::dynamic_pointer_cast<Ident>(u->e))
                {
                    FSValue cur = env->Get(id->name);
                    double d = std::holds_alternative<double>(cur) ? std::get<double>(cur) : 0;
                    const FSValue oldValue = d;
                    if (u->op == TK::PlusPlus)
                    {
                        d += 1;
                    }
                    else
                    {
                        d -= 1;
                    }
                    FSValue nv = d;
                    std::string violated;
                    if (!env->Set(id->name, nv, false, &violated))
                    {
                        error = "cannot assign to const variable: " + violated;
                        return FSValue{};
                    }
                    return u->postfix ? oldValue : nv;
                }
            }
            FSValue v = EvalExpr(u->e, env);
            if (u->op == TK::Minus)
            {
                if (std::holds_alternative<double>(v))
                {
                    return -std::get<double>(v);
                }
            }
            if (u->op == TK::Not)
            {
                return !IsTruthy(v);
            }
            if (u->op == TK::Tilde)
            {
                if (std::holds_alternative<double>(v))
                {
                    return (double)(~(long long)std::get<double>(v));
                }
            }
            return v;
        }
        if (auto bin = std::dynamic_pointer_cast<Binary>(e))
        {
            if (bin->op == TK::And)
            {
                FSValue l = EvalExpr(bin->l, env);
                if (!IsTruthy(l))
                {
                    return false;
                }
                return IsTruthy(EvalExpr(bin->r, env));
            }
            if (bin->op == TK::Or)
            {
                FSValue l = EvalExpr(bin->l, env);
                if (IsTruthy(l))
                {
                    return true;
                }
                return IsTruthy(EvalExpr(bin->r, env));
            }
            FSValue l = EvalExpr(bin->l, env), r = EvalExpr(bin->r, env);
            if (bin->op == TK::Plus)
            {
                if (std::holds_alternative<std::string>(l) || std::holds_alternative<std::string>(r))
                {
                    return ToString(l) + ToString(r);
                }
                if (std::holds_alternative<double>(l) && std::holds_alternative<double>(r))
                {
                    return std::get<double>(l) + std::get<double>(r);
                }
            }
            if (bin->op == TK::Minus && std::holds_alternative<double>(l) && std::holds_alternative<double>(r))
            {
                return std::get<double>(l) - std::get<double>(r);
            }
            if (bin->op == TK::Star && std::holds_alternative<double>(l) && std::holds_alternative<double>(r))
            {
                return std::get<double>(l) * std::get<double>(r);
            }
            if (bin->op == TK::Slash && std::holds_alternative<double>(l) && std::holds_alternative<double>(r))
            {
                double d = std::get<double>(r);
                if (d == 0)
                {
                    error = "div by zero";
                    return FSValue{};
                }
                return std::get<double>(l) / d;
            }
            if (bin->op == TK::Percent && std::holds_alternative<double>(l) && std::holds_alternative<double>(r))
            {
                double d = std::get<double>(r);
                if (d == 0)
                {
                    error = "mod by zero";
                    return FSValue{};
                }
                return std::fmod(std::get<double>(l), d);
            }
            if (bin->op == TK::EqEq)
            {
                return Equals(l, r);
            }
            if (bin->op == TK::BangEq)
            {
                return !Equals(l, r);
            }
            if (bin->op == TK::Lt && std::holds_alternative<double>(l) && std::holds_alternative<double>(r))
            {
                return std::get<double>(l) < std::get<double>(r);
            }
            if (bin->op == TK::Gt && std::holds_alternative<double>(l) && std::holds_alternative<double>(r))
            {
                return std::get<double>(l) > std::get<double>(r);
            }
            if (bin->op == TK::LtEq && std::holds_alternative<double>(l) && std::holds_alternative<double>(r))
            {
                return std::get<double>(l) <= std::get<double>(r);
            }
            if (bin->op == TK::GtEq && std::holds_alternative<double>(l) && std::holds_alternative<double>(r))
            {
                return std::get<double>(l) >= std::get<double>(r);
            }
            return FSValue{};
        }
        if (auto asg = std::dynamic_pointer_cast<Assign>(e))
        {
            FSValue v = EvalExpr(asg->value, env);
            if (asg->op == TK::PlusAssign)
            {
                FSValue cur = env->Get(asg->name);
                if (std::holds_alternative<double>(cur) && std::holds_alternative<double>(v))
                {
                    v = std::get<double>(cur) + std::get<double>(v);
                }
            }
            else if (asg->op == TK::MinusAssign)
            {
                FSValue cur = env->Get(asg->name);
                if (std::holds_alternative<double>(cur) && std::holds_alternative<double>(v))
                {
                    v = std::get<double>(cur) - std::get<double>(v);
                }
            }
            else if (asg->op == TK::StarAssign)
            {
                FSValue cur = env->Get(asg->name);
                if (std::holds_alternative<double>(cur) && std::holds_alternative<double>(v))
                {
                    v = std::get<double>(cur) * std::get<double>(v);
                }
            }
            else if (asg->op == TK::SlashAssign)
            {
                FSValue cur = env->Get(asg->name);
                if (std::holds_alternative<double>(cur) && std::holds_alternative<double>(v))
                {
                    double d = std::get<double>(v);
                    if (d != 0)
                    {
                        v = std::get<double>(cur) / d;
                    }
                }
            }
            if (env->Has(asg->name))
            {
                FSType dt = env->GetType(asg->name);
                CheckAssignType(dt, v, "assignment to " + asg->name);
                if (!error.empty())
                {
                    return FSValue{};
                }
            }
            std::string violated;
            if (!env->Set(asg->name, v, false, &violated))
            {
                error = "cannot assign to const variable: " + violated;
                return FSValue{};
            }
            return v;
        }
        if (auto c = std::dynamic_pointer_cast<Call>(e))
        {
            std::vector<FSValue> args;
            args.reserve(c->args.size());
            for (auto& a : c->args)
            {
                args.push_back(EvalExpr(a, env));
                if (!error.empty())
                {
                    return FSValue{};
                }
            }
            return InvokeFunction(c->callee, args);
        }
        if (auto ie = std::dynamic_pointer_cast<IndexExpr>(e))
        {
            FSValue t = EvalExpr(ie->target, env);
            FSValue idx = EvalExpr(ie->index, env);
            if (std::holds_alternative<std::vector<std::shared_ptr<FSValueHolder>>>(t))
            {
                auto& vec = std::get<std::vector<std::shared_ptr<FSValueHolder>>>(t);
                if (std::holds_alternative<double>(idx))
                {
                    const double di = std::get<double>(idx);
                    if (di >= 0.0 && di < static_cast<double>(vec.size()))
                    {
                        const size_t i = static_cast<size_t>(di);
                        if (vec[i])
                        {
                            return vec[i]->value;
                        }
                    }
                }
                error = "array index out of range";
                return FSValue{};
            }
            return FSValue{};
        }
        if (auto tr = std::dynamic_pointer_cast<Ternary>(e))
        {
            FSValue c = EvalExpr(tr->cond, env);
            return IsTruthy(c) ? EvalExpr(tr->thenE, env) : EvalExpr(tr->elseE, env);
        }
        if (auto ne = std::dynamic_pointer_cast<NewExpr>(e))
        {
            // FemboyScript is a data-oriented language: arrays and injected
            // natives cover structured data. Pretending to construct an object
            // and silently returning null would hide a real authoring mistake.
            if (error.empty())
            {
                error = "object construction is not supported: use arrays or a native factory";
            }
            return FSValue{};
        }
        return FSValue{};
    }

    void ExecBlock(std::shared_ptr<Block> b, std::shared_ptr<Env> env)
    {
        if (!b)
        {
            return;
        }
        for (auto& s : b->stmts)
        {
            Tick();
            ExecStmt(s, env);
            if (!error.empty())
            {
                return;
            }
        }
    }
    void ExecStmt(std::shared_ptr<Stmt> s, std::shared_ptr<Env> env)
    {
        if (!s)
        {
            return;
        }
        Tick();
        if (auto vd = std::dynamic_pointer_cast<VarDecl>(s))
        {
            FSValue v = vd->init ? EvalExpr(vd->init, env) : FSValue{};
            CheckAssignType(vd->type, v, "variable " + vd->name);
            if (!error.empty())
            {
                return;
            }
            env->Declare(vd->name, vd->type, v, vd->isConst);
        }
        else if (auto es = std::dynamic_pointer_cast<ExprStmt>(s))
        {
            EvalExpr(es->expr, env);
        }
        else if (auto ifs = std::dynamic_pointer_cast<IfStmt>(s))
        {
            FSValue c = EvalExpr(ifs->cond, env);
            if (IsTruthy(c))
            {
                ExecBlock(ifs->thenB, env);
            }
            else if (ifs->elseB)
            {
                ExecBlock(ifs->elseB, env);
            }
        }
        else if (auto fs = std::dynamic_pointer_cast<ForStmt>(s))
        {
            auto loopEnv = std::make_shared<Env>();
            loopEnv->parent = env;
            if (fs->init)
            {
                if (auto vd = std::dynamic_pointer_cast<VarDecl>(fs->init))
                {
                    FSValue v = vd->init ? EvalExpr(vd->init, env) : FSValue{};
                    if (!error.empty())
                    {
                        return;
                    }
                    CheckAssignType(vd->type, v, "variable " + vd->name);
                    if (!error.empty())
                    {
                        return;
                    }
                    loopEnv->Declare(vd->name, vd->type, v, vd->isConst);
                }
                else
                {
                    ExecStmt(fs->init, loopEnv);
                }
                if (!error.empty())
                {
                    return;
                }
            }
            while (true)
            {
                if (fs->cond)
                {
                    FSValue c = EvalExpr(fs->cond, loopEnv);
                    if (!error.empty())
                    {
                        return;
                    }
                    if (!IsTruthy(c))
                    {
                        break;
                    }
                }
                try
                {
                    ExecBlock(fs->body, loopEnv);
                }
                catch (BreakExc&)
                {
                    break;
                }
                catch (ContinueExc&)
                { /* fall through to inc */
                }
                if (!error.empty())
                {
                    return;
                }
                if (fs->inc)
                {
                    EvalExpr(fs->inc, loopEnv);
                    if (!error.empty())
                    {
                        return;
                    }
                }
            }
        }
        else if (auto ws = std::dynamic_pointer_cast<WhileStmt>(s))
        {
            while (true)
            {
                const FSValue c = EvalExpr(ws->cond, env);
                if (!error.empty())
                {
                    return;
                }
                if (!IsTruthy(c))
                {
                    break;
                }
                try
                {
                    ExecBlock(ws->body, env);
                }
                catch (BreakExc&)
                {
                    break;
                }
                catch (ContinueExc&)
                {
                    continue;
                }
                if (!error.empty())
                {
                    return;
                }
            }
        }
        else if (auto dw = std::dynamic_pointer_cast<DoWhileStmt>(s))
        {
            do
            {
                try
                {
                    ExecBlock(dw->body, env);
                }
                catch (BreakExc&)
                {
                    break;
                }
                catch (ContinueExc&)
                { /* continue */
                }
                if (!error.empty())
                {
                    return;
                }
                const FSValue c = EvalExpr(dw->cond, env);
                if (!error.empty())
                {
                    return;
                }
                if (!IsTruthy(c))
                {
                    break;
                }
            } while (true);
        }
        else if (auto ss = std::dynamic_pointer_cast<SwitchStmt>(s))
        {
            const FSValue v = EvalExpr(ss->value, env);

            // Find the matching case first. `default` only runs when no case
            // matches, even when it is written before the matching case.
            size_t start = ss->cases.size();
            size_t defaultIndex = ss->cases.size();
            for (size_t i = 0; i < ss->cases.size(); ++i)
            {
                const auto& c = ss->cases[i];
                if (c.isDefault)
                {
                    if (defaultIndex == ss->cases.size())
                    {
                        defaultIndex = i;
                    }
                    continue;
                }
                const FSValue p = EvalExpr(c.pattern, env);
                if (Equals(v, p))
                {
                    start = i;
                    break;
                }
            }
            if (start == ss->cases.size())
            {
                start = defaultIndex;
            }
            if (start < ss->cases.size())
            {
                // C-like fallthrough: keep executing the following case bodies
                // until a `break` is hit.
                try
                {
                    for (size_t i = start; i < ss->cases.size(); ++i)
                    {
                        ExecBlock(ss->cases[i].body, env);
                        if (!error.empty())
                        {
                            return;
                        }
                    }
                }
                catch (BreakExc&)
                {
                    // `break` leaves the switch.
                }
            }
        }
        else if (std::dynamic_pointer_cast<BreakStmt>(s))
        {
            // The parser has produced BreakStmt since the first version, but
            // execStmt never handled it: every `break;` was a no-op, so loops
            // could not exit early and switch cases fell through. These
            // exceptions are caught by the loop and switch executors above.
            throw BreakExc{};
        }
        else if (std::dynamic_pointer_cast<ContinueStmt>(s))
        {
            throw ContinueExc{};
        }
        else if (auto rs = std::dynamic_pointer_cast<ReturnStmt>(s))
        {
            FSValue v = rs->value ? EvalExpr(rs->value, env) : FSValue{};
            returnValue = v;
            throw ReturnExc{v};
        }
        else if (auto ps = std::dynamic_pointer_cast<PrintStmt>(s))
        {
            const std::string text = ToString(EvalExpr(ps->expr, env));
            if (!error.empty())
            {
                return;
            }
            if (printSink)
            {
                printSink(text);
            }
            else
            {
                BOY_INFO("FemboyScript", text);
            }
        }
        else if (auto fd = std::dynamic_pointer_cast<FnDecl>(s))
        {
            FuncObj f;
            f.returnType = fd->returnType;
            f.body = fd->body;
            f.closure = env;
            for (auto& p : fd->params)
            {
                f.params.push_back({p.type, p.name});
            }
            functions[fd->name] = f;
        }
        else if (auto b = std::dynamic_pointer_cast<Block>(s))
        {
            auto newEnv = std::make_shared<Env>();
            newEnv->parent = env;
            ExecBlock(b, newEnv);
        }
    }

    bool IsTruthy(const FSValue& v)
    {
        if (std::holds_alternative<std::monostate>(v))
        {
            return false;
        }
        if (std::holds_alternative<bool>(v))
        {
            return std::get<bool>(v);
        }
        if (std::holds_alternative<double>(v))
        {
            return std::get<double>(v) != 0;
        }
        if (std::holds_alternative<std::string>(v))
        {
            return !std::get<std::string>(v).empty();
        }
        if (std::holds_alternative<std::vector<std::shared_ptr<FSValueHolder>>>(v))
        {
            return !std::get<std::vector<std::shared_ptr<FSValueHolder>>>(v).empty();
        }
        if (std::holds_alternative<FEnumValue>(v))
        {
            return true;
        }
        if (const auto* value = std::get_if<FStructValue>(&v))
        {
            return !value->type.empty();
        }
        return true;
    }
    std::string ToString(const FSValue& v)
    {
        if (std::holds_alternative<std::monostate>(v))
        {
            return "null";
        }
        if (std::holds_alternative<bool>(v))
        {
            return std::get<bool>(v) ? "true" : "false";
        }
        if (std::holds_alternative<double>(v))
        {
            std::ostringstream o;
            o << std::get<double>(v);
            return o.str();
        }
        if (std::holds_alternative<std::string>(v))
        {
            return std::get<std::string>(v);
        }
        if (const auto* value = std::get_if<FEnumValue>(&v))
        {
            const FemEnumType* type = FemEnumRegistry::Instance().Find(value->type);
            const FemEnumEntry* entry = type ? type->FindValue(value->value) : nullptr;
            return value->type + "." + (entry ? entry->name : std::to_string(value->value));
        }
        if (const auto* value = std::get_if<FStructValue>(&v))
        {
            return value->type + " " + FemboyScript::ToJson(v).dump();
        }
        return "[array]";
    }
    bool Equals(const FSValue& a, const FSValue& b)
    {
        return ValueEquals(a, b);
    }
};

// FemboyScript impl wrapper
struct FemboyScript::Program::Impl
{
    std::string source;
    std::shared_ptr<::Fem::Program> ast;
};

FemboyScript::Program::Program() : impl_(std::make_shared<Impl>())
{
}

FemboyScript::Program::~Program() = default;

const std::string& FemboyScript::Program::Source() const
{
    static const std::string empty;
    return impl_ ? impl_->source : empty;
}

bool FemboyScript::Program::Valid() const
{
    return impl_ && impl_->ast;
}

struct FemboyScript::Impl
{
    Interpreter interp;
    std::unordered_map<std::string, FemboyScript::ProgramPtr> cache;
    std::size_t cacheCapacity = 64;
    std::size_t cacheHits = 0;
    std::size_t cacheMisses = 0;
};

static FemboyScript::Result ExecuteAst(Interpreter& interp, const std::shared_ptr<Program>& prog)
{
    if (!prog)
    {
        return {false, "null program", FSValue{}};
    }

    // Every run starts from a clean budget so a previous timeout does not
    // permanently poison the interpreter.
    interp.error.clear();
    interp.steps = 0;
    interp.depth = 0;
    interp.callDepth = 0;
    interp.returnValue = FSValue{};
    interp.global->constViolation = false;
    interp.global->constName.clear();

    try
    {
        for (auto& st : prog->stmts)
        {
            interp.ExecStmt(st, interp.global);
            if (!interp.error.empty())
            {
                break;
            }
        }
    }
    catch (Interpreter::ReturnExc& re)
    {
        return {true, "", re.v};
    }
    catch (Interpreter::BudgetExc&)
    {
        return {false, "script exceeded execution budget", FSValue{}};
    }
    catch (Interpreter::BreakExc&)
    {
        return {false, "break outside loop or switch", FSValue{}};
    }
    catch (Interpreter::ContinueExc&)
    {
        return {false, "continue outside loop", FSValue{}};
    }
    catch (const std::exception& e)
    {
        return {false, std::string("runtime error: ") + e.what(), FSValue{}};
    }
    catch (...)
    {
        return {false, "runtime error: " + interp.error, FSValue{}};
    }
    if (!interp.error.empty())
    {
        return {false, interp.error, FSValue{}};
    }
    return {true, "", interp.returnValue};
}

namespace
{
double NumericArg(const std::vector<FSValue>& args, std::size_t index, double fallback = 0.0)
{
    if (index >= args.size())
    {
        return fallback;
    }
    if (const double* value = std::get_if<double>(&args[index]))
    {
        return *value;
    }
    return fallback;
}

std::string StringArg(const std::vector<FSValue>& args, std::size_t index, const std::string& fallback = std::string())
{
    if (index >= args.size())
    {
        return fallback;
    }
    if (const std::string* value = std::get_if<std::string>(&args[index]))
    {
        return *value;
    }
    return fallback;
}

void InstallDefaultNatives(FemboyScript& script)
{
    // Small math and array helpers shared by text and visual scripts. They are
    // ordinary native functions, so a host can override any of them and a
    // reset() keeps them without keeping game state.
    script.RegisterFunction("clamp",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                const double value = NumericArg(a, 0);
                                double low = NumericArg(a, 1);
                                double high = NumericArg(a, 2);
                                if (low > high)
                                {
                                    std::swap(low, high);
                                }
                                return std::clamp(value, low, high);
                            });
    script.RegisterFunction("lerp",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                const double from = NumericArg(a, 0);
                                const double to = NumericArg(a, 1);
                                const double t = NumericArg(a, 2);
                                return from + (to - from) * t;
                            });
    script.RegisterFunction("min", [](const std::vector<FSValue>& a) -> FSValue
                            { return std::min(NumericArg(a, 0), NumericArg(a, 1)); });
    script.RegisterFunction("max", [](const std::vector<FSValue>& a) -> FSValue
                            { return std::max(NumericArg(a, 0), NumericArg(a, 1)); });
    script.RegisterFunction("abs", [](const std::vector<FSValue>& a) -> FSValue { return std::abs(NumericArg(a, 0)); });
    script.RegisterFunction("floor",
                            [](const std::vector<FSValue>& a) -> FSValue { return std::floor(NumericArg(a, 0)); });
    script.RegisterFunction("ceil",
                            [](const std::vector<FSValue>& a) -> FSValue { return std::ceil(NumericArg(a, 0)); });
    script.RegisterFunction("round",
                            [](const std::vector<FSValue>& a) -> FSValue { return std::round(NumericArg(a, 0)); });
    script.RegisterFunction("sqrt", [](const std::vector<FSValue>& a) -> FSValue
                            { return std::sqrt(std::max(0.0, NumericArg(a, 0))); });
    script.RegisterFunction("pow", [](const std::vector<FSValue>& a) -> FSValue
                            { return std::pow(NumericArg(a, 0), NumericArg(a, 1)); });
    script.RegisterFunction("sin", [](const std::vector<FSValue>& a) -> FSValue { return std::sin(NumericArg(a, 0)); });
    script.RegisterFunction("cos", [](const std::vector<FSValue>& a) -> FSValue { return std::cos(NumericArg(a, 0)); });
    script.RegisterFunction("tan", [](const std::vector<FSValue>& a) -> FSValue { return std::tan(NumericArg(a, 0)); });
    script.RegisterFunction("atan2", [](const std::vector<FSValue>& a) -> FSValue
                            { return std::atan2(NumericArg(a, 0), NumericArg(a, 1)); });
    script.RegisterFunction("arrayLength",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.empty())
                                {
                                    return 0.0;
                                }
                                if (const auto* values =
                                        std::get_if<std::vector<std::shared_ptr<FSValueHolder>>>(&a[0]))
                                {
                                    return static_cast<double>(values->size());
                                }
                                return 0.0;
                            });
    script.RegisterFunction("toString",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.empty())
                                {
                                    return std::string("null");
                                }
                                if (const auto* text = std::get_if<std::string>(&a[0]))
                                {
                                    return *text;
                                }
                                if (const auto* flag = std::get_if<bool>(&a[0]))
                                {
                                    return std::string(*flag ? "true" : "false");
                                }
                                if (const auto* value = std::get_if<double>(&a[0]))
                                {
                                    std::ostringstream out;
                                    out << *value;
                                    return out.str();
                                }
                                if (const auto* value = std::get_if<FEnumValue>(&a[0]))
                                {
                                    const FemEnumType* type = FemEnumRegistry::Instance().Find(value->type);
                                    const FemEnumEntry* entry = type ? type->FindValue(value->value) : nullptr;
                                    return value->type + "." + (entry ? entry->name : std::to_string(value->value));
                                }
                                if (const auto* value = std::get_if<FStructValue>(&a[0]))
                                {
                                    return value->type + " " + FemboyScript::ToJson(a[0]).dump();
                                }
                                return std::string("null");
                            });
    script.RegisterFunction("toNumber",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.empty())
                                {
                                    return 0.0;
                                }
                                if (const auto* value = std::get_if<double>(&a[0]))
                                {
                                    return *value;
                                }
                                if (const auto* text = std::get_if<std::string>(&a[0]))
                                {
                                    try
                                    {
                                        std::size_t used = 0;
                                        const double parsed = std::stod(*text, &used);
                                        return used > 0 ? parsed : 0.0;
                                    }
                                    catch (const std::exception&)
                                    {
                                        return 0.0;
                                    }
                                }
                                return 0.0;
                            });

    // Reflection-backed values. Enums and structs stay ordinary FSValues; the
    // registries only supply names, defaults and field metadata.
    script.RegisterFunction("enumValue",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                FEnumValue value;
                                if (!a.empty())
                                {
                                    if (const auto* name = std::get_if<std::string>(&a[0]))
                                    {
                                        value.type = *name;
                                    }
                                }
                                if (a.size() > 1)
                                {
                                    if (const auto* number = std::get_if<double>(&a[1]))
                                    {
                                        value.value = static_cast<std::int64_t>(*number);
                                    }
                                    else if (const auto* name = std::get_if<std::string>(&a[1]))
                                    {
                                        if (const FemEnumType* type = FemEnumRegistry::Instance().Find(value.type))
                                        {
                                            if (const FemEnumEntry* entry = type->FindName(*name))
                                            {
                                                value.value = entry->value;
                                            }
                                        }
                                    }
                                }
                                return value;
                            });
    script.RegisterFunction("enumName",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.empty())
                                {
                                    return std::string();
                                }
                                if (const auto* value = std::get_if<FEnumValue>(&a[0]))
                                {
                                    if (const FemEnumType* type = FemEnumRegistry::Instance().Find(value->type))
                                    {
                                        if (const FemEnumEntry* entry = type->FindValue(value->value))
                                        {
                                            return entry->name;
                                        }
                                    }
                                }
                                return std::string();
                            });
    script.RegisterFunction("enumInt",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (!a.empty())
                                {
                                    if (const auto* value = std::get_if<FEnumValue>(&a[0]))
                                    {
                                        return static_cast<double>(value->value);
                                    }
                                }
                                return 0.0;
                            });
    script.RegisterFunction("structNew",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                FStructValue value;
                                if (a.empty())
                                {
                                    return value;
                                }
                                const auto* name = std::get_if<std::string>(&a[0]);
                                if (!name)
                                {
                                    return value;
                                }
                                value.type = *name;
                                const FemStructType* type = FemStructRegistry::Instance().Find(*name);
                                if (!type)
                                {
                                    return value;
                                }
                                for (const auto& field : type->fields)
                                {
                                    auto holder = std::make_shared<FSValueHolder>();
                                    holder->value = FemboyScript::FromJson(field.defaultJson);
                                    value.fields[field.name] = std::move(holder);
                                }
                                return value;
                            });
    script.RegisterFunction("structGet",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.size() < 2)
                                {
                                    return FSValue{};
                                }
                                const auto* object = std::get_if<FStructValue>(&a[0]);
                                const auto* fieldName = std::get_if<std::string>(&a[1]);
                                if (!object || !fieldName)
                                {
                                    return FSValue{};
                                }
                                const auto it = object->fields.find(*fieldName);
                                return it != object->fields.end() && it->second ? it->second->value : FSValue{};
                            });
    script.RegisterFunction("structSet",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.size() < 3)
                                {
                                    return a.empty() ? FSValue{} : a[0];
                                }
                                const auto* object = std::get_if<FStructValue>(&a[0]);
                                const auto* fieldName = std::get_if<std::string>(&a[1]);
                                if (!object || !fieldName)
                                {
                                    return a[0];
                                }
                                FStructValue copy = *object;
                                auto holder = std::make_shared<FSValueHolder>();
                                holder->value = a[2];
                                copy.fields[*fieldName] = std::move(holder);
                                return copy;
                            });
    script.RegisterFunction("structType",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.empty())
                                {
                                    return std::string();
                                }
                                if (const auto* value = std::get_if<FStructValue>(&a[0]))
                                {
                                    return value->type;
                                }
                                if (const auto* value = std::get_if<FEnumValue>(&a[0]))
                                {
                                    return value->type;
                                }
                                return std::string();
                            });
    script.RegisterFunction("structJson",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.empty())
                                {
                                    return std::string("null");
                                }
                                return FemboyScript::ToJson(a[0]).dump();
                            });
    script.RegisterFunction("structParse",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.empty())
                                {
                                    return FSValue{};
                                }
                                const auto* text = std::get_if<std::string>(&a[0]);
                                if (!text)
                                {
                                    return FSValue{};
                                }
                                try
                                {
                                    return FemboyScript::FromJson(nlohmann::json::parse(*text));
                                }
                                catch (const std::exception&)
                                {
                                    return FSValue{};
                                }
                            });

    // Engine display settings. These mirror the Display tab so a script or a
    // visual graph can read and adjust the same values without touching C++.
    script.RegisterFunction("displayNoise",
                            [](const std::vector<FSValue>&) -> FSValue
                            { return Fem::DisplaySettings::Instance().noise; });
    script.RegisterFunction("setDisplayNoise",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                Fem::DisplaySettings::Instance().noise = static_cast<float>(NumericArg(a, 0));
                                Fem::DisplaySettings::Instance().Apply();
                                return Fem::DisplaySettings::Instance().noise;
                            });
    script.RegisterFunction("displayDissolveEnabled",
                            [](const std::vector<FSValue>&) -> FSValue
                            { return Fem::DisplaySettings::Instance().uiDissolveEnabled; });
    script.RegisterFunction("setDisplayDissolveEnabled",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                Fem::DisplaySettings::Instance().uiDissolveEnabled =
                                    (a.empty() ? false : std::get_if<bool>(&a[0]) ? *std::get_if<bool>(&a[0]) : NumericArg(a, 0) != 0.0);
                                Fem::DisplaySettings::Instance().Apply();
                                return Fem::DisplaySettings::Instance().uiDissolveEnabled;
                            });
    script.RegisterFunction("displayPixelShiftEnabled",
                            [](const std::vector<FSValue>&) -> FSValue
                            { return Fem::DisplaySettings::Instance().uiPixelShiftEnabled; });
    script.RegisterFunction("setDisplayPixelShiftEnabled",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                Fem::DisplaySettings::Instance().uiPixelShiftEnabled =
                                    (a.empty() ? true : std::get_if<bool>(&a[0]) ? *std::get_if<bool>(&a[0]) : NumericArg(a, 0) != 0.0);
                                Fem::DisplaySettings::Instance().Apply();
                                return Fem::DisplaySettings::Instance().uiPixelShiftEnabled;
                            });

    // Engine accessibility settings.
    script.RegisterFunction("accessibilityReduceMotion",
                            [](const std::vector<FSValue>&) -> FSValue
                            { return Fem::AccessibilitySettings::Instance().reduceMotion; });
    script.RegisterFunction("setAccessibilityReduceMotion",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                Fem::AccessibilitySettings::Instance().reduceMotion =
                                    (a.empty() ? false : std::get_if<bool>(&a[0]) ? *std::get_if<bool>(&a[0]) : NumericArg(a, 0) != 0.0);
                                Fem::AccessibilitySettings::Instance().WriteToRegistry();
                                Fem::AccessibilitySettings::Instance().Apply();
                                return Fem::AccessibilitySettings::Instance().reduceMotion;
                            });
    script.RegisterFunction("accessibilityReduceFlash",
                            [](const std::vector<FSValue>&) -> FSValue
                            { return Fem::AccessibilitySettings::Instance().reduceFlash; });
    script.RegisterFunction("setAccessibilityReduceFlash",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                Fem::AccessibilitySettings::Instance().reduceFlash =
                                    (a.empty() ? false : std::get_if<bool>(&a[0]) ? *std::get_if<bool>(&a[0]) : NumericArg(a, 0) != 0.0);
                                Fem::AccessibilitySettings::Instance().WriteToRegistry();
                                Fem::AccessibilitySettings::Instance().Apply();
                                return Fem::AccessibilitySettings::Instance().reduceFlash;
                            });
    script.RegisterFunction("accessibilityHighContrast",
                            [](const std::vector<FSValue>&) -> FSValue
                            { return Fem::AccessibilitySettings::Instance().highContrast; });
    script.RegisterFunction("setAccessibilityHighContrast",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                Fem::AccessibilitySettings::Instance().highContrast =
                                    (a.empty() ? false : std::get_if<bool>(&a[0]) ? *std::get_if<bool>(&a[0]) : NumericArg(a, 0) != 0.0);
                                Fem::AccessibilitySettings::Instance().WriteToRegistry();
                                Fem::AccessibilitySettings::Instance().Apply();
                                return Fem::AccessibilitySettings::Instance().highContrast;
                            });
    script.RegisterFunction("accessibilitySubtitles",
                            [](const std::vector<FSValue>&) -> FSValue
                            { return Fem::AccessibilitySettings::Instance().subtitles; });
    script.RegisterFunction("setAccessibilitySubtitles",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                Fem::AccessibilitySettings::Instance().subtitles =
                                    (a.empty() ? true : std::get_if<bool>(&a[0]) ? *std::get_if<bool>(&a[0]) : NumericArg(a, 0) != 0.0);
                                Fem::AccessibilitySettings::Instance().WriteToRegistry();
                                Fem::AccessibilitySettings::Instance().Apply();
                                return Fem::AccessibilitySettings::Instance().subtitles;
                            });
    script.RegisterFunction("accessibilitySubtitleScale",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.empty())
                                {
                                    return Fem::AccessibilitySettings::Instance().subtitleScale;
                                }
                                Fem::AccessibilitySettings::Instance().subtitleScale = static_cast<float>(NumericArg(a, 0));
                                Fem::AccessibilitySettings::Instance().WriteToRegistry();
                                Fem::AccessibilitySettings::Instance().Apply();
                                return Fem::AccessibilitySettings::Instance().subtitleScale;
                            });

    // Generic access to the settings registry. Values travel through JSON so the
    // script language stays type-agnostic while the registry keeps validation.
    script.RegisterFunction("settingsGet",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.empty())
                                {
                                    return std::string("null");
                                }
                                const std::string key = StringArg(a, 0);
                                const nlohmann::json value = Fem::SettingsRegistry::Instance().GetJson(key);
                                return Fem::FemboyScript::FromJson(value);
                            });
    script.RegisterFunction("settingsSet",
                            [](const std::vector<FSValue>& a) -> FSValue
                            {
                                if (a.size() < 2)
                                {
                                    return false;
                                }
                                const std::string key = StringArg(a, 0);
                                const nlohmann::json value = Fem::FemboyScript::ToJson(a[1]);
                                const bool ok = Fem::SettingsRegistry::Instance().Set(key, value, true);
                                return ok;
                            });
}
} // namespace

FemboyScript::FemboyScript()
{
    impl_ = std::make_shared<Impl>();
    InstallDefaultNatives(*this);
}

void FemboyScript::RegisterFunction(const std::string& name, std::function<FSValue(const std::vector<FSValue>&)> fn)
{
    impl_->interp.nativeFns[name] = std::move(fn);
}

void FemboyScript::Inject(const std::string& name, const FSValue& v)
{
    impl_->interp.global->vars[name] = {FSType::Unknown, v, false};
}

void FemboyScript::SetPrintHandler(std::function<void(const std::string&)> sink)
{
    impl_->interp.printSink = std::move(sink);
}

FemboyScript::ProgramPtr FemboyScript::Compile(const std::string& source, std::string* error)
{
    if (impl_->cacheCapacity > 0)
    {
        auto it = impl_->cache.find(source);
        if (it != impl_->cache.end())
        {
            ++impl_->cacheHits;
            return it->second;
        }
    }

    ++impl_->cacheMisses;
    auto lr = Lex(source);
    if (!lr.err.empty())
    {
        const std::string message = "lex error: " + lr.err;
        if (error)
        {
            *error = message;
        }
        return nullptr;
    }

    Parser p;
    p.toks = lr.toks;
    auto ast = p.ParseProgram();
    if (!p.err.empty())
    {
        const std::string message = "parse error: " + p.err;
        if (error)
        {
            *error = message;
        }
        return nullptr;
    }

    auto program = std::make_shared<Program>();
    program->impl_->source = source;
    program->impl_->ast = ast;

    if (impl_->cacheCapacity > 0)
    {
        // A tiny bounded cache is enough: games have a handful of scripts, and
        // an unbounded cache keyed by source text is a memory leak with a nice
        // name. Eviction is deliberately not LRU; correctness never depends on
        // a hit.
        if (impl_->cache.size() >= impl_->cacheCapacity)
        {
            impl_->cache.erase(impl_->cache.begin());
        }
        impl_->cache.emplace(source, program);
    }
    return program;
}

FemboyScript::Result FemboyScript::Run(const std::string& source)
{
    std::string error;
    ProgramPtr program = Compile(source, &error);
    if (!program)
    {
        return {false, error, FSValue{}};
    }
    return Run(program);
}

FemboyScript::Result FemboyScript::Run(const ProgramPtr& program)
{
    if (!program || !program->impl_ || !program->impl_->ast)
    {
        return {false, "null program", FSValue{}};
    }
    return ExecuteAst(impl_->interp, program->impl_->ast);
}

FemboyScript::Result FemboyScript::RunFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        return {false, "cannot open file: " + path, FSValue{}};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (!f.good() && !f.eof())
    {
        return {false, "read error: " + path, FSValue{}};
    }
    return Run(ss.str());
}

FemboyScript::Result FemboyScript::Call(const std::string& function, const std::vector<FSValue>& args)
{
    return impl_->interp.Invoke(function, args);
}

bool FemboyScript::HasFunction(const std::string& function) const
{
    return impl_->interp.nativeFns.count(function) != 0 || impl_->interp.functions.count(function) != 0;
}

void FemboyScript::SetExecutionBudget(long long maxSteps)
{
    impl_->interp.maxSteps = maxSteps > 0 ? maxSteps : 1;
}

void FemboyScript::SetMaxDepth(int maxDepth)
{
    impl_->interp.maxDepth = maxDepth > 0 ? maxDepth : 1;
}

void FemboyScript::SetMaxCallDepth(int maxCallDepth)
{
    impl_->interp.maxCallDepth = maxCallDepth > 0 ? maxCallDepth : 1;
}

void FemboyScript::SetCacheCapacity(std::size_t entries)
{
    impl_->cacheCapacity = entries;
    if (entries == 0)
    {
        impl_->cache.clear();
    }
    else if (impl_->cache.size() > entries)
    {
        while (impl_->cache.size() > entries)
        {
            impl_->cache.erase(impl_->cache.begin());
        }
    }
}

void FemboyScript::ClearCache()
{
    impl_->cache.clear();
    impl_->cacheHits = 0;
    impl_->cacheMisses = 0;
}

std::size_t FemboyScript::CacheSize() const
{
    return impl_->cache.size();
}

std::size_t FemboyScript::CacheHits() const
{
    return impl_->cacheHits;
}

std::size_t FemboyScript::CacheMisses() const
{
    return impl_->cacheMisses;
}

void FemboyScript::Reset()
{
    // Replacing the interpreter is the only way to guarantee that closures,
    // globals and pending errors from a previous game all disappear. Native
    // functions and the parsed-program cache survive, because they are host
    // configuration rather than script state.
    auto natives = impl_->interp.nativeFns;
    auto sink = impl_->interp.printSink;
    impl_->interp = Interpreter{};
    impl_->interp.nativeFns = std::move(natives);
    impl_->interp.printSink = std::move(sink);
}

nlohmann::json FemboyScript::ToJson(const FSValue& v)
{
    if (std::holds_alternative<std::monostate>(v))
    {
        return nullptr;
    }
    if (std::holds_alternative<bool>(v))
    {
        return std::get<bool>(v);
    }
    if (std::holds_alternative<double>(v))
    {
        return std::get<double>(v);
    }
    if (std::holds_alternative<std::string>(v))
    {
        return std::get<std::string>(v);
    }
    if (std::holds_alternative<std::vector<std::shared_ptr<FSValueHolder>>>(v))
    {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& h : std::get<std::vector<std::shared_ptr<FSValueHolder>>>(v))
        {
            arr.push_back(ToJson(h->value));
        }
        return arr;
    }
    if (const auto* value = std::get_if<FEnumValue>(&v))
    {
        return {{"$type", "enum"}, {"name", value->type}, {"value", value->value}};
    }
    if (const auto* value = std::get_if<FStructValue>(&v))
    {
        nlohmann::json fields = nlohmann::json::object();
        for (const auto& [name, holder] : value->fields)
        {
            fields[name] = holder ? ToJson(holder->value) : nlohmann::json(nullptr);
        }
        return {{"$type", "struct"}, {"name", value->type}, {"fields", std::move(fields)}};
    }
    return nullptr;
}
FSValue FemboyScript::FromJson(const nlohmann::json& j)
{
    if (j.is_null())
    {
        return FSValue{};
    }
    if (j.is_boolean())
    {
        return j.get<bool>();
    }
    if (j.is_number())
    {
        return j.get<double>();
    }
    if (j.is_string())
    {
        return j.get<std::string>();
    }
    if (j.is_array())
    {
        std::vector<std::shared_ptr<FSValueHolder>> vec;
        for (auto& e : j)
        {
            auto h = std::make_shared<FSValueHolder>();
            h->value = FromJson(e);
            vec.push_back(h);
        }
        return vec;
    }
    if (j.is_object() && j.contains("$type") && j["$type"].is_string())
    {
        const std::string kind = j["$type"].get<std::string>();
        if (kind == "enum")
        {
            FEnumValue value;
            if (j.contains("name") && j["name"].is_string())
            {
                value.type = j["name"].get<std::string>();
            }
            if (j.contains("value"))
            {
                if (j["value"].is_number())
                {
                    value.value = j["value"].get<std::int64_t>();
                }
                else if (j["value"].is_string())
                {
                    const std::string name = j["value"].get<std::string>();
                    value.value = 0;
                    if (const FemEnumType* type = FemEnumRegistry::Instance().Find(value.type))
                    {
                        if (const FemEnumEntry* entry = type->FindName(name))
                        {
                            value.value = entry->value;
                        }
                    }
                }
            }
            return value;
        }
        if (kind == "struct")
        {
            FStructValue value;
            if (j.contains("name") && j["name"].is_string())
            {
                value.type = j["name"].get<std::string>();
            }
            if (j.contains("fields") && j["fields"].is_object())
            {
                for (const auto& [name, field] : j["fields"].items())
                {
                    auto holder = std::make_shared<FSValueHolder>();
                    holder->value = FromJson(field);
                    value.fields[name] = holder;
                }
            }
            return value;
        }
    }
    return FSValue{};
}

} // namespace Fem
