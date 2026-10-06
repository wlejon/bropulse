# bropulse

**bropulse** is a modern C++20 audio policy and stream router for Linux desktop audio. It provides a direct PipeWire 0.3 native client with a PulseAudio introspection fallback, maintaining a thread-safe graph of nodes, ports, and links while applying desktop audio policies like application routing rules, volume limits, persistent route memory, and communication ducking.

## Architecture

```
                       +-----------------------+
                       |      AudioRouter      |
                       +-----------+-----------+
                                   |
         +-------------------------+-------------------------+
         |                                                   |
+--------v---------+                               +---------v--------+
|  PipeWireRouter  |                               |   PulseRouter    |
|  (libpipewire)   |                               |    (libpulse)    |
+--------+---------+                               +---------+--------+
         |                                                   |
         +-------------------------+-------------------------+
                                   | updates
                       +-----------v-----------+
                       |      StreamGraph      |
                       |  (Nodes, Ports, Links)|
                       +-----------+-----------+
                                   | evaluated by
                       +-----------v-----------+
                       |     PolicyManager     |
                       | (Routes, Ducking, Vol)|
                       +-----------------------+
```

### Core Subsystems

1. **`StreamGraph`** (`include/bropulse/stream_graph.h`, `src/stream_graph.cpp`):
   - Thread-safe, in-memory topology of streams, sink nodes, ports, and links.
   - Dynamic route resolution via active graph links, target node properties, and default sink fallback.
   - Read-write locking (`std::shared_mutex`) enabling high-throughput concurrent inspection and safe mutation.

2. **`PipeWireRouter`** (`src/pipewire/pw_router.cpp`):
   - Direct PipeWire 0.3 native client without external wrappers.
   - Listens on `pw_registry` for `PW_TYPE_INTERFACE_Node`, `Port`, `Link`, and `Metadata`.
   - Binds to `pw_node` and extracts parameters (`SPA_PARAM_Props`) using SPA POD parsing for volume and mute state.
   - Sets stream/sink volume and mute using `pw_node_set_param`.
   - Dynamically re-links streams to target sinks via `link-factory` and metadata `target.node`/`target.object`.
   - Synchronous roundtrip synchronization with `pw_core_sync`.

3. **`PulseRouter`** (`src/pulse/pa_router.cpp`):
   - Native PulseAudio fallback client using `pa_threaded_mainloop` and `pa_context`.
   - Subscribes to sink and sink-input events, introspecting streams, volumes, and server defaults.
   - Manages volume, mute, stream moves, and default sink selection.

4. **`PolicyManager`** (`include/bropulse/policy.h`, `src/policy.cpp`):
   - **Route Rules**: Match streams by `app_id`, `name`, or `media_role` with regex/substring patterns and route to preferred sinks with priority ordering.
   - **Volume Rules**: Enforce maximum volume clamps and initial creation volume overrides.
   - **Persistent Route Memory**: Remembers user stream-to-sink route preferences (`app_id -> sink_name`) across restarts.
   - **Audio Ducking**: Automatically attenuates background audio (e.g. music, games) by a configurable duck factor when communication or alert streams are active, restoring original volumes when they end.

## Requirements

- Linux
- C++20 compliant compiler (GCC 12+, Clang 15+)
- CMake 3.24+
- Ninja build system
- `libpipewire-0.3` (>= 0.3.0)
- `libpulse` (>= 15.0)

## Building

```bash
mkdir build && cd build
cmake -G Ninja ..
ninja
```

## Running Tests

Tests use the custom zero-dependency `tests/check.h` harness without assertions, exercising live node discovery, stream graph topology, volume math, and policy rules:

```bash
ctest --output-on-failure
```

Or run individual test binaries:
```bash
./tests/test_stream_graph
./tests/test_volume
./tests/test_policy
./tests/test_pipewire
./tests/test_pulse
./tests/test_router
```

## Example Usage

```cpp
#include <bropulse/router.h>
#include <iostream>

int main() {
    auto router = bropulse::AudioRouter::create(bropulse::Backend::Auto);
    if (!router || !router->start()) {
        std::cerr << "Failed to start audio router\n";
        return 1;
    }

    std::cout << "Connected using: " << bropulse::backend_to_string(router->backend()) << "\n";

    for (const auto& sink : router->sinks()) {
        std::cout << "Sink: [" << sink.id << "] " << sink.name << " (" << sink.description << ")\n";
    }

    for (const auto& stream : router->streams()) {
        std::cout << "Stream: [" << stream.id << "] " << stream.name
                  << " (app: " << stream.app_id << ", sink: " << stream.sink_id << ")\n";
    }

    router->stop();
    return 0;
}
```

## License

MIT
