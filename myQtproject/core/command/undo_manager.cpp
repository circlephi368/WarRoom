#include "undo_manager.h"
#include "command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

    UndoManager::UndoManager(int maxDepth)
        : maxDepth_(maxDepth)
    {}

    void UndoManager::executeCommand(std::unique_ptr<Command> cmd, WarRoomModel& model)
    {
        cmd->execute(model);
        undoStack_.push_back(std::move(cmd));
        redoStack_.clear();

        // 深度限制
        while (static_cast<int>(undoStack_.size()) > maxDepth_) {
            undoStack_.erase(undoStack_.begin());
        }
    }

    bool UndoManager::undo(WarRoomModel& model)
    {
        if (undoStack_.empty()) return false;
        auto cmd = std::move(undoStack_.back());
        undoStack_.pop_back();
        cmd->undo(model);
        redoStack_.push_back(std::move(cmd));
        return true;
    }

    bool UndoManager::redo(WarRoomModel& model)
    {
        if (redoStack_.empty()) return false;
        auto cmd = std::move(redoStack_.back());
        redoStack_.pop_back();
        cmd->execute(model);
        undoStack_.push_back(std::move(cmd));
        return true;
    }

    void UndoManager::clear()
    {
        undoStack_.clear();
        redoStack_.clear();
    }

    bool UndoManager::canUndo() const { return !undoStack_.empty(); }
    bool UndoManager::canRedo() const { return !redoStack_.empty(); }

    std::string UndoManager::undoDescription() const
    {
        if (undoStack_.empty()) return "";
        return undoStack_.back()->description();
    }

    std::string UndoManager::redoDescription() const
    {
        if (redoStack_.empty()) return "";
        return redoStack_.back()->description();
    }

} // namespace warroom