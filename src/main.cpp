// varjson5 - jq-like JSON5 processor with {{vars}} substitution
// Build: make  (see Makefile)

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// ============================================================================
// Value
// ============================================================================

struct Value;
using Array  = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>; // insertion-ordered

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    using Var = std::variant<
        std::monostate,                  // Null
        bool,                            // Bool
        double,                          // Number
        std::string,                     // String
        std::shared_ptr<Array>,          // Array
        std::shared_ptr<Object>          // Object
    >;

    Type type = Type::Null;
    Var  data;

    Value() = default;
    explicit Value(bool b)               : type(Type::Bool),   data(b) {}
    explicit Value(double d)             : type(Type::Number), data(d) {}
    explicit Value(const std::string& s) : type(Type::String), data(s) {}
    explicit Value(std::string&& s)      : type(Type::String), data(std::move(s)) {}

    static Value make_array (Array  a = {}) {
        Value v; v.type = Type::Array;
        v.data = std::make_shared<Array>(std::move(a)); return v;
    }
    static Value make_object(Object o = {}) {
        Value v; v.type = Type::Object;
        v.data = std::make_shared<Object>(std::move(o)); return v;
    }

    bool is_null()   const { return type == Type::Null;   }
    bool is_bool()   const { return type == Type::Bool;   }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array()  const { return type == Type::Array;  }
    bool is_object() const { return type == Type::Object; }

    bool               as_bool()   const { return std::get<bool>(data); }
    double             as_number() const { return std::get<double>(data); }
    const std::string& as_string() const { return std::get<std::string>(data); }
    Array&             as_array()        { return *std::get<std::shared_ptr<Array>>(data); }
    const Array&       as_array()  const { return *std::get<std::shared_ptr<Array>>(data); }
    Object&            as_object()       { return *std::get<std::shared_ptr<Object>>(data); }
    const Object&      as_object() const { return *std::get<std::shared_ptr<Object>>(data); }

    bool truthy() const {
        if (is_null()) return false;
        if (is_bool()) return as_bool();
        return true;
    }

    const std::string type_name() const {
        switch (type) {
            case Type::Null:   return "null";
            case Type::Bool:   return "boolean";
            case Type::Number: return "number";
            case Type::String: return "string";
            case Type::Array:  return "array";
            case Type::Object: return "object";
        }
        return "unknown";
    }

    // Lookup by key (returns nullptr if not found / not object)
    const Value* get(const std::string& key) const {
        if (!is_object()) return nullptr;
        for (const auto& [k, v] : as_object())
            if (k == key) return &v;
        return nullptr;
    }
};

// ============================================================================
// JSON Output
// ============================================================================

static std::string escape_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"' : out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else { out += (char)c; }
        }
    }
    return out;
}

