#pragma once
#include "command.h"
#include "war_node.h"
#include "warroom_types.h"

namespace warroom {

    class DeleteNodeCommand : public Command {
    public:
        // 删除节点时，需要保存完整信息以便 undo
        DeleteNodeCommand(const Uuid& nodeId,
            const WarNode& savedNode,
            const Uuid& parentId,
            int index);

        void execute(WarRoomModel& model) override;
        void undo(WarRoomModel& model) override;
        std::string description() const override;

    private:
        Uuid nodeId_;
        WarNode savedNode_;      // 保存被删除节点的完整数据
        Uuid parentId_;          // 原父节点
        int index_;              // 在原父节点 children 中的位置
        bool executed_ = false;
    };

} // namespace warroom