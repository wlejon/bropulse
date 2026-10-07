#pragma once

#include "bropulse/router.h"

#include <memory>

namespace bropulse::api {

/// Mounts `bro.pulse` in the current Bronze realm.
void installPulse();

/// Pumps async audio router events and dispatches callbacks to JS.
void tickPulseAsync();

/// Cleans up observer registrations and pending events.
void shutdownPulseAsync();

/// Sets the AudioRouter used by the API (defaults to AudioRouter::create(Backend::Auto)).
void setAudioRouter(std::shared_ptr<bropulse::AudioRouter> router);

/// Gets the AudioRouter currently used by the API.
std::shared_ptr<bropulse::AudioRouter> getAudioRouter();

/// Gets the internal StreamObserver used to capture audio router events.
bropulse::StreamObserver* pulseObserver();

} // namespace bropulse::api

using bropulse::api::installPulse;
using bropulse::api::tickPulseAsync;
using bropulse::api::shutdownPulseAsync;
using bropulse::api::setAudioRouter;
using bropulse::api::getAudioRouter;
using bropulse::api::pulseObserver;

