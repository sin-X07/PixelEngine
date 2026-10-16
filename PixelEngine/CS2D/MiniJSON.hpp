#pragma once
// 最小 JSON DOM —— 仅支持 CS2D TMJ 地图解析所需的功能子集
// 不需要第三方依赖，直接内联到项目中使用。
// 
// 支持：对象/数组/字符串/数字/bool/null，嵌套读写
// 不支持：Unicode转义、指数记数法数字

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace MiniJSON {

struct Value;

using Object = std::unordered_map<std::string, Value>;
using Array  = std::vector<Value>;

struct Value {
    enum Type { Null, Bool, Number, String, ObjectType, ArrayType };

    Type type = Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::shared_ptr<Object> objVal;
    std::shared_ptr<Array>  arrVal;

    Value() : type(Null) {}
    Value(bool v) : type(Bool), boolVal(v) {}
    Value(int v) : type(Number), numVal(static_cast<double>(v)) {}
    Value(double v) : type(Number), numVal(v) {}
    Value(const std::string& s) : type(String), strVal(s) {}
    Value(const char* s) : type(String), strVal(s) {}
    Value(const Object& o) : type(ObjectType), objVal(std::make_shared<Object>(o)) {}
    Value(const Array& a) : type(ArrayType), arrVal(std::make_shared<Array>(a)) {}

    bool is_object() const { return type == ObjectType; }
    bool is_array()  const { return type == ArrayType; }
    bool is_string() const { return type == String; }
    bool is_number() const { return type == Number; }
    bool is_bool()   const { return type == Bool; }
    bool is_null()   const { return type == Null; }

    const Value& operator[](const std::string& key) const {
        static Value nullVal;
        if (!is_object() || !objVal) return nullVal;
        auto it = objVal->find(key);
        return it != objVal->end() ? it->second : nullVal;
    }

    const Value& operator[](size_t idx) const {
        static Value nullVal;
        if (!is_array() || !arrVal || idx >= arrVal->size()) return nullVal;
        return (*arrVal)[idx];
    }

    size_t size() const {
        if (is_array() && arrVal) return arrVal->size();
        return 0;
    }

    bool contains(const std::string& key) const {
        if (!is_object() || !objVal) return false;
        return objVal->find(key) != objVal->end();
    }

    template<typename T>
    T value(const std::string& key, T def) const {
        if (!is_object() || !objVal) return def;
        auto it = objVal->find(key);
        if (it == objVal->end()) return def;
        return it->second.safe_get<T>(def);
    }

    std::string get_str() const { return is_string() ? strVal : ""; }

    template<typename T>
    T safe_get(T def) const {
        if constexpr (std::is_same_v<T, std::string>) {
            return is_string() ? strVal : def;
        } else if constexpr (std::is_same_v<T, bool>) {
            return is_bool() ? boolVal : def;
        } else if constexpr (std::is_same_v<T, int>) {
            return is_number() ? static_cast<int>(numVal) : def;
        } else if constexpr (std::is_same_v<T, double>) {
            return is_number() ? numVal : def;
        } else if constexpr (std::is_same_v<T, float>) {
            return is_number() ? static_cast<float>(numVal) : def;
        }
        return def;
    }
};

// ---- 解析器 ----
class Parser {
public:
    explicit Parser(const std::string& src) : src_(src), pos_(0) {}

    Value parse() {
        skipWS();
        Value v = parseValue();
        return v;
    }

    std::string getError() const { return error_; }

private:
    const std::string& src_;
    size_t pos_;
    std::string error_;

    char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char next() { return pos_ < src_.size() ? src_[pos_++] : '\0'; }

    void skipWS() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos_; continue; }
            break;
        }
    }

    Value parseValue() {
        skipWS();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        error_ = std::string("Unexpected char: '") + c + "' at pos " + std::to_string(pos_);
        return Value();
    }

    Value parseObject() {
        next(); // '{'
        Object obj;
        skipWS();
        if (peek() == '}') { next(); return Value(obj); }
        while (true) {
            skipWS();
            std::string key = parseStringRaw();
            skipWS();
            if (next() != ':') { error_ = "Expected ':'"; return Value(); }
            Value v = parseValue();
            obj[key] = std::move(v);
            skipWS();
            if (peek() == '}') { next(); break; }
            if (next() != ',') { error_ = "Expected ',' or '}'"; return Value(); }
        }
        return Value(obj);
    }

    Value parseArray() {
        next(); // '['
        Array arr;
        skipWS();
        if (peek() == ']') { next(); return Value(arr); }
        while (true) {
            arr.push_back(parseValue());
            skipWS();
            if (peek() == ']') { next(); break; }
            if (next() != ',') { error_ = "Expected ',' or ']'"; return Value(); }
        }
        return Value(arr);
    }

    std::string parseStringRaw() {
        std::string s;
        if (next() != '"') return s;
        while (pos_ < src_.size()) {
            char c = next();
            if (c == '"') return s;
            if (c == '\\' && pos_ < src_.size()) {
                char ec = next();
                if (ec == '"' || ec == '\\' || ec == '/') s += ec;
                else if (ec == 'n') s += '\n';
                else if (ec == 't') s += '\t';
                else if (ec == 'r') s += '\r';
                else s += ec;
            } else {
                s += c;
            }
        }
        return s;
    }

    Value parseString() {
        return Value(parseStringRaw());
    }

    Value parseBool() {
        if (src_.compare(pos_, 4, "true") == 0) { pos_ += 4; return Value(true); }
        if (src_.compare(pos_, 5, "false") == 0) { pos_ += 5; return Value(false); }
        error_ = "Invalid bool";
        return Value();
    }

    Value parseNull() {
        if (src_.compare(pos_, 4, "null") == 0) { pos_ += 4; return Value(); }
        error_ = "Invalid null";
        return Value();
    }

    Value parseNumber() {
        size_t start = pos_;
        if (peek() == '-') next();
        while (peek() >= '0' && peek() <= '9') next();
        if (peek() == '.') {
            next();
            while (peek() >= '0' && peek() <= '9') next();
        }
        std::string numStr = src_.substr(start, pos_ - start);
        return Value(std::atof(numStr.c_str()));
    }
};

inline Value parse(const std::string& json) {
    Parser p(json);
    return p.parse();
}

} // namespace MiniJSON