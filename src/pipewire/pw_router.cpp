#include "src/pipewire/pw_router.h"

#include <bropulse/types.h>

#include <pipewire/pipewire.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace bropulse {

namespace {

// C Callbacks for PipeWire registry
void on_registry_global(void* data, uint32_t id, uint32_t permissions,
                        const char* type, uint32_t version,
                        const struct spa_dict* props) {
    auto* self = static_cast<PipeWireRouter*>(data);
    self->handle_global(id, permissions, type, version, props);
}

void on_registry_global_remove(void* data, uint32_t id) {
    auto* self = static_cast<PipeWireRouter*>(data);
    self->handle_global_remove(id);
}

const struct pw_registry_events registry_events = []() {
    struct pw_registry_events ev{};
    ev.version = PW_VERSION_REGISTRY_EVENTS;
    ev.global = on_registry_global;
    ev.global_remove = on_registry_global_remove;
    return ev;
}();

// C Callbacks for PipeWire node
void on_node_info(void* data, const struct pw_node_info* info) {
    auto* np = static_cast<NodeProxyData*>(data);
    np->router->handle_node_info(np->id, info);
}

void on_node_param(void* data, int seq, uint32_t id, uint32_t index,
                   uint32_t next, const struct spa_pod* param) {
    auto* np = static_cast<NodeProxyData*>(data);
    np->router->handle_node_param(np->id, seq, id, index, next, param);
}

const struct pw_node_events node_events = []() {
    struct pw_node_events ev{};
    ev.version = PW_VERSION_NODE_EVENTS;
    ev.info = on_node_info;
    ev.param = on_node_param;
    return ev;
}();

// C Callbacks for PipeWire metadata
int on_metadata_property(void* data, uint32_t subject, const char* key,
                         const char* type, const char* value) {
    auto* self = static_cast<PipeWireRouter*>(data);
    return self->handle_metadata_property(subject, key, type, value);
}

const struct pw_metadata_events metadata_events = []() {
    struct pw_metadata_events ev{};
    ev.version = PW_VERSION_METADATA_EVENTS;
    ev.property = on_metadata_property;
    return ev;
}();

// C Callbacks for Core sync
struct SyncContext {
    pw_thread_loop* loop{nullptr};
    int expected_seq{-1};
    bool done{false};
};

void on_core_done(void* data, uint32_t id, int seq) {
    auto* ctx = static_cast<SyncContext*>(data);
    if (id == PW_ID_CORE && seq == ctx->expected_seq) {
        ctx->done = true;
        if (ctx->loop) {
            pw_thread_loop_signal(ctx->loop, false);
        }
    }
}

const struct pw_core_events core_events = []() {
    struct pw_core_events ev{};
    ev.version = PW_VERSION_CORE_EVENTS;
    ev.done = on_core_done;
    return ev;
}();

std::string extract_json_name(const std::string& json_str) {
    // Looks for "name": "something"
    auto pos = json_str.find("\"name\"");
    if (pos == std::string::npos) {
        return json_str;
    }
    auto colon = json_str.find(':', pos);
    if (colon == std::string::npos) return json_str;
    auto first_quote = json_str.find('"', colon);
    if (first_quote == std::string::npos) return json_str;
    auto second_quote = json_str.find('"', first_quote + 1);
    if (second_quote == std::string::npos) return json_str;
    return json_str.substr(first_quote + 1, second_quote - first_quote - 1);
}

} // namespace

PipeWireRouter::PipeWireRouter() = default;

PipeWireRouter::~PipeWireRouter() {
    stop();
}

bool PipeWireRouter::start() {
    if (running_.load()) {
        return true;
    }

    pw_init(nullptr, nullptr);

    loop_ = pw_thread_loop_new("bropulse-pw", nullptr);
    if (!loop_) {
        return false;
    }

    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    if (!context_) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    if (pw_thread_loop_start(loop_) < 0) {
        pw_context_destroy(context_);
        pw_thread_loop_destroy(loop_);
        context_ = nullptr;
        loop_ = nullptr;
        return false;
    }

    pw_thread_loop_lock(loop_);

    core_ = pw_context_connect(context_, nullptr, 0);
    if (!core_) {
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_stop(loop_);
        pw_context_destroy(context_);
        pw_thread_loop_destroy(loop_);
        context_ = nullptr;
        loop_ = nullptr;
        return false;
    }

    registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
    if (!registry_) {
        pw_core_disconnect(core_);
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_stop(loop_);
        pw_context_destroy(context_);
        pw_thread_loop_destroy(loop_);
        core_ = nullptr;
        context_ = nullptr;
        loop_ = nullptr;
        return false;
    }

    spa_zero(registry_listener_);
    pw_registry_add_listener(registry_, &registry_listener_, &registry_events, this);

    running_.store(true);
    pw_thread_loop_unlock(loop_);

    // Initial roundtrip to gather existing nodes, ports, links, and metadata
    sync_roundtrip();

    return true;
}

