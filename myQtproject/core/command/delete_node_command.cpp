#include "delete_node_command.h"
#include "core/warroom/war_room_model.h"

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
		model.removeNode(nodeId_, true);
		executed_ = true;
	}

	void DeleteNodeCommand::undo(WarRoomModel& model) {
		// 恢复节点
		warroom::Uuid newId = model.addNode(savedNode_, parentId_, index_);

		// 注意：addNode 内部会根据 savedNode_.id 添加，不会生成新 id
		// 但需要确保子节点关系正确
		if (!savedNode_.children_ids.empty()) {
			warroom::WarNode* restoredNode = model.getNodeMutable(savedNode_.id);
			if (restoredNode) {
				// 恢复子节点关系
				for (const auto& childId : savedNode_.children_ids) {
					warroom::WarNode* child = model.getNodeMutable(childId);
					if (child && child->parent_id != savedNode_.id) {
						child->parent_id = savedNode_.id;
						restoredNode->children_ids.push_back(childId);
					}
				}
			}
		}
	}

	std::string DeleteNodeCommand::description() const {
		return "删除节点: " + savedNode_.title;
	}

} // namespace warroom