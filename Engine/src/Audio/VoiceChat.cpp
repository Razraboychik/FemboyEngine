// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/VoiceChat.hpp"
#include "FemboyEngine/Boy.hpp"

#ifdef FEMBOY_HAS_VOICE

#include "FemboyEngine/Audio.hpp"
#include "FemboyEngine/AudioMixer.hpp"
#include "FemboyEngine/Networking.hpp"
#include "FemboyEngine/Vanity.hpp"

#include <SFML/Audio.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <opus.h>

// miniaudio is a single-header library. It is compiled here, in exactly one
// translation unit, and only the capture device is opened: playback remains on
// the SFML audio device that the rest of the engine already uses.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace Fem
{
namespace
{

constexpr std::uint32_t kSampleRate = 48000;
constexpr std::size_t kFrameSamples = kSampleRate / 50; // 20 ms
constexpr std::size_t kMaxPacket = 1024;
constexpr std::size_t kMaxPeers = 16;
constexpr std::size_t kSlotCount = 64;
constexpr std::size_t kCaptureCapacity = kSampleRate;     // 1 second
constexpr std::size_t kPcmCapacity = kSampleRate * 2 * 2; // 2 seconds stereo
constexpr std::size_t kHeaderSize = 14;
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kFlagVoice = 1;
// After a real silence (push-to-talk release, a disconnected burst) the
// receiver must treat the next packet as a new timeline. Otherwise it keeps
// advancing nextSequence with PLC while nothing arrives and rejects the first
// packet after the pause as "late".
constexpr std::uint64_t kPeerSilenceResetMs = 250;

// Interleaved int16 producer/consumer ring. One writer and one reader only:
// capture is written by miniaudio's callback and read by Update(); the decoded
// mix is written by Update() and read by SFML's stream thread.
template <std::size_t Capacity> class SpscRing
{
public:
    std::size_t Push(const std::int16_t* samples, std::size_t count)
    {
        const std::size_t write = write_.load(std::memory_order_relaxed);
        const std::size_t read = read_.load(std::memory_order_acquire);
        const std::size_t used = (write + Capacity - read) % Capacity;
        const std::size_t freeSlots = Capacity - used;
        const std::size_t accepted = std::min(count, freeSlots);
        for (std::size_t i = 0; i < accepted; ++i)
        {
            data_[(write + i) % Capacity] = samples[i];
        }
        write_.store((write + accepted) % Capacity, std::memory_order_release);
        return accepted;
    }

    std::size_t Pop(std::int16_t* samples, std::size_t count)
    {
        const std::size_t read = read_.load(std::memory_order_relaxed);
        const std::size_t write = write_.load(std::memory_order_acquire);
        const std::size_t available = (write + Capacity - read) % Capacity;
        const std::size_t taken = std::min(count, available);
        for (std::size_t i = 0; i < taken; ++i)
        {
            samples[i] = data_[(read + i) % Capacity];
        }
        read_.store((read + taken) % Capacity, std::memory_order_release);
        return taken;
    }

    std::size_t Available() const
    {
        const std::size_t read = read_.load(std::memory_order_acquire);
        const std::size_t write = write_.load(std::memory_order_acquire);
        return (write + Capacity - read) % Capacity;
    }

    void Reset()
    {
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
    }

private:
    std::array<std::int16_t, Capacity> data_{};
    std::atomic<std::size_t> read_{0};
    std::atomic<std::size_t> write_{0};
};

class VoiceStream final : public sf::SoundStream
{
public:
    explicit VoiceStream(SpscRing<kPcmCapacity>& ring) : ring_(ring)
    {
        initialize(2, kSampleRate, {sf::SoundChannel::FrontLeft, sf::SoundChannel::FrontRight});
        setVolume(100.f);
    }

protected:
    bool onGetData(Chunk& data) override
    {
        const std::size_t got = ring_.Pop(buffer_.data(), kFrameSamples * 2);
        if (got < buffer_.size())
        {
            std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(got), buffer_.end(), std::int16_t{0});
        }
        // Always return a full chunk: returning zero samples makes SFML stop the
        // stream, and an underrun should be a short silence, not a dead voice.
        data.samples = buffer_.data();
        data.sampleCount = buffer_.size();
        return true;
    }

    void onSeek(sf::Time) override
    {
        // The ring is reset from the main thread while the stream is stopped.
        // Touching read/write pointers from this thread would violate the SPSC
        // contract, so seeking intentionally does nothing.
    }

private:
    SpscRing<kPcmCapacity>& ring_;
    std::array<std::int16_t, kFrameSamples * 2> buffer_{};
};

inline std::int16_t SeqDelta(std::uint16_t a, std::uint16_t b)
{
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(a - b));
}

