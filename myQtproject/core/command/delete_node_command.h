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
	// - 构造时只保存节点 id 与父节点位置信息
	// - execute() 在执行时立刻做节点完整快照（包括关联连线），再从模型移除节点
	// - undo() 根据快照重新把节点插入到原来的位置并恢复子节点关系和关联连线
	class DeleteNodeCommand : public Command {
	public:
		DeleteNodeCommand(const Uuid& nodeId);

		void execute(WarRoomModel& model) override;
		void undo(WarRoomModel& model) override;
		std::string description() const override;

	private:
		static NodeLinkSnapshot::AnchorSnap captureAnchor(const Anchor* a);
		static std::unique_ptr<Anchor> restoreAnchor(const NodeLinkSnapshot::AnchorSnap& s);

		Uuid nodeId_;
		WarNode savedNode_;    // 执行删除时捕获的节点完整快照
		Uuid parentId_;        // 原父节点
		int index_ = -1;       // 原父节点 children_ids 中的位置
		std::vector<NodeLinkSnapshot> savedLinks_; // 关联连线快照
		bool captured_ = false;
	};

} // namespace warroom
