#include <bropulse/stream_graph.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace bropulse {

namespace {

std::string get_prop_or(const std::unordered_map<std::string, std::string>& props,
                        const std::string& key,
                        const std::string& fallback = {}) {
    auto it = props.find(key);
    if (it != props.end() && !it->second.empty()) {
        return it->second;
    }
    return fallback;
}

} // namespace

AudioStream NodeInfo::to_audio_stream(uint32_t resolved_sink_id) const {
    AudioStream s;
    s.id = id;
    s.name = get_prop_or(properties, "media.name");
    if (s.name.empty()) {
        s.name = !name.empty() ? name : get_prop_or(properties, "application.name");
    }
    if (s.name.empty()) {
        s.name = !description.empty() ? description : "Audio Stream";
    }

    std::string pid_str = get_prop_or(properties, "application.process.id");
    if (!pid_str.empty()) {
        s.pid = static_cast<uint32_t>(std::strtoul(pid_str.c_str(), nullptr, 10));
    }

    s.app_id = get_prop_or(properties, "application.id");
    if (s.app_id.empty()) {
        s.app_id = get_prop_or(properties, "application.process.binary");
    }
    if (s.app_id.empty()) {
        s.app_id = s.name;
    }

    s.icon = get_prop_or(properties, "application.icon-name");
    if (s.icon.empty()) {
        s.icon = get_prop_or(properties, "application.icon");
    }

    s.volume = volume;
    s.is_muted = is_muted;
    s.sink_id = resolved_sink_id;
    s.channel_volumes = channel_volumes;
    s.media_role = get_prop_or(properties, "media.role");
    s.properties = properties;
    return s;
}

AudioSink NodeInfo::to_audio_sink() const {
    AudioSink sink;
    sink.id = id;
    sink.name = !name.empty() ? name : get_prop_or(properties, "node.name", "Sink " + std::to_string(id));
    sink.description = !description.empty() ? description
        : get_prop_or(properties, "node.description",
                      get_prop_or(properties, "device.description", sink.name));
    sink.volume = volume;
    sink.is_default = is_default;
    sink.is_muted = is_muted;
    sink.channels = channels;
    sink.channel_volumes = channel_volumes;
    sink.state = state;
    sink.properties = properties;
    return sink;
}

StreamGraph::StreamGraph() = default;

StreamGraph::StreamGraph(StreamGraph&& other) noexcept {
    std::unique_lock<std::shared_mutex> lock(other.mutex_);
    nodes_ = std::move(other.nodes_);
    ports_ = std::move(other.ports_);
    links_ = std::move(other.links_);
    default_sink_id_ = other.default_sink_id_;
    default_sink_name_ = std::move(other.default_sink_name_);
    change_callback_ = std::move(other.change_callback_);
}

StreamGraph& StreamGraph::operator=(StreamGraph&& other) noexcept {
    if (this == &other) return *this;
    std::unique_lock<std::shared_mutex> lock_this(mutex_, std::defer_lock);
    std::unique_lock<std::shared_mutex> lock_other(other.mutex_, std::defer_lock);
    std::lock(lock_this, lock_other);

    nodes_ = std::move(other.nodes_);
    ports_ = std::move(other.ports_);
    links_ = std::move(other.links_);
    default_sink_id_ = other.default_sink_id_;
    default_sink_name_ = std::move(other.default_sink_name_);
    change_callback_ = std::move(other.change_callback_);
    return *this;
}

void StreamGraph::add_or_update_node(const NodeInfo& node) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(node.id);
    if (it != nodes_.end()) {
        it->second = node;
    } else {
        nodes_.emplace(node.id, node);
    }

    if (node.is_sink()) {
        if (default_sink_id_ == node.id || (!default_sink_name_.empty() && node.name == default_sink_name_)) {
            nodes_[node.id].is_default = true;
            default_sink_id_ = node.id;
        } else if (default_sink_id_ == 0 && nodes_[node.id].is_default) {
            default_sink_id_ = node.id;
        }
    }

    notify_change_locked();
}

bool StreamGraph::remove_node(uint32_t id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return false;
    }

    // Remove associated ports
    for (auto p = ports_.begin(); p != ports_.end();) {
        if (p->second.node_id == id) {
            // Remove associated links
            uint32_t port_id = p->first;
            for (auto l = links_.begin(); l != links_.end();) {
                if (l->second.output_port_id == port_id || l->second.input_port_id == port_id) {
                    l = links_.erase(l);
                } else {
                    ++l;
                }
            }
            p = ports_.erase(p);
        } else {
            ++p;
        }
    }

    // Remove links connected to this node
    for (auto l = links_.begin(); l != links_.end();) {
        if (l->second.output_node_id == id || l->second.input_node_id == id) {
            l = links_.erase(l);
        } else {
            ++l;
        }
    }

    nodes_.erase(it);

    if (default_sink_id_ == id) {
        default_sink_id_ = 0;
        // Re-elect first available sink as default if any
        for (auto& [nid, n] : nodes_) {
            if (n.is_sink()) {
                default_sink_id_ = nid;
                n.is_default = true;
                break;
            }
        }
    }

    notify_change_locked();
    return true;
}

