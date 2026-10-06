#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bropulse {

enum class Backend {
    Auto,
    PipeWire,
    PulseAudio,
    None
};

inline const char* backend_to_string(Backend b) {
    switch (b) {
        case Backend::Auto: return "Auto";
        case Backend::PipeWire: return "PipeWire";
        case Backend::PulseAudio: return "PulseAudio";
        case Backend::None: return "None";
    }
    return "Unknown";
}

struct AudioStream {
    uint32_t id{0};
    std::string name;
    uint32_t pid{0};
    std::string app_id;
    std::string icon;
    float volume{1.0f};
    bool is_muted{false};
    uint32_t sink_id{0};

    std::vector<float> channel_volumes{};
    std::string media_role;
    std::unordered_map<std::string, std::string> properties{};

    bool operator==(const AudioStream& other) const {
        return id == other.id &&
               name == other.name &&
               pid == other.pid &&
               app_id == other.app_id &&
               icon == other.icon &&
               std::abs(volume - other.volume) < 0.0001f &&
               is_muted == other.is_muted &&
               sink_id == other.sink_id;
    }

    bool operator!=(const AudioStream& other) const {
        return !(*this == other);
    }
};

struct AudioSink {
    uint32_t id{0};
    std::string name;
    std::string description;
    float volume{1.0f};
    bool is_default{false};

    bool is_muted{false};
    uint32_t channels{2};
    std::vector<float> channel_volumes{};
    std::string state{"running"};  // "running", "idle", "suspended"
    std::unordered_map<std::string, std::string> properties{};

    bool operator==(const AudioSink& other) const {
        return id == other.id &&
               name == other.name &&
               description == other.description &&
               std::abs(volume - other.volume) < 0.0001f &&
               is_default == other.is_default &&
               is_muted == other.is_muted &&
               channels == other.channels;
    }

    bool operator!=(const AudioSink& other) const {
        return !(*this == other);
    }
};

struct StreamRoute {
    uint32_t stream_id{0};
    uint32_t sink_id{0};
    std::string stream_name;
    std::string sink_name;
    bool is_active{false};

    bool operator==(const StreamRoute& other) const {
        return stream_id == other.stream_id &&
               sink_id == other.sink_id &&
               stream_name == other.stream_name &&
               sink_name == other.sink_name &&
               is_active == other.is_active;
    }

    bool operator!=(const StreamRoute& other) const {
        return !(*this == other);
    }
};

namespace volume {

// PipeWire and PulseAudio standard volume curves
// Cubic volume scale: amplitude = volume^3 (volume in [0.0, 1.0])
inline float linear_to_cubic(float linear) {
    if (linear <= 0.0f) return 0.0f;
    return std::cbrt(linear);
}

inline float cubic_to_linear(float cubic) {
    if (cubic <= 0.0f) return 0.0f;
    return cubic * cubic * cubic;
}

// Decibel conversions: dB = 20 * log10(linear)
inline float volume_to_db(float vol) {
    if (vol <= 0.00001f) return -96.0f; // Silence floor
    return 20.0f * std::log10(vol);
}

inline float db_to_volume(float db) {
    if (db <= -96.0f) return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

inline float clamp_volume(float vol, float min_vol = 0.0f, float max_vol = 1.5f) {
    if (vol < min_vol) return min_vol;
    if (vol > max_vol) return max_vol;
    return vol;
}

} // namespace volume

} // namespace bropulse
