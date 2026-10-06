#include "src/pulse/pa_router.h"

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace bropulse {

namespace {

// C callbacks
void context_state_callback(pa_context* c, void* userdata) {
    pa_threaded_mainloop* m = static_cast<pa_threaded_mainloop*>(userdata);
    pa_context_state_t state = pa_context_get_state(c);
    if (state == PA_CONTEXT_READY || state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        pa_threaded_mainloop_signal(m, 0);
    }
}

void sink_info_callback(pa_context* /*c*/, const pa_sink_info* info, int eol, void* userdata) {
    auto* self = static_cast<PulseRouter*>(userdata);
    self->handle_sink_info(info, eol);
}

void sink_input_info_callback(pa_context* /*c*/, const pa_sink_input_info* info, int eol, void* userdata) {
    auto* self = static_cast<PulseRouter*>(userdata);
    self->handle_sink_input_info(info, eol);
}

void server_info_callback(pa_context* /*c*/, const pa_server_info* info, void* userdata) {
    auto* self = static_cast<PulseRouter*>(userdata);
    self->handle_server_info(info);
}

void subscribe_callback(pa_context* /*c*/, pa_subscription_event_type_t t, uint32_t idx, void* userdata) {
    auto* self = static_cast<PulseRouter*>(userdata);
    self->handle_subscription(t, idx);
}

void generic_success_callback(pa_context* /*c*/, int /*success*/, void* userdata) {
    if (userdata) {
        auto* m = static_cast<pa_threaded_mainloop*>(userdata);
        pa_threaded_mainloop_signal(m, 0);
    }
}

} // namespace

PulseRouter::PulseRouter() = default;

PulseRouter::~PulseRouter() {
    stop();
}

void PulseRouter::wait_operation(pa_operation* op) {
    if (!op || !mainloop_) return;
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(mainloop_);
    }
    pa_operation_unref(op);
}

bool PulseRouter::start() {
    if (running_.load()) return true;

    mainloop_ = pa_threaded_mainloop_new();
    if (!mainloop_) return false;

    pa_mainloop_api* api = pa_threaded_mainloop_get_api(mainloop_);
    context_ = pa_context_new(api, "bropulse");
    if (!context_) {
        pa_threaded_mainloop_free(mainloop_);
        mainloop_ = nullptr;
        return false;
    }

    pa_context_set_state_callback(context_, context_state_callback, mainloop_);

    if (pa_threaded_mainloop_start(mainloop_) < 0) {
        pa_context_unref(context_);
        pa_threaded_mainloop_free(mainloop_);
        context_ = nullptr;
        mainloop_ = nullptr;
        return false;
    }

    pa_threaded_mainloop_lock(mainloop_);
    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        pa_threaded_mainloop_unlock(mainloop_);
        stop();
        return false;
    }

    while (true) {
        pa_context_state_t state = pa_context_get_state(context_);
        if (state == PA_CONTEXT_READY) break;
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            pa_threaded_mainloop_unlock(mainloop_);
            stop();
            return false;
        }
        pa_threaded_mainloop_wait(mainloop_);
    }

    // Subscribe to events
    pa_context_set_subscribe_callback(context_, subscribe_callback, this);
    auto mask = static_cast<pa_subscription_mask_t>(
        PA_SUBSCRIPTION_MASK_SINK |
        PA_SUBSCRIPTION_MASK_SINK_INPUT |
        PA_SUBSCRIPTION_MASK_SERVER);
    pa_operation* sub_op = pa_context_subscribe(context_, mask, nullptr, nullptr);
    if (sub_op) pa_operation_unref(sub_op);

    // Initial enumeration
    pa_operation* op1 = pa_context_get_server_info(context_, server_info_callback, this);
    if (op1) pa_operation_unref(op1);

    pa_operation* op2 = pa_context_get_sink_info_list(context_, sink_info_callback, this);
    if (op2) pa_operation_unref(op2);

    pa_operation* op3 = pa_context_get_sink_input_info_list(context_, sink_input_info_callback, this);
    if (op3) pa_operation_unref(op3);

    running_.store(true);
    pa_threaded_mainloop_unlock(mainloop_);

    // Allow time for initial introspection callbacks to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return true;
}