void PipeWireRouter::sync_roundtrip() {
    if (!running_.load() || !loop_ || !core_) return;

    pw_thread_loop_lock(loop_);

    SyncContext ctx;
    ctx.loop = loop_;
    spa_hook core_listener;
    spa_zero(core_listener);
    pw_core_add_listener(core_, &core_listener, &core_events, &ctx);

    ctx.expected_seq = pw_core_sync(core_, PW_ID_CORE, 0);

    while (!ctx.done) {
        pw_thread_loop_wait(loop_);
    }

    spa_hook_remove(&core_listener);
    pw_thread_loop_unlock(loop_);
}

void PipeWireRouter::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (loop_) {
        pw_thread_loop_lock(loop_);

        if (default_metadata_) {
            spa_hook_remove(&metadata_listener_);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(default_metadata_));
            default_metadata_ = nullptr;
            default_metadata_id_ = 0;
        }

        {
            std::lock_guard<std::mutex> lk(proxies_mutex_);
            for (auto& [id, np] : node_proxies_) {
                spa_hook_remove(&np->listener);
                pw_proxy_destroy(reinterpret_cast<pw_proxy*>(np->proxy));
            }
            node_proxies_.clear();
        }

        if (registry_) {
            spa_hook_remove(&registry_listener_);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry_));
            registry_ = nullptr;
        }

        if (core_) {
            pw_core_disconnect(core_);
            core_ = nullptr;
        }

        pw_thread_loop_unlock(loop_);

        pw_thread_loop_stop(loop_);
        pw_context_destroy(context_);
        pw_thread_loop_destroy(loop_);
        context_ = nullptr;
        loop_ = nullptr;
    }

    graph_.clear();
    pw_deinit();
}

bool PipeWireRouter::is_running() const {
    return running_.load();
}

void PipeWireRouter::handle_global(uint32_t id, uint32_t /*permissions*/,
                                   const char* type, uint32_t /*version*/,
                                   const spa_dict* props) {
    if (!type) return;

    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        NodeInfo node;
        node.id = id;

        if (props) {
            const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
            const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
            const char* mclass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
            if (name) node.name = name;
            if (desc) node.description = desc;
            if (mclass) node.media_class = mclass;

            const struct spa_dict_item* item;
            spa_dict_for_each(item, props) {
                if (item->key && item->value) {
                    node.properties[item->key] = item->value;
                }
            }
        }

        graph_.add_or_update_node(node);

        // Bind node proxy to listen for info and params
        if (registry_) {
            pw_node* np_proxy = static_cast<pw_node*>(
                pw_registry_bind(registry_, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
            if (np_proxy) {
                auto np = std::make_unique<NodeProxyData>();
                np->id = id;
                np->proxy = np_proxy;
                np->router = this;
                spa_zero(np->listener);
                pw_node_add_listener(np_proxy, &np->listener, &node_events, np.get());

                uint32_t ids[] = { SPA_PARAM_Props, SPA_PARAM_PropInfo };
                pw_node_subscribe_params(np_proxy, ids, 2);

                std::lock_guard<std::mutex> lk(proxies_mutex_);
                node_proxies_[id] = std::move(np);
            }
        }

        if (node.is_sink()) {
            notify_sink_added(node.to_audio_sink());
        } else if (node.is_audio_stream()) {
            notify_stream_added(node.to_audio_stream(graph_.resolve_sink_for_stream(id)));
        }
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
        PortInfo port;
        port.id = id;
        if (props) {
            const char* nid = spa_dict_lookup(props, PW_KEY_NODE_ID);
            const char* dir = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
            const char* name = spa_dict_lookup(props, PW_KEY_PORT_NAME);
            const char* ch = spa_dict_lookup(props, PW_KEY_AUDIO_CHANNEL);

            if (nid) port.node_id = static_cast<uint32_t>(std::strtoul(nid, nullptr, 10));
            if (dir) port.direction = dir;
            if (name) port.name = name;
            if (ch) port.audio_channel = ch;
        }
        graph_.add_or_update_port(port);
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        LinkInfo link;
        link.id = id;
        if (props) {
            const char* onode = spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_NODE);
            const char* oport = spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_PORT);
            const char* inode = spa_dict_lookup(props, PW_KEY_LINK_INPUT_NODE);
            const char* iport = spa_dict_lookup(props, PW_KEY_LINK_INPUT_PORT);

            if (onode) link.output_node_id = static_cast<uint32_t>(std::strtoul(onode, nullptr, 10));
            if (oport) link.output_port_id = static_cast<uint32_t>(std::strtoul(oport, nullptr, 10));
            if (inode) link.input_node_id = static_cast<uint32_t>(std::strtoul(inode, nullptr, 10));
            if (iport) link.input_port_id = static_cast<uint32_t>(std::strtoul(iport, nullptr, 10));
        }
        graph_.add_or_update_link(link);
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        if (props) {
            const char* mname = spa_dict_lookup(props, "metadata.name");
            if (mname && std::strcmp(mname, "default") == 0) {
                if (!default_metadata_ && registry_) {
                    default_metadata_ = static_cast<pw_metadata*>(
                        pw_registry_bind(registry_, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
                    default_metadata_id_ = id;
                    spa_zero(metadata_listener_);
                    pw_metadata_add_listener(default_metadata_, &metadata_listener_, &metadata_events, this);
                }
            }
        }
    }
}

void PipeWireRouter::destroy_node_proxy(uint32_t id) {
    std::lock_guard<std::mutex> lk(proxies_mutex_);
    auto it = node_proxies_.find(id);
    if (it != node_proxies_.end()) {
        spa_hook_remove(&it->second->listener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(it->second->proxy));
        node_proxies_.erase(it);
    }
}

void PipeWireRouter::handle_global_remove(uint32_t id) {
    if (id == default_metadata_id_) {
        if (default_metadata_) {
            spa_hook_remove(&metadata_listener_);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(default_metadata_));
            default_metadata_ = nullptr;
            default_metadata_id_ = 0;
        }
        return;
    }

    auto node = graph_.get_node(id);
    if (node) {
        bool is_s = node->is_sink();
        bool is_st = node->is_audio_stream();

        destroy_node_proxy(id);
        graph_.remove_node(id);

        if (is_s) {
            notify_sink_removed(id);
        } else if (is_st) {
            notify_stream_removed(id);
        }
    } else {
        graph_.remove_port(id);
        graph_.remove_link(id);
    }
}

