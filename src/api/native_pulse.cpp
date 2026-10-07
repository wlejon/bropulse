#include "host_pulse_internal.h"
#include "arg_reader.h"
#include "object_builder.h"
#include "bropulse/types.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <vector>

namespace bropulse::api {

namespace {

struct Subscription {
    std::string event;
    std::shared_ptr<ev::Persistent> callback;
};

std::mutex g_sub_mu;
std::vector<Subscription> g_subscriptions;

struct QueuedPulseEvent {
    std::string eventName;
    std::optional<AudioSink> sink;
    std::optional<AudioStream> stream;
    uint32_t id{0};
};

std::mutex g_event_queue_mu;
std::vector<QueuedPulseEvent> g_queued_events;

class PulseApiObserver : public StreamObserver {
public:
    void on_sink_added(const AudioSink& sink) override {
        std::lock_guard lock(g_event_queue_mu);
        g_queued_events.push_back({"sinkAdded", sink, std::nullopt, sink.id});
    }

    void on_sink_updated(const AudioSink& sink) override {
        std::lock_guard lock(g_event_queue_mu);
        g_queued_events.push_back({"sinkUpdated", sink, std::nullopt, sink.id});
    }

    void on_sink_removed(uint32_t sink_id) override {
        std::lock_guard lock(g_event_queue_mu);
        g_queued_events.push_back({"sinkRemoved", std::nullopt, std::nullopt, sink_id});
    }

    void on_stream_added(const AudioStream& stream) override {
        std::lock_guard lock(g_event_queue_mu);
        g_queued_events.push_back({"streamAdded", std::nullopt, stream, stream.id});
    }

    void on_stream_updated(const AudioStream& stream) override {
        std::lock_guard lock(g_event_queue_mu);
        g_queued_events.push_back({"streamUpdated", std::nullopt, stream, stream.id});
    }

    void on_stream_removed(uint32_t stream_id) override {
        std::lock_guard lock(g_event_queue_mu);
        g_queued_events.push_back({"streamRemoved", std::nullopt, std::nullopt, stream_id});
    }

