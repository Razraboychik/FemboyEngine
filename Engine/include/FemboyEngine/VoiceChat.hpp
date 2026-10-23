// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <SFML/System/Vector2.hpp>
#include "FemboyEngine/Core.hpp"
#include "FemboyEngine/FemObject.hpp"

namespace Fem
{

class Network;

// Everything a project or an editor persists for the voice service. Put the
// configuration in one value so a game does not have to mirror eight setters in
// its own settings struct just to survive a restart.
struct VoiceSettings
{
    bool enabled = false; // privacy default: capture is opt-in
    bool muted = false;
    bool pushToTalk = true;
    bool voiceActivation = false;
    float voiceActivationThreshold = 0.02f;
    float inputGain = 1.f;
    float outputVolume = 0.8f;
    std::string inputDevice; // empty = system default

    nlohmann::json ToJson() const;
    // Accepts a partial object: missing keys keep their current value. Values
    // are clamped to the ranges the public setters accept.
    bool FromJson(const nlohmann::json& data);
};

// Transport seam for a project that does not use Fem::Network. The engine's
// Network overload of Update() remains the zero-configuration path; setting a
// transport makes VoiceChat independent from the networking module.
struct VoiceTransport
{
    // Send one packet to a peer. On a client, peerId 0 is the server.
    std::function<bool(std::uint32_t peerId, const std::uint8_t* data, std::size_t size, bool reliable)> sendTo;
    // Server relay: send to everyone except exceptPeerId (0 = nobody).
    std::function<bool(const std::uint8_t* data, std::size_t size, std::uint32_t exceptPeerId, bool reliable)>
        broadcast;
    std::function<bool()> isServer;
    std::function<bool()> isConnected;

    bool Valid() const
    {
        return static_cast<bool>(sendTo) && static_cast<bool>(broadcast) && static_cast<bool>(isServer) &&
               static_cast<bool>(isConnected);
    }
};

// Low-latency microphone chat.
//
// Capture is handled by miniaudio because SFML has no input API. Packets are
// Opus-encoded 48 kHz mono, 20 ms frames, and the decoded mix is played through
// one SFML SoundStream so the existing SFML audio device remains the only output.
//
// The class is intentionally independent from the game layer: a project decides
// when to call setEnabled(), who owns push-to-talk and which peers are audible.
// All public entry points must be called from the main thread. The only method
// invoked from an audio thread is the internal capture callback, which writes to
// a lock-free ring.
class VoiceChat : public FemObject
{
public:
    FEMCLASS_NO_CREATE(VoiceChat, FemObject, false)
    static VoiceChat& Instance();


    struct Stats
    {
        bool available = false;
        bool enabled = false;
        bool capturing = false;
        bool transmitting = false;
        float inputLevel = 0.f; // 0..1, smoothed peak
        float jitterMs = 0.f;   // current playout delay estimate
        std::size_t peers = 0;  // remote talkers with a decoder
        std::uint64_t packetsSent = 0;
        std::uint64_t packetsReceived = 0;
        std::uint64_t packetsLost = 0;
        std::uint64_t packetsDropped = 0;
        // Local pipeline counters. They advance even without a Network, which
        // makes it possible to verify a real microphone end to end in the
        // `--voice-test` diagnostic without sending anything.
        std::uint64_t capturedFrames = 0;
        std::uint64_t encodedFrames = 0;
        // Device diagnostics. `activeInputDevice` is the endpoint miniaudio
        // actually opened; it is empty when capture is stopped or unavailable.
        std::size_t inputDevices = 0;
        std::string activeInputDevice;
        std::uint64_t deviceGeneration = 0;
        std::string lastError;
    };

    // Result of a blocking hardware capture test. The test opens the real
    // microphone, runs the production capture/encode path, and restores the
    // previous settings before returning.
    struct CaptureTestResult
    {
        bool passed = false;
        bool available = false;
        bool capturing = false;
        std::uint64_t capturedFrames = 0;
        std::uint64_t encodedFrames = 0;
        float peak = 0.f;
        std::string error;
    };

    bool Initialize();
    void Shutdown();
    bool Available() const;

    // Privacy default is off: capture only starts after setEnabled(true).
    bool Enabled() const;
    void SetEnabled(bool enabled);
    bool Muted() const;
    void SetMuted(bool muted);

    // Push-to-talk is the default. With it disabled the microphone transmits
    // continuously while enabled (subject to voiceActivation()).
    bool PushToTalk() const;
    void SetPushToTalk(bool enabled);
    void SetTalking(bool talking);
    bool Talking() const;

    // Optional gate for open microphones. Threshold is normalised RMS, 0..1.
    bool VoiceActivation() const;
    void SetVoiceActivation(bool enabled);
    float VoiceActivationThreshold() const;
    void SetVoiceActivationThreshold(float threshold);

    float InputGain() const;
    void SetInputGain(float gain);
    float OutputVolume() const;
    void SetOutputVolume(float volume);

    // Empty selects the system default. Changing it while enabled restarts the
    // capture device; a missing name falls back to the default.
    void SetInputDevice(const std::string& name);
    std::string InputDevice() const;
    std::vector<std::string> InputDevices() const;

    // Re-enumerates endpoints and reacts to hot-plug: a removed explicit device
    // falls back to the system default, and a changed default is reopened when
    // the player has not selected a device explicitly. Update() calls this every
    // two seconds; an editor may call it after a device-notification event.
    void RefreshDevices();
    std::string ActiveInputDevice() const;
    std::string LastError() const;
    std::uint64_t DeviceGeneration() const;

    // Apply/read the whole configuration in one call. Project settings screens
    // should use this instead of mirroring every setter.
    VoiceSettings GetSettings() const;
    void ApplySettings(const VoiceSettings& settings);
    bool LoadSettings(const std::string& path);
    bool SaveSettings(const std::string& path) const;

    // Optional transport. When set, it takes precedence over the Network*
    // argument of Update(). This is the seam for a custom socket layer or a
    // unit test that wants to inspect packets without starting ENet.
    void SetTransport(VoiceTransport transport);
    void ClearTransport();

    // Optional positional voice. Position is in world pixels; the current
    // AudioSystem listener position supplies attenuation and left/right pan.
    void SetPeerPosition(std::uint32_t peerId, sf::Vector2f position);
    void ClearPeerPosition(std::uint32_t peerId);
    void SetPeerVolume(std::uint32_t peerId, float volume);
    void SetPeerMuted(std::uint32_t peerId, bool muted);
    void RemovePeer(std::uint32_t peerId);

    // Call once per frame. `network` may be null for offline/editor preview;
    // received packets are still decoded and played.
    void Update(float dt, Network* network = nullptr);

    // Called by Network's binary receive callback. Safe to call during poll().
    void OnBinaryPacket(std::uint32_t peerId, const std::uint8_t* data, std::size_t size);

    Stats GetStats() const;

    // Exercises Opus encode/decode without touching an audio device. Used by the
    // test suite and by an editor's "test microphone" button.
    static bool CodecSelfTest(std::string* error = nullptr);

    // Opens the capture device for `seconds` and verifies that real samples
    // reached both the capture ring and the Opus encoder. Nothing is sent and
    // local playback is muted. Blocks the calling thread; intended for an editor
    // or a command-line diagnostic, never for a gameplay frame.
    CaptureTestResult CaptureSelfTest(float seconds, const std::string& device = {});

private:
    VoiceChat();
    ~VoiceChat();
    VoiceChat(const VoiceChat&) = delete;
    VoiceChat& operator=(const VoiceChat&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Fem
