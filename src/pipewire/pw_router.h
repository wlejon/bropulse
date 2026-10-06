#pragma once

#include <bropulse/router.h>

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/utils/hook.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bropulse {

struct NodeProxyData {
    uint32_t id{0};
    pw_node* proxy{nullptr};
    spa_hook listener{};
    class PipeWireRouter* router{nullptr};
};

class PipeWireRouter : public AudioRouter {
public:
    PipeWireRouter();
    ~PipeWireRouter() override;

    Backend backend() const override { return Backend::PipeWire; }
    bool start() override;
    void stop() override;
    bool is_running() const override;

    // Queries
    std::vector<AudioStream> streams() const override;
    std::optional<AudioStream> stream(uint32_t id) const override;
    std::vector<AudioSink> sinks() const override;
    std::optional<AudioSink> sink(uint32_t id) const override;
    std::optional<AudioSink> default_sink() const override;
    std::vector<StreamRoute> routes() const override;
    std::optional<StreamRoute> route(uint32_t stream_id) const override;

    // Stream controls
    bool set_stream_volume(uint32_t stream_id, float volume) override;
    bool set_stream_mute(uint32_t stream_id, bool muted) override;

    // Sink controls
    bool set_sink_volume(uint32_t sink_id, float volume) override;
    bool set_sink_mute(uint32_t sink_id, bool muted) override;

    // Routing controls
    bool move_stream(uint32_t stream_id, uint32_t sink_id) override;
    bool set_default_sink(uint32_t sink_id) override;

    // Observers
    void add_observer(StreamObserver* observer) override;
    void remove_observer(StreamObserver* observer) override;

    // Subsystems
    StreamGraph& graph() override { return graph_; }
    const StreamGraph& graph() const override { return graph_; }
    PolicyManager& policy() override { return policy_; }
    const PolicyManager& policy() const override { return policy_; }

    // PipeWire internal callbacks
    void handle_global(uint32_t id, uint32_t permissions, const char* type, uint32_t version, const spa_dict* props);
    void handle_global_remove(uint32_t id);
    void handle_node_info(uint32_t id, const pw_node_info* info);
    void handle_node_param(uint32_t id, int seq, uint32_t param_id, uint32_t index, uint32_t next, const spa_pod* param);
    int handle_metadata_property(uint32_t subject, const char* key, const char* type, const char* value);

    // Synchronize with PipeWire loop
    void sync_roundtrip();

private:
    std::atomic<bool> running_{false};
    StreamGraph graph_;
    PolicyManager policy_;

    mutable std::mutex observers_mutex_;
    std::vector<StreamObserver*> observers_;

    pw_thread_loop* loop_{nullptr};
    pw_context* context_{nullptr};
    pw_core* core_{nullptr};
    pw_registry* registry_{nullptr};
    spa_hook registry_listener_{};
    spa_hook core_listener_{};

    pw_metadata* default_metadata_{nullptr};
    spa_hook metadata_listener_{};
    uint32_t default_metadata_id_{0};

    std::mutex proxies_mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<NodeProxyData>> node_proxies_;

    void notify_stream_added(const AudioStream& stream);
    void notify_stream_updated(const AudioStream& stream);
    void notify_stream_removed(uint32_t stream_id);
    void notify_sink_added(const AudioSink& sink);
    void notify_sink_updated(const AudioSink& sink);
    void notify_sink_removed(uint32_t sink_id);
    void notify_default_sink_changed(uint32_t sink_id);
    void notify_route_changed(const StreamRoute& route);

    bool set_node_spa_param(uint32_t node_id, std::optional<float> volume, std::optional<bool> mute);
    void destroy_node_proxy(uint32_t id);
};

} // namespace bropulse