void PulseRouter::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (mainloop_) {
        pa_threaded_mainloop_lock(mainloop_);
        if (context_) {
            pa_context_disconnect(context_);
            pa_context_unref(context_);
            context_ = nullptr;
        }
        pa_threaded_mainloop_unlock(mainloop_);

        pa_threaded_mainloop_stop(mainloop_);
        pa_threaded_mainloop_free(mainloop_);
        mainloop_ = nullptr;
    }

    graph_.clear();
}

bool PulseRouter::is_running() const {
    return running_.load();
}

void PulseRouter::handle_sink_info(const pa_sink_info* info, int eol) {
    if (eol > 0 || !info) return;

    NodeInfo node;
    node.id = info->index;
    node.name = info->name ? info->name : "";
    node.description = info->description ? info->description : "";
    node.media_class = "Audio/Sink";
    node.channels = info->channel_map.channels;

    if (info->channel_map.channels > 0) {
        node.volume = static_cast<float>(pa_cvolume_avg(&info->volume)) / static_cast<float>(PA_VOLUME_NORM);
        node.channel_volumes.resize(info->channel_map.channels);
        for (uint8_t c = 0; c < info->channel_map.channels; ++c) {
            node.channel_volumes[c] = static_cast<float>(info->volume.values[c]) / static_cast<float>(PA_VOLUME_NORM);
        }
    }
    node.is_muted = (info->mute != 0);

    switch (info->state) {
        case PA_SINK_RUNNING: node.state = "running"; break;
        case PA_SINK_IDLE: node.state = "idle"; break;
        case PA_SINK_SUSPENDED: node.state = "suspended"; break;
        default: node.state = "unknown"; break;
    }

    if (info->proplist) {
        void* state = nullptr;
        while (const char* key = pa_proplist_iterate(info->proplist, &state)) {
            const char* val = pa_proplist_gets(info->proplist, key);
            if (val) node.properties[key] = val;
        }
    }

    bool is_new = !graph_.get_node(info->index).has_value();
    graph_.add_or_update_node(node);

    if (is_new) {
        notify_sink_added(node.to_audio_sink());
    } else {
        notify_sink_updated(node.to_audio_sink());
    }
}

void PulseRouter::handle_sink_input_info(const pa_sink_input_info* info, int eol) {
    if (eol > 0 || !info) return;

    NodeInfo node;
    node.id = info->index;
    node.name = info->name ? info->name : "";
    node.media_class = "Stream/Output/Audio";
    node.target_node_id = info->sink;
    node.channels = info->channel_map.channels;

    if (info->channel_map.channels > 0) {
        node.volume = static_cast<float>(pa_cvolume_avg(&info->volume)) / static_cast<float>(PA_VOLUME_NORM);
        node.channel_volumes.resize(info->channel_map.channels);
        for (uint8_t c = 0; c < info->channel_map.channels; ++c) {
            node.channel_volumes[c] = static_cast<float>(info->volume.values[c]) / static_cast<float>(PA_VOLUME_NORM);
        }
    }
    node.is_muted = (info->mute != 0);
    node.state = "running";

    if (info->proplist) {
        void* state = nullptr;
        while (const char* key = pa_proplist_iterate(info->proplist, &state)) {
            const char* val = pa_proplist_gets(info->proplist, key);
            if (val) node.properties[key] = val;
        }
    }

    bool is_new = !graph_.get_node(info->index).has_value();
    graph_.add_or_update_node(node);

    if (is_new) {
        notify_stream_added(node.to_audio_stream(info->sink));
    } else {
        notify_stream_updated(node.to_audio_stream(info->sink));
    }
}

