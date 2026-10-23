// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace Fem
{

// One player's command for one simulation frame. Buttons are a bit mask; the
// two axes are already normalized by the input layer.
struct InputFrame
{
    std::uint32_t frame = 0;
    std::uint64_t buttons = 0;
    float axisX = 0.f;
    float axisY = 0.f;

    bool operator==(const InputFrame& other) const
    {
        return frame == other.frame && buttons == other.buttons && axisX == other.axisX && axisY == other.axisY;
    }
};

struct PlayerInput
{
    int player = 0;
    InputFrame input{};
};

// Deterministic rollback session for peer-to-peer lockstep.
//
// This is infrastructure, not a netcode policy: it does not open sockets and it
// does not decide who owns which entity. The game provides two callbacks -
// simulate(frame, inputs) and snapshot/restore - and RollbackSession guarantees
// that when a late remote input arrives, the world is rewound to the frame
// before it and re-simulated with the corrected history. That is the same shape
// as GGPO but with the state owned by the game, which is necessary because the
// engine's ECS, physics and particle systems are not trivially serialisable.
//
// Rules for the game's simulate callback:
//   * it must be deterministic given (frame, inputs),
//   * it must not read wall-clock time, unseeded RNG or unordered iteration,
//   * it must use the input array, not the current keyboard/mouse.
//
// Physics is the usual source of non-determinism. Box2D is deterministic for a
// fixed step, fixed iteration count and identical build, which is why the engine
// already runs a 120 Hz fixed step. Cross-platform determinism (MSVC vs GCC) is
// not guaranteed by floating point; rollback across different platforms needs
// fixed-point or integer physics and is out of scope here.
class RollbackSession
{
public:
    using SimulateFn = std::function<void(std::uint32_t frame, const std::vector<PlayerInput>& inputs)>;
    using SnapshotFn = std::function<std::vector<std::uint8_t>()>;
    using RestoreFn = std::function<bool(const std::vector<std::uint8_t>&)>;
    using ChecksumFn = std::function<std::uint64_t()>;

    void Configure(int players = 2, int localPlayer = 0, int maxRollbackFrames = 8, int inputDelay = 2);
    void SetCallbacks(SimulateFn simulate, SnapshotFn snapshot, RestoreFn restore, ChecksumFn checksum = {});
    void Reset(std::uint32_t startFrame = 0);

    int PlayerCount() const
    {
        return playerCount_;
    }
    int LocalPlayer() const
    {
        return localPlayer_;
    }
    int MaxRollbackFrames() const
    {
        return maxRollbackFrames_;
    }
    int InputDelay() const
    {
        return inputDelay_;
    }
    void SetInputDelay(int frames)
    {
        inputDelay_ = std::max(0, frames);
    }

    std::uint32_t CurrentFrame() const
    {
        return currentFrame_;
    }
    // Highest frame for which every remote player's input is known. The
    // difference currentFrame() - confirmedFrame() is the prediction depth; a
    // value above maxRollbackFrames() means the session should stall instead of
    // predicting further.
    std::uint32_t ConfirmedFrame() const
    {
        return confirmedFrame_;
    }
    std::uint32_t PredictionDepth() const
    {
        return currentFrame_ > confirmedFrame_ ? currentFrame_ - confirmedFrame_ : 0;
    }
    bool CanAdvance() const
    {
        return PredictionDepth() < static_cast<std::uint32_t>(maxRollbackFrames_);
    }

    // Queues the local input for currentFrame()+inputDelay and stores it in the
    // history. The delay gives remote peers time to deliver before the frame is
    // simulated; inputDelay 0 is local-only play.
    void SubmitLocalInput(std::uint64_t buttons, float axisX = 0.f, float axisY = 0.f);

    // Delivers a remote input. If the frame was already simulated with a
    // different prediction, the session rolls back and re-simulates.
    // Returns the number of frames re-simulated (0 when the input was new).
    int ReceiveRemoteInput(int player, const InputFrame& input);

    // Simulates the next frame. Returns false when callbacks are missing or the
    // prediction window is exhausted.
    bool Advance();

    // Forces a rewind to `frame` and re-simulation of every frame after it, using
    // the stored history. Mainly for a debugging "desync now" command.
    int RewindTo(std::uint32_t frame);

    // Total frames re-simulated since reset. A high ratio is a network problem,
    // not an engine problem; expose it in a netgraph.
    std::uint64_t ResimulatedFrames() const
    {
        return resimulatedFrames_;
    }
    // Checksum of the most recent simulation, when a checksum callback exists.
    std::uint64_t LastChecksum() const
    {
        return lastChecksum_;
    }

private:
    struct Snapshot
    {
        std::uint32_t frame = 0;
        std::vector<std::uint8_t> data;
        std::uint64_t checksum = 0;
    };

    std::vector<PlayerInput> InputsFor(std::uint32_t frame);
    bool SimulateFrame(std::uint32_t frame);
    bool RestoreBefore(std::uint32_t frame);
    void Trim();
    bool AllRemoteKnown(std::uint32_t frame) const;
    void UpdateConfirmed();

    int playerCount_ = 2;
    int localPlayer_ = 0;
    int maxRollbackFrames_ = 8;
    int inputDelay_ = 2;

    std::uint32_t currentFrame_ = 0;
    std::uint32_t confirmedFrame_ = 0;
    std::uint64_t resimulatedFrames_ = 0;
    std::uint64_t lastChecksum_ = 0;

    SimulateFn simulate_;
    SnapshotFn snapshot_;
    RestoreFn restore_;
    ChecksumFn checksum_;

    // Frame -> player -> input. The local player's entry is the command that was
    // actually simulated; remote entries may start as predictions and be
    // corrected later.
    std::map<std::uint32_t, std::map<int, InputFrame>> history_;
    std::map<std::uint32_t, std::map<int, bool>> received_;
    std::map<int, InputFrame> latestRemote_;
    std::deque<Snapshot> snapshots_;
};

} // namespace Fem
