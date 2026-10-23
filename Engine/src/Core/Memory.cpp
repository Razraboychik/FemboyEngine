// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Memory.hpp"
#include <cstdlib>
#include <cstdint>

namespace Fem
{

namespace
{
size_t RoundUpPow2(size_t v)
{
    size_t r = 1;
    while (r < v)
    {
        if (r > (SIZE_MAX >> 1))
        {
            return 0; // overflow
        }
        r <<= 1;
    }
    return r;
}
} // namespace

Arena::Arena(size_t capacity) : capacity_(capacity)
{
    base_ = (char*)std::malloc(capacity_ ? capacity_ : 1);
    if (!base_)
    {
        throw std::bad_alloc();
    }
}

Arena::~Arena()
{
    DestroyObjects();
    std::free(base_);
}

Arena::Arena(Arena&& other) noexcept
    : base_(other.base_), capacity_(other.capacity_), used_(other.used_), dtors_(std::move(other.dtors_))
{
    other.base_ = nullptr;
    other.capacity_ = 0;
    other.used_ = 0;
    other.dtors_.clear();
}

Arena& Arena::operator=(Arena&& other) noexcept
{
    if (this != &other)
    {
        DestroyObjects();
        std::free(base_);
        base_ = other.base_;
        capacity_ = other.capacity_;
        used_ = other.used_;
        dtors_ = std::move(other.dtors_);
        other.base_ = nullptr;
        other.capacity_ = 0;
        other.used_ = 0;
        other.dtors_.clear();
    }
    return *this;
}

void* Arena::Allocate(size_t size, size_t align)
{
    if (size == 0)
    {
        size = 1;
    }
    if (align == 0)
    {
        align = 1;
    }
    if (align & (align - 1))
    {
        // Not a power of two: round up so the alignment arithmetic stays valid.
        align = RoundUpPow2(align);
        if (align == 0)
        {
            return nullptr;
        }
    }

    // Align relative to the real base address, not just the used_ offset.
    // malloc only guarantees max_align_t, so over-aligned types (SSE/AVX,
    // cache-line padding) would otherwise be handed misaligned storage.
    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(base_);
    const uintptr_t current = baseAddr + used_;
    if (current < baseAddr)
    {
        return nullptr; // overflow
    }
    const uintptr_t aligned = (current + align - 1) & ~(static_cast<uintptr_t>(align) - 1);
    if (aligned < current)
    {
        return nullptr; // overflow in rounding
    }

    const size_t pad = static_cast<size_t>(aligned - baseAddr);
    if (pad > capacity_ || size > capacity_ - pad)
    {
        return nullptr; // no room
    }
    used_ = pad + size;
    return reinterpret_cast<void*>(aligned);
}

void Arena::DestroyObjects()
{
    for (auto it = dtors_.rbegin(); it != dtors_.rend(); ++it)
    {
        if (it->fn)
        {
            it->fn(it->ptr);
        }
    }
    dtors_.clear();
}

void Arena::Reset()
{
    DestroyObjects();
    used_ = 0;
}

} // namespace Fem
