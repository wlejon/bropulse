#pragma once

#include <bropulse/types.h>

#include <functional>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bropulse {

struct RouteRule {
    std::string id;
    std::string app_id_pattern;    // regex or exact string
    std::string name_pattern;      // regex or exact string
    std::string role_pattern;      // e.g. "communication", "music", "game"
    std::string target_sink_name;  // sink name or regex pattern
    int priority{0};               // higher numbers evaluate first
};

struct VolumeRule {
    std::string id;
    std::string app_id_pattern;
    std::string name_pattern;
    std::string role_pattern;
    std::optional<float> max_volume;      // clamp to max
    std::optional<float> initial_volume;  // applied on stream creation
};

struct DuckingRule {
    std::string id;
    std::string trigger_role{"communication"}; // e.g. "communication", "alert"
    std::string trigger_app_id_pattern;
    float duck_factor{0.25f};                  // target volume multiplier (e.g. 0.25 = -12 dB)
    std::string target_role_pattern;           // empty means all non-trigger streams
};

class PolicyManager {
public:
    PolicyManager();
    ~PolicyManager() = default;

    // Rules management
    void add_route_rule(const RouteRule& rule);
    bool remove_route_rule(const std::string& rule_id);
    void clear_route_rules();
    std::vector<RouteRule> route_rules() const;

    void add_volume_rule(const VolumeRule& rule);
    bool remove_volume_rule(const std::string& rule_id);
    void clear_volume_rules();
    std::vector<VolumeRule> volume_rules() const;

    void add_ducking_rule(const DuckingRule& rule);
    bool remove_ducking_rule(const std::string& rule_id);
    void clear_ducking_rules();
    std::vector<DuckingRule> ducking_rules() const;

    // Remembered app routes (persistent preference)
    void remember_route(const std::string& app_id, const std::string& sink_name);
    void forget_route(const std::string& app_id);
    std::optional<std::string> remembered_route(const std::string& app_id) const;
    std::unordered_map<std::string, std::string> all_remembered_routes() const;
    void clear_remembered_routes();

    // Policy evaluation
    std::optional<uint32_t> evaluate_route(
        const AudioStream& stream,
        const std::vector<AudioSink>& sinks) const;

    float evaluate_volume(const AudioStream& stream, float requested_volume) const;
    std::optional<float> initial_volume(const AudioStream& stream) const;

    // Audio ducking calculation
    // Takes currently active streams, evaluates triggers, and returns map of stream_id -> adjusted_volume
    struct DuckingAction {
        uint32_t stream_id;
        float original_volume;
        float target_volume;
    };
    std::vector<DuckingAction> evaluate_ducking(const std::vector<AudioStream>& active_streams);

    // Reset ducking internal tracking (e.g. if graph cleared)
    void reset_ducking();

private:
    mutable std::mutex mutex_;
    std::vector<RouteRule> route_rules_;
    std::vector<VolumeRule> volume_rules_;
    std::vector<DuckingRule> ducking_rules_;
    std::unordered_map<std::string, std::string> remembered_routes_;

    // Ducking tracking: stream_id -> pre-duck volume
    std::unordered_map<uint32_t, float> pre_duck_volumes_;

    static bool pattern_matches(const std::string& pattern, const std::string& value);
};

} // namespace bropulse