void PipeWireRouter::handle_node_info(uint32_t id, const pw_node_info* info) {
    if (!info) return;

    auto opt_node = graph_.get_node(id);
    NodeInfo node = opt_node.value_or(NodeInfo{});
    node.id = id;

    if (info->props) {
        const char* name = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);
        const char* desc = spa_dict_lookup(info->props, PW_KEY_NODE_DESCRIPTION);
        const char* mclass = spa_dict_lookup(info->props, PW_KEY_MEDIA_CLASS);
        if (name) node.name = name;
        if (desc) node.description = desc;
        if (mclass) node.media_class = mclass;

        const struct spa_dict_item* item;
        spa_dict_for_each(item, info->props) {
            if (item->key && item->value) {
                node.properties[item->key] = item->value;
            }
        }
    }

    switch (info->state) {
        case PW_NODE_STATE_RUNNING: node.state = "running"; break;
        case PW_NODE_STATE_IDLE: node.state = "idle"; break;
        case PW_NODE_STATE_SUSPENDED: node.state = "suspended"; break;
        case PW_NODE_STATE_ERROR: node.state = "error"; break;
        case PW_NODE_STATE_CREATING: node.state = "creating"; break;
    }

    graph_.add_or_update_node(node);

    if (node.is_sink()) {
        notify_sink_updated(node.to_audio_sink());
    } else if (node.is_audio_stream()) {
        notify_stream_updated(node.to_audio_stream(graph_.resolve_sink_for_stream(id)));
    }
}

void PipeWireRouter::handle_node_param(uint32_t id, int /*seq*/, uint32_t param_id,
                                       uint32_t /*index*/, uint32_t /*next*/,
                                       const spa_pod* param) {
    if (param_id != SPA_PARAM_Props || !param) return;

    float vol = 1.0f;
    bool has_vol = false;
    bool mute = false;
    bool has_mute = false;

    const struct spa_pod_prop* prop = nullptr;
    const auto* obj = reinterpret_cast<const struct spa_pod_object*>(param);

    SPA_POD_OBJECT_FOREACH(obj, prop) {
        if (prop->key == SPA_PROP_volume) {
            if (spa_pod_get_float(&prop->value, &vol) == 0) {
                has_vol = true;
            }
        } else if (prop->key == SPA_PROP_mute) {
            bool m = false;
            if (spa_pod_get_bool(&prop->value, &m) == 0) {
                mute = m;
                has_mute = true;
            }
        }
    }

    if (has_vol) {
        graph_.set_node_volume(id, vol);
    }
    if (has_mute) {
        graph_.set_node_mute(id, mute);
    }

    if (has_vol || has_mute) {
        auto node = graph_.get_node(id);
        if (node) {
            if (node->is_sink()) {
                notify_sink_updated(node->to_audio_sink());
            } else if (node->is_audio_stream()) {
                notify_stream_updated(node->to_audio_stream(graph_.resolve_sink_for_stream(id)));
            }
        }
    }
}