    void on_default_sink_changed(uint32_t sink_id) override {
        std::lock_guard lock(g_event_queue_mu);
        g_queued_events.push_back({"defaultSinkChanged", std::nullopt, std::nullopt, sink_id});
    }
};

static PulseApiObserver g_pulse_observer;

void dispatchPulseEvent(std::string_view event, Value payload, uint32_t rawId) {
    ev::Persistent payloadRoot(payload);

    std::vector<std::shared_ptr<ev::Persistent>> targets;
    {
        std::lock_guard lock(g_sub_mu);
        for (const auto& sub : g_subscriptions) {
            if (sub.event == event || sub.event == "*" || sub.event == "change") {
                if (sub.callback) {
                    targets.push_back(sub.callback);
                }
            }
        }
    }

    Value args[2] = { payloadRoot.get(), ev::fromDouble(static_cast<double>(rawId)) };
    for (const auto& cb : targets) {
        if (!cb || !ev::isFunction(cb->get())) continue;
        ev::catchThrow([&]() {
            return ev::call(cb->get(), ev::undefined(), std::span<const Value>(args, 2)).value;
        });
    }

    // Check optional property listener on bro.pulse (e.g. onsinkAdded)
    ev::Persistent pulseObj(ensureBroPulse());
    if (ev::isObject(pulseObj.get())) {
        std::string propName = "on" + std::string(event);
        ev::Persistent handler(ev::getProperty(pulseObj.get(), propName));
        if (ev::isFunction(handler.get())) {
            ev::catchThrow([&]() {
                return ev::call(handler.get(), pulseObj.get(), std::span<const Value>(args, 2)).value;
            });
        }
    }
}

Value sinkToJs(const AudioSink& sink) {
    ObjectBuilder b;
    b.set("id", static_cast<double>(sink.id));
    b.set("name", sink.name);
    b.set("description", sink.description);
    b.set("volume", static_cast<double>(sink.volume));
    b.set("isMuted", sink.is_muted);
    b.set("isDefault", sink.is_default);
    b.set("channels", static_cast<double>(sink.channels));
    b.set("state", sink.state);
    return b.build();
}

Value streamToJs(const AudioStream& stream) {
    ObjectBuilder b;
    b.set("id", static_cast<double>(stream.id));
    b.set("name", stream.name);
    b.set("pid", static_cast<double>(stream.pid));
    b.set("appId", stream.app_id);
    b.set("icon", stream.icon);
    b.set("volume", static_cast<double>(stream.volume));
    b.set("isMuted", stream.is_muted);
    b.set("sinkId", static_cast<double>(stream.sink_id));
    return b.build();
}

} // namespace

StreamObserver* pulseObserver() {
    return &g_pulse_observer;
}

void addPulseEventListener(const std::string& event, Value callback) {
    if (!ev::isFunction(callback)) return;
    std::lock_guard lock(g_sub_mu);
    Subscription sub;
    sub.event = event;
    sub.callback = std::make_shared<ev::Persistent>(callback);
    g_subscriptions.push_back(std::move(sub));
}

void removePulseEventListener(const std::string& event, Value callback) {
    if (!ev::isFunction(callback)) return;
    std::lock_guard lock(g_sub_mu);
    g_subscriptions.erase(
        std::remove_if(g_subscriptions.begin(), g_subscriptions.end(),
                       [&](const Subscription& sub) {
                           if (sub.event != event) return false;
                           return sub.callback && sub.callback->get() == callback;
                       }),
        g_subscriptions.end());
}

void clearPulseSubscriptions() {
    std::lock_guard lock(g_sub_mu);
    g_subscriptions.clear();
}

void clearQueuedPulseEvents() {
    std::lock_guard lock(g_event_queue_mu);
    g_queued_events.clear();
}

void drainPulseEvents() {
    std::vector<QueuedPulseEvent> events;
    {
        std::lock_guard lock(g_event_queue_mu);
        events.swap(g_queued_events);
    }

    for (const auto& evData : events) {
        if (evData.sink) {
            ev::Persistent p(sinkToJs(*evData.sink));
            dispatchPulseEvent(evData.eventName, p.get(), evData.id);
        } else if (evData.stream) {
            ev::Persistent p(streamToJs(*evData.stream));
            dispatchPulseEvent(evData.eventName, p.get(), evData.id);
        } else {
            ObjectBuilder b;
            b.set("type", evData.eventName);
            b.set("id", static_cast<double>(evData.id));
            if (evData.eventName == "sinkRemoved" || evData.eventName == "defaultSinkChanged") {
                b.set("sinkId", static_cast<double>(evData.id));
            } else if (evData.eventName == "streamRemoved") {
                b.set("streamId", static_cast<double>(evData.id));
            }
            b.def("valueOf", 0, [id = evData.id](Value, std::span<const Value>) -> Value {
                return ev::fromDouble(static_cast<double>(id));
            });
            b.def("toString", 0, [id = evData.id](Value, std::span<const Value>) -> Value {
                return ev::fromUtf8(std::to_string(id));
            });
            ev::Persistent p(b.build());
            dispatchPulseEvent(evData.eventName, p.get(), evData.id);
        }
    }

    ev::drainMicrotasks();
}

void installPulseOnto(Value pulseVal) {
    ObjectBuilder pulse(pulseVal);

    // bro.pulse.getSinks() -> array of { id, name, description, volume, isMuted, isDefault }
    pulse.def("getSinks", 0, [](Value, std::span<const Value>) -> Value {
        auto router = activeAudioRouter();
        if (!router) return ev::makeArray(0);
        auto sinks = router->sinks();
        ev::Persistent arr(ev::makeArray(static_cast<uint32_t>(sinks.size())));
        for (uint32_t i = 0; i < sinks.size(); ++i) {
            ev::Persistent item(sinkToJs(sinks[i]));
            arr.set(ev::setElement(arr.get(), i, item.get()));
        }
        return arr.get();
    });

    // bro.pulse.getDefaultSink() -> default sink object or null
    pulse.def("getDefaultSink", 0, [](Value, std::span<const Value>) -> Value {
        auto router = activeAudioRouter();
        if (!router) return ev::null();
        auto def = router->default_sink();
        if (!def) return ev::null();
        return sinkToJs(*def);
    });

    // bro.pulse.setDefaultSink(sinkId) -> boolean
    pulse.def("setDefaultSink", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (!reader.has(0)) return ev::fromBool(false);
        uint32_t sinkId = reader.getUint(0);
        auto router = activeAudioRouter();
        if (!router) return ev::fromBool(false);
        return ev::fromBool(router->set_default_sink(sinkId));
    });

