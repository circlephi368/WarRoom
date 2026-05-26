#pragma once
#include "command.h"
#include "war_node.h"

namespace warroom {

    class AddNodeCommand : public Command {
    public:
        // node: 要添加的节点（id必须已生成）
        // parentId: 父节点id，空则挂到文档根
        // index: 插入位置，-1表示末尾
        AddNodeCommand(WarNode node, Uuid parentId, int index = -1);

        void execute(WarRoomModel& model) override;
        void undo(WarRoomModel& model) override;
        std::string description() const override;

    private:
        WarNode node_;
        Uuid nodeId_;
        Uuid parentId_;
        int index_;
        bool executed_ = false;  // 防止重复执行
    };

} // namespace warroom