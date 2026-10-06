#include "src/pulse/pa_router.h"
#include "check.h"

#include <chrono>

int main() {
    if (!bropulse::AudioRouter::is_pulseaudio_available()) {
        bstest::skip("test_pulse", "PulseAudio daemon is not running or accessible");
    }

    bropulse::PulseRouter router;
    CHECK_FALSE(router.is_running());

    bool started = router.start();
    REQUIRE(started);
    CHECK_TRUE(router.is_running());

    // Wait for sinks to be discovered via introspection
    bool found_sinks = bstest::wait_until([&router]() {
        return !router.sinks().empty();
    }, std::chrono::milliseconds(1500));

    CHECK_TRUE(found_sinks);

    auto sinks = router.sinks();
    std::printf("[test_pulse] Discovered %zu PulseAudio sink(s):\n", sinks.size());
    for (const auto& sink : sinks) {
        std::printf("  - Sink ID: %u, Name: %s, Desc: %s, Vol: %.2f\n",
                    sink.id, sink.name.c_str(), sink.description.c_str(), sink.volume);
        CHECK_FALSE(sink.name.empty());
    }

    auto def_sink = router.default_sink();
    if (def_sink) {
        std::printf("[test_pulse] Default sink: ID %u (%s)\n", def_sink->id, def_sink->name.c_str());
    }

    router.stop();
    CHECK_FALSE(router.is_running());

    return bstest::finish("test_pulse");
}
