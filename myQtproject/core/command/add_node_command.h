#pragma once
#include "command.h"
#include "core/warroom/war_node.h"

namespace warroom {

	// 添加节点命令
	// - 在构造时完整拷贝节点数据
	// - execute() 把快照拷贝写入模型
	// - undo() 从模型移除节点
	// - 支持多次 execute/undo 循环（用于 UndoManager 的 redo）
	class AddNodeCommand : public Command {
	public:
		AddNodeCommand(WarNode node, Uuid parentId, int index = -1);

		void execute(WarRoomModel& model) override;
		void undo(WarRoomModel& model) override;
		std::string description() const override;

		// 获取要添加的节点 ID（构造时已确定）
		const Uuid& getNodeId() const { return nodeId_; }

	private:
		WarNode nodeSnapshot_; // 完整节点数据快照（深拷贝）
		Uuid nodeId_;          // 该节点的 uuid（来自 snapshot）
		Uuid parentId_;        // 父节点 uuid
		int index_;            // 插入位置，-1 表示末尾
	};

} // namespace warroom
