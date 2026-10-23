// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Rollback.hpp"

#include "FemboyEngine/Boy.hpp"

#include <algorithm>

namespace Fem
{

void RollbackSession::Configure(int players, int localPlayer, int maxRollbackFrames, int inputDelay)
{
    playerCount_ = std::clamp(players, 1, 8);
    localPlayer_ = std::clamp(localPlayer, 0, playerCount_ - 1);
    maxRollbackFrames_ = std::clamp(maxRollbackFrames, 1, 60);
    inputDelay_ = std::clamp(inputDelay, 0, maxRollbackFrames_);
    Reset(currentFrame_);
}

void RollbackSession::SetCallbacks(SimulateFn simulate, SnapshotFn snapshot, RestoreFn restore, ChecksumFn checksum)
{
    simulate_ = std::move(simulate);
    snapshot_ = std::move(snapshot);
    restore_ = std::move(restore);
    checksum_ = std::move(checksum);
}

void RollbackSession::Reset(std::uint32_t startFrame)
{
    currentFrame_ = startFrame;
    confirmedFrame_ = startFrame;
    resimulatedFrames_ = 0;
    lastChecksum_ = 0;
    history_.clear();
    latestRemote_.clear();
    received_.clear();
    snapshots_.clear();
}

void RollbackSession::SubmitLocalInput(std::uint64_t buttons, float axisX, float axisY)
{
    InputFrame input;
    input.frame = currentFrame_ + static_cast<std::uint32_t>(std::max(1, inputDelay_));
    input.buttons = buttons;
    input.axisX = axisX;
    input.axisY = axisY;
    history_[input.frame][localPlayer_] = input;
}

bool RollbackSession::AllRemoteKnown(std::uint32_t frame) const
{
    const auto frameIt = received_.find(frame);
    if (frameIt == received_.end())
    {
        return false;
    }
    for (int player = 0; player < playerCount_; ++player)
    {
        if (player == localPlayer_)
        {
            continue;
        }
        const auto it = frameIt->second.find(player);
        if (it == frameIt->second.end() || !it->second)
        {
            return false;
        }
    }
    return true;
}

void RollbackSession::UpdateConfirmed()
{
    while (AllRemoteKnown(confirmedFrame_ + 1))
    {
        ++confirmedFrame_;
    }
}

std::vector<PlayerInput> RollbackSession::InputsFor(std::uint32_t frame)
{
    std::vector<PlayerInput> inputs;
    inputs.reserve(static_cast<std::size_t>(playerCount_));

    auto& frameHistory = history_[frame];

    const auto localIt = frameHistory.find(localPlayer_);
    if (localIt != frameHistory.end())
    {
        inputs.push_back(PlayerInput{localPlayer_, localIt->second});
    }
    else
    {
        // No local input queued for this frame (input starvation or a replay of
        // a frame older than the delay). Neutral input keeps determinism.
        InputFrame neutral;
        neutral.frame = frame;
        frameHistory[localPlayer_] = neutral;
        inputs.push_back(PlayerInput{localPlayer_, neutral});
    }

    for (int player = 0; player < playerCount_; ++player)
    {
        if (player == localPlayer_)
        {
            continue;
        }

        const auto it = frameHistory.find(player);
        if (it != frameHistory.end())
        {
            inputs.push_back(PlayerInput{player, it->second});
            continue;
        }

        // Prediction: repeat the last received input. Storing it in history is
        // what lets a later receiveRemoteInput() notice that the prediction was
        // wrong and trigger a rewind.
        InputFrame predicted;
        const auto latest = latestRemote_.find(player);
        if (latest != latestRemote_.end())
        {
            predicted = latest->second;
        }
        predicted.frame = frame;
        frameHistory[player] = predicted;
        inputs.push_back(PlayerInput{player, predicted});
    }

    return inputs;
}

bool RollbackSession::SimulateFrame(std::uint32_t frame)
{
    if (!simulate_)
    {
        return false;
    }

    const std::vector<PlayerInput> inputs = InputsFor(frame);
    simulate_(frame, inputs);
    lastChecksum_ = checksum_ ? checksum_() : 0;

    if (snapshot_)
    {
        Snapshot snapshot;
        snapshot.frame = frame;
        snapshot.data = snapshot_();
        snapshot.checksum = lastChecksum_;
        snapshots_.push_back(std::move(snapshot));
    }

    currentFrame_ = frame;
    UpdateConfirmed();
    return true;
}

bool RollbackSession::Advance()
{
    if (!simulate_)
    {
        BOY_ERROR("Rollback", "advance() without a simulate callback");
        return false;
    }
    if (!CanAdvance())
    {
        return false;
    }

    // A rewind of the very first simulated frame needs the state *before* it.
    // Capture the starting state lazily so callers can configure callbacks and
    // then reset() without a special "take base snapshot" call.
    if (snapshots_.empty() && snapshot_)
    {
        Snapshot base;
        base.frame = currentFrame_;
        base.data = snapshot_();
        base.checksum = checksum_ ? checksum_() : 0;
        snapshots_.push_back(std::move(base));
    }

    return SimulateFrame(currentFrame_ + 1);
}

int RollbackSession::ReceiveRemoteInput(int player, const InputFrame& input)
{
    if (player < 0 || player >= playerCount_ || player == localPlayer_)
    {
        return 0;
    }

    latestRemote_[player] = input;
    received_[input.frame][player] = true;

    auto& frameHistory = history_[input.frame];
    const auto existing = frameHistory.find(player);
    const bool existed = existing != frameHistory.end();
    const bool changed = existed && !(existing->second == input);
    frameHistory[player] = input;

    if (changed && input.frame <= currentFrame_)
    {
        const int resimulated = RewindTo(input.frame);
        UpdateConfirmed();
        return resimulated;
    }

    UpdateConfirmed();
    return 0;
}

bool RollbackSession::RestoreBefore(std::uint32_t frame)
{
    if (!restore_ || snapshots_.empty() || frame == 0)
    {
        return false;
    }

    // Largest snapshot at or before frame-1.
    auto it = snapshots_.end();
    while (it != snapshots_.begin())
    {
        --it;
        if (it->frame <= frame - 1)
        {
            break;
        }
    }
    if (it == snapshots_.end() || it->frame > frame - 1)
    {
        return false;
    }

    const std::uint32_t restoreFrame = it->frame;
    if (!restore_(it->data))
    {
        return false;
    }

    // Drop snapshots and predictions newer than the restore point. The history
    // itself is kept: it contains the late input that caused the rewind.
    snapshots_.erase(it + 1, snapshots_.end());
    currentFrame_ = restoreFrame;
    return true;
}

int RollbackSession::RewindTo(std::uint32_t frame)
{
    if (frame > currentFrame_ || !restore_)
    {
        return 0;
    }

    const std::uint32_t oldCurrent = currentFrame_;
    if (!RestoreBefore(frame))
    {
        return 0;
    }

    int count = 0;
    for (std::uint32_t f = currentFrame_ + 1; f <= oldCurrent; ++f)
    {
        if (!SimulateFrame(f))
        {
            break;
        }
        ++count;
    }
    resimulatedFrames_ += static_cast<std::uint64_t>(count);
    Trim();
    return count;
}

void RollbackSession::Trim()
{
    // Keep one extra snapshot beyond the prediction window so a late input at
    // the edge can still find the state before its frame.
    const std::size_t keep = static_cast<std::size_t>(maxRollbackFrames_ + 2);
    while (snapshots_.size() > keep)
    {
        snapshots_.pop_front();
    }

    if (!history_.empty() && confirmedFrame_ > static_cast<std::uint32_t>(maxRollbackFrames_ * 2))
    {
        const std::uint32_t cutoff = confirmedFrame_ - static_cast<std::uint32_t>(maxRollbackFrames_ * 2);
        history_.erase(history_.begin(), history_.lower_bound(cutoff));
        received_.erase(received_.begin(), received_.lower_bound(cutoff));
    }
}

} // namespace Fem
