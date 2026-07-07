#pragma once

#include "tracearbiter/error.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tracearbiter {

class YamlValue {
public:
    using Map = std::map<std::string, YamlValue>;
    using Seq = std::vector<YamlValue>;

    YamlValue() = default;
    explicit YamlValue(std::string scalar) : scalar_(std::move(scalar)) {}
    explicit YamlValue(Map map) : map_(std::move(map)) {}

    bool is_map() const { return map_.has_value(); }
    const Map* as_map() const { return map_ ? &*map_ : nullptr; }
    Map* as_map() { return map_ ? &*map_ : nullptr; }
    const std::string* as_scalar() const { return scalar_ ? &*scalar_ : nullptr; }

    const YamlValue* child(const std::string& key) const {
        if (!map_) {
            return nullptr;
        }
        auto it = map_->find(key);
        return it == map_->end() ? nullptr : &it->second;
    }

    std::optional<std::string> scalar(const std::string& key) const {
        const YamlValue* node = child(key);
        if (!node || !node->scalar_) {
            return std::nullopt;
        }
        return *node->scalar_;
    }

private:
    std::optional<std::string> scalar_;
    std::optional<Map> map_;
};

Result<YamlValue> load_yaml_file(const std::string& path);

} // namespace tracearbiter
