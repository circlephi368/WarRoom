#include "core/command/add_node_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

    AddNodeCommand::AddNodeCommand(WarNode node, Uuid parentId, int index)
        : node_(std::move(node))
        , nodeId_(node_.id)
        , parentId_(std::move(parentId))
        , index_(index)
    {}

    void AddNodeCommand::execute(WarRoomModel& model) {
        if (executed_) return;
        model.addNode(std::move(node_), parentId_, index_);
        executed_ = true;
    }

    void AddNodeCommand::undo(WarRoomModel& model) {
        model.removeNode(nodeId_);
    }

    std::string AddNodeCommand::description() const {
        return "添加节点: " + node_.title;
    }

} // namespace warroom