void PulseRouter::handle_server_info(const pa_server_info* info) {
    if (!info || !info->default_sink_name) return;
    graph_.set_default_sink_name(info->default_sink_name);
    notify_default_sink_changed(graph_.default_sink_id());
}

void PulseRouter::handle_subscription(pa_subscription_event_type_t t, uint32_t idx) {
    auto facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    auto type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
        if (type == PA_SUBSCRIPTION_EVENT_REMOVE) {
            graph_.remove_node(idx);
            notify_sink_removed(idx);
        } else {
            pa_operation* op = pa_context_get_sink_info_by_index(context_, idx, sink_info_callback, this);
            if (op) pa_operation_unref(op);
        }
    } else if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
        if (type == PA_SUBSCRIPTION_EVENT_REMOVE) {
            graph_.remove_node(idx);
            notify_stream_removed(idx);
        } else {
            pa_operation* op = pa_context_get_sink_input_info(context_, idx, sink_input_info_callback, this);
            if (op) pa_operation_unref(op);
        }
    } else if (facility == PA_SUBSCRIPTION_EVENT_SERVER) {
        pa_operation* op = pa_context_get_server_info(context_, server_info_callback, this);
        if (op) pa_operation_unref(op);
    }
}

std::vector<AudioStream> PulseRouter::streams() const {
    return graph_.streams();
}

std::optional<AudioStream> PulseRouter::stream(uint32_t id) const {
    return graph_.stream(id);
}

std::vector<AudioSink> PulseRouter::sinks() const {
    return graph_.sinks();
}

std::optional<AudioSink> PulseRouter::sink(uint32_t id) const {
    return graph_.sink(id);
}

std::optional<AudioSink> PulseRouter::default_sink() const {
    return graph_.default_sink();
}

std::vector<StreamRoute> PulseRouter::routes() const {
    return graph_.routes();
}

std::optional<StreamRoute> PulseRouter::route(uint32_t stream_id) const {
    return graph_.route(stream_id);
}

bool PulseRouter::set_stream_volume(uint32_t stream_id, float volume) {
    auto st = graph_.stream(stream_id);
    if (!st || !mainloop_ || !context_) return false;

    float evaluated = policy_.evaluate_volume(*st, volume);

    pa_threaded_mainloop_lock(mainloop_);
    pa_cvolume cv;
    pa_cvolume_init(&cv);
    uint32_t channels = st->channel_volumes.empty() ? 2 : static_cast<uint32_t>(st->channel_volumes.size());
    auto pa_vol = static_cast<pa_volume_t>(evaluated * static_cast<float>(PA_VOLUME_NORM));
    pa_cvolume_set(&cv, channels, pa_vol);

    pa_operation* op = pa_context_set_sink_input_volume(context_, stream_id, &cv, generic_success_callback, mainloop_);
    wait_operation(op);
    pa_threaded_mainloop_unlock(mainloop_);

    graph_.set_node_volume(stream_id, evaluated);
    auto updated = graph_.stream(stream_id);
    if (updated) notify_stream_updated(*updated);
    return true;
}

bool PulseRouter::set_stream_mute(uint32_t stream_id, bool muted) {
    auto st = graph_.stream(stream_id);
    if (!st || !mainloop_ || !context_) return false;

    pa_threaded_mainloop_lock(mainloop_);
    pa_operation* op = pa_context_set_sink_input_mute(context_, stream_id, muted ? 1 : 0,
                                                      generic_success_callback, mainloop_);
    wait_operation(op);
    pa_threaded_mainloop_unlock(mainloop_);

    graph_.set_node_mute(stream_id, muted);
    auto updated = graph_.stream(stream_id);
    if (updated) notify_stream_updated(*updated);
    return true;
}

