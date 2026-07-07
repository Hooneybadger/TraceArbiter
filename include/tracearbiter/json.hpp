#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tracearbiter {

class Json {
public:
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;

    Json() = default;
    static Json null();
    static Json boolean(bool value);
    static Json number(double value);
    static Json integer(std::int64_t value);
    static Json string(std::string value);
    static Json object();
    static Json array();

    Json& operator[](const std::string& key);
    const Json* find(const std::string& key) const;
    void push(Json value);

    std::string dump(int indent = 2) const;

private:
    enum class Kind { Null, Bool, Number, Integer, String, Object, Array };
    Kind kind_{Kind::Null};
    bool bool_{false};
    double number_{0};
    std::int64_t integer_{0};
    std::string string_;
    Object object_;
    Array array_;

    void dump_into(std::string& out, int indent, int depth) const;
};

} // namespace tracearbiter
