#include "core/command/set_node_color_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

	SetNodeColorCommand::SetNodeColorCommand(Uuid nodeId, Color newColor)
		: nodeId_(std::move(nodeId))
		, newColor_(std::move(newColor))
	{}

	void SetNodeColorCommand::execute(WarRoomModel& model) {
		// 第一次执行时，保存原颜色（用于 undo）
		WarNode* node = model.getNodeMutable(nodeId_);
		if (!node) return;

		if (!captured_) {
			oldColor_ = node->color;
			captured_ = true;
		}

		node->color = newColor_;
	}

	void SetNodeColorCommand::undo(WarRoomModel& model) {
		WarNode* node = model.getNodeMutable(nodeId_);
		if (!node) return;
		node->color = oldColor_;
	}

	std::string SetNodeColorCommand::description() const {
		return "修改节点颜色";
	}

} // namespace warroom
