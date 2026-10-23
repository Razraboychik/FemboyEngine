// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Fem
{

// Cryptographically strong random number generator backed by the operating
// system. It is intentionally small and allocation-free. Use it for seeds,
// security-sensitive values and rare critical rolls; use FastRandom for
// high-volume gameplay randomness.
class CryptoRandom
{
public:
    CryptoRandom() = default;

    uint32_t NextU32();
    uint64_t NextU64();
    float NextFloat(); // [0, 1)
    float NextFloat(float min, float max);
    int NextInt(int min, int max); // inclusive [min, max]
    bool NextBool(float probability = 0.5f);

    void Fill(uint8_t* buffer, std::size_t size);

private:
    static bool FillBytes(void* buffer, std::size_t size);
};

// Process-wide instance. Thread-safe on platforms whose OS RNG is thread-safe.
CryptoRandom& CryptoRandomInstance();

} // namespace Fem
