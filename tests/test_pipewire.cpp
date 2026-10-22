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
    // Offline unit test for PipeWireRouter default sink flag propagation & move_stream notifications
    {
        bropulse::PipeWireRouter offline_router;
        struct DetailedObserver : public bropulse::StreamObserver {
            std::vector<bropulse::AudioSink> added_sinks;
            std::vector<bropulse::AudioSink> updated_sinks;
            std::vector<bropulse::AudioStream> updated_streams;
            std::vector<bropulse::StreamRoute> changed_routes;
            void on_sink_added(const bropulse::AudioSink& s) override { added_sinks.push_back(s); }
            void on_sink_updated(const bropulse::AudioSink& s) override { updated_sinks.push_back(s); }
            void on_stream_updated(const bropulse::AudioStream& s) override { updated_streams.push_back(s); }
            void on_route_changed(const bropulse::StreamRoute& r) override { changed_routes.push_back(r); }
        } obs;
        offline_router.add_observer(&obs);

        offline_router.graph().set_default_sink_name("alsa_output.default");

        spa_dict_item sink_items[] = {
            { PW_KEY_NODE_NAME, "alsa_output.default" },
            { PW_KEY_NODE_DESCRIPTION, "Default Sink" },
            { PW_KEY_MEDIA_CLASS, "Audio/Sink" }
        };
        spa_dict sink_dict = SPA_DICT_INIT(sink_items, 3);
        offline_router.handle_global(10, 0, PW_TYPE_INTERFACE_Node, 3, &sink_dict);
        REQUIRE(obs.added_sinks.size() == 1);
        CHECK_TRUE(obs.added_sinks[0].is_default);

        pw_node_info ninfo{};
        ninfo.id = 10;
        ninfo.state = PW_NODE_STATE_RUNNING;
        ninfo.props = &sink_dict;
        offline_router.handle_node_info(10, &ninfo);
        REQUIRE(obs.updated_sinks.size() == 1);
        CHECK_TRUE(obs.updated_sinks[0].is_default);

        // Add second sink and a stream, plus ports including monitor ("out") on sink
        spa_dict_item sink2_items[] = {
            { PW_KEY_NODE_NAME, "alsa_output.secondary" },
            { PW_KEY_NODE_DESCRIPTION, "Secondary Sink" },
            { PW_KEY_MEDIA_CLASS, "Audio/Sink" }
        };
        spa_dict sink2_dict = SPA_DICT_INIT(sink2_items, 3);
        offline_router.handle_global(11, 0, PW_TYPE_INTERFACE_Node, 3, &sink2_dict);
        REQUIRE(obs.added_sinks.size() == 2);
        CHECK_FALSE(obs.added_sinks[1].is_default);

        spa_dict_item stream_items[] = {
            { PW_KEY_NODE_NAME, "Music Player" },
            { PW_KEY_MEDIA_CLASS, "Stream/Output/Audio" }
        };
        spa_dict stream_dict = SPA_DICT_INIT(stream_items, 2);
        offline_router.handle_global(20, 0, PW_TYPE_INTERFACE_Node, 3, &stream_dict);

        CHECK_TRUE(offline_router.move_stream(20, 11));
        REQUIRE(obs.updated_streams.size() == 1);
        CHECK_EQ(obs.updated_streams[0].id, 20u);
        CHECK_EQ(obs.updated_streams[0].sink_id, 11u);
        REQUIRE(obs.changed_routes.size() == 1);
        CHECK_EQ(obs.changed_routes[0].stream_id, 20u);
        CHECK_EQ(obs.changed_routes[0].sink_id, 11u);
    }

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
