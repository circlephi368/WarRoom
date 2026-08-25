#include "core/command/move_node_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

	MoveNodeCommand::MoveNodeCommand(Uuid nodeId, float oldX, float oldY, float newX, float newY)
		: nodeId_(std::move(nodeId))
		, oldX_(oldX), oldY_(oldY)
		, newX_(newX), newY_(newY)
	{}

	void MoveNodeCommand::execute(WarRoomModel& model)
	{
		WarNode* node = model.getNodeMutable(nodeId_);
		if (!node) return;

		node->pos_x = newX_;
		node->pos_y = newY_;

		// 同步相对坐标（以父节点为参考）
		const WarNode* parent = model.getNode(node->parent_id);
		if (parent && node->parent_id != model.getDocumentRootId()) {
			node->rel_x = newX_ - parent->pos_x;
			node->rel_y = newY_ - parent->pos_y;
		} else {
			node->rel_x = newX_;
			node->rel_y = newY_;
		}
	}

	void MoveNodeCommand::undo(WarRoomModel& model)
	{
		WarNode* node = model.getNodeMutable(nodeId_);
		if (!node) return;

		node->pos_x = oldX_;
		node->pos_y = oldY_;

		const WarNode* parent = model.getNode(node->parent_id);
		if (parent && node->parent_id != model.getDocumentRootId()) {
			node->rel_x = oldX_ - parent->pos_x;
			node->rel_y = oldY_ - parent->pos_y;
		} else {
			node->rel_x = oldX_;
			node->rel_y = oldY_;
		}
	}

	std::string MoveNodeCommand::description() const
	{
		return "移动节点";
	}

} // namespace warroom
