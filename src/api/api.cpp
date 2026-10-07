#include "api.h"
#include "host_pulse_internal.h"
#include "bropulse/router.h"

#include <mutex>

namespace bropulse::api {

namespace {

std::mutex g_router_mu;
std::shared_ptr<AudioRouter> g_custom_audio_router;
std::shared_ptr<AudioRouter> g_default_audio_router;
AudioRouter* g_observed_router = nullptr;

void ensureObserverAttachedLocked(AudioRouter* router) {
    if (g_observed_router == router) return;
    if (g_observed_router) {
        g_observed_router->remove_observer(pulseObserver());
        g_observed_router = nullptr;
    }
    if (router) {
        router->add_observer(pulseObserver());
        g_observed_router = router;
    }
}

} // namespace

std::shared_ptr<AudioRouter> activeAudioRouter() {
    std::lock_guard lock(g_router_mu);
    if (g_custom_audio_router) {
        ensureObserverAttachedLocked(g_custom_audio_router.get());
        return g_custom_audio_router;
    }
    if (!g_default_audio_router) {
        g_default_audio_router = AudioRouter::create(Backend::Auto);
        if (g_default_audio_router) {
            g_default_audio_router->start();
        }
    }
    if (g_default_audio_router) {
        ensureObserverAttachedLocked(g_default_audio_router.get());
    }
    return g_default_audio_router;
}

void setAudioRouter(std::shared_ptr<AudioRouter> router) {
    std::lock_guard lock(g_router_mu);
    if (g_observed_router && g_observed_router != router.get()) {
        g_observed_router->remove_observer(pulseObserver());
        g_observed_router = nullptr;
    }
    g_custom_audio_router = std::move(router);
    if (g_custom_audio_router) {
        ensureObserverAttachedLocked(g_custom_audio_router.get());
    }
}

std::shared_ptr<AudioRouter> getAudioRouter() {
    return activeAudioRouter();
}

Value ensureBroPulse() {
    ev::Persistent globalThisVal;
    auto gt = ev::globalValue("globalThis");
    if (gt.found && ev::isObject(gt.value)) {
        globalThisVal.set(gt.value);
    }

    ev::Persistent broP;
    auto bro = ev::globalValue("bro");
    if (bro.found && ev::isObject(bro.value)) broP.set(bro.value);
    if (!ev::isObject(broP.get()) && ev::isObject(globalThisVal.get())) {
        Value candidate = ev::getProperty(globalThisVal.get(), "bro");
        if (ev::isObject(candidate)) broP.set(candidate);
    }
    if (!ev::isObject(broP.get())) {
        broP.set(ev::createObject());
        ev::registerGlobal("bro", broP.get());
        if (ev::isObject(globalThisVal.get())) {
            globalThisVal.set(ev::setProperty(globalThisVal.get(), "bro", broP.get()));
        }
    }

    ev::Persistent pulseP(ev::getProperty(broP.get(), "pulse"));
    if (!ev::isObject(pulseP.get())) {
        pulseP.set(ev::createObject());
        broP.set(ev::setProperty(broP.get(), "pulse", pulseP.get()));
    }
    return pulseP.get();
}

void installPulse() {
    ev::Persistent pulseObj(ensureBroPulse());
    installPulseOnto(pulseObj.get());
}

void tickPulseAsync() {
    activeAudioRouter();
    drainPulseEvents();
}

void shutdownPulseAsync() {
    {
        std::lock_guard lock(g_router_mu);
        if (g_observed_router) {
            g_observed_router->remove_observer(pulseObserver());
            g_observed_router = nullptr;
        }
        if (g_default_audio_router) {
            g_default_audio_router->stop();
            g_default_audio_router.reset();
        }
    }
    clearPulseSubscriptions();
    clearQueuedPulseEvents();
}

} // namespace bropulse::api
