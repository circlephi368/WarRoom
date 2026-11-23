#pragma once
#include "command.h"
#include "core/warroom/war_node.h"
#include "core/warroom/warroom_types.h"
#include "core/warroom/war_link.h"
#include <vector>
#include <memory>
#include <string>

namespace warroom {

	// 节点删除时保存的关联连线快照（用于 undo 时恢复）
	struct NodeLinkSnapshot {
		Uuid linkId;

		// 锚点快照
		enum class AnchorType { Node, Free };
		struct AnchorSnap {
			AnchorType type = AnchorType::Free;
			// NodeAnchor
			Uuid node_id;
			float offset_x = 0.0f;
			float offset_y = 0.0f;
			int edge = -1;
			// FreeAnchor
			float x = 0.0f;
			float y = 0.0f;
		};

		AnchorSnap startAnchor;
		AnchorSnap endAnchor;
		std::vector<AnchorSnap> waypoints;
		LinkType type = LinkType::Dependency;
		std::string label;
		std::string color;
	};

	// 删除节点命令
	// - execute() 在执行时捕获被删节点的完整子树快照（所有后代节点 + 关联连线），再从模型移除
	// - undo() 按父→子顺序把整棵子树重新插入，并恢复关联连线
	class DeleteNodeCommand : public Command {
	public:
		DeleteNodeCommand(const Uuid& nodeId);

		void execute(WarRoomModel& model) override;
		void undo(WarRoomModel& model) override;
		std::string description() const override;

	private:
		static NodeLinkSnapshot::AnchorSnap captureAnchor(const Anchor* a);
		static std::unique_ptr<Anchor> restoreAnchor(const NodeLinkSnapshot::AnchorSnap& s);

		// 子树中单个节点的快照
		struct SubtreeNodeSnapshot {
			Uuid id;
			WarNode node;       // 完整节点数据（含 children_ids）
			Uuid parentId;      // 原父节点 id
			int index = -1;     // 在原父节点 children_ids 中的位置
		};

		Uuid nodeId_;
		Uuid parentId_;                          // 被删根节点的原父节点
		int index_ = -1;                         // 被删根节点在原父节点中的位置
		std::vector<SubtreeNodeSnapshot> subtree_; // 先根后子的快照序列
		std::vector<NodeLinkSnapshot> savedLinks_; // 子树关联的连线快照
		bool captured_ = false;
	};

} // namespace warroom