int PipeWireRouter::handle_metadata_property(uint32_t subject, const char* key,
                                             const char* /*type*/, const char* value) {
    if (!key || !value) return 0;

    if (std::strcmp(key, "default.audio.sink") == 0) {
        std::string sink_name = extract_json_name(value);
        graph_.set_default_sink_name(sink_name);
        notify_default_sink_changed(graph_.default_sink_id());
    } else if (std::strcmp(key, "target.node") == 0) {
        char* end = nullptr;
        unsigned long tid = std::strtoul(value, &end, 10);
        uint32_t target_id = (end != value && *end == '\0') ? static_cast<uint32_t>(tid) : 0;
        graph_.set_node_target(subject, target_id);
        auto r = graph_.route(subject);
        if (r) notify_route_changed(*r);
    }

    return 0;
}

bool PipeWireRouter::set_node_spa_param(uint32_t node_id, std::optional<float> volume,
                                        std::optional<bool> mute) {
    if (!running_.load() || !loop_) return false;

    pw_thread_loop_lock(loop_);

    pw_node* proxy = nullptr;
    {
        std::lock_guard<std::mutex> lk(proxies_mutex_);
        auto it = node_proxies_.find(node_id);
        if (it != node_proxies_.end()) {
            proxy = it->second->proxy;
        }
    }

    if (!proxy) {
        pw_thread_loop_unlock(loop_);
        return false;
    }

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);

    if (volume.has_value()) {
        spa_pod_builder_add(&b, SPA_PROP_volume, SPA_POD_Float(*volume), 0);
    }
    if (mute.has_value()) {
        spa_pod_builder_add(&b, SPA_PROP_mute, SPA_POD_Bool(*mute), 0);
    }

    auto* param = reinterpret_cast<spa_pod*>(spa_pod_builder_pop(&b, &f));
    int res = pw_node_set_param(proxy, SPA_PARAM_Props, 0, param);

    pw_thread_loop_unlock(loop_);
    return res >= 0;
}

std::vector<AudioStream> PipeWireRouter::streams() const {
    return graph_.streams();
}

std::optional<AudioStream> PipeWireRouter::stream(uint32_t id) const {
    return graph_.stream(id);
}

std::vector<AudioSink> PipeWireRouter::sinks() const {
    return graph_.sinks();
}

std::optional<AudioSink> PipeWireRouter::sink(uint32_t id) const {
    return graph_.sink(id);
}

std::optional<AudioSink> PipeWireRouter::default_sink() const {
    return graph_.default_sink();
}

std::vector<StreamRoute> PipeWireRouter::routes() const {
    return graph_.routes();
}

std::optional<StreamRoute> PipeWireRouter::route(uint32_t stream_id) const {
    return graph_.route(stream_id);
}

bool PipeWireRouter::set_stream_volume(uint32_t stream_id, float volume) {
    auto st = graph_.stream(stream_id);
    if (!st) return false;

    float evaluated = policy_.evaluate_volume(*st, volume);
    set_node_spa_param(stream_id, evaluated, std::nullopt);
    graph_.set_node_volume(stream_id, evaluated);

    auto updated = graph_.stream(stream_id);
    if (updated) notify_stream_updated(*updated);
    return true;
}

bool PipeWireRouter::set_stream_mute(uint32_t stream_id, bool muted) {
    auto st = graph_.stream(stream_id);
    if (!st) return false;

    set_node_spa_param(stream_id, std::nullopt, muted);
    graph_.set_node_mute(stream_id, muted);

    auto updated = graph_.stream(stream_id);
    if (updated) notify_stream_updated(*updated);
    return true;
}

bool PipeWireRouter::set_sink_volume(uint32_t sink_id, float volume) {
    auto sk = graph_.sink(sink_id);
    if (!sk) return false;

    float clamped = volume::clamp_volume(volume);
    set_node_spa_param(sink_id, clamped, std::nullopt);
    graph_.set_node_volume(sink_id, clamped);

    auto updated = graph_.sink(sink_id);
    if (updated) notify_sink_updated(*updated);
    return true;
}