static std::string num_to_str(double d) {
    if (std::isnan(d))  return "null";
    if (std::isinf(d))  return d > 0 ? "1.7976931348623157e+308" : "-1.7976931348623157e+308";
    if (d == std::trunc(d) && std::abs(d) < 1e15) {
        return std::to_string((long long)d);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", d);
    return buf;
}

static void dump(const Value& v, std::ostream& out, int indent, int depth) {
    auto nl   = [&]() { if (indent > 0) out << '\n'; };
    auto pad  = [&](int d) { if (indent > 0) out << std::string(d * indent, ' '); };
    auto sep  = [&]() { if (indent > 0) out << ' '; };

    switch (v.type) {
        case Value::Type::Null:   out << "null";  break;
        case Value::Type::Bool:   out << (v.as_bool() ? "true" : "false"); break;
        case Value::Type::Number: out << num_to_str(v.as_number()); break;
        case Value::Type::String: out << '"' << escape_string(v.as_string()) << '"'; break;
        case Value::Type::Array: {
            const auto& arr = v.as_array();
            if (arr.empty()) { out << "[]"; break; }
            out << '['; nl();
            for (size_t i = 0; i < arr.size(); ++i) {
                pad(depth + 1);
                dump(arr[i], out, indent, depth + 1);
                if (i + 1 < arr.size()) out << ',';
                nl();
            }
            pad(depth); out << ']';
            break;
        }
        case Value::Type::Object: {
            const auto& obj = v.as_object();
            if (obj.empty()) { out << "{}"; break; }
            out << '{'; nl();
            for (size_t i = 0; i < obj.size(); ++i) {
                pad(depth + 1);
                out << '"' << escape_string(obj[i].first) << '"' << ':'; sep();
                dump(obj[i].second, out, indent, depth + 1);
                if (i + 1 < obj.size()) out << ',';
                nl();
            }
            pad(depth); out << '}';
            break;
        }
    }
}

static std::string to_json(const Value& v, bool compact = false) {
    std::ostringstream oss;
    dump(v, oss, compact ? 0 : 2, 0);
    return oss.str();
}

// ============================================================================
// JSON5 Lexer
// ============================================================================

enum class Tok {
    LBrace, RBrace, LBracket, RBracket,
    Colon, Comma,
    Str, Num, Ident,
    True, False, Null,
    Eof
};

struct Token {
    Tok         kind = Tok::Eof;
    std::string sval = {};
    double      nval = 0;
};

class Lexer {
    const std::string& src_;
    size_t             pos_ = 0;

    void skip() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_+1] == '/') {
                pos_ += 2;
                while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
            } else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_+1] == '*') {
                pos_ += 2;
                while (pos_ + 1 < src_.size() && !(src_[pos_] == '*' && src_[pos_+1] == '/')) ++pos_;
                if (pos_ + 1 < src_.size()) pos_ += 2;
                else throw std::runtime_error("Unterminated block comment");
            } else break;
        }
    }

    std::string read_str(char q) {
        ++pos_; // skip opening quote
        std::string s;
        while (pos_ < src_.size() && src_[pos_] != q) {
            if (src_[pos_] != '\\') { s += src_[pos_++]; continue; }
            ++pos_;
            if (pos_ >= src_.size()) throw std::runtime_error("Unexpected end of escape");
            char e = src_[pos_++];
            switch (e) {
                case '"' : s += '"';  break;
                case '\'': s += '\''; break;
                case '\\': s += '\\'; break;
                case '/':  s += '/';  break;
                case 'n':  s += '\n'; break;
                case 'r':  s += '\r'; break;
                case 't':  s += '\t'; break;
                case 'b':  s += '\b'; break;
                case 'f':  s += '\f'; break;
                case 'v':  s += '\v'; break;
                case '0':  s += '\0'; break;
                case '\n': case '\r': break; // line continuation
                case 'u': {
                    if (pos_ + 4 > src_.size()) throw std::runtime_error("Bad \\u");
                    uint32_t cp = std::stoul(src_.substr(pos_, 4), nullptr, 16);
                    pos_ += 4;
                    if      (cp < 0x80)  { s += (char)cp; }
                    else if (cp < 0x800) { s += (char)(0xC0|(cp>>6)); s += (char)(0x80|(cp&0x3F)); }
                    else                 { s += (char)(0xE0|(cp>>12)); s += (char)(0x80|((cp>>6)&0x3F)); s += (char)(0x80|(cp&0x3F)); }
                    break;
                }
                case 'x': {
                    if (pos_ + 2 > src_.size()) throw std::runtime_error("Bad \\x");
                    s += (char)std::stoul(src_.substr(pos_, 2), nullptr, 16);
                    pos_ += 2;
                    break;
                }
                default: s += e; break;
            }
        }
        if (pos_ >= src_.size()) throw std::runtime_error("Unterminated string");
        ++pos_; return s;
    }

    Token read_num(bool neg) {
        // hex
        if (pos_ + 1 < src_.size() && src_[pos_] == '0' && (src_[pos_+1] == 'x' || src_[pos_+1] == 'X')) {
            pos_ += 2;
            size_t st = pos_;
            while (pos_ < src_.size() && std::isxdigit((unsigned char)src_[pos_])) ++pos_;
            double v = std::stoul(src_.substr(st, pos_ - st), nullptr, 16);
            return {Tok::Num, "", neg ? -v : v};
        }
        size_t st = pos_;
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (std::isdigit((unsigned char)c) || c == '.' || c == 'e' || c == 'E') { ++pos_; }
            else if ((c == '+' || c == '-') && pos_ > st && (src_[pos_-1] == 'e' || src_[pos_-1] == 'E')) { ++pos_; }
            else break;
        }
        std::string ns = src_.substr(st, pos_ - st);
        if (ns.empty() || ns == ".") throw std::runtime_error("Invalid number");
        try { double v = std::stod(ns); return {Tok::Num, "", neg ? -v : v}; }
        catch (...) { throw std::runtime_error("Invalid number: " + ns); }
    }