bool PulseRouter::set_sink_volume(uint32_t sink_id, float volume) {
    auto sk = graph_.sink(sink_id);
    if (!sk || !mainloop_ || !context_) return false;

    float clamped = volume::clamp_volume(volume);

    pa_threaded_mainloop_lock(mainloop_);
    pa_cvolume cv;
    pa_cvolume_init(&cv);
    auto pa_vol = static_cast<pa_volume_t>(clamped * static_cast<float>(PA_VOLUME_NORM));
    pa_cvolume_set(&cv, sk->channels, pa_vol);

    pa_operation* op = pa_context_set_sink_volume_by_index(context_, sink_id, &cv,
                                                           generic_success_callback, mainloop_);
    wait_operation(op);
    pa_threaded_mainloop_unlock(mainloop_);

    graph_.set_node_volume(sink_id, clamped);
    auto updated = graph_.sink(sink_id);
    if (updated) notify_sink_updated(*updated);
    return true;
}

bool PulseRouter::set_sink_mute(uint32_t sink_id, bool muted) {
    auto sk = graph_.sink(sink_id);
    if (!sk || !mainloop_ || !context_) return false;

    pa_threaded_mainloop_lock(mainloop_);
    pa_operation* op = pa_context_set_sink_mute_by_index(context_, sink_id, muted ? 1 : 0,
                                                        generic_success_callback, mainloop_);
    wait_operation(op);
    pa_threaded_mainloop_unlock(mainloop_);

    graph_.set_node_mute(sink_id, muted);
    auto updated = graph_.sink(sink_id);
    if (updated) notify_sink_updated(*updated);
    return true;
}

bool PulseRouter::move_stream(uint32_t stream_id, uint32_t sink_id) {
    auto st = graph_.stream(stream_id);
    auto sk = graph_.sink(sink_id);
    if (!st || !sk || !mainloop_ || !context_) return false;

    pa_threaded_mainloop_lock(mainloop_);
    pa_operation* op = pa_context_move_sink_input_by_index(context_, stream_id, sink_id,
                                                           generic_success_callback, mainloop_);
    wait_operation(op);
    pa_threaded_mainloop_unlock(mainloop_);

    graph_.set_node_target(stream_id, sink_id);
    policy_.remember_route(st->app_id, sk->name);

    auto r = graph_.route(stream_id);
    if (r) notify_route_changed(*r);
    return true;
}

bool PulseRouter::set_default_sink(uint32_t sink_id) {
    auto sk = graph_.sink(sink_id);
    if (!sk || !mainloop_ || !context_) return false;

    pa_threaded_mainloop_lock(mainloop_);
    pa_operation* op = pa_context_set_default_sink(context_, sk->name.c_str(),
                                                   generic_success_callback, mainloop_);
    wait_operation(op);
    pa_threaded_mainloop_unlock(mainloop_);

    graph_.set_default_sink_id(sink_id);
    notify_default_sink_changed(sink_id);
    return true;
}

void PulseRouter::add_observer(StreamObserver* observer) {
    if (!observer) return;
    std::lock_guard<std::mutex> lk(observers_mutex_);
    observers_.push_back(observer);
}

void PulseRouter::remove_observer(StreamObserver* observer) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    auto it = std::remove(observers_.begin(), observers_.end(), observer);
    observers_.erase(it, observers_.end());
}

void PulseRouter::notify_stream_added(const AudioStream& stream) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) obs->on_stream_added(stream);
}

void PulseRouter::notify_stream_updated(const AudioStream& stream) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) obs->on_stream_updated(stream);
}

void PulseRouter::notify_stream_removed(uint32_t stream_id) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) obs->on_stream_removed(stream_id);
}

void PulseRouter::notify_sink_added(const AudioSink& sink) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) obs->on_sink_added(sink);
}

void PulseRouter::notify_sink_updated(const AudioSink& sink) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) obs->on_sink_updated(sink);
}

void PulseRouter::notify_sink_removed(uint32_t sink_id) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) obs->on_sink_removed(sink_id);
}

void PulseRouter::notify_default_sink_changed(uint32_t sink_id) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) obs->on_default_sink_changed(sink_id);
}

void PulseRouter::notify_route_changed(const StreamRoute& route) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) obs->on_route_changed(route);
}

} // namespace bropulse
