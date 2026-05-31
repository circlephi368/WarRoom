#include "resize_node_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

    ResizeNodeCommand::ResizeNodeCommand(Uuid nodeId,
        float oldWidth, float oldHeight,
        float newWidth, float newHeight)
        : nodeId_(std::move(nodeId))
        , oldWidth_(oldWidth), oldHeight_(oldHeight)
        , newWidth_(newWidth), newHeight_(newHeight)
    {}

    void ResizeNodeCommand::execute(WarRoomModel& model) {
        if (executed_) return;
        model.setNodesize(nodeId_, newWidth_, newHeight_);
        executed_ = true;
    }

    void ResizeNodeCommand::undo(WarRoomModel& model) {
        model.setNodesize(nodeId_, oldWidth_, oldHeight_);
    }

    std::string ResizeNodeCommand::description() const {
        return "调整节点大小";
    }

}