public:
    explicit Lexer(const std::string& src) : src_(src) {}

    Token next() {
        skip();
        if (pos_ >= src_.size()) return {Tok::Eof};
        char c = src_[pos_];
        switch (c) {
            case '{': ++pos_; return {Tok::LBrace};
            case '}': ++pos_; return {Tok::RBrace};
            case '[': ++pos_; return {Tok::LBracket};
            case ']': ++pos_; return {Tok::RBracket};
            case ':': ++pos_; return {Tok::Colon};
            case ',': ++pos_; return {Tok::Comma};
            case '"': case '\'': return {Tok::Str, read_str(c)};
        }
        // Sign + number
        if ((c == '-' || c == '+') && pos_ + 1 < src_.size()) {
            char nx = src_[pos_+1];
            if (std::isdigit((unsigned char)nx) || nx == '.' || nx == 'I') {
                bool neg = (c == '-'); ++pos_;
                if (src_[pos_] == 'I') { // Infinity
                    pos_ += 8;
                    double v = std::numeric_limits<double>::infinity();
                    return {Tok::Num, "", neg ? -v : v};
                }
                return read_num(neg);
            }
        }
        // Digit or leading dot
        if (std::isdigit((unsigned char)c) || c == '.') {
            return read_num(false);
        }
        // Identifier / keyword
        if (std::isalpha((unsigned char)c) || c == '_' || c == '$') {
            std::string id;
            while (pos_ < src_.size() && (std::isalnum((unsigned char)src_[pos_]) || src_[pos_] == '_' || src_[pos_] == '$'))
                id += src_[pos_++];
            if (id == "true")     return {Tok::True};
            if (id == "false")    return {Tok::False};
            if (id == "null")     return {Tok::Null};
            if (id == "Infinity") return {Tok::Num, "", std::numeric_limits<double>::infinity()};
            if (id == "NaN")      return {Tok::Num, "", std::numeric_limits<double>::quiet_NaN()};
            return {Tok::Ident, id};
        }
        throw std::runtime_error(std::string("Unexpected char: '") + c + "'");
    }

    std::vector<Token> tokenize() {
        std::vector<Token> ts;
        for (;;) { ts.push_back(next()); if (ts.back().kind == Tok::Eof) break; }
        return ts;
    }
};

// ============================================================================
// JSON5 Parser
// ============================================================================

class Parser {
    std::vector<Token> ts_;
    size_t             pos_ = 0;

    Token& peek()  { return ts_[pos_]; }
    Token  consume(){ return ts_[pos_++]; }

    void expect(Tok k) {
        if (ts_[pos_].kind != k)
            throw std::runtime_error("JSON5 parse error: unexpected token");
        ++pos_;
    }

    Value parse_val() {
        auto& t = peek();
        switch (t.kind) {
            case Tok::LBrace:   return parse_obj();
            case Tok::LBracket: return parse_arr();
            case Tok::Str:      { auto s = t.sval; consume(); return Value(std::move(s)); }
            case Tok::Num:      { double d = t.nval; consume(); return Value(d); }
            case Tok::True:     consume(); return Value(true);
            case Tok::False:    consume(); return Value(false);
            case Tok::Null:     consume(); return Value();
            case Tok::Ident:    { auto s = t.sval; consume(); return Value(std::move(s)); } // lenient
            default: throw std::runtime_error("JSON5 parse error: unexpected token in value");
        }
    }

    Value parse_obj() {
        expect(Tok::LBrace);
        Object obj;
        while (peek().kind != Tok::RBrace && peek().kind != Tok::Eof) {
            std::string key;
            auto& kt = peek();
            if (kt.kind == Tok::Str || kt.kind == Tok::Ident) { key = kt.sval; consume(); }
            else throw std::runtime_error("Expected object key");
            expect(Tok::Colon);
            obj.push_back({std::move(key), parse_val()});
            if (peek().kind == Tok::Comma) consume(); else break;
        }
        expect(Tok::RBrace);
        return Value::make_object(std::move(obj));
    }

    Value parse_arr() {
        expect(Tok::LBracket);
        Array arr;
        while (peek().kind != Tok::RBracket && peek().kind != Tok::Eof) {
            arr.push_back(parse_val());
            if (peek().kind == Tok::Comma) consume(); else break;
        }
        expect(Tok::RBracket);
        return Value::make_array(std::move(arr));
    }

public:
    explicit Parser(const std::string& src) {
        Lexer lex(src);
        ts_ = lex.tokenize();
    }

    Value parse() {
        Value v = parse_val();
        if (peek().kind != Tok::Eof)
            throw std::runtime_error("Unexpected tokens after value");
        return v;
    }
};

// ============================================================================
// {{vars}} Substitution
// ============================================================================

