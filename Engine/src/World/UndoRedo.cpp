// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/UndoRedo.hpp"

namespace Fem
{

UndoStack& UndoStack::Instance()
{
    static UndoStack u;
    return u;
}

void UndoStack::Push(std::shared_ptr<Command> cmd)
{
    if (!cmd)
    {
        return;
    }
    cmd->Execute();
    undo_.push_back(std::move(cmd));
    redo_.clear();
    if (maxSize_ > 0)
    {
        while (undo_.size() > maxSize_)
        {
            undo_.erase(undo_.begin());
        }
    }
}

void UndoStack::Undo()
{
    if (undo_.empty())
    {
        return;
    }
    auto cmd = undo_.back();
    undo_.pop_back();
    if (cmd)
    {
        cmd->Undo();
    }
    redo_.push_back(std::move(cmd));
    if (maxSize_ > 0)
    {
        while (redo_.size() > maxSize_)
        {
            redo_.erase(redo_.begin());
        }
    }
}

void UndoStack::Redo()
{
    if (redo_.empty())
    {
        return;
    }
    auto cmd = redo_.back();
    redo_.pop_back();
    if (cmd)
    {
        cmd->Execute();
    }
    undo_.push_back(std::move(cmd));
    if (maxSize_ > 0)
    {
        while (undo_.size() > maxSize_)
        {
            undo_.erase(undo_.begin());
        }
    }
}

void UndoStack::Clear()
{
    undo_.clear();
    redo_.clear();
}

void UndoStack::SetMaxSize(size_t n)
{
    maxSize_ = n;
    if (maxSize_ == 0)
    {
        return;
    }
    while (undo_.size() > maxSize_)
    {
        undo_.erase(undo_.begin());
    }
    while (redo_.size() > maxSize_)
    {
        redo_.erase(redo_.begin());
    }
}

} // namespace Fem
