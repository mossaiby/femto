#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <variant>
#include <sstream>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <type_traits>

namespace femto::lsp {

class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

class JsonValue {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    JsonValue() : kind_(Kind::Null), data_(std::monostate{}) {}
    JsonValue(std::nullptr_t) : kind_(Kind::Null), data_(std::monostate{}) {}
    JsonValue(bool b) : kind_(Kind::Bool), data_(b) {}

    template <typename T>
    requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    JsonValue(T n) : kind_(Kind::Number), data_((double)n) {}

    JsonValue(double d) : kind_(Kind::Number), data_(d) {}
    JsonValue(float f) : kind_(Kind::Number), data_((double)f) {}
    JsonValue(const char* s) : kind_(Kind::String), data_(std::string(s)) {}
    JsonValue(std::string s) : kind_(Kind::String), data_(std::move(s)) {}
    JsonValue(std::string_view s) : kind_(Kind::String), data_(std::string(s)) {}
    JsonValue(JsonArray arr) : kind_(Kind::Array), data_(std::move(arr)) {}
    JsonValue(JsonObject obj) : kind_(Kind::Object), data_(std::move(obj)) {}

    Kind kind() const { return kind_; }
    bool is_null() const { return kind_ == Kind::Null; }
    bool is_bool() const { return kind_ == Kind::Bool; }
    bool is_number() const { return kind_ == Kind::Number; }
    bool is_string() const { return kind_ == Kind::String; }
    bool is_array() const { return kind_ == Kind::Array; }
    bool is_object() const { return kind_ == Kind::Object; }

    bool as_bool() const { return std::get<bool>(data_); }
    double as_number() const { return std::get<double>(data_); }
    int64_t as_int() const { return (int64_t)std::get<double>(data_); }
    const std::string& as_string() const { return std::get<std::string>(data_); }
    const JsonArray& as_array() const { return std::get<JsonArray>(data_); }
    const JsonObject& as_object() const { return std::get<JsonObject>(data_); }
    JsonObject& as_object() { return std::get<JsonObject>(data_); }

    bool contains(const std::string& key) const {
        if (!is_object()) return false;
        return as_object().find(key) != as_object().end();
    }

    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue null_val;
        if (!is_object()) return null_val;
        auto it = as_object().find(key);
        return it != as_object().end() ? it->second : null_val;
    }

    std::string serialize() const {
        std::ostringstream oss;
        dump(oss);
        return oss.str();
    }

    void dump(std::ostream& os) const {
        switch (kind_) {
            case Kind::Null: os << "null"; break;
            case Kind::Bool: os << (as_bool() ? "true" : "false"); break;
            case Kind::Number: os << as_number(); break;
            case Kind::String: {
                os << '"';
                for (char c : as_string()) {
                    if (c == '"') os << "\\\"";
                    else if (c == '\\') os << "\\\\";
                    else if (c == '\n') os << "\\n";
                    else if (c == '\r') os << "\\r";
                    else if (c == '\t') os << "\\t";
                    else os << c;
                }
                os << '"';
                break;
            }
            case Kind::Array: {
                os << '[';
                const auto& arr = as_array();
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) os << ',';
                    arr[i].dump(os);
                }
                os << ']';
                break;
            }
            case Kind::Object: {
                os << '{';
                const auto& obj = as_object();
                bool first = true;
                for (const auto& [k, v] : obj) {
                    if (!first) os << ',';
                    first = false;
                    os << '"' << k << "\":";
                    v.dump(os);
                }
                os << '}';
                break;
            }
        }
    }

    static JsonValue parse(std::string_view src) {
        size_t idx = 0;
        return parse_val(src, idx);
    }

private:
    static void skip_ws(std::string_view src, size_t& idx) {
        while (idx < src.size() && std::isspace((unsigned char)src[idx])) idx++;
    }

    static JsonValue parse_val(std::string_view src, size_t& idx) {
        skip_ws(src, idx);
        if (idx >= src.size()) return JsonValue();

        char c = src[idx];
        if (c == 'n') { idx += 4; return JsonValue(); }
        if (c == 't') { idx += 4; return JsonValue(true); }
        if (c == 'f') { idx += 5; return JsonValue(false); }
        if (c == '"') return parse_str(src, idx);
        if (c == '[') return parse_arr(src, idx);
        if (c == '{') return parse_obj(src, idx);
        if (c == '-' || std::isdigit((unsigned char)c)) return parse_num(src, idx);
        return JsonValue();
    }

    static JsonValue parse_str(std::string_view src, size_t& idx) {
        idx++; // skip '"'
        std::string s;
        while (idx < src.size() && src[idx] != '"') {
            if (src[idx] == '\\' && idx + 1 < src.size()) {
                idx++;
                if (src[idx] == 'n') s.push_back('\n');
                else if (src[idx] == 'r') s.push_back('\r');
                else if (src[idx] == 't') s.push_back('\t');
                else s.push_back(src[idx]);
            } else {
                s.push_back(src[idx]);
            }
            idx++;
        }
        if (idx < src.size()) idx++; // skip closing '"'
        return JsonValue(std::move(s));
    }

    static JsonValue parse_num(std::string_view src, size_t& idx) {
        size_t start = idx;
        if (src[idx] == '-') idx++;
        while (idx < src.size() && (std::isdigit((unsigned char)src[idx]) || src[idx] == '.' || src[idx] == 'e' || src[idx] == 'E' || src[idx] == '+' || src[idx] == '-')) {
            idx++;
        }
        double d = 0.0;
        std::string s(src.substr(start, idx - start));
        try { d = std::stod(s); } catch (...) {}
        return JsonValue(d);
    }

    static JsonValue parse_arr(std::string_view src, size_t& idx) {
        idx++; // skip '['
        JsonArray arr;
        skip_ws(src, idx);
        if (idx < src.size() && src[idx] == ']') { idx++; return JsonValue(std::move(arr)); }

        while (idx < src.size()) {
            arr.push_back(parse_val(src, idx));
            skip_ws(src, idx);
            if (idx < src.size() && src[idx] == ',') idx++;
            else if (idx < src.size() && src[idx] == ']') { idx++; break; }
        }
        return JsonValue(std::move(arr));
    }

    static JsonValue parse_obj(std::string_view src, size_t& idx) {
        idx++; // skip '{'
        JsonObject obj;
        skip_ws(src, idx);
        if (idx < src.size() && src[idx] == '}') { idx++; return JsonValue(std::move(obj)); }

        while (idx < src.size()) {
            skip_ws(src, idx);
            if (idx >= src.size() || src[idx] != '"') break;
            std::string key = parse_str(src, idx).as_string();
            skip_ws(src, idx);
            if (idx < src.size() && src[idx] == ':') idx++;
            JsonValue val = parse_val(src, idx);
            obj[std::move(key)] = std::move(val);
            skip_ws(src, idx);
            if (idx < src.size() && src[idx] == ',') idx++;
            else if (idx < src.size() && src[idx] == '}') { idx++; break; }
        }
        return JsonValue(std::move(obj));
    }

    Kind kind_;
    std::variant<std::monostate, bool, double, std::string, JsonArray, JsonObject> data_;
};

} // namespace femto::lsp