static std::string apply_vars(const std::string& s, const std::map<std::string, Value>& vars) {
    std::string out;
    for (size_t i = 0; i < s.size(); ) {
        if (i + 1 < s.size() && s[i] == '{' && s[i+1] == '{') {
            size_t end = s.find("}}", i + 2);
            if (end != std::string::npos) {
                std::string key = s.substr(i + 2, end - i - 2);
                // trim
                size_t a = key.find_first_not_of(" \t");
                size_t b = key.find_last_not_of(" \t");
                if (a != std::string::npos) key = key.substr(a, b - a + 1);
                auto it = vars.find(key);
                if (it != vars.end()) {
                    const Value& v = it->second;
                    out += v.is_string() ? v.as_string() : to_json(v, true);
                } else {
                    out += s.substr(i, end + 2 - i);
                }
                i = end + 2; continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// Resolve {{key}} references within vars itself, iterating until stable.
// This allows vars to reference other vars regardless of definition order.
// Circular references are left unexpanded (the {{key}} pattern stays as-is).
static std::map<std::string, Value> resolve_vars(std::map<std::string, Value> vars) {
    for (int pass = 0; pass < 100; ++pass) {
        bool changed = false;
        std::map<std::string, Value> next;
        for (const auto& [k, v] : vars) {
            // substitute only string values (non-string vars cannot contain {{...}})
            if (!v.is_string()) { next[k] = v; continue; }
            std::string before = v.as_string();
            std::string after  = apply_vars(before, vars);
            if (before != after) changed = true;
            next[k] = Value(std::move(after));
        }
        vars = std::move(next);
        if (!changed) break;
    }
    return vars;
}

static Value substitute(const Value& v, const std::map<std::string, Value>& vars) {
    switch (v.type) {
        case Value::Type::String: return Value(apply_vars(v.as_string(), vars));
        case Value::Type::Array: {
            Array arr;
            for (const auto& item : v.as_array()) arr.push_back(substitute(item, vars));
            return Value::make_array(std::move(arr));
        }
        case Value::Type::Object: {
            Object obj;
            for (const auto& [k, val] : v.as_object()) obj.push_back({k, substitute(val, vars)});
            return Value::make_object(std::move(obj));
        }
        default: return v;
    }
}

// ============================================================================
// Filter Engine
// ============================================================================

using Results = std::vector<Value>;

// Forward declaration
Results eval(const Value& input, const std::string& expr);

// --- String utilities -------------------------------------------------------

static std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\n\r");
    return s.substr(a, b - a + 1);
}

// Find top-level occurrence of ch, skipping nested () [] and strings
static int find_top(const std::string& s, char ch, size_t from = 0) {
    int depth = 0; bool in_str = false; char sc = 0;
    for (size_t i = from; i < s.size(); ++i) {
        char c = s[i];
        if (in_str) { if (c == '\\') { ++i; continue; } if (c == sc) in_str = false; }
        else if (c == '"' || c == '\'') { in_str = true; sc = c; }
        else if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        else if (c == ch && depth == 0) return (int)i;
    }
    return -1;
}

// Split on top-level pipes
static std::vector<std::string> split_pipe(const std::string& s) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        int idx = find_top(s, '|', start);
        if (idx < 0) break;
        parts.push_back(trim(s.substr(start, idx - start)));
        start = (size_t)idx + 1;
    }
    parts.push_back(trim(s.substr(start)));
    return parts;
}

// Split on top-level commas
static std::vector<std::string> split_comma(const std::string& s) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        int idx = find_top(s, ',', start);
        if (idx < 0) break;
        parts.push_back(trim(s.substr(start, idx - start)));
        start = (size_t)idx + 1;
    }
    parts.push_back(trim(s.substr(start)));
    return parts;
}

// --- Path evaluation --------------------------------------------------------

static Results eval_path(const Value& in, const std::string& path) {
    if (path.empty()) return {in};

    // .[] - iterate
    if (path.size() >= 2 && path[0] == '[' && path[1] == ']') {
        std::string rest = path.size() > 2 ? path.substr(path[2] == '.' ? 3 : 2) : "";
        Results out;
        auto push_sub = [&](const Value& v) {
            for (auto& r : eval_path(v, rest)) out.push_back(r);
        };
        if (in.is_array())       { for (const auto& item : in.as_array())  push_sub(item); }
        else if (in.is_object()) { for (const auto& [k,v] : in.as_object()) push_sub(v); }
        else throw std::runtime_error(".[] cannot iterate over " + in.type_name());
        return out;
    }

    // [...]
    if (path[0] == '[') {
        size_t cl = path.find(']');
        if (cl == std::string::npos) throw std::runtime_error("Unmatched [");
        std::string inner = path.substr(1, cl - 1);
        std::string rest  = path.substr(cl + 1);
        if (!rest.empty() && rest[0] == '.') rest = rest.substr(1);

        // Slice n:m
        size_t colon = inner.find(':');
        if (colon != std::string::npos) {
            auto normalize = [](int i, int len) {
                if (i < 0) i += len;
                return std::max(0, std::min(i, len));
            };
            int len = in.is_array() ? (int)in.as_array().size() : in.is_string() ? (int)in.as_string().size() : 0;
            int a = inner.substr(0, colon).empty() ? 0   : std::stoi(inner.substr(0, colon));
            int b = inner.substr(colon+1).empty()  ? len : std::stoi(inner.substr(colon+1));
            a = normalize(a, len); b = normalize(b, len);
            if (in.is_array()) {
                Array sl(in.as_array().begin()+a, in.as_array().begin()+b);
                return eval_path(Value::make_array(std::move(sl)), rest);
            } else if (in.is_string()) {
                return eval_path(Value(in.as_string().substr(a, b-a)), rest);
            }
            return {Value()};
        }

        // Quoted key: ["foo"]
        if (!inner.empty() && (inner[0] == '"' || inner[0] == '\'')) {
            std::string key = inner.substr(1, inner.size()-2);
            if (in.is_object()) { const Value* v = in.get(key); return eval_path(v ? *v : Value(), rest); }
            return {Value()};
        }

        // Numeric index
        try {
            int idx = std::stoi(inner);
            if (in.is_array()) {
                int n = (int)in.as_array().size();
                if (idx < 0) idx += n;
                if (idx < 0 || idx >= n) return {Value()};
                return eval_path(in.as_array()[idx], rest);
            }
            return {Value()};
        } catch (...) {}
        return {Value()};
    }

    // Identifier key
    size_t end = 0;
    while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
    std::string key  = path.substr(0, end);
    std::string rest = path.substr(end);
    if (!rest.empty() && rest[0] == '.') rest = rest.substr(1);

    if (in.is_object()) {
        const Value* v = in.get(key);
        return eval_path(v ? *v : Value(), rest);
    }
    if (in.is_null()) return {Value()};
    throw std::runtime_error("." + key + " cannot index " + in.type_name());
}

