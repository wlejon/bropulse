#pragma once

#include <bropulse/policy.h>
#include <bropulse/stream_graph.h>
#include <bropulse/types.h>

#include <memory>
#include <optional>
#include <vector>

namespace bropulse {

class StreamObserver {
public:
    virtual ~StreamObserver() = default;

    virtual void on_stream_added(const AudioStream& /*stream*/) {}
    virtual void on_stream_updated(const AudioStream& /*stream*/) {}
    virtual void on_stream_removed(uint32_t /*stream_id*/) {}

    virtual void on_sink_added(const AudioSink& /*sink*/) {}
    virtual void on_sink_updated(const AudioSink& /*sink*/) {}
    virtual void on_sink_removed(uint32_t /*sink_id*/) {}

    virtual void on_default_sink_changed(uint32_t /*sink_id*/) {}
    virtual void on_route_changed(const StreamRoute& /*route*/) {}
};

class AudioRouter {
public:
    static std::unique_ptr<AudioRouter> create(Backend backend = Backend::Auto);
    static bool is_pipewire_available();
    static bool is_pulseaudio_available();

    virtual ~AudioRouter() = default;

    virtual Backend backend() const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;

    // Stream queries
    virtual std::vector<AudioStream> streams() const = 0;
    virtual std::optional<AudioStream> stream(uint32_t id) const = 0;

    // Sink queries
    virtual std::vector<AudioSink> sinks() const = 0;
    virtual std::optional<AudioSink> sink(uint32_t id) const = 0;
    virtual std::optional<AudioSink> default_sink() const = 0;

    // Route queries
    virtual std::vector<StreamRoute> routes() const = 0;
    virtual std::optional<StreamRoute> route(uint32_t stream_id) const = 0;

    // Stream controls
    virtual bool set_stream_volume(uint32_t stream_id, float volume) = 0;
    virtual bool set_stream_mute(uint32_t stream_id, bool muted) = 0;

    // Sink controls
    virtual bool set_sink_volume(uint32_t sink_id, float volume) = 0;
    virtual bool set_sink_mute(uint32_t sink_id, bool muted) = 0;

    // Routing controls
    virtual bool move_stream(uint32_t stream_id, uint32_t sink_id) = 0;
    virtual bool set_default_sink(uint32_t sink_id) = 0;

    // Observers
    virtual void add_observer(StreamObserver* observer) = 0;
    virtual void remove_observer(StreamObserver* observer) = 0;

    // Subsystems
    virtual StreamGraph& graph() = 0;
    virtual const StreamGraph& graph() const = 0;

    virtual PolicyManager& policy() = 0;
    virtual const PolicyManager& policy() const = 0;
};

} // namespace bropulse
