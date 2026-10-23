// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include "FemboyEngine/FastRandom.hpp"

#include <cstdint>
#include <istream>
#include <mutex>
#include <ostream>
#include <random>
#include <string>

namespace Fem
{

// RandomSystem provides several random streams with different stability and
// predictability guarantees. Gameplay code should choose the stream that matches
// its needs instead of sharing one global generator.
class RandomSystem
{
public:
    // Fast, entropy-seeded PCG generator. Use for visuals, drops and damage.
    static FastRandom& Global();

    // One stable sequence per frame. The sequence changes on the next frame.
    static FastRandom& Frame();

    // One stable sequence per call. The result does not depend on call order
    // inside the same frame.
    static FastRandom& Call();

    // Predictable Mersenne Twister for lockstep, replays and network sync.
    static std::mt19937_64& Network();

    // Reseed the frame generator. Call once per frame from the engine loop.
    static void BeginFrame(uint64_t frameCounter = 0);

    static void SeedNetwork(uint64_t seed);

    // Convenience for a host that starts a session: draw a seed from the OS and
    // return it, so the same value can be sent once to every peer.
    static uint64_t SeedNetworkFromHost();
    static uint64_t NetworkSeed();

    // Serialize and restore the network generator state. The text form is the
    // standard mt19937_64 stream form, which is compact and portable between
    // builds that use the same standard library.
    static void SerializeNetwork(std::ostream& out);
    static bool DeserializeNetwork(std::istream& in);
    static std::string CaptureNetworkState();
    static bool RestoreNetworkState(const std::string& state);

    static uint64_t FrameCounter();
    static uint64_t CallCounter();

private:
    static uint64_t baseSeed_;
    static uint64_t frameCounter_;
    static uint64_t callCounter_;
    static uint64_t networkSeed_;
    static std::mutex mutex_;
};

} // namespace Fem