// --- Builtins ---------------------------------------------------------------

static Results builtin_sort(const Value& in, const std::string& by_expr) {
    if (!in.is_array()) throw std::runtime_error("sort/sort_by: not an array");
    Array arr = in.as_array();
    auto key = [&](const Value& v) -> std::string {
        if (by_expr.empty()) return to_json(v, true);
        auto res = eval(v, by_expr);
        return res.empty() ? "null" : to_json(res[0], true);
    };
    std::stable_sort(arr.begin(), arr.end(), [&](const Value& a, const Value& b) {
        // type order: null < bool < number < string < array < object
        auto ord = [](const Value& v) {
            switch (v.type) {
                case Value::Type::Null:   return 0;
                case Value::Type::Bool:   return v.as_bool() ? 2 : 1;
                case Value::Type::Number: return 3;
                case Value::Type::String: return 4;
                case Value::Type::Array:  return 5;
                case Value::Type::Object: return 6;
            } return 7;
        };
        int oa = ord(a), ob = ord(b);
        if (oa != ob) return oa < ob;
        if (a.is_number() && b.is_number()) return a.as_number() < b.as_number();
        if (a.is_string() && b.is_string()) return a.as_string() < b.as_string();
        return key(a) < key(b);
    });
    return {Value::make_array(std::move(arr))};
}

// --- Main evaluator ---------------------------------------------------------

