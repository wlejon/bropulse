#include <bropulse/router.h>
#include "check.h"

#include <chrono>

class RouterObserver : public bropulse::StreamObserver {
public:
    int route_changes{0};
    int sink_events{0};

    void on_sink_added(const bropulse::AudioSink&) override {
        ++sink_events;
    }
    void on_route_changed(const bropulse::StreamRoute&) override {
        ++route_changes;
    }
};

int main() {
    // 1. Backend creation
    auto router = bropulse::AudioRouter::create(bropulse::Backend::Auto);
    REQUIRE(router != nullptr);
    CHECK_TRUE(router->backend() == bropulse::Backend::PipeWire ||
               router->backend() == bropulse::Backend::PulseAudio);

    RouterObserver obs;
    router->add_observer(&obs);

    // 2. Policy manager access
    auto& policy = router->policy();
    bropulse::RouteRule rule;
    rule.id = "browser_rule";
    rule.app_id_pattern = "org.mozilla.firefox";
    rule.target_sink_name = "Speakers";
    policy.add_route_rule(rule);

    CHECK_EQ(policy.route_rules().size(), 1u);

    // 3. Start router
    bool started = router->start();
    REQUIRE(started);
    CHECK_TRUE(router->is_running());

    // Sinks should be discovered
    bstest::wait_until([&router]() {
        return !router->sinks().empty();
    }, std::chrono::milliseconds(1500));

    auto sinks = router->sinks();
    std::printf("[test_router] Backend %s found %zu sinks\n",
                bropulse::backend_to_string(router->backend()), sinks.size());
    CHECK_FALSE(sinks.empty());

    router->remove_observer(&obs);
    router->stop();
    CHECK_FALSE(router->is_running());

    return bstest::finish("test_router");
}
