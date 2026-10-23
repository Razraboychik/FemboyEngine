// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/RandomSystem.hpp"

#include "FemboyEngine/CryptoRandom.hpp"

#include <sstream>

namespace Fem
{

uint64_t RandomSystem::baseSeed_ = 0;
uint64_t RandomSystem::frameCounter_ = 0;
uint64_t RandomSystem::callCounter_ = 0;
uint64_t RandomSystem::networkSeed_ = 0;
std::mutex RandomSystem::mutex_;

FastRandom& RandomSystem::Global()
{
    return GlobalRandom();
}

FastRandom& RandomSystem::Frame()
{
    static thread_local FastRandom generator(1);
    return generator;
}

FastRandom& RandomSystem::Call()
{
    static thread_local FastRandom generator(1);
    uint64_t counter = 0;
    uint64_t base = 0;
    uint64_t frame = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        counter = ++callCounter_;
        if (baseSeed_ == 0)
        {
            baseSeed_ = CryptoRandom().NextU64() | 1ULL;
        }
        base = baseSeed_;
        frame = frameCounter_;
    }
    const uint64_t seed = base ^ (frame * 0x9e3779b97f4a7c15ULL) ^ (counter * 0xc2b2ae3d27d4eb4fULL);
    generator.Seed(seed);
    return generator;
}

std::mt19937_64& RandomSystem::Network()
{
    static std::mt19937_64 generator;
    return generator;
}

void RandomSystem::BeginFrame(uint64_t frameCounter)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (baseSeed_ == 0)
    {
        baseSeed_ = CryptoRandom().NextU64();
    }
    if (frameCounter == 0)
    {
        ++frameCounter_;
    }
    else
    {
        frameCounter_ = frameCounter;
    }
    callCounter_ = 0;

    const uint64_t seed = baseSeed_ ^ (frameCounter_ * 0x9e3779b97f4a7c15ULL);
    Frame().Seed(seed);
}

void RandomSystem::SeedNetwork(uint64_t seed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    networkSeed_ = seed;
    Network().seed(seed);
}

uint64_t RandomSystem::SeedNetworkFromHost()
{
    const uint64_t seed = CryptoRandom().NextU64();
    SeedNetwork(seed);
    return seed;
}

uint64_t RandomSystem::NetworkSeed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return networkSeed_;
}

void RandomSystem::SerializeNetwork(std::ostream& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    out << Network();
}

bool RandomSystem::DeserializeNetwork(std::istream& in)
{
    std::lock_guard<std::mutex> lock(mutex_);
    in >> Network();
    return !in.fail();
}

std::string RandomSystem::CaptureNetworkState()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    out << networkSeed_ << ' ' << Network();
    return out.str();
}

bool RandomSystem::RestoreNetworkState(const std::string& state)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::istringstream in(state);
    uint64_t seed = 0;
    in >> seed;
    if (!in)
    {
        return false;
    }
    in >> Network();
    if (in.fail())
    {
        return false;
    }
    networkSeed_ = seed;
    return true;
}

uint64_t RandomSystem::FrameCounter()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return frameCounter_;
}

uint64_t RandomSystem::CallCounter()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return callCounter_;
}

} // namespace Fem