Results eval(const Value& input, const std::string& raw) {
    std::string expr = trim(raw);
    if (expr.empty() || expr == ".") return {input};

    // Pipe
    {
        auto parts = split_pipe(expr);
        if (parts.size() > 1) {
            Results cur = {input};
            for (const auto& part : parts) {
                Results nxt;
                for (const auto& v : cur)
                    for (auto& r : eval(v, part)) nxt.push_back(r);
                cur = std::move(nxt);
            }
            return cur;
        }
    }

    // Comma (multiple outputs)
    {
        auto parts = split_comma(expr);
        if (parts.size() > 1) {
            Results out;
            for (const auto& p : parts)
                for (auto& r : eval(input, p)) out.push_back(r);
            return out;
        }
    }

    // Path
    if (expr[0] == '.') return eval_path(input, expr.substr(1));

    // Literals
    if (expr == "null")  return {Value()};
    if (expr == "true")  return {Value(true)};
    if (expr == "false") return {Value(false)};

    // Numeric literal
    {
        size_t consumed;
        try { double d = std::stod(expr, &consumed); if (consumed == expr.size()) return {Value(d)}; }
        catch (...) {}
    }

    // String literal
    if (expr.size() >= 2 && expr.front() == '"' && expr.back() == '"')
        return {Value(expr.substr(1, expr.size()-2))};

    // --- Builtins ---

    if (expr == "keys") {
        if (input.is_object()) {
            Array a; for (const auto& [k,v] : input.as_object()) a.push_back(Value(k));
            std::sort(a.begin(), a.end(), [](const Value& x, const Value& y){ return x.as_string() < y.as_string(); });
            return {Value::make_array(std::move(a))};
        }
        if (input.is_array()) {
            Array a; for (size_t i = 0; i < input.as_array().size(); ++i) a.push_back(Value((double)i));
            return {Value::make_array(std::move(a))};
        }
        throw std::runtime_error("keys: not an object or array");
    }

    if (expr == "keys_unsorted") {
        if (input.is_object()) {
            Array a; for (const auto& [k,v] : input.as_object()) a.push_back(Value(k));
            return {Value::make_array(std::move(a))};
        }
        if (input.is_array()) {
            Array a; for (size_t i = 0; i < input.as_array().size(); ++i) a.push_back(Value((double)i));
            return {Value::make_array(std::move(a))};
        }
    }

    if (expr == "values") {
        if (input.is_object()) {
            Array a; for (const auto& [k,v] : input.as_object()) a.push_back(v);
            return {Value::make_array(std::move(a))};
        }
        if (input.is_array()) return {input};
    }

    if (expr == "length") {
        if (input.is_null())   return {Value(0.0)};
        if (input.is_string()) return {Value((double)input.as_string().size())};
        if (input.is_array())  return {Value((double)input.as_array().size())};
        if (input.is_object()) return {Value((double)input.as_object().size())};
        if (input.is_number()) return {Value(std::abs(input.as_number()))};
        throw std::runtime_error("length: unsupported type");
    }

    if (expr == "type")   return {Value(input.type_name())};
    if (expr == "not")    return {Value(!input.truthy())};
    if (expr == "empty")  return {};
    if (expr == "recurse") {
        Results out;
        std::function<void(const Value&)> rec = [&](const Value& v) {
            out.push_back(v);
            if (v.is_array())  for (const auto& item : v.as_array())  rec(item);
            if (v.is_object()) for (const auto& [k,val] : v.as_object()) rec(val);
        };
        rec(input); return out;
    }

    if (expr == "to_entries") {
        if (!input.is_object()) throw std::runtime_error("to_entries: not an object");
        Array a;
        for (const auto& [k,v] : input.as_object()) {
            Object e; e.push_back({"key", Value(k)}); e.push_back({"value", v});
            a.push_back(Value::make_object(std::move(e)));
        }
        return {Value::make_array(std::move(a))};
    }

    if (expr == "from_entries") {
        if (!input.is_array()) throw std::runtime_error("from_entries: not an array");
        Object obj;
        for (const auto& item : input.as_array()) {
            if (!item.is_object()) throw std::runtime_error("from_entries: element not object");
            std::string key; Value val;
            for (const auto& [k,v] : item.as_object()) {
                if (k == "key" || k == "name") key = v.is_string() ? v.as_string() : to_json(v, true);
                if (k == "value") val = v;
            }
            obj.push_back({std::move(key), std::move(val)});
        }
        return {Value::make_object(std::move(obj))};
    }

    if (expr == "add") {
        if (!input.is_array()) throw std::runtime_error("add: not an array");
        const auto& arr = input.as_array();
        if (arr.empty()) return {Value()};
        Value acc = arr[0];
        for (size_t i = 1; i < arr.size(); ++i) {
            const Value& cur = arr[i];
            if      (acc.is_string() && cur.is_string()) acc = Value(acc.as_string() + cur.as_string());
            else if (acc.is_number() && cur.is_number()) acc = Value(acc.as_number() + cur.as_number());
            else if (acc.is_array()  && cur.is_array()) {
                Array m = acc.as_array();
                for (const auto& v : cur.as_array()) m.push_back(v);
                acc = Value::make_array(std::move(m));
            } else if (acc.is_object() && cur.is_object()) {
                Object m = acc.as_object();
                for (const auto& [k,v] : cur.as_object()) {
                    bool found = false;
                    for (auto& [mk,mv] : m) { if (mk == k) { mv = v; found = true; break; } }
                    if (!found) m.push_back({k, v});
                }
                acc = Value::make_object(std::move(m));
            } else throw std::runtime_error("add: type mismatch");
        }
        return {acc};
    }

    if (expr == "sort") return builtin_sort(input, "");

    if (expr == "reverse") {
        if (input.is_array()) { Array a = input.as_array(); std::reverse(a.begin(), a.end()); return {Value::make_array(std::move(a))}; }
        if (input.is_string()) { std::string s = input.as_string(); std::reverse(s.begin(), s.end()); return {Value(std::move(s))}; }
    }

    if (expr == "unique") {
        if (!input.is_array()) throw std::runtime_error("unique: not an array");
        Array out;
        for (const auto& item : input.as_array()) {
            std::string k = to_json(item, true);
            bool found = false;
            for (const auto& e : out) if (to_json(e, true) == k) { found = true; break; }
            if (!found) out.push_back(item);
        }
        return {Value::make_array(std::move(out))};
    }

    if (expr == "flatten") {
        if (!input.is_array()) throw std::runtime_error("flatten: not an array");
        std::function<Array(const Array&)> flat = [&](const Array& arr) -> Array {
            Array out;
            for (const auto& item : arr) {
                if (item.is_array()) { auto s = flat(item.as_array()); out.insert(out.end(), s.begin(), s.end()); }
                else out.push_back(item);
            }
            return out;
        };
        return {Value::make_array(flat(input.as_array()))};
    }

    if (expr == "ascii_downcase") {
        if (!input.is_string()) throw std::runtime_error("ascii_downcase: not a string");
        std::string s = input.as_string();
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return {Value(std::move(s))};
    }

    if (expr == "ascii_upcase") {
        if (!input.is_string()) throw std::runtime_error("ascii_upcase: not a string");
        std::string s = input.as_string();
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return {Value(std::move(s))};
    }

    if (expr == "tostring") {
        if (input.is_string()) return {input};
        return {Value(to_json(input, true))};
    }

    if (expr == "tonumber") {
        if (input.is_number()) return {input};
        if (input.is_string()) {
            try { return {Value(std::stod(input.as_string()))}; }
            catch (...) { throw std::runtime_error("tonumber: invalid number string"); }
        }
        throw std::runtime_error("tonumber: cannot convert " + input.type_name());
    }

    if (expr == "tojson")    return {Value(to_json(input, true))};
    if (expr == "fromjson") {
        if (!input.is_string()) throw std::runtime_error("fromjson: not a string");
        return {Parser(input.as_string()).parse()};
    }

    // --- Function-call style builtins ---

    // Extract function name and argument
    auto func_arg = [&](const std::string& name) -> std::string {
        if (expr.size() > name.size() + 2 &&
            expr.substr(0, name.size() + 1) == name + "(" &&
            expr.back() == ')')
            return expr.substr(name.size() + 1, expr.size() - name.size() - 2);
        return "";
    };

    {
        std::string inner = func_arg("map");
        if (!inner.empty()) {
            if (!input.is_array()) throw std::runtime_error("map: not an array");
            Array out;
            for (const auto& item : input.as_array())
                for (auto& r : eval(item, inner)) out.push_back(r);
            return {Value::make_array(std::move(out))};
        }
    }

    {
        std::string inner = func_arg("map_values");
        if (!inner.empty()) {
            if (input.is_array()) {
                Array out;
                for (const auto& item : input.as_array()) {
                    auto r = eval(item, inner); if (!r.empty()) out.push_back(r[0]);
                }
                return {Value::make_array(std::move(out))};
            }
            if (input.is_object()) {
                Object out;
                for (const auto& [k,v] : input.as_object()) {
                    auto r = eval(v, inner); if (!r.empty()) out.push_back({k, r[0]});
                }
                return {Value::make_object(std::move(out))};
            }
        }
    }

    {
        std::string inner = func_arg("select");
        if (!inner.empty()) {
            auto res = eval(input, inner);
            return (!res.empty() && res[0].truthy()) ? Results{input} : Results{};
        }
    }

    {
        std::string inner = func_arg("sort_by");
        if (!inner.empty()) return builtin_sort(input, inner);
    }

    {
        std::string inner = func_arg("group_by");
        if (!inner.empty()) {
            if (!input.is_array()) throw std::runtime_error("group_by: not an array");
            std::vector<std::pair<std::string, Array>> groups;
            for (const auto& item : input.as_array()) {
                auto kr = eval(item, inner);
                std::string k = kr.empty() ? "null" : to_json(kr[0], true);
                bool found = false;
                for (auto& [gk, ga] : groups) { if (gk == k) { ga.push_back(item); found = true; break; } }
                if (!found) groups.push_back({k, {item}});
            }
            Array out;
            for (auto& [k, ga] : groups) out.push_back(Value::make_array(std::move(ga)));
            return {Value::make_array(std::move(out))};
        }
    }

    {
        std::string inner = func_arg("unique_by");
        if (!inner.empty()) {
            if (!input.is_array()) throw std::runtime_error("unique_by: not an array");
            Array out;
            std::vector<std::string> seen;
            for (const auto& item : input.as_array()) {
                auto kr = eval(item, inner);
                std::string k = kr.empty() ? "null" : to_json(kr[0], true);
                if (std::find(seen.begin(), seen.end(), k) == seen.end()) {
                    seen.push_back(k); out.push_back(item);
                }
            }
            return {Value::make_array(std::move(out))};
        }
    }

    {
        std::string inner = func_arg("has");
        if (!inner.empty()) {
            inner = trim(inner);
            if (!inner.empty() && inner[0] == '"') {
                std::string key = inner.substr(1, inner.size()-2);
                return {Value(input.is_object() && input.get(key) != nullptr)};
            }
            try { int idx = std::stoi(inner); return {Value(input.is_array() && idx < (int)input.as_array().size())}; }
            catch (...) {}
            return {Value(false)};
        }
    }

    {
        std::string inner = func_arg("recurse");
        if (!inner.empty()) {
            // recurse(f) - apply f repeatedly until empty
            Results out;
            std::function<void(const Value&)> rec = [&](const Value& v) {
                out.push_back(v);
                for (auto& r : eval(v, inner)) if (r.truthy()) rec(r);
            };
            rec(input); return out;
        }
    }

    {
        std::string inner = func_arg("any");
        if (!inner.empty()) {
            if (input.is_array()) {
                for (const auto& item : input.as_array()) {
                    auto r = eval(item, inner);
                    if (!r.empty() && r[0].truthy()) return {Value(true)};
                }
                return {Value(false)};
            }
        }
    }

    {
        std::string inner = func_arg("all");
        if (!inner.empty()) {
            if (input.is_array()) {
                for (const auto& item : input.as_array()) {
                    auto r = eval(item, inner);
                    if (r.empty() || !r[0].truthy()) return {Value(false)};
                }
                return {Value(true)};
            }
        }
    }

    {
        std::string inner = func_arg("flatten");
        if (!inner.empty()) {
            // flatten(depth)
            int depth = std::stoi(inner);
            std::function<Array(const Array&, int)> flat = [&](const Array& arr, int d) -> Array {
                Array out;
                for (const auto& item : arr) {
                    if (item.is_array() && d > 0) { auto s = flat(item.as_array(), d-1); out.insert(out.end(), s.begin(), s.end()); }
                    else out.push_back(item);
                }
                return out;
            };
            if (!input.is_array()) throw std::runtime_error("flatten: not an array");
            return {Value::make_array(flat(input.as_array(), depth))};
        }
    }

    throw std::runtime_error("Unsupported filter: " + expr);
}