void StreamGraph::add_or_update_port(const PortInfo& port) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ports_[port.id] = port;
    notify_change_locked();
}

bool StreamGraph::remove_port(uint32_t id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = ports_.find(id);
    if (it == ports_.end()) {
        return false;
    }

    for (auto l = links_.begin(); l != links_.end();) {
        if (l->second.output_port_id == id || l->second.input_port_id == id) {
            l = links_.erase(l);
        } else {
            ++l;
        }
    }

    ports_.erase(it);
    notify_change_locked();
    return true;
}

void StreamGraph::add_or_update_link(const LinkInfo& link) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    links_[link.id] = link;
    notify_change_locked();
}

bool StreamGraph::remove_link(uint32_t id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = links_.find(id);
    if (it == links_.end()) {
        return false;
    }
    links_.erase(it);
    notify_change_locked();
    return true;
}

bool StreamGraph::set_node_volume(uint32_t id, float volume, const std::vector<float>& channel_vols) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    it->second.volume = volume;
    if (!channel_vols.empty()) {
        it->second.channel_volumes = channel_vols;
    }
    notify_change_locked();
    return true;
}

bool StreamGraph::set_node_mute(uint32_t id, bool muted) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    it->second.is_muted = muted;
    notify_change_locked();
    return true;
}

bool StreamGraph::set_node_target(uint32_t id, uint32_t target_node_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    it->second.target_node_id = target_node_id;
    notify_change_locked();
    return true;
}

void StreamGraph::set_default_sink_id(uint32_t sink_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    default_sink_id_ = sink_id;
    for (auto& [id, node] : nodes_) {
        if (node.is_sink()) {
            node.is_default = (id == sink_id);
            if (node.is_default) {
                default_sink_name_ = node.name;
            }
        }
    }
    notify_change_locked();
}

void StreamGraph::set_default_sink_name(const std::string& sink_name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    default_sink_name_ = sink_name;
    default_sink_id_ = 0;
    for (auto& [id, node] : nodes_) {
        if (node.is_sink()) {
            if (node.name == sink_name) {
                node.is_default = true;
                default_sink_id_ = id;
            } else {
                node.is_default = false;
            }
        }
    }
    notify_change_locked();
}

uint32_t StreamGraph::default_sink_id() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return default_sink_id_;
}

std::string StreamGraph::default_sink_name() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return default_sink_name_;
}

void StreamGraph::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    nodes_.clear();
    ports_.clear();
    links_.clear();
    default_sink_id_ = 0;
    default_sink_name_.clear();
    notify_change_locked();
}

uint32_t StreamGraph::resolve_sink_for_stream_locked(uint32_t stream_id) const {
    auto nit = nodes_.find(stream_id);
    if (nit == nodes_.end()) return 0;
    const auto& node = nit->second;

    // 1. Direct target node ID
    if (node.target_node_id != 0) {
        auto sit = nodes_.find(node.target_node_id);
        if (sit != nodes_.end() && sit->second.is_sink()) {
            return node.target_node_id;
        }
    }

    // 2. target.node property (ID or name)
    auto pit = node.properties.find("target.node");
    if (pit == node.properties.end()) {
        pit = node.properties.find("node.target");
    }
    if (pit != node.properties.end() && !pit->second.empty()) {
        const std::string& target_str = pit->second;
        char* end = nullptr;
        unsigned long tid = std::strtoul(target_str.c_str(), &end, 10);
        if (end != target_str.c_str() && *end == '\0') {
            auto sit = nodes_.find(static_cast<uint32_t>(tid));
            if (sit != nodes_.end() && sit->second.is_sink()) {
                return static_cast<uint32_t>(tid);
            }
        }
        // Match by name
        for (const auto& [nid, n] : nodes_) {
            if (n.is_sink() && n.name == target_str) {
                return nid;
            }
        }
    }

    // 3. Resolve through links from stream output ports
    for (const auto& [lid, link] : links_) {
        if (link.output_node_id == stream_id) {
            auto sit = nodes_.find(link.input_node_id);
            if (sit != nodes_.end() && sit->second.is_sink()) {
                return link.input_node_id;
            }
        } else {
            // Check if link's output port belongs to this stream
            auto opt_it = ports_.find(link.output_port_id);
            if (opt_it != ports_.end() && opt_it->second.node_id == stream_id) {
                // Link's input port
                auto ipt_it = ports_.find(link.input_port_id);
                if (ipt_it != ports_.end()) {
                    auto sit = nodes_.find(ipt_it->second.node_id);
                    if (sit != nodes_.end() && sit->second.is_sink()) {
                        return sit->first;
                    }
                }
            }
        }
    }

    // 4. Default sink
    if (default_sink_id_ != 0) {
        auto dit = nodes_.find(default_sink_id_);
        if (dit != nodes_.end() && dit->second.is_sink()) {
            return default_sink_id_;
        }
    }

    // 5. Fallback: first sink in map
    for (const auto& [nid, n] : nodes_) {
        if (n.is_sink()) return nid;
    }

    return 0;
}

