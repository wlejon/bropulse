#pragma once

#include <bropulse/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bropulse {

struct NodeInfo {
    uint32_t id{0};
    std::string name;
    std::string description;
    std::string media_class;
    std::unordered_map<std::string, std::string> properties{};
    float volume{1.0f};
    bool is_muted{false};
    uint32_t channels{2};
    std::vector<float> channel_volumes{};
    uint32_t target_node_id{0};
    bool is_default{false};
    std::string state{"running"};

    bool is_sink() const {
        return media_class == "Audio/Sink";
    }

    bool is_source() const {
        return media_class == "Audio/Source";
    }

    bool is_audio_stream() const {
        return media_class == "Stream/Output/Audio" ||
               (media_class.rfind("Stream/Output", 0) == 0);
    }

    AudioStream to_audio_stream(uint32_t resolved_sink_id) const;
    AudioSink to_audio_sink() const;
};

struct PortInfo {
    uint32_t id{0};
    uint32_t node_id{0};
    std::string direction; // "input" or "output"
    std::string name;
    std::string audio_channel;
};

struct LinkInfo {
    uint32_t id{0};
    uint32_t output_node_id{0};
    uint32_t output_port_id{0};
    uint32_t input_node_id{0};
    uint32_t input_port_id{0};
    std::string state{"active"};
};

class StreamGraph {
public:
    using ChangeCallback = std::function<void()>;

    StreamGraph();
    ~StreamGraph() = default;

    // Non-copyable, movable
    StreamGraph(const StreamGraph&) = delete;
    StreamGraph& operator=(const StreamGraph&) = delete;
    StreamGraph(StreamGraph&&) noexcept;
    StreamGraph& operator=(StreamGraph&&) noexcept;

    // Mutation methods
    void add_or_update_node(const NodeInfo& node);
    bool remove_node(uint32_t id);

    void add_or_update_port(const PortInfo& port);
    bool remove_port(uint32_t id);

    void add_or_update_link(const LinkInfo& link);
    bool remove_link(uint32_t id);

    bool set_node_volume(uint32_t id, float volume, const std::vector<float>& channel_vols = {});
    bool set_node_mute(uint32_t id, bool muted);
    bool set_node_target(uint32_t id, uint32_t target_node_id);

    void set_default_sink_id(uint32_t sink_id);
    void set_default_sink_name(const std::string& sink_name);
    uint32_t default_sink_id() const;
    std::string default_sink_name() const;

    void clear();

    // Query methods
    std::vector<AudioStream> streams() const;
    std::optional<AudioStream> stream(uint32_t id) const;

    std::vector<AudioSink> sinks() const;
    std::optional<AudioSink> sink(uint32_t id) const;
    std::optional<AudioSink> default_sink() const;

    std::vector<StreamRoute> routes() const;
    std::optional<StreamRoute> route(uint32_t stream_id) const;

    // Topology queries
    std::optional<NodeInfo> get_node(uint32_t id) const;
    std::optional<PortInfo> get_port(uint32_t id) const;
    std::optional<LinkInfo> get_link(uint32_t id) const;

    std::vector<NodeInfo> all_nodes() const;
    std::vector<PortInfo> ports_for_node(uint32_t node_id) const;
    std::vector<LinkInfo> links_for_node(uint32_t node_id) const;

    uint32_t resolve_sink_for_stream(uint32_t stream_id) const;

    void set_change_callback(ChangeCallback cb);

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, NodeInfo> nodes_;
    std::unordered_map<uint32_t, PortInfo> ports_;
    std::unordered_map<uint32_t, LinkInfo> links_;
    uint32_t default_sink_id_{0};
    std::string default_sink_name_;
    ChangeCallback change_callback_{};

    void notify_change_locked();
    uint32_t resolve_sink_for_stream_locked(uint32_t stream_id) const;
};

} // namespace bropulse