bool PipeWireRouter::set_sink_mute(uint32_t sink_id, bool muted) {
    auto sk = graph_.sink(sink_id);
    if (!sk) return false;

    set_node_spa_param(sink_id, std::nullopt, muted);
    graph_.set_node_mute(sink_id, muted);

    auto updated = graph_.sink(sink_id);
    if (updated) notify_sink_updated(*updated);
    return true;
}

bool PipeWireRouter::move_stream(uint32_t stream_id, uint32_t sink_id) {
    auto st = graph_.stream(stream_id);
    auto sk = graph_.sink(sink_id);
    if (!st || !sk) return false;

    if (loop_) {
        pw_thread_loop_lock(loop_);

        if (default_metadata_) {
            std::string sink_id_str = std::to_string(sink_id);
            pw_metadata_set_property(default_metadata_, stream_id, "target.node",
                                     "Spa:Id", sink_id_str.c_str());
            pw_metadata_set_property(default_metadata_, stream_id, "target.object",
                                     "Spa:String", sk->name.c_str());
        }

        // Also create a link between stream output ports and sink input ports if ports exist
        auto out_ports = graph_.ports_for_node(stream_id);
        auto in_ports = graph_.ports_for_node(sink_id);

        for (size_t i = 0; i < out_ports.size() && i < in_ports.size(); ++i) {
            struct spa_dict_item items[] = {
                { PW_KEY_LINK_OUTPUT_NODE, std::to_string(stream_id).c_str() },
                { PW_KEY_LINK_OUTPUT_PORT, std::to_string(out_ports[i].id).c_str() },
                { PW_KEY_LINK_INPUT_NODE, std::to_string(sink_id).c_str() },
                { PW_KEY_LINK_INPUT_PORT, std::to_string(in_ports[i].id).c_str() },
                { PW_KEY_OBJECT_LINGER, "1" }
            };
            struct spa_dict dict = SPA_DICT_INIT(items, 5);
            pw_core_create_object(core_, "link-factory", PW_TYPE_INTERFACE_Link,
                                  PW_VERSION_LINK, &dict, 0);
        }

        pw_thread_loop_unlock(loop_);
    }

    graph_.set_node_target(stream_id, sink_id);
    policy_.remember_route(st->app_id, sk->name);

    auto r = graph_.route(stream_id);
    if (r) notify_route_changed(*r);

    return true;
}

bool PipeWireRouter::set_default_sink(uint32_t sink_id) {
    auto sk = graph_.sink(sink_id);
    if (!sk) return false;

    if (loop_) {
        pw_thread_loop_lock(loop_);
        if (default_metadata_) {
            std::string val = "{\"name\":\"" + sk->name + "\"}";
            pw_metadata_set_property(default_metadata_, PW_ID_CORE, "default.audio.sink",
                                     "Spa:String:JSON", val.c_str());
        }
        pw_thread_loop_unlock(loop_);
    }

    graph_.set_default_sink_id(sink_id);
    notify_default_sink_changed(sink_id);
    return true;
}

void PipeWireRouter::add_observer(StreamObserver* observer) {
    if (!observer) return;
    std::lock_guard<std::mutex> lk(observers_mutex_);
    observers_.push_back(observer);
}

void PipeWireRouter::remove_observer(StreamObserver* observer) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    auto it = std::remove(observers_.begin(), observers_.end(), observer);
    observers_.erase(it, observers_.end());
}

void PipeWireRouter::notify_stream_added(const AudioStream& stream) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) {
        obs->on_stream_added(stream);
    }
}

void PipeWireRouter::notify_stream_updated(const AudioStream& stream) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) {
        obs->on_stream_updated(stream);
    }
}

void PipeWireRouter::notify_stream_removed(uint32_t stream_id) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) {
        obs->on_stream_removed(stream_id);
    }
}

void PipeWireRouter::notify_sink_added(const AudioSink& sink) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) {
        obs->on_sink_added(sink);
    }
}

void PipeWireRouter::notify_sink_updated(const AudioSink& sink) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) {
        obs->on_sink_updated(sink);
    }
}

void PipeWireRouter::notify_sink_removed(uint32_t sink_id) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) {
        obs->on_sink_removed(sink_id);
    }
}

void PipeWireRouter::notify_default_sink_changed(uint32_t sink_id) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) {
        obs->on_default_sink_changed(sink_id);
    }
}

void PipeWireRouter::notify_route_changed(const StreamRoute& route) {
    std::lock_guard<std::mutex> lk(observers_mutex_);
    for (auto* obs : observers_) {
        obs->on_route_changed(route);
    }
}

} // namespace bropulse
