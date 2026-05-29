#pragma once
#include <vector>
#include <memory>
#include <string>

namespace warroom {

    class Command;
    class WarRoomModel;

    class UndoManager {
    public:
        explicit UndoManager(int maxDepth = 1000);

        void executeCommand(std::unique_ptr<Command> cmd, WarRoomModel& model);
        bool undo(WarRoomModel& model);
        bool redo(WarRoomModel& model);
        void clear();

        bool canUndo() const;
        bool canRedo() const;
        std::string undoDescription() const;
        std::string redoDescription() const;

    private:
        std::vector<std::unique_ptr<Command>> undoStack_;
        std::vector<std::unique_ptr<Command>> redoStack_;
        int maxDepth_;
    };

} // namespace warroom