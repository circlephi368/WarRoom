#pragma once
#include "command.h"
#include "core/warroom/warroom_types.h"

namespace warroom {

    class EditNodeCommand : public Command {
    public:
        EditNodeCommand(Uuid nodeId,
            std::string oldTitle, std::string newTitle,
            std::string oldFullText, std::string newFullText);

        void execute(WarRoomModel& model) override;
        void undo(WarRoomModel& model) override;
        std::string description() const override;

    private:
        Uuid nodeId_;
        std::string oldTitle_;
        std::string newTitle_;
        std::string oldFullText_;
        std::string newFullText_;
    };

} // namespace warroom