// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/CryptoRandom.hpp"

#include <algorithm>
#include <cstring>
#include <random>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#elif defined(__linux__)
#include <sys/random.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#endif

namespace Fem
{

bool CryptoRandom::FillBytes(void* buffer, std::size_t size)
{
    if (size == 0)
    {
        return true;
    }

#if defined(_WIN32)
    const NTSTATUS status = BCryptGenRandom(nullptr, static_cast<PUCHAR>(buffer), static_cast<ULONG>(size),
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return BCRYPT_SUCCESS(status);
#elif defined(__linux__)
    std::size_t filled = 0;
    while (filled < size)
    {
        const ssize_t result = getrandom(static_cast<char*>(buffer) + filled, size - filled, 0);
        if (result <= 0)
        {
            return false;
        }
        filled += static_cast<std::size_t>(result);
    }
    return true;
#elif defined(__APPLE__)
    arc4random_buf(buffer, size);
    return true;
#else
    std::random_device device;
    std::size_t filled = 0;
    while (filled < size)
    {
        const unsigned int value = device();
        const std::size_t chunk = std::min(size - filled, sizeof(value));
        std::memcpy(static_cast<char*>(buffer) + filled, &value, chunk);
        filled += chunk;
    }
    return true;
#endif
}

CryptoRandom& CryptoRandomInstance()
{
    static CryptoRandom instance;
    return instance;
}

void CryptoRandom::Fill(uint8_t* buffer, std::size_t size)
{
    if (size == 0 || !buffer)
    {
        return;
    }

    if (!FillBytes(buffer, size))
    {
        std::random_device device;
        for (std::size_t i = 0; i < size; ++i)
        {
            buffer[i] = static_cast<uint8_t>(device());
        }
    }
}

uint32_t CryptoRandom::NextU32()
{
    uint32_t value = 0;
    Fill(reinterpret_cast<uint8_t*>(&value), sizeof(value));
    return value;
}

uint64_t CryptoRandom::NextU64()
{
    uint64_t value = 0;
    Fill(reinterpret_cast<uint8_t*>(&value), sizeof(value));
    return value;
}

float CryptoRandom::NextFloat()
{
    // 24 significant bits: every value is strictly below 1.0f. Converting all
    // 32 bits would round 0xFFFFFFFF up to 1.0f.
    return static_cast<float>(NextU32() >> 8) * (1.0f / 16777216.0f);
}

float CryptoRandom::NextFloat(float min, float max)
{
    if (min > max)
    {
        std::swap(min, max);
    }
    return min + (max - min) * NextFloat();
}

int CryptoRandom::NextInt(int min, int max)
{
    if (min > max)
    {
        std::swap(min, max);
    }

    const uint64_t range = static_cast<uint64_t>(max) - static_cast<uint64_t>(min) + 1ULL;
    if (range == 0)
    {
        return min;
    }

    const uint64_t limit = (UINT64_MAX / range) * range;
    uint64_t value = 0;
    do
    {
        value = NextU64();
    } while (value >= limit);

    return static_cast<int>(static_cast<std::int64_t>(min) + static_cast<std::int64_t>(value % range));
}

bool CryptoRandom::NextBool(float probability)
{
    probability = std::clamp(probability, 0.f, 1.f);
    return NextFloat() < probability;
}

} // namespace Fem
