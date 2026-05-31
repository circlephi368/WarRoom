#pragma once
#include "core/command/command.h"
#include "core/warroom/warroom_types.h"

namespace warroom {

    class ResizeNodeCommand : public Command {
    public:
        ResizeNodeCommand(Uuid nodeId,
            float oldWidth, float oldHeight,
            float newWidth, float newHeight);

        void execute(WarRoomModel& model) override;
        void undo(WarRoomModel& model) override;
        std::string description() const override;

    private:
        Uuid nodeId_;
        float oldWidth_, oldHeight_;
        float newWidth_, newHeight_;
        bool executed_ = false;
    };

} // namespace warroom