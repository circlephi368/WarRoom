#include "add_node_command.h"
#include "war_room_model.h"

namespace warroom {

    AddNodeCommand::AddNodeCommand(WarNode node, Uuid parentId, int index)
        : node_(std::move(node))
        , parentId_(std::move(parentId))
        , index_(index)
    {}

    void AddNodeCommand::execute(WarRoomModel& model) {
        if (executed_) return;
        model.addNode(std::move(node_), parentId_, index_);
        executed_ = true;
    }

    void AddNodeCommand::undo(WarRoomModel& model) {
        model.removeNode(node_.id);
    }

    std::string AddNodeCommand::description() const {
        return "添加节点: " + node_.title;
    }

} // namespace warroom