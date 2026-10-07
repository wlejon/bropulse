#include "bropulse/api.h"
#include "embed/embed.h"
#include "eval/eval.h"
#include "check.h"

#include <iostream>
#include <string>

int main() {
    namespace ev = bronze::embed;
    using namespace bronze::eval;

    std::cout << "Starting bropulse JavaScript API test..." << std::endl;

    // 1. Install bro.pulse into Bronze realm
    bropulse::api::installPulse();

    auto g = ev::globalValue("bro");
    REQUIRE(g.found);
    REQUIRE(ev::isObject(g.value));

    ev::Persistent pulse(ev::getProperty(g.value, "pulse"));
    REQUIRE(ev::isObject(pulse.get()));
    std::cout << "  Mounted bro.pulse successfully." << std::endl;

    // Verify all required methods exist
    const char* methods[] = {
        "getSinks", "getDefaultSink", "setDefaultSink", "setSinkVolume", "setSinkMuted",
        "getStreams", "setStreamVolume", "setStreamMuted", "moveStream",
        "on", "off", "addEventListener", "removeEventListener"
    };
    for (const char* m : methods) {
        auto fn = ev::getProperty(pulse.get(), m);
        CHECK_TRUE(ev::isFunction(fn));
        std::cout << "  Found bro.pulse." << m << std::endl;
    }

    // 2. Test getSinks()
    std::cout << "Testing getSinks()..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  const sinks = bro.pulse.getSinks();\n"
            "  if (!Array.isArray(sinks)) return false;\n"
            "  for (const s of sinks) {\n"
            "    if (typeof s.id !== 'number') return false;\n"
            "    if (typeof s.name !== 'string') return false;\n"
            "    if (typeof s.description !== 'string') return false;\n"
            "    if (typeof s.volume !== 'number') return false;\n"
            "    if (typeof s.isMuted !== 'boolean') return false;\n"
            "    if (typeof s.isDefault !== 'boolean') return false;\n"
            "  }\n"
            "  return true;\n"
            "})()\n"
        );
        if (r.thrown) {
            std::cerr << "getSinks eval threw: " << ev::toUtf8(r.value) << std::endl;
        }
        REQUIRE(!r.thrown);
        CHECK_TRUE(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  getSinks() [PASS]" << std::endl;
    }

    // 3. Test getDefaultSink()
    std::cout << "Testing getDefaultSink()..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  const def = bro.pulse.getDefaultSink();\n"
            "  if (def === null) return true;\n"
            "  if (typeof def !== 'object') return false;\n"
            "  if (typeof def.id !== 'number') return false;\n"
            "  if (typeof def.name !== 'string') return false;\n"
            "  if (typeof def.description !== 'string') return false;\n"
            "  if (typeof def.volume !== 'number') return false;\n"
            "  if (typeof def.isMuted !== 'boolean') return false;\n"
            "  if (typeof def.isDefault !== 'boolean') return false;\n"
            "  return true;\n"
            "})()\n"
        );
        if (r.thrown) {
            std::cerr << "getDefaultSink eval threw: " << ev::toUtf8(r.value) << std::endl;
        }
        REQUIRE(!r.thrown);
        CHECK_TRUE(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  getDefaultSink() [PASS]" << std::endl;
    }

    // 4. Test getStreams()
    std::cout << "Testing getStreams()..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  const streams = bro.pulse.getStreams();\n"
            "  if (!Array.isArray(streams)) return false;\n"
            "  for (const st of streams) {\n"
            "    if (typeof st.id !== 'number') return false;\n"
            "    if (typeof st.name !== 'string') return false;\n"
            "    if (typeof st.pid !== 'number') return false;\n"
            "    if (typeof st.appId !== 'string') return false;\n"
            "    if (typeof st.icon !== 'string') return false;\n"
            "    if (typeof st.volume !== 'number') return false;\n"
            "    if (typeof st.isMuted !== 'boolean') return false;\n"
            "    if (typeof st.sinkId !== 'number') return false;\n"
            "  }\n"
            "  return true;\n"
            "})()\n"
        );
        if (r.thrown) {
            std::cerr << "getStreams eval threw: " << ev::toUtf8(r.value) << std::endl;
        }
        REQUIRE(!r.thrown);
        CHECK_TRUE(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  getStreams() [PASS]" << std::endl;
    }

    // 5. Test controls with invalid IDs
    std::cout << "Testing control error handling..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  const badSinkVol = bro.pulse.setSinkVolume(999999, 0.5);\n"
            "  if (badSinkVol !== false) return false;\n"
            "  const badSinkMute = bro.pulse.setSinkMuted(999999, true);\n"
            "  if (badSinkMute !== false) return false;\n"
            "  const badDefSink = bro.pulse.setDefaultSink(999999);\n"
            "  if (badDefSink !== false) return false;\n"
            "  const badStreamVol = bro.pulse.setStreamVolume(999999, 0.5);\n"
            "  if (badStreamVol !== false) return false;\n"
            "  const badStreamMute = bro.pulse.setStreamMuted(999999, true);\n"
            "  if (badStreamMute !== false) return false;\n"
            "  const badMove = bro.pulse.moveStream(999999, 999998);\n"
            "  if (badMove !== false) return false;\n"
            "  return true;\n"
            "})()\n"
        );
        if (r.thrown) {
            std::cerr << "control test eval threw: " << ev::toUtf8(r.value) << std::endl;
        }
        REQUIRE(!r.thrown);
        CHECK_TRUE(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  controls with invalid IDs [PASS]" << std::endl;
    }

    // 6. Test event listeners: on, addEventListener, off, removeEventListener
    std::cout << "Testing event listeners..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  let eventsReceived = [];\n"
            "  const listener = (evt) => { eventsReceived.push(evt); };\n"
            "  bro.pulse.on('sinkAdded', listener);\n"
            "  bro.pulse.addEventListener('sinkUpdated', listener);\n"
            "  bro.pulse.on('sinkRemoved', listener);\n"
            "  bro.pulse.on('streamAdded', listener);\n"
            "  bro.pulse.on('streamUpdated', listener);\n"
            "  bro.pulse.on('streamRemoved', listener);\n"
            "  bro.pulse.on('defaultSinkChanged', listener);\n"
            "  bro.pulse.off('sinkAdded', listener);\n"
            "  bro.pulse.removeEventListener('sinkUpdated', listener);\n"
            "  return true;\n"
            "})()\n"
        );
        REQUIRE(!r.thrown);
        CHECK_TRUE(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  event registration [PASS]" << std::endl;
    }

    // 7. Test tickPulseAsync() and event dispatching for all 7 event types
    std::cout << "Testing tickPulseAsync() and event dispatching..." << std::endl;
    {
        evalScript(
            "globalThis.__events = {};\n"
            "const record = (name) => (e) => {\n"
            "  globalThis.__events[name] = globalThis.__events[name] || [];\n"
            "  globalThis.__events[name].push(e);\n"
            "};\n"
            "bro.pulse.on('sinkAdded', record('sinkAdded'));\n"
            "bro.pulse.on('sinkUpdated', record('sinkUpdated'));\n"
            "bro.pulse.on('sinkRemoved', record('sinkRemoved'));\n"
            "bro.pulse.on('streamAdded', record('streamAdded'));\n"
            "bro.pulse.on('streamUpdated', record('streamUpdated'));\n"
            "bro.pulse.on('streamRemoved', record('streamRemoved'));\n"
            "bro.pulse.on('defaultSinkChanged', record('defaultSinkChanged'));\n"
        );

        auto obs = bropulse::api::pulseObserver();
        REQUIRE(obs != nullptr);

        bropulse::AudioSink testSink;
        testSink.id = 42;
        testSink.name = "TestSink";
        testSink.description = "Test Sink Description";
        testSink.volume = 0.85f;
        testSink.is_muted = false;
        testSink.is_default = true;

        bropulse::AudioStream testStream;
        testStream.id = 84;
        testStream.name = "TestStream";
        testStream.pid = 1234;
        testStream.app_id = "org.test.App";
        testStream.icon = "audio-card";
        testStream.volume = 0.7f;
        testStream.is_muted = false;
        testStream.sink_id = 42;

        obs->on_sink_added(testSink);
        obs->on_sink_updated(testSink);
        obs->on_sink_removed(42);
        obs->on_stream_added(testStream);
        obs->on_stream_updated(testStream);
        obs->on_stream_removed(84);
        obs->on_default_sink_changed(42);

        bropulse::api::tickPulseAsync();

        auto r = evalScript(
            "(function() {\n"
            "  const evs = globalThis.__events;\n"
            "  if (!evs.sinkAdded || !evs.sinkAdded.some(e => e.id === 42 && e.name === 'TestSink' && e.description === 'Test Sink Description')) return false;\n"
            "  if (!evs.sinkUpdated || !evs.sinkUpdated.some(e => e.id === 42 && e.name === 'TestSink')) return false;\n"
            "  if (!evs.sinkRemoved || !evs.sinkRemoved.some(e => (e.sinkId === 42 || e.id === 42))) return false;\n"
            "  if (!evs.streamAdded || !evs.streamAdded.some(e => e.id === 84 && e.appId === 'org.test.App' && e.pid === 1234)) return false;\n"
            "  if (!evs.streamUpdated || !evs.streamUpdated.some(e => e.id === 84)) return false;\n"
            "  if (!evs.streamRemoved || !evs.streamRemoved.some(e => (e.streamId === 84 || e.id === 84))) return false;\n"
            "  if (!evs.defaultSinkChanged || !evs.defaultSinkChanged.some(e => (e.sinkId === 42 || e.id === 42))) return false;\n"
            "  return true;\n"
            "})()\n"
        );

        REQUIRE(!r.thrown);
        CHECK_TRUE(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  tickPulseAsync() and event dispatching [PASS]" << std::endl;
    }

    // 8. Test setAudioRouter / getAudioRouter
    std::cout << "Testing AudioRouter accessors..." << std::endl;
    {
        auto router = bropulse::api::getAudioRouter();
        REQUIRE(router != nullptr);
        CHECK_TRUE(router->is_running());

        // Create an explicit secondary router and set it
        auto router2 = bropulse::AudioRouter::create(bropulse::Backend::Auto);
        if (router2) {
            router2->start();
            bropulse::api::setAudioRouter(std::move(router2));
            auto current = bropulse::api::getAudioRouter();
            CHECK_TRUE(current != nullptr);
        }
        std::cout << "  AudioRouter accessors [PASS]" << std::endl;
    }

    // 9. Test real sink operations if any sinks exist
    std::cout << "Testing real sink volume/mute if sinks available..." << std::endl;
    {
        auto r = evalScript(
            "(function() {\n"
            "  const sinks = bro.pulse.getSinks();\n"
            "  if (sinks.length > 0) {\n"
            "    const s = sinks[0];\n"
            "    const origVol = s.volume;\n"
            "    const origMute = s.isMuted;\n"
            "    // Test setting volume and mute back to original\n"
            "    bro.pulse.setSinkVolume(s.id, origVol);\n"
            "    bro.pulse.setSinkMuted(s.id, origMute);\n"
            "  }\n"
            "  return true;\n"
            "})()\n"
        );
        CHECK_TRUE(!r.thrown && ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  real sink volume/mute [PASS]" << std::endl;
    }

    // 10. Test shutdownPulseAsync()
    std::cout << "Testing shutdownPulseAsync()..." << std::endl;
    bropulse::api::shutdownPulseAsync();
    std::cout << "  shutdownPulseAsync() [PASS]" << std::endl;

    return bstest::finish("test_api");
}