// ============================================================================
// Main
// ============================================================================

static void show_help() {
    std::cout <<
        "Usage: varjson5 [options] [filter] [file...]\n"
        "\n"
        "A jq-like JSON5 processor with {{vars}} substitution.\n"
        "\n"
        "Options:\n"
        "  -r, --raw-output      Output strings without JSON encoding\n"
        "  -c, --compact-output  Compact (single-line) output\n"
        "  -n, --null-input      Use null as input\n"
        "  -h, --help            Show this help\n"
        "\n"
        "Filter examples:\n"
        "  .                  identity\n"
        "  .key               object key\n"
        "  .key.sub           nested key\n"
        "  .[]                iterate array or object values\n"
        "  .[0]               array index\n"
        "  .[-1]              last element\n"
        "  .[2:4]             slice\n"
        "  .a, .b             multiple outputs\n"
        "  .a | .b            pipe\n"
        "  keys               sorted object keys\n"
        "  values             object values\n"
        "  length             length\n"
        "  type               type name\n"
        "  map(expr)          map over array\n"
        "  select(expr)       filter by condition\n"
        "  sort / sort_by(f)  sort array\n"
        "  group_by(f)        group array\n"
        "  unique / unique_by(f)\n"
        "  to_entries / from_entries\n"
        "  add                sum/concatenate array\n"
        "  flatten            flatten nested arrays\n"
        "  reverse            reverse array or string\n"
        "  has(\"key\")         test key existence\n"
        "  tostring / tonumber / tojson / fromjson\n"
        "  ascii_downcase / ascii_upcase\n"
        "  recurse            recursive descent\n"
        "\n"
        "{{vars}} substitution:\n"
        "  Values in the \"vars\" key are substituted into {{key}} patterns\n"
        "  throughout the document before filtering.\n"
        "\n"
        "Examples:\n"
        "  varjson5 '.' file.json5\n"
        "  varjson5 '.body.txt' file.json5\n"
        "  varjson5 -r '.body.txt' file.json5\n"
        "  varjson5 '.items[]' file.json5\n"
        "  varjson5 'map(.name)' file.json5\n"
        "  echo '{a:1}' | varjson5 '.a'\n";
}

