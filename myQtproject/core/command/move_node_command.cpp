#include "move_node_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

    MoveNodeCommand::MoveNodeCommand(Uuid nodeId, float oldX, float oldY, float newX, float newY)
        : nodeId_(std::move(nodeId)), oldX_(oldX), oldY_(oldY), newX_(newX), newY_(newY)
    {}

    void MoveNodeCommand::execute(WarRoomModel& model)
    {
        if (executed_) return;
        WarNode* node = model.getNodeMutable(nodeId_);
        if (node) {
            node->pos_x = newX_;
            node->pos_y = newY_;
        }
        executed_ = true;
    }

    void MoveNodeCommand::undo(WarRoomModel& model)
    {
        WarNode* node = model.getNodeMutable(nodeId_);
        if (node) {
            node->pos_x = oldX_;
            node->pos_y = oldY_;
        }
    }

    std::string MoveNodeCommand::description() const
    {
        return "移动节点";
    }

} // namespace warroom