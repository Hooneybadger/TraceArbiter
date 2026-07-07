#include "tracearbiter/json.hpp"

#include <cstdio>
#include <sstream>

namespace tracearbiter {
namespace {

void append_escaped(std::string& out, const std::string& text) {
    out.push_back('"');
    for (unsigned char ch : text) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                out += buf;
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
    }
    out.push_back('"');
}

} // namespace

Json Json::null() { return Json{}; }

Json Json::boolean(bool value) {
    Json json;
    json.kind_ = Kind::Bool;
    json.bool_ = value;
    return json;
}

Json Json::number(double value) {
    Json json;
    json.kind_ = Kind::Number;
    json.number_ = value;
    return json;
}

Json Json::integer(std::int64_t value) {
    Json json;
    json.kind_ = Kind::Integer;
    json.integer_ = value;
    return json;
}

Json Json::string(std::string value) {
    Json json;
    json.kind_ = Kind::String;
    json.string_ = std::move(value);
    return json;
}

Json Json::object() {
    Json json;
    json.kind_ = Kind::Object;
    return json;
}

Json Json::array() {
    Json json;
    json.kind_ = Kind::Array;
    return json;
}

Json& Json::operator[](const std::string& key) {
    if (kind_ != Kind::Object) {
        kind_ = Kind::Object;
        object_.clear();
    }
    return object_[key];
}

const Json* Json::find(const std::string& key) const {
    if (kind_ != Kind::Object) {
        return nullptr;
    }
    auto it = object_.find(key);
    return it == object_.end() ? nullptr : &it->second;
}

void Json::push(Json value) {
    if (kind_ != Kind::Array) {
        kind_ = Kind::Array;
        array_.clear();
    }
    array_.push_back(std::move(value));
}

void Json::dump_into(std::string& out, int indent, int depth) const {
    auto newline = [&] {
        if (indent <= 0) {
            return;
        }
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent * depth), ' ');
    };
    switch (kind_) {
    case Kind::Null:
        out += "null";
        break;
    case Kind::Bool:
        out += bool_ ? "true" : "false";
        break;
    case Kind::Integer:
        out += std::to_string(integer_);
        break;
    case Kind::Number: {
        std::ostringstream stream;
        stream.setf(std::ios::fmtflags(0), std::ios::floatfield);
        stream.precision(15);
        stream << number_;
        out += stream.str();
        break;
    }
    case Kind::String:
        append_escaped(out, string_);
        break;
    case Kind::Object: {
        out.push_back('{');
        bool first = true;
        for (const auto& [key, value] : object_) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            newline();
            if (indent > 0) {
                out.append(static_cast<std::size_t>(indent), ' ');
            }
            append_escaped(out, key);
            out += indent > 0 ? ": " : ":";
            value.dump_into(out, indent, depth + 1);
        }
        if (!object_.empty()) {
            newline();
        }
        out.push_back('}');
        break;
    }
    case Kind::Array: {
        out.push_back('[');
        bool first = true;
        for (const auto& value : array_) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            newline();
            if (indent > 0) {
                out.append(static_cast<std::size_t>(indent), ' ');
            }
            value.dump_into(out, indent, depth + 1);
        }
        if (!array_.empty()) {
            newline();
        }
        out.push_back(']');
        break;
    }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_into(out, indent, 0);
    out.push_back('\n');
    return out;
}

} // namespace tracearbiter
