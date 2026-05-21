#pragma once
#include "command.h"
#include "warroom_types.h"

namespace warroom {

    class MoveNodeCommand : public Command {
    public:
        MoveNodeCommand(Uuid nodeId, float oldX, float oldY, float newX, float newY);
        void execute(WarRoomModel& model) override;
        void undo(WarRoomModel& model) override;
        std::string description() const override;

    private:
        Uuid nodeId_;
        float oldX_, oldY_, newX_, newY_;
    };

} // namespace warroom