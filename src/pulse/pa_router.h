#pragma once

#include <bropulse/router.h>

#include <pulse/pulseaudio.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bropulse {

class PulseRouter : public AudioRouter {
public:
    PulseRouter();
    ~PulseRouter() override;

    Backend backend() const override { return Backend::PulseAudio; }
    bool start() override;
    void stop() override;
    bool is_running() const override;

    // Stream queries
    std::vector<AudioStream> streams() const override;
    std::optional<AudioStream> stream(uint32_t id) const override;

    // Sink queries
    std::vector<AudioSink> sinks() const override;
    std::optional<AudioSink> sink(uint32_t id) const override;
    std::optional<AudioSink> default_sink() const override;

    // Route queries
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

    // PulseAudio callback dispatchers
    void handle_sink_info(const pa_sink_info* info, int eol);
    void handle_sink_input_info(const pa_sink_input_info* info, int eol);
    void handle_server_info(const pa_server_info* info);
    void handle_subscription(pa_subscription_event_type_t t, uint32_t idx);

private:
    std::atomic<bool> running_{false};
    StreamGraph graph_;
    PolicyManager policy_;

    mutable std::mutex observers_mutex_;
    std::vector<StreamObserver*> observers_;

    pa_threaded_mainloop* mainloop_{nullptr};
    pa_context* context_{nullptr};

    void notify_stream_added(const AudioStream& stream);
    void notify_stream_updated(const AudioStream& stream);
    void notify_stream_removed(uint32_t stream_id);
    void notify_sink_added(const AudioSink& sink);
    void notify_sink_updated(const AudioSink& sink);
    void notify_sink_removed(uint32_t sink_id);
    void notify_default_sink_changed(uint32_t sink_id);
    void notify_route_changed(const StreamRoute& route);

    void wait_operation(pa_operation* op);
};

} // namespace bropulse