inline void WriteU16(std::uint8_t* out, std::uint16_t value)
{
    out[0] = static_cast<std::uint8_t>(value & 0xffu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
}

inline std::uint16_t ReadU16(const std::uint8_t* in)
{
    return static_cast<std::uint16_t>(in[0] | (static_cast<std::uint16_t>(in[1]) << 8));
}

inline void WriteU32(std::uint8_t* out, std::uint32_t value)
{
    out[0] = static_cast<std::uint8_t>(value & 0xffu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    out[2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    out[3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

inline std::uint32_t ReadU32(const std::uint8_t* in)
{
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) |
           (static_cast<std::uint32_t>(in[2]) << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
}

inline bool HasMagic(const std::uint8_t* in)
{
    return in[0] == 'F' && in[1] == 'B' && in[2] == 'V' && in[3] == '1';
}

struct OpusDecoderDeleter
{
    void operator()(OpusDecoder* decoder) const
    {
        if (decoder)
        {
            opus_decoder_destroy(decoder);
        }
    }
};
using DecoderPtr = std::unique_ptr<OpusDecoder, OpusDecoderDeleter>;

} // namespace

struct VoiceChat::Impl
{
    struct Slot
    {
        bool used = false;
        std::uint16_t sequence = 0;
        std::uint16_t size = 0;
        std::uint32_t timestamp = 0;
        std::array<std::uint8_t, kMaxPacket> data{};
    };

    struct Peer
    {
        std::uint32_t id = 0;
        DecoderPtr decoder;
        std::array<Slot, kSlotCount> slots{};
        std::uint16_t nextSequence = 0;
        bool gotFirst = false;
        bool started = false;
        std::uint64_t firstPacketMs = 0;
        std::uint64_t lastPacketMs = 0;
        double playoutMs = 0.0;
        float volume = 1.f;
        bool muted = false;
        bool hasPosition = false;
        sf::Vector2f position{};

        std::size_t Buffered() const
        {
            std::size_t count = 0;
            for (const Slot& slot : slots)
            {
                count += slot.used ? 1u : 0u;
            }
            return count;
        }
    };

    bool initialized = false;
    bool available = false;
    bool enabled = false;
    bool muted = false;
    bool pushToTalk = true;
    bool talking = false;
    bool voiceActivation = false;
    float activationThreshold = 0.02f;
    float inputGain = 1.f;
    float outputVolume = 1.f;
    float inputLevel = 0.f;
    std::string inputDevice;
    std::vector<std::string> inputDevices;
    std::string defaultDeviceName;
    std::string activeDeviceName;
    std::string lastError;
    std::uint64_t deviceGeneration = 0;
    double devicePollMs = 0.0;
    VoiceTransport transport;
    bool hasTransport = false;
    std::vector<Peer> peers;

    OpusEncoder* encoder = nullptr;
    ma_context context{};
    ma_device device{};
    bool contextReady = false;
    bool deviceReady = false;
    bool deviceStarted = false;
    bool streamStarted = false;
    bool streamInitialized = false;

    SpscRing<kCaptureCapacity> capture;
    SpscRing<kPcmCapacity> pcm;
    std::unique_ptr<VoiceStream> stream;

    std::array<std::int16_t, kFrameSamples> frame{};
    std::array<std::int16_t, kFrameSamples> encodeFrame{};
    std::array<std::int16_t, kFrameSamples> decoded{};
    std::array<std::int32_t, kFrameSamples * 2> mix{};

    std::uint16_t sequence = 0;
    std::uint32_t timestamp = 0;
    double mixClockMs = 0.0;
    std::uint64_t packetsSent = 0;
    std::uint64_t packetsReceived = 0;
    std::uint64_t packetsLost = 0;
    std::uint64_t packetsDropped = 0;
    std::uint64_t capturedFrames = 0;
    std::uint64_t encodedFrames = 0;
    std::uint64_t lastSendMs = 0;
    Network* network = nullptr;
    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();

    std::uint64_t NowMs() const
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt)
                .count());
    }

    static void ResetPeerTimeline(Peer& peer)
    {
        for (Slot& slot : peer.slots)
        {
            slot.used = false;
        }
        peer.gotFirst = false;
        peer.started = false;
        peer.nextSequence = 0;
        peer.playoutMs = 0.0;
        if (peer.decoder)
        {
            opus_decoder_ctl(peer.decoder.get(), OPUS_RESET_STATE);
        }
    }

    Peer* FindPeer(std::uint32_t peerId)
    {
        for (Peer& peer : peers)
        {
            if (peer.id == peerId)
            {
                return &peer;
            }
        }
        return nullptr;
    }

    Peer* EnsurePeer(std::uint32_t peerId)
    {
        if (Peer* existing = FindPeer(peerId))
        {
            return existing;
        }
        if (peers.size() >= kMaxPeers)
        {
            // Evict a silent participant first. Settings for a genuinely active
            // peer survive; an idle slot is what a new talker needs.
            auto victim = std::max_element(peers.begin(), peers.end(), [](const Peer& a, const Peer& b)
                                           { return a.lastPacketMs < b.lastPacketMs; });
            if (victim != peers.end())
            {
                peers.erase(victim);
            }
        }
        if (peers.size() >= kMaxPeers)
        {
            return nullptr;
        }

        int error = OPUS_OK;
        DecoderPtr decoder(opus_decoder_create(static_cast<opus_int32>(kSampleRate), 1, &error));
        if (!decoder || error != OPUS_OK)
        {
            BOY_WARN("VoiceChat", std::string("Opus decoder create failed: ") + opus_strerror(error));
            return nullptr;
        }

        Peer peer;
        peer.id = peerId;
        peer.decoder = std::move(decoder);
        peers.push_back(std::move(peer));
        return &peers.back();
    }

    bool ShouldTransmit() const
    {
        if (!enabled || muted || !encoder)
        {
            return false;
        }
        if (pushToTalk)
        {
            return talking;
        }
        if (!voiceActivation)
        {
            return true;
        }
        return inputLevel >= activationThreshold;
    }

    void OnCapture(const void* input, ma_uint32 frameCount)
    {
        if (!input || frameCount == 0)
        {
            return;
        }
        const auto* samples = static_cast<const std::int16_t*>(input);
        capture.Push(samples, static_cast<std::size_t>(frameCount));
    }

    bool TransportIsServer() const
    {
        return hasTransport && transport.isServer && transport.isServer();
    }

    bool TransportIsConnected() const
    {
        return hasTransport && transport.isConnected && transport.isConnected();
    }

    bool SendVoicePacket(const std::uint8_t* data, std::size_t size)
    {
        if (hasTransport && transport.Valid())
        {
            if (TransportIsServer())
            {
                return transport.broadcast(data, size, 0, false);
            }
            if (!TransportIsConnected())
            {
                return false;
            }
            return transport.sendTo(0, data, size, false);
        }

        if (!network || (!network->IsServer() && !network->IsConnected()))
        {
            return false;
        }
        if (network->IsServer())
        {
            return network->BroadcastBinary(Network::kVoiceChannel, data, size, 0);
        }
        return network->SendBinaryTo(0, Network::kVoiceChannel, data, size, false);
    }

    void RelayVoicePacket(const std::uint8_t* data, std::size_t size, std::uint32_t sender)
    {
        if (hasTransport && transport.broadcast && TransportIsServer())
        {
            transport.broadcast(data, size, sender, false);
            return;
        }
        if (network && network->IsServer())
        {
            network->BroadcastBinary(Network::kVoiceChannel, data, size, sender);
        }
    }

    void DrainCapture()
    {
        while (capture.Available() >= kFrameSamples)
        {
            capture.Pop(frame.data(), kFrameSamples);
            capturedFrames += kFrameSamples;

            std::int64_t sum = 0;
            for (std::int16_t sample : frame)
            {
                sum += static_cast<std::int64_t>(sample) * sample;
            }
            const float rms = std::sqrt(static_cast<float>(sum) / static_cast<float>(kFrameSamples)) / 32768.f;
            const float scaled = std::clamp(rms * inputGain, 0.f, 1.f);
            // Fast attack, slow release: the meter responds to speech without
            // flickering on every syllable.
            inputLevel = scaled > inputLevel ? scaled : inputLevel * 0.82f + scaled * 0.18f;

            if (!ShouldTransmit())
            {
                // The sequence only advances for packets that actually exist.
                // Timestamps still describe the captured sample stream so a
                // receiver can reason about gaps.
                timestamp += static_cast<std::uint32_t>(kFrameSamples);
                continue;
            }

            // Input gain belongs to the transmitted signal, not just the level
            // meter. Clamp after amplification so a hot microphone cannot wrap
            // around (which would sound like loud distortion, not gain).
            for (std::size_t i = 0; i < frame.size(); ++i)
            {
                const long amplified = std::lround(static_cast<float>(frame[i]) * inputGain);
                encodeFrame[i] = static_cast<std::int16_t>(std::clamp(amplified, -32768L, 32767L));
            }

            std::array<std::uint8_t, kMaxPacket> packet{};
            const int encoded =
                opus_encode(encoder, encodeFrame.data(), static_cast<opus_int32>(kFrameSamples),
                            packet.data() + kHeaderSize, static_cast<opus_int32>(kMaxPacket - kHeaderSize));
            if (encoded <= 0)
            {
                BOY_WARN("VoiceChat", std::string("Opus encode failed: ") + opus_strerror(encoded));
                timestamp += static_cast<std::uint32_t>(kFrameSamples);
                continue;
            }

            ++encodedFrames;
            packet[0] = 'F';
            packet[1] = 'B';
            packet[2] = 'V';
            packet[3] = '1';
            packet[4] = kVersion;
            packet[5] = kFlagVoice;
            WriteU16(packet.data() + 6, sequence);
            WriteU32(packet.data() + 8, timestamp);
            WriteU16(packet.data() + 12, static_cast<std::uint16_t>(encoded));

            const std::size_t total = kHeaderSize + static_cast<std::size_t>(encoded);
            if (SendVoicePacket(packet.data(), total))
            {
                ++packetsSent;
                lastSendMs = NowMs();
            }

            ++sequence;
            timestamp += static_cast<std::uint32_t>(kFrameSamples);
        }
    }

    void ApplyPan(const Peer& peer, float& left, float& right) const
    {
        // The stream volume already carries outputVolume() and the "voice"
        // mixer group. Applying outputVolume here as well would square it.
        float gain = peer.volume;
        float pan = 0.f;
        if (peer.hasPosition)
        {
            const sf::Vector2f listener = AudioSystem::Instance().ListenerPosition();
            const float dx = peer.position.x - listener.x;
            const float dy = peer.position.y - listener.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            // Voice is meant to stay intelligible at gameplay distances; this
            // is a soft falloff, not the aggressive rolloff used for gunshots.
            gain *= std::clamp(1.f - distance / 1800.f, 0.f, 1.f);
            pan = std::clamp(dx / 700.f, -1.f, 1.f);
        }
        if (peer.muted)
        {
            gain = 0.f;
        }
        // Linear pan keeps a centred speaker at full level on both channels,
        // which is what players expect from a non-positional voice chat.
        left = gain * (pan <= 0.f ? 1.f : 1.f - pan);
        right = gain * (pan >= 0.f ? 1.f : 1.f + pan);
    }

    bool DecodeNext(Peer& peer)
    {
        Slot& slot = peer.slots[peer.nextSequence % kSlotCount];
        bool plc = false;
        if (!slot.used || slot.sequence != peer.nextSequence)
        {
            // Missing packet: ask Opus for packet-loss concealment. This keeps
            // the timeline moving instead of letting the jitter buffer stall.
            plc = true;
            ++packetsLost;
        }

        int decodedSamples = 0;
        if (plc)
        {
            decodedSamples =
                opus_decode(peer.decoder.get(), nullptr, 0, decoded.data(), static_cast<opus_int32>(kFrameSamples), 0);
        }
        else
        {
            decodedSamples = opus_decode(peer.decoder.get(), slot.data.data(), static_cast<opus_int32>(slot.size),
                                         decoded.data(), static_cast<opus_int32>(kFrameSamples), 0);
            slot.used = false;
        }

        ++peer.nextSequence;
        if (decodedSamples <= 0)
        {
            std::fill(decoded.begin(), decoded.end(), std::int16_t{0});
            return false;
        }
        if (decodedSamples < static_cast<int>(kFrameSamples))
        {
            std::fill(decoded.begin() + decodedSamples, decoded.end(), std::int16_t{0});
        }
        return true;
    }

    void MixOneFrame()
    {
        std::fill(mix.begin(), mix.end(), 0);
        bool any = false;

        for (Peer& peer : peers)
        {
            if (!peer.started)
            {
                continue;
            }

            float left = 1.f;
            float right = 1.f;
            ApplyPan(peer, left, right);
            if (left <= 0.f && right <= 0.f)
            {
                // Still advance the decoder so a muted peer does not build an
                // unbounded backlog that becomes a burst after unmuting.
                DecodeNext(peer);
                any = true;
                continue;
            }

            if (!DecodeNext(peer))
            {
                continue;
            }
            any = true;
            for (std::size_t i = 0; i < kFrameSamples; ++i)
            {
                mix[i * 2] += static_cast<std::int32_t>(static_cast<float>(decoded[i]) * left);
                mix[i * 2 + 1] += static_cast<std::int32_t>(static_cast<float>(decoded[i]) * right);
            }
        }

        if (!any)
        {
            return;
        }

        std::array<std::int16_t, kFrameSamples * 2> output{};
        for (std::size_t i = 0; i < output.size(); ++i)
        {
            output[i] = static_cast<std::int16_t>(std::clamp(mix[i], -32768, 32767));
        }
        if (pcm.Push(output.data(), output.size()) < output.size())
        {
            ++packetsDropped;
        }
    }

    void UpdatePeers(double dtMs)
    {
        const std::uint64_t now = NowMs();
        for (Peer& peer : peers)
        {
            // A long gap is a new utterance, not a stream that should keep
            // consuming PLC frames forever. Clearing the timeline also stops
            // nextSequence from drifting past the sender's real sequence while
            // push-to-talk is released.
            if ((peer.started || peer.gotFirst) && peer.lastPacketMs != 0 &&
                now - peer.lastPacketMs > kPeerSilenceResetMs)
            {
                ResetPeerTimeline(peer);
            }

            // Drop frames for which the playout cursor has already moved on.
            // A late packet is no longer useful, and leaving it in a slot makes
            // the buffer count (and the jitter estimate) lie.
            for (Slot& slot : peer.slots)
            {
                if (slot.used && peer.gotFirst && SeqDelta(slot.sequence, peer.nextSequence) < 0)
                {
                    slot.used = false;
                }
            }

            if (!peer.started && peer.gotFirst && peer.Buffered() >= 2)
            {
                peer.started = true;
            }
            if (!peer.started && peer.gotFirst && now - peer.firstPacketMs >= 60)
            {
                peer.started = true;
            }

            if (peer.started)
            {
                peer.playoutMs += dtMs;
                // The global mixer below advances in exact 20 ms ticks. The
                // per-peer accumulator is kept for diagnostics and for callers
                // that inspect jitter through stats().
                peer.playoutMs = std::min(peer.playoutMs, 100.0);
            }
        }
    }

    void EraseIdlePeers()
    {
        const std::uint64_t now = NowMs();
        peers.erase(std::remove_if(peers.begin(), peers.end(), [now](const Peer& peer)
                                   { return peer.lastPacketMs != 0 && now - peer.lastPacketMs > 15000; }),
                    peers.end());
    }

    void StopDevice()
    {
        if (deviceStarted)
        {
            ma_device_stop(&device);
            deviceStarted = false;
        }
        if (deviceReady)
        {
            ma_device_uninit(&device);
            deviceReady = false;
        }
        activeDeviceName.clear();
        capture.Reset();
    }

    void RememberActiveDevice()
    {
        char name[256]{};
        size_t length = 0;
        if (ma_device_get_name(&device, ma_device_type_capture, name, sizeof(name), &length) == MA_SUCCESS)
        {
            activeDeviceName = name;
        }
        else
        {
            activeDeviceName.clear();
        }
    }

    bool StartDevice()
    {
        if (!contextReady)
        {
            lastError = "audio context is not available";
            return false;
        }
        if (deviceReady)
        {
            if (!deviceStarted)
            {
                const ma_result result = ma_device_start(&device);
                if (result != MA_SUCCESS)
                {
                    lastError = std::string("microphone could not start: ") + ma_result_description(result);
                    return false;
                }
                deviceStarted = true;
                RememberActiveDevice();
            }
            return deviceStarted;
        }

        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.format = ma_format_s16;
        config.capture.channels = 1;
        config.sampleRate = kSampleRate;
        config.periodSizeInFrames = static_cast<ma_uint32>(kFrameSamples);
        config.periods = 2;
        config.dataCallback = [](ma_device* device, void*, const void* input, ma_uint32 frameCount)
        {
            auto* self = static_cast<Impl*>(device->pUserData);
            if (self)
            {
                self->OnCapture(input, frameCount);
            }
        };
        config.pUserData = this;

        ma_device_id selectedId{};
        bool hasSelected = false;
        if (!inputDevice.empty() && !inputDevices.empty())
        {
            ma_device_info* playbackInfos = nullptr;
            ma_uint32 playbackCount = 0;
            ma_device_info* captureInfos = nullptr;
            ma_uint32 captureCount = 0;
            if (ma_context_get_devices(&context, &playbackInfos, &playbackCount, &captureInfos, &captureCount) ==
                MA_SUCCESS)
            {
                for (ma_uint32 i = 0; i < captureCount; ++i)
                {
                    if (inputDevice == captureInfos[i].name)
                    {
                        selectedId = captureInfos[i].id;
                        hasSelected = true;
                        break;
                    }
                }
            }
        }
        const bool selectedMissing = !inputDevice.empty() && !hasSelected;
        if (hasSelected)
        {
            config.capture.pDeviceID = &selectedId;
        }

        const ma_result initResult = ma_device_init(&context, &config, &device);
        if (initResult != MA_SUCCESS)
        {
            lastError = std::string("could not open the microphone: ") + ma_result_description(initResult);
            BOY_WARN("VoiceChat", lastError + "; receive-only mode");
            return false;
        }
        deviceReady = true;
        const ma_result startResult = ma_device_start(&device);
        if (startResult != MA_SUCCESS)
        {
            lastError = std::string("microphone opened but could not start: ") + ma_result_description(startResult);
            BOY_WARN("VoiceChat", lastError);
            ma_device_uninit(&device);
            deviceReady = false;
            return false;
        }
        deviceStarted = true;
        lastError = selectedMissing ? "selected microphone is unavailable; using the system default" : "";
        RememberActiveDevice();
        return true;
    }

    void EnumerateDevices()
    {
        const std::vector<std::string> previousDevices = inputDevices;
        const std::string previousDefault = defaultDeviceName;
        inputDevices.clear();
        defaultDeviceName.clear();
        if (!contextReady)
        {
            return;
        }
        ma_device_info* playbackInfos = nullptr;
        ma_uint32 playbackCount = 0;
        ma_device_info* captureInfos = nullptr;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context, &playbackInfos, &playbackCount, &captureInfos, &captureCount) !=
            MA_SUCCESS)
        {
            return;
        }
        inputDevices.reserve(captureCount);
        for (ma_uint32 i = 0; i < captureCount; ++i)
        {
            inputDevices.emplace_back(captureInfos[i].name);
            if (captureInfos[i].isDefault)
            {
                defaultDeviceName = captureInfos[i].name;
            }
        }
        if (previousDevices != inputDevices || previousDefault != defaultDeviceName)
        {
            ++deviceGeneration;
            if (!activeDeviceName.empty())
            {
                const bool activeStillPresent =
                    std::find(inputDevices.begin(), inputDevices.end(), activeDeviceName) != inputDevices.end();
                if (!activeStillPresent)
                {
                    lastError = "active microphone was disconnected";
                }
            }
        }
    }

    void ResetVoiceState()
    {
        peers.clear();
        pcm.Reset();
        mixClockMs = 0.0;
        inputLevel = 0.f;
        // Keep sequence/timestamp monotonic across enable/disable. A receiver
        // still holds its old timeline for a moment; rewinding to zero would
        // make the first new packet look stale.
    }

    void RefreshDeviceState()
    {
        if (!contextReady)
        {
            return;
        }
        const std::string previousActive = activeDeviceName;
        const std::string previousDefault = defaultDeviceName;
        EnumerateDevices();

        bool explicitMissing = !inputDevice.empty() &&
                               std::find(inputDevices.begin(), inputDevices.end(), inputDevice) == inputDevices.end();
        // startDevice() already fell back to the default when the saved device
        // was absent. Do not restart capture every poll while that is still true.
        if (explicitMissing && !defaultDeviceName.empty() && activeDeviceName == defaultDeviceName)
        {
            explicitMissing = false;
        }
        const bool activeLost = !previousActive.empty() && std::find(inputDevices.begin(), inputDevices.end(),
                                                                     previousActive) == inputDevices.end();
        const bool defaultChanged =
            inputDevice.empty() && !previousDefault.empty() && previousDefault != defaultDeviceName;

        if (!enabled || !deviceStarted)
        {
            return;
        }
        if (explicitMissing || activeLost || defaultChanged)
        {
            // Reopening resets the capture ring, which is preferable to silently
            // recording from a headset the player just unplugged.
            StopDevice();
            StartDevice();
        }
    }
};