uint32_t StreamGraph::resolve_sink_for_stream(uint32_t stream_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return resolve_sink_for_stream_locked(stream_id);
}

std::vector<AudioStream> StreamGraph::streams() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<AudioStream> result;
    for (const auto& [id, node] : nodes_) {
        if (node.is_audio_stream()) {
            uint32_t sink_id = resolve_sink_for_stream_locked(id);
            result.push_back(node.to_audio_stream(sink_id));
        }
    }
    return result;
}

std::optional<AudioStream> StreamGraph::stream(uint32_t id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(id);
    if (it == nodes_.end() || !it->second.is_audio_stream()) {
        return std::nullopt;
    }
    uint32_t sink_id = resolve_sink_for_stream_locked(id);
    return it->second.to_audio_stream(sink_id);
}

std::vector<AudioSink> StreamGraph::sinks() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<AudioSink> result;
    for (const auto& [id, node] : nodes_) {
        if (node.is_sink()) {
            result.push_back(node.to_audio_sink());
        }
    }
    return result;
}

std::optional<AudioSink> StreamGraph::sink(uint32_t id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(id);
    if (it == nodes_.end() || !it->second.is_sink()) {
        return std::nullopt;
    }
    return it->second.to_audio_sink();
}

std::optional<AudioSink> StreamGraph::default_sink() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (default_sink_id_ != 0) {
        auto it = nodes_.find(default_sink_id_);
        if (it != nodes_.end() && it->second.is_sink()) {
            return it->second.to_audio_sink();
        }
    }
    for (const auto& [id, node] : nodes_) {
        if (node.is_sink() && node.is_default) {
            return node.to_audio_sink();
        }
    }
    for (const auto& [id, node] : nodes_) {
        if (node.is_sink()) {
            return node.to_audio_sink();
        }
    }
    return std::nullopt;
}

std::vector<StreamRoute> StreamGraph::routes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<StreamRoute> result;
    for (const auto& [id, node] : nodes_) {
        if (node.is_audio_stream()) {
            uint32_t sink_id = resolve_sink_for_stream_locked(id);
            StreamRoute r;
            r.stream_id = id;
            r.sink_id = sink_id;
            r.stream_name = node.name.empty() ? ("Stream " + std::to_string(id)) : node.name;
            auto sit = nodes_.find(sink_id);
            if (sit != nodes_.end()) {
                r.sink_name = sit->second.name;
                r.is_active = (node.state == "running" && sit->second.state == "running");
            } else {
                r.sink_name = "None";
                r.is_active = false;
            }
            result.push_back(r);
        }
    }
    return result;
}

std::optional<StreamRoute> StreamGraph::route(uint32_t stream_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto nit = nodes_.find(stream_id);
    if (nit == nodes_.end() || !nit->second.is_audio_stream()) {
        return std::nullopt;
    }
    uint32_t sink_id = resolve_sink_for_stream_locked(stream_id);
    StreamRoute r;
    r.stream_id = stream_id;
    r.sink_id = sink_id;
    r.stream_name = nit->second.name.empty() ? ("Stream " + std::to_string(stream_id)) : nit->second.name;
    auto sit = nodes_.find(sink_id);
    if (sit != nodes_.end()) {
        r.sink_name = sit->second.name;
        r.is_active = (nit->second.state == "running" && sit->second.state == "running");
    } else {
        r.sink_name = "None";
        r.is_active = false;
    }
    return r;
}

std::optional<NodeInfo> StreamGraph::get_node(uint32_t id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(id);
    if (it != nodes_.end()) return it->second;
    return std::nullopt;
}

std::optional<PortInfo> StreamGraph::get_port(uint32_t id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = ports_.find(id);
    if (it != ports_.end()) return it->second;
    return std::nullopt;
}

std::optional<LinkInfo> StreamGraph::get_link(uint32_t id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = links_.find(id);
    if (it != links_.end()) return it->second;
    return std::nullopt;
}

std::vector<NodeInfo> StreamGraph::all_nodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<NodeInfo> result;
    result.reserve(nodes_.size());
    for (const auto& [id, node] : nodes_) {
        result.push_back(node);
    }
    return result;
}

std::vector<PortInfo> StreamGraph::ports_for_node(uint32_t node_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<PortInfo> result;
    for (const auto& [id, port] : ports_) {
        if (port.node_id == node_id) {
            result.push_back(port);
        }
    }
    return result;
}

std::vector<LinkInfo> StreamGraph::links_for_node(uint32_t node_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<LinkInfo> result;
    for (const auto& [id, link] : links_) {
        if (link.output_node_id == node_id || link.input_node_id == node_id) {
            result.push_back(link);
        }
    }
    return result;
}

void StreamGraph::set_change_callback(ChangeCallback cb) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    change_callback_ = std::move(cb);
}

void StreamGraph::notify_change_locked() {
    if (change_callback_) {
        change_callback_();
    }
}

} // namespace bropulse
