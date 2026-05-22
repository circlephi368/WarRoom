#include "delete_node_command.h"
#include "war_room_model.h"

namespace warroom {

    DeleteNodeCommand::DeleteNodeCommand(const Uuid& nodeId,
        const WarNode& savedNode,
        const Uuid& parentId,
        int index)
        : nodeId_(nodeId)
        , savedNode_(savedNode)
        , parentId_(parentId)
        , index_(index)
    {}

    void DeleteNodeCommand::execute(WarRoomModel& model) {
        if (executed_) return;
        model.removeNode(nodeId_);
        executed_ = true;
    }

    void DeleteNodeCommand::undo(WarRoomModel& model) {
        // 恢复节点到原位置
        model.addNode(savedNode_, parentId_, index_);

        // 注意：addNode 内部会根据 savedNode_.id 添加，不会生成新 id
        // 因为 savedNode_.id 就是原来的 nodeId_
    }

    std::string DeleteNodeCommand::description() const {
        return "删除节点: " + savedNode_.title;
    }

} // namespace warroom