    // bro.pulse.setSinkVolume(sinkId, volume) -> boolean
    pulse.def("setSinkVolume", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (!reader.has(0) || !reader.has(1)) return ev::fromBool(false);
        uint32_t sinkId = reader.getUint(0);
        float volume = static_cast<float>(reader.getDouble(1));
        auto router = activeAudioRouter();
        if (!router) return ev::fromBool(false);
        return ev::fromBool(router->set_sink_volume(sinkId, volume));
    });

    // bro.pulse.setSinkMuted(sinkId, isMuted) -> boolean
    pulse.def("setSinkMuted", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (!reader.has(0) || !reader.has(1)) return ev::fromBool(false);
        uint32_t sinkId = reader.getUint(0);
        bool muted = reader.getBool(1);
        auto router = activeAudioRouter();
        if (!router) return ev::fromBool(false);
        return ev::fromBool(router->set_sink_mute(sinkId, muted));
    });

    // Alias: bro.pulse.setSinkMute(sinkId, isMuted)
    pulse.def("setSinkMute", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (!reader.has(0) || !reader.has(1)) return ev::fromBool(false);
        uint32_t sinkId = reader.getUint(0);
        bool muted = reader.getBool(1);
        auto router = activeAudioRouter();
        if (!router) return ev::fromBool(false);
        return ev::fromBool(router->set_sink_mute(sinkId, muted));
    });

    // bro.pulse.getStreams() -> array of { id, name, pid, appId, icon, volume, isMuted, sinkId }
    pulse.def("getStreams", 0, [](Value, std::span<const Value>) -> Value {
        auto router = activeAudioRouter();
        if (!router) return ev::makeArray(0);
        auto streams = router->streams();
        ev::Persistent arr(ev::makeArray(static_cast<uint32_t>(streams.size())));
        for (uint32_t i = 0; i < streams.size(); ++i) {
            ev::Persistent item(streamToJs(streams[i]));
            arr.set(ev::setElement(arr.get(), i, item.get()));
        }
        return arr.get();
    });

    // bro.pulse.setStreamVolume(streamId, volume) -> boolean
    pulse.def("setStreamVolume", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (!reader.has(0) || !reader.has(1)) return ev::fromBool(false);
        uint32_t streamId = reader.getUint(0);
        float volume = static_cast<float>(reader.getDouble(1));
        auto router = activeAudioRouter();
        if (!router) return ev::fromBool(false);
        return ev::fromBool(router->set_stream_volume(streamId, volume));
    });

    // bro.pulse.setStreamMuted(streamId, isMuted) -> boolean
    pulse.def("setStreamMuted", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (!reader.has(0) || !reader.has(1)) return ev::fromBool(false);
        uint32_t streamId = reader.getUint(0);
        bool muted = reader.getBool(1);
        auto router = activeAudioRouter();
        if (!router) return ev::fromBool(false);
        return ev::fromBool(router->set_stream_mute(streamId, muted));
    });

    // Alias: bro.pulse.setStreamMute(streamId, isMuted)
    pulse.def("setStreamMute", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (!reader.has(0) || !reader.has(1)) return ev::fromBool(false);
        uint32_t streamId = reader.getUint(0);
        bool muted = reader.getBool(1);
        auto router = activeAudioRouter();
        if (!router) return ev::fromBool(false);
        return ev::fromBool(router->set_stream_mute(streamId, muted));
    });

    // bro.pulse.moveStream(streamId, sinkId) -> boolean
    pulse.def("moveStream", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (!reader.has(0) || !reader.has(1)) return ev::fromBool(false);
        uint32_t streamId = reader.getUint(0);
        uint32_t sinkId = reader.getUint(1);
        auto router = activeAudioRouter();
        if (!router) return ev::fromBool(false);
        return ev::fromBool(router->move_stream(streamId, sinkId));
    });

    // bro.pulse.on(event, callback)
    pulse.def("on", 2, [](Value self, std::span<const Value> args) -> Value {
        if (args.size() < 2) return self;
        ev::Persistent arg0(args[0]);
        ev::Persistent arg1(args[1]);
        if (!ev::isString(arg0.get()) || !ev::isFunction(arg1.get())) return self;
        addPulseEventListener(ev::toUtf8(arg0.get()), arg1.get());
        return self;
    });

    // bro.pulse.off(event, callback)
    pulse.def("off", 2, [](Value self, std::span<const Value> args) -> Value {
        if (args.size() < 2) return self;
        ev::Persistent arg0(args[0]);
        ev::Persistent arg1(args[1]);
        if (!ev::isString(arg0.get()) || !ev::isFunction(arg1.get())) return self;
        removePulseEventListener(ev::toUtf8(arg0.get()), arg1.get());
        return self;
    });

    // bro.pulse.addEventListener(event, callback)
    pulse.def("addEventListener", 2, [](Value self, std::span<const Value> args) -> Value {
        if (args.size() < 2) return self;
        ev::Persistent arg0(args[0]);
        ev::Persistent arg1(args[1]);
        if (!ev::isString(arg0.get()) || !ev::isFunction(arg1.get())) return self;
        addPulseEventListener(ev::toUtf8(arg0.get()), arg1.get());
        return self;
    });

    // bro.pulse.removeEventListener(event, callback)
    pulse.def("removeEventListener", 2, [](Value self, std::span<const Value> args) -> Value {
        if (args.size() < 2) return self;
        ev::Persistent arg0(args[0]);
        ev::Persistent arg1(args[1]);
        if (!ev::isString(arg0.get()) || !ev::isFunction(arg1.get())) return self;
        removePulseEventListener(ev::toUtf8(arg0.get()), arg1.get());
        return self;
    });
}

} // namespace bropulse::api
