#include <bropulse/policy.h>

#include <algorithm>
#include <cctype>
#include <regex>

namespace bropulse {

namespace {

bool ci_contains(const std::string& str, const std::string& substr) {
    if (substr.empty()) return true;
    if (str.length() < substr.length()) return false;
    auto it = std::search(
        str.begin(), str.end(),
        substr.begin(), substr.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return it != str.end();
}

} // namespace

bool PolicyManager::pattern_matches(const std::string& pattern, const std::string& value) {
    if (pattern.empty()) return true;
    try {
        std::regex re(pattern, std::regex::icase | std::regex::optimize);
        if (std::regex_search(value, re)) {
            return true;
        }
    } catch (const std::regex_error&) {
        // Fall back to case-insensitive substring
    }
    return ci_contains(value, pattern);
}

PolicyManager::PolicyManager() = default;

void PolicyManager::add_route_rule(const RouteRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(route_rules_.begin(), route_rules_.end(),
                           [&](const RouteRule& r) { return r.id == rule.id; });
    if (it != route_rules_.end()) {
        *it = rule;
    } else {
        route_rules_.push_back(rule);
    }
    // Sort descending by priority
    std::stable_sort(route_rules_.begin(), route_rules_.end(),
                     [](const RouteRule& a, const RouteRule& b) {
                         return a.priority > b.priority;
                     });
}

bool PolicyManager::remove_route_rule(const std::string& rule_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(route_rules_.begin(), route_rules_.end(),
                             [&](const RouteRule& r) { return r.id == rule_id; });
    if (it != route_rules_.end()) {
        route_rules_.erase(it, route_rules_.end());
        return true;
    }
    return false;
}

void PolicyManager::clear_route_rules() {
    std::lock_guard<std::mutex> lock(mutex_);
    route_rules_.clear();
}

std::vector<RouteRule> PolicyManager::route_rules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return route_rules_;
}

void PolicyManager::add_volume_rule(const VolumeRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(volume_rules_.begin(), volume_rules_.end(),
                           [&](const VolumeRule& r) { return r.id == rule.id; });
    if (it != volume_rules_.end()) {
        *it = rule;
    } else {
        volume_rules_.push_back(rule);
    }
}

bool PolicyManager::remove_volume_rule(const std::string& rule_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(volume_rules_.begin(), volume_rules_.end(),
                             [&](const VolumeRule& r) { return r.id == rule_id; });
    if (it != volume_rules_.end()) {
        volume_rules_.erase(it, volume_rules_.end());
        return true;
    }
    return false;
}

void PolicyManager::clear_volume_rules() {
    std::lock_guard<std::mutex> lock(mutex_);
    volume_rules_.clear();
}

std::vector<VolumeRule> PolicyManager::volume_rules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return volume_rules_;
}

void PolicyManager::add_ducking_rule(const DuckingRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(ducking_rules_.begin(), ducking_rules_.end(),
                           [&](const DuckingRule& r) { return r.id == rule.id; });
    if (it != ducking_rules_.end()) {
        *it = rule;
    } else {
        ducking_rules_.push_back(rule);
    }
}

bool PolicyManager::remove_ducking_rule(const std::string& rule_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(ducking_rules_.begin(), ducking_rules_.end(),
                             [&](const DuckingRule& r) { return r.id == rule_id; });
    if (it != ducking_rules_.end()) {
        ducking_rules_.erase(it, ducking_rules_.end());
        return true;
    }
    return false;
}

void PolicyManager::clear_ducking_rules() {
    std::lock_guard<std::mutex> lock(mutex_);
    ducking_rules_.clear();
}

std::vector<DuckingRule> PolicyManager::ducking_rules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ducking_rules_;
}

