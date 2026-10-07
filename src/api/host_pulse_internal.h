#pragma once

#include "embed/embed.h"
#include "bropulse/router.h"

#include <memory>
#include <string>

namespace bropulse::api {

namespace ev = bronze::embed;
using Value = bronze::Value;

Value ensureBroPulse();
void installPulseOnto(Value pulseObj);

void drainPulseEvents();
void clearPulseSubscriptions();
void clearQueuedPulseEvents();

void addPulseEventListener(const std::string& event, Value callback);
void removePulseEventListener(const std::string& event, Value callback);

bropulse::StreamObserver* pulseObserver();
std::shared_ptr<bropulse::AudioRouter> activeAudioRouter();

} // namespace bropulse::api