static int process(const std::string& content, const std::string& filter,
                   bool raw, bool compact) {
    try {
        Value data = Parser(content).parse();

        // Variable substitution
        if (data.is_object()) {
            const Value* vv = data.get("vars");
            if (vv && vv->is_object()) {
                std::map<std::string, Value> vars;
                for (const auto& [k,v] : vv->as_object()) vars[k] = v;
                // Step 1: resolve inter-variable references within vars
                vars = resolve_vars(std::move(vars));
                // Step 2: expand vars throughout the rest of the document
                data = substitute(data, vars);
            }
        }

        Results results = eval(data, filter);
        for (const auto& r : results) {
            if (raw && r.is_string()) std::cout << r.as_string() << '\n';
            else std::cout << to_json(r, compact) << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "varjson5: " << e.what() << '\n';
        return 1;
    }
}

int main(int argc, char* argv[]) {
    bool raw = false, compact = false, null_input = false;
    std::string filter = ".";
    std::vector<std::string> files;
    bool filter_set = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "-r" || a == "--raw-output")     raw = true;
        else if (a == "-c" || a == "--compact-output") compact = true;
        else if (a == "-n" || a == "--null-input")     null_input = true;
        else if (a == "-h" || a == "--help")           { show_help(); return 0; }
        else if (!filter_set)                          { filter = a; filter_set = true; }
        else                                           files.push_back(a);
    }

    if (null_input) return process("null", filter, raw, compact);

    if (files.empty()) {
        std::ostringstream oss; oss << std::cin.rdbuf();
        return process(oss.str(), filter, raw, compact);
    }

    int ret = 0;
    for (const auto& path : files) {
        std::ifstream f(path);
        if (!f) { std::cerr << "varjson5: cannot open: " << path << '\n'; ret = 1; continue; }
        std::ostringstream oss; oss << f.rdbuf();
        int r = process(oss.str(), filter, raw, compact);
        if (r) ret = r;
    }
    return ret;
}
