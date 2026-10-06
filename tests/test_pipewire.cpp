#include "src/pipewire/pw_router.h"
#include "check.h"

#include <chrono>
#include <string>
#include <thread>

class TestObserver : public bropulse::StreamObserver {
public:
    int sinks_added{0};
    int sinks_updated{0};
    int streams_added{0};

    void on_sink_added(const bropulse::AudioSink&) override {
        ++sinks_added;
    }

    void on_sink_updated(const bropulse::AudioSink&) override {
        ++sinks_updated;
    }

    void on_stream_added(const bropulse::AudioStream&) override {
        ++streams_added;
    }
};

int main() {
    if (!bropulse::AudioRouter::is_pipewire_available()) {
        bstest::skip("test_pipewire", "PipeWire daemon is not running or accessible");
    }

    bropulse::PipeWireRouter router;
    TestObserver observer;
    router.add_observer(&observer);

    CHECK_FALSE(router.is_running());
    bool started = router.start();
    REQUIRE(started);
    CHECK_TRUE(router.is_running());

    // Allow registry events to be processed
    bool found_sinks = bstest::wait_until([&router]() {
        return !router.sinks().empty();
    }, std::chrono::milliseconds(1500));

    CHECK_TRUE(found_sinks);

    auto sinks = router.sinks();
    std::printf("[test_pipewire] Discovered %zu PipeWire audio sink(s):\n", sinks.size());
    for (const auto& sink : sinks) {
        std::printf("  - ID: %u, Name: %s, Desc: %s, Vol: %.2f, Channels: %u, Default: %d\n",
                    sink.id, sink.name.c_str(), sink.description.c_str(),
                    sink.volume, sink.channels, sink.is_default);
        CHECK_TRUE(sink.id > 0);
        CHECK_FALSE(sink.name.empty());
    }

    // Check default sink
    auto def_sink = router.default_sink();
    if (def_sink) {
        std::printf("[test_pipewire] Default sink: ID %u (%s)\n", def_sink->id, def_sink->name.c_str());
    }

    // Observer should have caught sink discovery
    CHECK_TRUE(observer.sinks_added > 0 || observer.sinks_updated > 0);

    // Test stream list (may be empty if no music playing, which is expected and valid)
    auto streams = router.streams();
    std::printf("[test_pipewire] Discovered %zu active audio stream(s)\n", streams.size());
    for (const auto& st : streams) {
        std::printf("  - Stream ID: %u, Name: %s, App: %s, Sink: %u\n",
                    st.id, st.name.c_str(), st.app_id.c_str(), st.sink_id);
    }

    router.remove_observer(&observer);
    router.stop();
    CHECK_FALSE(router.is_running());

    return bstest::finish("test_pipewire");
}
