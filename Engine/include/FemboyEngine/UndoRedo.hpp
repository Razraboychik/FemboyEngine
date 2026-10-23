// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <vector>
#include <functional>
#include <memory>
#include <string>

namespace Fem
{

struct Command
{
    virtual ~Command() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
    virtual std::string Name() const = 0;
};

class UndoStack
{
public:
    static UndoStack& Instance();
    void Push(std::shared_ptr<Command> cmd);
    void Undo();
    void Redo();
    bool CanUndo() const
    {
        return !undo_.empty();
    }
    bool CanRedo() const
    {
        return !redo_.empty();
    }
    const std::vector<std::shared_ptr<Command>>& UndoHistory() const
    {
        return undo_;
    }
    const std::vector<std::shared_ptr<Command>>& RedoHistory() const
    {
        return redo_;
    }
    size_t MaxSize() const
    {
        return maxSize_;
    }
    void SetMaxSize(size_t n);
    void Clear();

private:
    UndoStack() = default;
    std::vector<std::shared_ptr<Command>> undo_;
    std::vector<std::shared_ptr<Command>> redo_;
    size_t maxSize_ = 256;
};

} // namespace Fem