void PolicyManager::remember_route(const std::string& app_id, const std::string& sink_name) {
    if (app_id.empty() || sink_name.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    remembered_routes_[app_id] = sink_name;
}

void PolicyManager::forget_route(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    remembered_routes_.erase(app_id);
}

std::optional<std::string> PolicyManager::remembered_route(const std::string& app_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = remembered_routes_.find(app_id);
    if (it != remembered_routes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::unordered_map<std::string, std::string> PolicyManager::all_remembered_routes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return remembered_routes_;
}

void PolicyManager::clear_remembered_routes() {
    std::lock_guard<std::mutex> lock(mutex_);
    remembered_routes_.clear();
}

std::optional<uint32_t> PolicyManager::evaluate_route(
    const AudioStream& stream,
    const std::vector<AudioSink>& sinks) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Check remembered routes
    if (!stream.app_id.empty()) {
        auto rem_it = remembered_routes_.find(stream.app_id);
        if (rem_it != remembered_routes_.end()) {
            for (const auto& sink : sinks) {
                if (sink.name == rem_it->second) {
                    return sink.id;
                }
            }
        }
    }

    // 2. Evaluate explicit rules in priority order
    for (const auto& rule : route_rules_) {
        if (!rule.app_id_pattern.empty() && !pattern_matches(rule.app_id_pattern, stream.app_id)) {
            continue;
        }
        if (!rule.name_pattern.empty() && !pattern_matches(rule.name_pattern, stream.name)) {
            continue;
        }
        if (!rule.role_pattern.empty() && !pattern_matches(rule.role_pattern, stream.media_role)) {
            continue;
        }

        // Match sink
        for (const auto& sink : sinks) {
            if (pattern_matches(rule.target_sink_name, sink.name) ||
                pattern_matches(rule.target_sink_name, sink.description)) {
                return sink.id;
            }
        }
    }

    return std::nullopt;
}

float PolicyManager::evaluate_volume(const AudioStream& stream, float requested_volume) const {
    std::lock_guard<std::mutex> lock(mutex_);
    float result = requested_volume;

    for (const auto& rule : volume_rules_) {
        if (!rule.app_id_pattern.empty() && !pattern_matches(rule.app_id_pattern, stream.app_id)) {
            continue;
        }
        if (!rule.name_pattern.empty() && !pattern_matches(rule.name_pattern, stream.name)) {
            continue;
        }
        if (!rule.role_pattern.empty() && !pattern_matches(rule.role_pattern, stream.media_role)) {
            continue;
        }

        if (rule.max_volume.has_value()) {
            result = std::min(result, *rule.max_volume);
        }
    }

    return volume::clamp_volume(result);
}

std::optional<float> PolicyManager::initial_volume(const AudioStream& stream) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& rule : volume_rules_) {
        if (!rule.app_id_pattern.empty() && !pattern_matches(rule.app_id_pattern, stream.app_id)) {
            continue;
        }
        if (!rule.name_pattern.empty() && !pattern_matches(rule.name_pattern, stream.name)) {
            continue;
        }
        if (!rule.role_pattern.empty() && !pattern_matches(rule.role_pattern, stream.media_role)) {
            continue;
        }

        if (rule.initial_volume.has_value()) {
            return *rule.initial_volume;
        }
    }
    return std::nullopt;
}

std::vector<PolicyManager::DuckingAction> PolicyManager::evaluate_ducking(
    const std::vector<AudioStream>& active_streams) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DuckingAction> actions;

    if (ducking_rules_.empty()) {
        return actions;
    }

    // Determine if any active stream triggers ducking
    bool is_triggered = false;
    float strongest_duck_factor = 1.0f;
    std::string target_role_filter;

    for (const auto& stream : active_streams) {
        if (stream.is_muted) continue;

        for (const auto& rule : ducking_rules_) {
            bool matches_trigger = false;
            if (!rule.trigger_role.empty() && pattern_matches(rule.trigger_role, stream.media_role)) {
                matches_trigger = true;
            }
            if (!rule.trigger_app_id_pattern.empty() && pattern_matches(rule.trigger_app_id_pattern, stream.app_id)) {
                matches_trigger = true;
            }

            if (matches_trigger) {
                is_triggered = true;
                if (rule.duck_factor < strongest_duck_factor) {
                    strongest_duck_factor = rule.duck_factor;
                    target_role_filter = rule.target_role_pattern;
                }
            }
        }
    }

    if (is_triggered) {
        // Duck non-trigger streams
        for (const auto& stream : active_streams) {
            // Skip trigger streams themselves
            bool is_trigger_stream = false;
            for (const auto& rule : ducking_rules_) {
                if ((!rule.trigger_role.empty() && pattern_matches(rule.trigger_role, stream.media_role)) ||
                    (!rule.trigger_app_id_pattern.empty() && pattern_matches(rule.trigger_app_id_pattern, stream.app_id))) {
                    is_trigger_stream = true;
                    break;
                }
            }
            if (is_trigger_stream) continue;

            // Check if this stream is a valid duck target
            if (!target_role_filter.empty() && !pattern_matches(target_role_filter, stream.media_role)) {
                continue;
            }

            // Save pre-duck volume if not already saved
            float base_vol = stream.volume;
            auto it = pre_duck_volumes_.find(stream.id);
            if (it == pre_duck_volumes_.end()) {
                pre_duck_volumes_[stream.id] = stream.volume;
                base_vol = stream.volume;
            } else {
                base_vol = it->second;
            }

            float target_vol = volume::clamp_volume(base_vol * strongest_duck_factor);
            if (std::abs(stream.volume - target_vol) > 0.001f) {
                actions.push_back({stream.id, base_vol, target_vol});
            }
        }
    } else {
        // Restore pre-duck volumes
        for (const auto& [stream_id, orig_vol] : pre_duck_volumes_) {
            auto sit = std::find_if(active_streams.begin(), active_streams.end(),
                                    [&](const AudioStream& s) { return s.id == stream_id; });
            if (sit != active_streams.end()) {
                if (std::abs(sit->volume - orig_vol) > 0.001f) {
                    actions.push_back({stream_id, sit->volume, orig_vol});
                }
            }
        }
        pre_duck_volumes_.clear();
    }

    // Clean up vanished streams from pre_duck_volumes_
    for (auto it = pre_duck_volumes_.begin(); it != pre_duck_volumes_.end();) {
        bool exists = std::any_of(active_streams.begin(), active_streams.end(),
                                  [&](const AudioStream& s) { return s.id == it->first; });
        if (!exists) {
            it = pre_duck_volumes_.erase(it);
        } else {
            ++it;
        }
    }

    return actions;
}

void PolicyManager::reset_ducking() {
    std::lock_guard<std::mutex> lock(mutex_);
    pre_duck_volumes_.clear();
}

} // namespace bropulse