nlohmann::json VoiceSettings::ToJson() const
{
    return nlohmann::json{
        {"enabled", enabled},
        {"muted", muted},
        {"pushToTalk", pushToTalk},
        {"voiceActivation", voiceActivation},
        {"voiceActivationThreshold", voiceActivationThreshold},
        {"inputGain", inputGain},
        {"outputVolume", outputVolume},
        {"inputDevice", inputDevice},
    };
}

bool VoiceSettings::FromJson(const nlohmann::json& data)
{
    if (!data.is_object())
    {
        return false;
    }

    const auto boolean = [&data](const char* key, bool& out)
    {
        if (data.contains(key) && data[key].is_boolean())
        {
            out = data[key].get<bool>();
        }
    };
    const auto number = [&data](const char* key, float& out)
    {
        if (data.contains(key) && data[key].is_number())
        {
            out = data[key].get<float>();
        }
    };
    const auto string = [&data](const char* key, std::string& out)
    {
        if (data.contains(key) && data[key].is_string())
        {
            out = data[key].get<std::string>();
        }
    };

    boolean("enabled", enabled);
    boolean("muted", muted);
    boolean("pushToTalk", pushToTalk);
    boolean("voiceActivation", voiceActivation);
    number("voiceActivationThreshold", voiceActivationThreshold);
    number("inputGain", inputGain);
    number("outputVolume", outputVolume);
    string("inputDevice", inputDevice);

    voiceActivationThreshold = std::clamp(voiceActivationThreshold, 0.f, 1.f);
    inputGain = std::clamp(inputGain, 0.f, 4.f);
    outputVolume = std::clamp(outputVolume, 0.f, 1.f);
    return true;
}

