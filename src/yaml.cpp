#include "tracearbiter/yaml.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

namespace tracearbiter {
namespace {

struct Line {
    int indent{0};
    std::string key;
    std::optional<std::string> value;
};

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string unquote(std::string text) {
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

Result<std::vector<Line>> tokenize(const std::string& text) {
    std::vector<Line> lines;
    std::istringstream stream(text);
    std::string raw;
    int number = 0;
    while (std::getline(stream, raw)) {
        ++number;
        std::string stripped = raw;
        auto hash = stripped.find('#');
        if (hash != std::string::npos) {
            stripped = stripped.substr(0, hash);
        }
        if (trim(stripped).empty()) {
            continue;
        }
        int indent = 0;
        while (indent < static_cast<int>(stripped.size()) && stripped[static_cast<std::size_t>(indent)] == ' ') {
            ++indent;
        }
        if (indent % 2 != 0) {
            return Result<std::vector<Line>>::err(
                make_error("YAML_INDENT", "line " + std::to_string(number)));
        }
        std::string content = trim(stripped);
        auto colon = std::string::npos;
        bool in_quote = false;
        char quote = '\0';
        for (std::size_t i = 0; i < content.size(); ++i) {
            const char ch = content[i];
            if (in_quote) {
                if (ch == quote) {
                    in_quote = false;
                }
                continue;
            }
            if (ch == '"' || ch == '\'') {
                in_quote = true;
                quote = ch;
                continue;
            }
            if (ch == ':') {
                colon = i;
                break;
            }
        }
        if (colon == std::string::npos) {
            return Result<std::vector<Line>>::err(
                make_error("YAML_SYNTAX", "line " + std::to_string(number)));
        }
        Line line;
        line.indent = indent / 2;
        line.key = unquote(trim(content.substr(0, colon)));
        std::string rest = trim(content.substr(colon + 1));
        if (!rest.empty()) {
            line.value = unquote(rest);
        }
        lines.push_back(std::move(line));
    }
    return Result<std::vector<Line>>::ok(std::move(lines));
}

YamlValue parse_level(const std::vector<Line>& lines, std::size_t& index, int level) {
    YamlValue::Map map;
    while (index < lines.size() && lines[index].indent >= level) {
        if (lines[index].indent > level) {
            break;
        }
        const Line& line = lines[index];
        ++index;
        if (line.value) {
            map.emplace(line.key, YamlValue(*line.value));
            continue;
        }
        if (index < lines.size() && lines[index].indent > line.indent) {
            map.emplace(line.key, parse_level(lines, index, line.indent + 1));
        } else {
            map.emplace(line.key, YamlValue{});
        }
    }
    return YamlValue(std::move(map));
}

} // namespace

Result<YamlValue> load_yaml_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return Result<YamlValue>::err(make_error("YAML_OPEN", path));
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    auto lines = tokenize(buffer.str());
    if (!lines) {
        return Result<YamlValue>::err(lines.error());
    }
    std::size_t index = 0;
    return Result<YamlValue>::ok(parse_level(lines.value(), index, 0));
}

} // namespace tracearbiter
