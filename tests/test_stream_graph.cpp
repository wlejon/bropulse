#include <bropulse/stream_graph.h>
#include "check.h"

#include <atomic>
#include <thread>
#include <vector>

int main() {
    bropulse::StreamGraph graph;

    // 1. Initial empty state
    CHECK_EQ(graph.streams().size(), 0u);
    CHECK_EQ(graph.sinks().size(), 0u);
    CHECK_FALSE(graph.default_sink().has_value());

    // 2. Add sinks
    bropulse::NodeInfo sink1;
    sink1.id = 100;
    sink1.name = "alsa_output.pci.stereo";
    sink1.description = "Speakers";
    sink1.media_class = "Audio/Sink";
    sink1.volume = 0.8f;
    sink1.is_default = true;
    sink1.channels = 2;
    graph.add_or_update_node(sink1);

    bropulse::NodeInfo sink2;
    sink2.id = 101;
    sink2.name = "bluez_output.headphones";
    sink2.description = "Bluetooth Headphones";
    sink2.media_class = "Audio/Sink";
    sink2.volume = 0.5f;
    sink2.channels = 2;
    graph.add_or_update_node(sink2);

    CHECK_EQ(graph.sinks().size(), 2u);
    auto def_sink = graph.default_sink();
    REQUIRE(def_sink.has_value());
    CHECK_EQ(def_sink->id, 100u);
    CHECK_EQ(def_sink->name, std::string("alsa_output.pci.stereo"));

    // 3. Add stream with explicit target_node_id
    bropulse::NodeInfo stream1;
    stream1.id = 200;
    stream1.name = "Spotify Music";
    stream1.media_class = "Stream/Output/Audio";
    stream1.properties["application.name"] = "Spotify";
    stream1.properties["application.id"] = "com.spotify.Client";
    stream1.properties["application.process.id"] = "1234";
    stream1.properties["media.role"] = "music";
    stream1.target_node_id = 101;
    stream1.volume = 0.7f;
    graph.add_or_update_node(stream1);

    CHECK_EQ(graph.streams().size(), 1u);
    auto st1 = graph.stream(200);
    REQUIRE(st1.has_value());
    CHECK_EQ(st1->name, std::string("Spotify Music"));
    CHECK_EQ(st1->app_id, std::string("com.spotify.Client"));
    CHECK_EQ(st1->pid, 1234u);
    CHECK_EQ(st1->sink_id, 101u);

    // 4. Test route resolution via links
    bropulse::NodeInfo stream2;
    stream2.id = 201;
    stream2.name = "Firefox Audio";
    stream2.media_class = "Stream/Output/Audio";
    stream2.properties["application.name"] = "Firefox";
    stream2.properties["application.id"] = "org.mozilla.firefox";
    graph.add_or_update_node(stream2);

    // Ports for stream2
    bropulse::PortInfo p_out1{301, 201, "output", "output_FL", "FL"};
    bropulse::PortInfo p_out2{302, 201, "output", "output_FR", "FR"};
    graph.add_or_update_port(p_out1);
    graph.add_or_update_port(p_out2);

    // Ports for sink1
    bropulse::PortInfo p_in1{401, 100, "input", "playback_FL", "FL"};
    bropulse::PortInfo p_in2{402, 100, "input", "playback_FR", "FR"};
    graph.add_or_update_port(p_in1);
    graph.add_or_update_port(p_in2);

    // Link stream2 -> sink1
    bropulse::LinkInfo link1{501, 201, 301, 100, 401, "active"};
    graph.add_or_update_link(link1);

    auto st2 = graph.stream(201);
    REQUIRE(st2.has_value());
    CHECK_EQ(st2->sink_id, 100u);

    // 5. Test volume and mute updates
    graph.set_node_volume(200, 0.45f);
    st1 = graph.stream(200);
    REQUIRE(st1.has_value());
    CHECK_NEAR(st1->volume, 0.45f, 0.001f);

    graph.set_node_mute(200, true);
    st1 = graph.stream(200);
    REQUIRE(st1.has_value());
    CHECK_TRUE(st1->is_muted);

    // 6. Test change callback
    int callback_count = 0;
    graph.set_change_callback([&]() {
        ++callback_count;
    });

    graph.set_node_volume(201, 0.9f);
    CHECK_EQ(callback_count, 1);

    // 7. Test cascading removal of node, ports, and links
    CHECK_TRUE(graph.get_port(301).has_value());
    CHECK_TRUE(graph.get_link(501).has_value());
    graph.remove_node(201);
    CHECK_FALSE(graph.get_node(201).has_value());
    CHECK_FALSE(graph.get_port(301).has_value());
    CHECK_FALSE(graph.get_port(302).has_value());
    CHECK_FALSE(graph.get_link(501).has_value());

    // 8. Test thread safety: concurrent reads and writes
    std::atomic<bool> stop_flag{false};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&graph, &stop_flag]() {
            while (!stop_flag.load()) {
                auto s = graph.streams();
                auto k = graph.sinks();
                auto r = graph.routes();
                (void)s; (void)k; (void)r;
            }
        });
    }

    // Writer thread performs updates
    for (int i = 0; i < 100; ++i) {
        bropulse::NodeInfo temp_node;
        temp_node.id = 3000 + i;
        temp_node.name = "Worker Stream";
        temp_node.media_class = "Stream/Output/Audio";
        temp_node.volume = 0.5f;
        graph.add_or_update_node(temp_node);
        graph.set_node_volume(temp_node.id, 0.6f);
        graph.remove_node(temp_node.id);
    }

    stop_flag.store(true);
    for (auto& t : readers) {
        t.join();
    }

    return bstest::finish("test_stream_graph");
}
