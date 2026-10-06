#include <bropulse/router.h>
#include "src/pipewire/pw_router.h"
#include "src/pulse/pa_router.h"

#include <cstdlib>
#include <string>
#include <unistd.h>

namespace bropulse {

bool AudioRouter::is_pipewire_available() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir) {
        std::string path = std::string(runtime_dir) + "/pipewire-0";
        if (access(path.c_str(), R_OK | W_OK) == 0) {
            return true;
        }
    }
    const char* pw_remote = std::getenv("PIPEWIRE_REMOTE");
    if (pw_remote && access(pw_remote, R_OK | W_OK) == 0) {
        return true;
    }
    return false;
}

bool AudioRouter::is_pulseaudio_available() {
    const char* pulse_server = std::getenv("PULSE_SERVER");
    if (pulse_server) {
        return true;
    }
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir) {
        std::string path = std::string(runtime_dir) + "/pulse/native";
        if (access(path.c_str(), R_OK | W_OK) == 0) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<AudioRouter> AudioRouter::create(Backend backend) {
    if (backend == Backend::Auto) {
        if (is_pipewire_available()) {
            return std::make_unique<PipeWireRouter>();
        }
        if (is_pulseaudio_available()) {
            return std::make_unique<PulseRouter>();
        }
        // Default to PipeWire if auto detection fails to locate socket
        return std::make_unique<PipeWireRouter>();
    }

    if (backend == Backend::PipeWire) {
        return std::make_unique<PipeWireRouter>();
    }

    if (backend == Backend::PulseAudio) {
        return std::make_unique<PulseRouter>();
    }

    return nullptr;
}

} // namespace bropulse
