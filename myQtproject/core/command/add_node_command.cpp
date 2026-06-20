#include "core/command/add_node_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

    AddNodeCommand::AddNodeCommand(WarNode node, Uuid parentId, int index)
        : nodeSnapshot_(std::move(node))  // 这里 move 没问题，因为是 command 内部持有
        , nodeId_(nodeSnapshot_.id)
        , parentId_(std::move(parentId))
        , index_(index)
    {
        // 如果节点 id 为空，生成一个
        if (nodeId_.empty()) {
            nodeId_ = generateUuid();
            nodeSnapshot_.id = nodeId_;
        }
    }

    void AddNodeCommand::execute(WarRoomModel& model) {
        // 每次都从快照拷贝一份写入模型，支持 redo 重复执行
        WarNode copy = nodeSnapshot_;
        model.addNode(std::move(copy), parentId_, index_);
    }

    void AddNodeCommand::undo(WarRoomModel& model) {
        // 不重新挂载子节点（保持与执行前一致的树结构），直接删除节点本身
        model.removeNode(nodeId_, false);
    }

    std::string AddNodeCommand::description() const {
        return "添加节点: " + nodeSnapshot_.title;
    }

} // namespace warroom