VoiceChat& VoiceChat::Instance()
{
    static VoiceChat chat;
    return chat;
}

VoiceChat::VoiceChat() : impl_(std::make_unique<Impl>())
{
}

VoiceChat::~VoiceChat()
{
    Shutdown();
}

bool VoiceChat::Initialize()
{
    if (impl_->initialized)
    {
        return true;
    }

    impl_->startedAt = std::chrono::steady_clock::now();
    impl_->lastError.clear();

    int error = OPUS_OK;
    impl_->encoder = opus_encoder_create(static_cast<opus_int32>(kSampleRate), 1, OPUS_APPLICATION_VOIP, &error);
    if (!impl_->encoder || error != OPUS_OK)
    {
        impl_->lastError = std::string("Opus encoder create failed: ") + opus_strerror(error);
        BOY_WARN("VoiceChat", impl_->lastError);
        if (impl_->encoder)
        {
            opus_encoder_destroy(impl_->encoder);
            impl_->encoder = nullptr;
        }
    }
    else
    {
        opus_encoder_ctl(impl_->encoder, OPUS_SET_BITRATE(32000));
        opus_encoder_ctl(impl_->encoder, OPUS_SET_COMPLEXITY(5));
        opus_encoder_ctl(impl_->encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(impl_->encoder, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(impl_->encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    }

    // Create SFML's playback stream before our own miniaudio capture context.
    // Static SFML embeds miniaudio too; initializing the capture context first
    // makes SFML's audio device fail with MA_INVALID_ARGS in a release build.
    // The stream only reads the lock-free PCM ring, so order is otherwise free.
    impl_->stream = std::make_unique<VoiceStream>(impl_->pcm);
    impl_->streamInitialized = true;
    impl_->stream->setVolume(100.f);

    if (ma_context_init(nullptr, 0, nullptr, &impl_->context) == MA_SUCCESS)
    {
        impl_->contextReady = true;
        impl_->EnumerateDevices();
    }
    else
    {
        impl_->lastError = "miniaudio context init failed; no microphone will be available";
        BOY_WARN("VoiceChat", impl_->lastError);
    }

    impl_->available = impl_->encoder != nullptr;
    impl_->initialized = true;

    if (impl_->available)
    {
        BOY_INFO("VoiceChat", "voice chat ready (Opus 48 kHz mono, " + std::to_string(impl_->inputDevices.size()) +
                                  " input device(s))");
    }
    else
    {
        BOY_WARN("VoiceChat", "voice chat initialized without a codec; API remains a no-op");
    }
    return true;
}

void VoiceChat::Shutdown()
{
    if (!impl_)
    {
        return;
    }
    impl_->enabled = false;
    if (impl_->stream)
    {
        impl_->stream->stop();
    }
    impl_->streamStarted = false;
    impl_->StopDevice();
    if (impl_->contextReady)
    {
        ma_context_uninit(&impl_->context);
        impl_->contextReady = false;
    }
    impl_->peers.clear();
    impl_->pcm.Reset();
    impl_->capture.Reset();
    impl_->inputDevices.clear();
    impl_->defaultDeviceName.clear();
    impl_->activeDeviceName.clear();
    if (impl_->encoder)
    {
        opus_encoder_destroy(impl_->encoder);
        impl_->encoder = nullptr;
    }
    impl_->stream.reset();
    impl_->streamInitialized = false;
    impl_->initialized = false;
    impl_->available = false;
}

bool VoiceChat::Available() const
{
    return impl_ && impl_->available;
}

bool VoiceChat::Enabled() const
{
    return impl_ && impl_->enabled;
}

void VoiceChat::SetEnabled(bool enabled)
{
    if (!impl_)
    {
        return;
    }
    if (enabled == impl_->enabled)
    {
        return;
    }
    if (enabled && !impl_->initialized)
    {
        Initialize();
    }
    if (enabled && !impl_->available)
    {
        impl_->enabled = false;
        impl_->lastError = "voice codec is not available";
        BOY_WARN("VoiceChat", "voice chat cannot be enabled: " + impl_->lastError);
        return;
    }
    impl_->enabled = enabled;
    if (enabled)
    {
        impl_->StartDevice();
        if (impl_->stream && !impl_->streamStarted)
        {
            impl_->stream->play();
            impl_->streamStarted = true;
        }
        BOY_INFO("VoiceChat", "voice chat enabled");
    }
    else
    {
        if (impl_->stream)
        {
            impl_->stream->stop();
        }
        impl_->streamStarted = false;
        impl_->StopDevice();
        impl_->ResetVoiceState();
        BOY_INFO("VoiceChat", "voice chat disabled");
    }
}

bool VoiceChat::Muted() const
{
    return impl_ && impl_->muted;
}

void VoiceChat::SetMuted(bool muted)
{
    if (impl_)
    {
        impl_->muted = muted;
    }
}

bool VoiceChat::PushToTalk() const
{
    return impl_ && impl_->pushToTalk;
}

void VoiceChat::SetPushToTalk(bool enabled)
{
    if (impl_)
    {
        impl_->pushToTalk = enabled;
    }
}

void VoiceChat::SetTalking(bool talking)
{
    if (impl_)
    {
        impl_->talking = talking;
    }
}

bool VoiceChat::Talking() const
{
    return impl_ && impl_->talking;
}

bool VoiceChat::VoiceActivation() const
{
    return impl_ && impl_->voiceActivation;
}

void VoiceChat::SetVoiceActivation(bool enabled)
{
    if (impl_)
    {
        impl_->voiceActivation = enabled;
    }
}

float VoiceChat::VoiceActivationThreshold() const
{
    return impl_ ? impl_->activationThreshold : 0.f;
}

void VoiceChat::SetVoiceActivationThreshold(float threshold)
{
    if (impl_)
    {
        impl_->activationThreshold = std::clamp(threshold, 0.f, 1.f);
    }
}

float VoiceChat::InputGain() const
{
    return impl_ ? impl_->inputGain : 1.f;
}

void VoiceChat::SetInputGain(float gain)
{
    if (impl_)
    {
        impl_->inputGain = std::clamp(gain, 0.f, 4.f);
    }
}

float VoiceChat::OutputVolume() const
{
    return impl_ ? impl_->outputVolume : 1.f;
}

void VoiceChat::SetOutputVolume(float volume)
{
    if (impl_)
    {
        impl_->outputVolume = std::clamp(volume, 0.f, 1.f);
    }
}

void VoiceChat::SetInputDevice(const std::string& name)
{
    if (!impl_ || impl_->inputDevice == name)
    {
        return;
    }
    impl_->inputDevice = name;
    if (impl_->enabled && impl_->contextReady)
    {
        impl_->StopDevice();
        impl_->StartDevice();
    }
}

std::string VoiceChat::InputDevice() const
{
    return impl_ ? impl_->inputDevice : std::string{};
}

std::vector<std::string> VoiceChat::InputDevices() const
{
    if (!impl_)
    {
        return {};
    }
    if (impl_->inputDevices.empty() && impl_->contextReady)
    {
        impl_->EnumerateDevices();
    }
    return impl_->inputDevices;
}

void VoiceChat::RefreshDevices()
{
    if (impl_)
    {
        impl_->RefreshDeviceState();
    }
}

std::string VoiceChat::ActiveInputDevice() const
{
    return impl_ ? impl_->activeDeviceName : std::string{};
}

std::string VoiceChat::LastError() const
{
    return impl_ ? impl_->lastError : std::string{};
}

std::uint64_t VoiceChat::DeviceGeneration() const
{
    return impl_ ? impl_->deviceGeneration : 0;
}

VoiceSettings VoiceChat::GetSettings() const
{
    VoiceSettings out;
    if (!impl_)
    {
        return out;
    }
    out.enabled = impl_->enabled;
    out.muted = impl_->muted;
    out.pushToTalk = impl_->pushToTalk;
    out.voiceActivation = impl_->voiceActivation;
    out.voiceActivationThreshold = impl_->activationThreshold;
    out.inputGain = impl_->inputGain;
    out.outputVolume = impl_->outputVolume;
    out.inputDevice = impl_->inputDevice;
    return out;
}

void VoiceChat::ApplySettings(const VoiceSettings& settings)
{
    if (!impl_)
    {
        return;
    }
    SetInputDevice(settings.inputDevice);
    SetPushToTalk(settings.pushToTalk);
    SetVoiceActivation(settings.voiceActivation);
    SetVoiceActivationThreshold(settings.voiceActivationThreshold);
    SetInputGain(settings.inputGain);
    SetOutputVolume(settings.outputVolume);
    SetMuted(settings.muted);
    SetEnabled(settings.enabled);
    if (!settings.enabled || settings.muted)
    {
        SetTalking(false);
    }
}

bool VoiceChat::LoadSettings(const std::string& path)
{
    if (!impl_)
    {
        return false;
    }
    const Result<nlohmann::json> parsed = Vanity::ReadJson(path);
    if (!parsed)
    {
        impl_->lastError = parsed.Error();
        return false;
    }
    VoiceSettings loaded = GetSettings();
    if (!loaded.FromJson(parsed.Value()))
    {
        impl_->lastError = "voice settings root must be an object";
        return false;
    }
    ApplySettings(loaded);
    return true;
}

bool VoiceChat::SaveSettings(const std::string& path) const
{
    const Result<void> written = Vanity::WriteJson(path, GetSettings().ToJson());
    if (!written)
    {
        if (impl_)
        {
            impl_->lastError = written.Error();
        }
        return false;
    }
    return true;
}

void VoiceChat::SetTransport(VoiceTransport transport)
{
    if (!impl_)
    {
        return;
    }
    impl_->transport = std::move(transport);
    impl_->hasTransport = impl_->transport.Valid();
}

void VoiceChat::ClearTransport()
{
    if (!impl_)
    {
        return;
    }
    impl_->transport = {};
    impl_->hasTransport = false;
}

void VoiceChat::SetPeerPosition(std::uint32_t peerId, sf::Vector2f position)
{
    if (!impl_)
    {
        return;
    }
    if (Impl::Peer* peer = impl_->EnsurePeer(peerId))
    {
        peer->hasPosition = true;
        peer->position = position;
    }
}

void VoiceChat::ClearPeerPosition(std::uint32_t peerId)
{
    if (!impl_)
    {
        return;
    }
    if (Impl::Peer* peer = impl_->FindPeer(peerId))
    {
        peer->hasPosition = false;
    }
}

void VoiceChat::SetPeerVolume(std::uint32_t peerId, float volume)
{
    if (!impl_)
    {
        return;
    }
    if (Impl::Peer* peer = impl_->EnsurePeer(peerId))
    {
        peer->volume = std::clamp(volume, 0.f, 2.f);
    }
}

void VoiceChat::SetPeerMuted(std::uint32_t peerId, bool muted)
{
    if (!impl_)
    {
        return;
    }
    if (Impl::Peer* peer = impl_->EnsurePeer(peerId))
    {
        peer->muted = muted;
    }
}

void VoiceChat::RemovePeer(std::uint32_t peerId)
{
    if (!impl_)
    {
        return;
    }
    auto& peers = impl_->peers;
    peers.erase(
        std::remove_if(peers.begin(), peers.end(), [peerId](const Impl::Peer& peer) { return peer.id == peerId; }),
        peers.end());
}

void VoiceChat::Update(float dt, Network* network)
{
    if (!impl_ || !impl_->initialized)
    {
        return;
    }

    impl_->network = network;
    if (dt < 0.f || !std::isfinite(dt))
    {
        dt = 0.f;
    }
    const double dtMs = std::min(static_cast<double>(dt) * 1000.0, 100.0);

    // Device enumeration is a control-plane operation: polling it at 2 Hz keeps
    // headset hot-plug responsive without querying the OS every frame.
    impl_->devicePollMs += dtMs;
    if (impl_->devicePollMs >= 2000.0)
    {
        impl_->devicePollMs = 0.0;
        impl_->RefreshDeviceState();
    }

    impl_->DrainCapture();
    impl_->UpdatePeers(dtMs);

    // Decode/mix on exact 20 ms ticks. The SFML stream pulls from the decoded
    // ring on its own thread, so update rate does not change pitch or speed.
    impl_->mixClockMs += dtMs;
    int guard = 0;
    while (impl_->mixClockMs >= 20.0 && guard++ < 8)
    {
        impl_->mixClockMs -= 20.0;
        impl_->MixOneFrame();
    }

    impl_->EraseIdlePeers();

    if (impl_->stream)
    {
        // Voice obeys the "voice" mixer group, then the per-user output volume.
        const float group = AudioMixer::Instance().EffectiveVolume("voice");
        impl_->stream->setVolume(std::clamp(impl_->outputVolume * group * 100.f, 0.f, 100.f));
    }
}

void VoiceChat::OnBinaryPacket(std::uint32_t peerId, const std::uint8_t* data, std::size_t size)
{
    if (!impl_ || !data || size < kHeaderSize)
    {
        return;
    }
    // Disabled means disabled: no relay, no decoder churn and no allocation for
    // packets the player cannot hear.
    if (!impl_->enabled)
    {
        return;
    }
    if (!HasMagic(data) || data[4] != kVersion)
    {
        return;
    }
    if ((data[5] & kFlagVoice) == 0)
    {
        return;
    }
    const std::uint16_t payloadSize = ReadU16(data + 12);
    if (payloadSize == 0 || payloadSize > kMaxPacket || payloadSize != size - kHeaderSize)
    {
        return;
    }

    // A server is a relay as well as an endpoint. Forward the raw packet before
    // buffering it locally; the sender is excluded, and the packet keeps its
    // original sequence number for every listener. A custom transport takes
    // precedence; otherwise fall back to the singleton if a packet arrived
    // during the first poll before Update() stored the Network pointer.
    if (impl_->hasTransport && impl_->transport.broadcast && impl_->transport.isServer && impl_->transport.isServer())
    {
        impl_->transport.broadcast(data, size, peerId, false);
    }
    else
    {
        Network& net = impl_->network ? *impl_->network : Network::Instance();
        if (net.IsServer())
        {
            net.BroadcastBinary(Network::kVoiceChannel, data, size, peerId);
        }
    }

    Impl::Peer* peer = impl_->EnsurePeer(peerId);
    if (!peer)
    {
        ++impl_->packetsDropped;
        return;
    }

    const std::uint16_t sequence = ReadU16(data + 6);
    const std::uint32_t timestamp = ReadU32(data + 8);
    const std::uint64_t now = impl_->NowMs();

    // The packet can arrive before Update() gets a chance to notice the gap.
    if (peer->lastPacketMs != 0 && now - peer->lastPacketMs > kPeerSilenceResetMs)
    {
        Impl::ResetPeerTimeline(*peer);
    }

    if (!peer->gotFirst)
    {
        peer->gotFirst = true;
        peer->nextSequence = sequence;
        peer->firstPacketMs = now;
    }
    else if (!peer->started && SeqDelta(sequence, peer->nextSequence) < 0 &&
             SeqDelta(peer->nextSequence, sequence) < 16)
    {
        // A later packet arriving first is common on UDP; start from the lower
        // sequence so the first decoded frame is the beginning of the burst.
        peer->nextSequence = sequence;
    }

    if (SeqDelta(sequence, peer->nextSequence) < -static_cast<std::int16_t>(kSlotCount))
    {
        ++impl_->packetsDropped;
        return;
    }

    Impl::Slot& slot = peer->slots[sequence % kSlotCount];
    if (slot.used && slot.sequence == sequence)
    {
        ++impl_->packetsDropped;
        return;
    }
    if (slot.used && SeqDelta(sequence, slot.sequence) < 0)
    {
        ++impl_->packetsDropped;
        return;
    }

    slot.used = true;
    slot.sequence = sequence;
    slot.timestamp = timestamp;
    slot.size = payloadSize;
    std::memcpy(slot.data.data(), data + kHeaderSize, payloadSize);
    peer->lastPacketMs = now;
    ++impl_->packetsReceived;
}

VoiceChat::Stats VoiceChat::GetStats() const
{
    Stats out;
    if (!impl_)
    {
        return out;
    }
    out.available = impl_->available;
    out.enabled = impl_->enabled;
    out.capturing = impl_->deviceStarted;
    out.transmitting = impl_->enabled && impl_->lastSendMs != 0 && impl_->NowMs() - impl_->lastSendMs < 150;
    out.inputLevel = impl_->inputLevel;
    out.jitterMs = 0.f;
    out.peers = impl_->peers.size();
    for (const Impl::Peer& peer : impl_->peers)
    {
        out.jitterMs = std::max(out.jitterMs, static_cast<float>(peer.Buffered()) * 20.f);
    }
    out.packetsSent = impl_->packetsSent;
    out.packetsReceived = impl_->packetsReceived;
    out.packetsLost = impl_->packetsLost;
    out.packetsDropped = impl_->packetsDropped;
    out.capturedFrames = impl_->capturedFrames;
    out.encodedFrames = impl_->encodedFrames;
    out.inputDevices = impl_->inputDevices.size();
    out.activeInputDevice = impl_->activeDeviceName;
    out.deviceGeneration = impl_->deviceGeneration;
    out.lastError = impl_->lastError;
    return out;
}

bool VoiceChat::CodecSelfTest(std::string* error)
{
    int opusError = OPUS_OK;
    OpusEncoder* encoder =
        opus_encoder_create(static_cast<opus_int32>(kSampleRate), 1, OPUS_APPLICATION_VOIP, &opusError);
    if (!encoder || opusError != OPUS_OK)
    {
        if (error)
        {
            *error = std::string("encoder: ") + opus_strerror(opusError);
        }
        return false;
    }

    DecoderPtr decoder(opus_decoder_create(static_cast<opus_int32>(kSampleRate), 1, &opusError));
    if (!decoder || opusError != OPUS_OK)
    {
        opus_encoder_destroy(encoder);
        if (error)
        {
            *error = std::string("decoder: ") + opus_strerror(opusError);
        }
        return false;
    }

    std::array<std::int16_t, kFrameSamples> input{};
    std::array<std::int16_t, kFrameSamples> output{};
    std::array<std::uint8_t, kMaxPacket> packet{};
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(kSampleRate);
        input[i] = static_cast<std::int16_t>(std::sin(t * 2.0 * 3.14159265358979323846 * 440.0) * 12000.0);
    }

    const int encoded = opus_encode(encoder, input.data(), static_cast<opus_int32>(input.size()), packet.data(),
                                    static_cast<opus_int32>(packet.size()));
    if (encoded <= 0)
    {
        opus_encoder_destroy(encoder);
        if (error)
        {
            *error = std::string("encode: ") + opus_strerror(encoded);
        }
        return false;
    }

    const int decoded =
        opus_decode(decoder.get(), packet.data(), encoded, output.data(), static_cast<opus_int32>(output.size()), 0);
    opus_encoder_destroy(encoder);
    if (decoded != static_cast<int>(output.size()))
    {
        if (error)
        {
            *error = "decoded sample count mismatch";
        }
        return false;
    }

    double energy = 0.0;
    for (std::int16_t sample : output)
    {
        energy += static_cast<double>(sample) * sample;
    }
    const double rms = std::sqrt(energy / static_cast<double>(output.size()));
    if (rms < 100.0)
    {
        if (error)
        {
            *error = "decoded signal is silent";
        }
        return false;
    }
    return true;
}

VoiceChat::CaptureTestResult VoiceChat::CaptureSelfTest(float seconds, const std::string& device)
{
    CaptureTestResult result;
    if (!impl_)
    {
        result.error = "voice chat is not initialized";
        return result;
    }

    std::string codecError;
    if (!CodecSelfTest(&codecError))
    {
        result.error = "Opus self-test failed: " + codecError;
        return result;
    }

    const bool wasInitialized = impl_->initialized;
    if (!impl_->initialized && !Initialize())
    {
        result.error = "voice chat initialization failed";
        return result;
    }
    result.available = impl_->available;
    if (!impl_->available)
    {
        result.error = impl_->lastError.empty() ? "voice codec is not available" : impl_->lastError;
        if (!wasInitialized)
        {
            Shutdown();
        }
        return result;
    }
    if (impl_->enabled)
    {
        result.error = "disable voice chat before running a capture test";
        if (!wasInitialized)
        {
            Shutdown();
        }
        return result;
    }

    const VoiceSettings saved = GetSettings();
    VoiceSettings test = saved;
    test.enabled = true;
    test.muted = false;
    test.pushToTalk = false;
    test.voiceActivation = false;
    test.inputGain = 1.f;
    test.outputVolume = 0.f; // never route the local microphone to the speakers
    if (!device.empty())
    {
        test.inputDevice = device;
    }

    const VoiceChat::Stats before = GetStats();
    ApplySettings(test);

    const float duration = std::clamp(seconds, 0.5f, 30.f);
    const auto started = std::chrono::steady_clock::now();
    VoiceChat::Stats snapshot;
    while (std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count() < duration)
    {
        Update(0.02f, nullptr);
        snapshot = GetStats();
        result.peak = std::max(result.peak, snapshot.inputLevel);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ApplySettings(saved);
    if (!wasInitialized)
    {
        Shutdown();
    }

    result.capturing = snapshot.capturing;
    result.capturedFrames = snapshot.capturedFrames - before.capturedFrames;
    result.encodedFrames = snapshot.encodedFrames - before.encodedFrames;
    result.passed = result.capturing && result.capturedFrames > 0 && result.encodedFrames > 0;
    if (!result.passed)
    {
        const std::string reason = snapshot.lastError.empty() ? impl_->lastError : snapshot.lastError;
        result.error = reason.empty() ? "no capture or encoded frames" : reason;
    }
    return result;
}

} // namespace Fem

#else // FEMBOY_HAS_VOICE

namespace Fem
{

struct VoiceChat::Impl
{
    Stats snapshot;
};

nlohmann::json VoiceSettings::ToJson() const
{
    return nlohmann::json{
        {"enabled", enabled},
        {"muted", muted},
        {"pushToTalk", pushToTalk},
        {"voiceActivation", voiceActivation},
        {"voiceActivationThreshold", voiceActivationThreshold},
        {"inputGain", inputGain},
        {"outputVolume", outputVolume},
        {"inputDevice", inputDevice},
    };
}

bool VoiceSettings::FromJson(const nlohmann::json& data)
{
    if (!data.is_object())
    {
        return false;
    }
    if (data.contains("enabled") && data["enabled"].is_boolean())
    {
        enabled = data["enabled"].get<bool>();
    }
    if (data.contains("muted") && data["muted"].is_boolean())
    {
        muted = data["muted"].get<bool>();
    }
    if (data.contains("pushToTalk") && data["pushToTalk"].is_boolean())
    {
        pushToTalk = data["pushToTalk"].get<bool>();
    }
    if (data.contains("voiceActivation") && data["voiceActivation"].is_boolean())
    {
        voiceActivation = data["voiceActivation"].get<bool>();
    }
    if (data.contains("voiceActivationThreshold") && data["voiceActivationThreshold"].is_number())
    {
        voiceActivationThreshold = data["voiceActivationThreshold"].get<float>();
    }
    if (data.contains("inputGain") && data["inputGain"].is_number())
    {
        inputGain = data["inputGain"].get<float>();
    }
    if (data.contains("outputVolume") && data["outputVolume"].is_number())
    {
        outputVolume = data["outputVolume"].get<float>();
    }
    if (data.contains("inputDevice") && data["inputDevice"].is_string())
    {
        inputDevice = data["inputDevice"].get<std::string>();
    }
    voiceActivationThreshold = std::clamp(voiceActivationThreshold, 0.f, 1.f);
    inputGain = std::clamp(inputGain, 0.f, 4.f);
    outputVolume = std::clamp(outputVolume, 0.f, 1.f);
    return true;
}

VoiceChat& VoiceChat::Instance()
{
    static VoiceChat chat;
    return chat;
}

VoiceChat::VoiceChat() : impl_(std::make_unique<Impl>())
{
}

VoiceChat::~VoiceChat() = default;

bool VoiceChat::Initialize()
{
    BOY_WARN("VoiceChat", "voice support not compiled (FEMBOY_HAS_VOICE is off)");
    return true;
}

void VoiceChat::Shutdown()
{
}
bool VoiceChat::Available() const
{
    return false;
}
bool VoiceChat::Enabled() const
{
    return false;
}
void VoiceChat::SetEnabled(bool)
{
}
bool VoiceChat::Muted() const
{
    return true;
}
void VoiceChat::SetMuted(bool)
{
}
bool VoiceChat::PushToTalk() const
{
    return true;
}
void VoiceChat::SetPushToTalk(bool)
{
}
void VoiceChat::SetTalking(bool)
{
}
bool VoiceChat::Talking() const
{
    return false;
}
bool VoiceChat::VoiceActivation() const
{
    return false;
}
void VoiceChat::SetVoiceActivation(bool)
{
}
float VoiceChat::VoiceActivationThreshold() const
{
    return 0.f;
}
void VoiceChat::SetVoiceActivationThreshold(float)
{
}
float VoiceChat::InputGain() const
{
    return 1.f;
}
void VoiceChat::SetInputGain(float)
{
}
float VoiceChat::OutputVolume() const
{
    return 1.f;
}
void VoiceChat::SetOutputVolume(float)
{
}
void VoiceChat::SetInputDevice(const std::string&)
{
}
std::string VoiceChat::InputDevice() const
{
    return {};
}
std::vector<std::string> VoiceChat::InputDevices() const
{
    return {};
}
void VoiceChat::RefreshDevices()
{
}
std::string VoiceChat::ActiveInputDevice() const
{
    return {};
}
std::string VoiceChat::LastError() const
{
    return "voice support not compiled";
}
std::uint64_t VoiceChat::DeviceGeneration() const
{
    return 0;
}
VoiceSettings VoiceChat::GetSettings() const
{
    return VoiceSettings{};
}
void VoiceChat::ApplySettings(const VoiceSettings&)
{
}
bool VoiceChat::LoadSettings(const std::string&)
{
    return false;
}
bool VoiceChat::SaveSettings(const std::string&) const
{
    return false;
}
void VoiceChat::SetTransport(VoiceTransport)
{
}
void VoiceChat::ClearTransport()
{
}
void VoiceChat::SetPeerPosition(std::uint32_t, sf::Vector2f)
{
}
void VoiceChat::ClearPeerPosition(std::uint32_t)
{
}
void VoiceChat::SetPeerVolume(std::uint32_t, float)
{
}
void VoiceChat::SetPeerMuted(std::uint32_t, bool)
{
}
void VoiceChat::RemovePeer(std::uint32_t)
{
}
void VoiceChat::Update(float, Network*)
{
}
void VoiceChat::OnBinaryPacket(std::uint32_t, const std::uint8_t*, std::size_t)
{
}
VoiceChat::Stats VoiceChat::GetStats() const
{
    return impl_ ? impl_->snapshot : Stats{};
}
bool VoiceChat::CodecSelfTest(std::string* error)
{
    if (error)
    {
        *error = "voice support not compiled";
    }
    return false;
}

VoiceChat::CaptureTestResult VoiceChat::CaptureSelfTest(float, const std::string&)
{
    CaptureTestResult result;
    result.error = "voice support not compiled";
    return result;
}

} // namespace Fem

#endif // FEMBOY_HAS_VOICE
