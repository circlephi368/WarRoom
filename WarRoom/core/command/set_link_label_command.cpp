#include "core/command/set_link_label_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

	SetLinkLabelCommand::SetLinkLabelCommand(Uuid linkId, std::string newLabel)
		: linkId_(std::move(linkId))
		, newLabel_(std::move(newLabel))
	{}

	void SetLinkLabelCommand::execute(WarRoomModel& model) {
		WarLink* link = model.getLinkMutable(linkId_);
		if (!link) return;

		if (!captured_) {
			oldLabel_ = link->label;
			captured_ = true;
		}

		link->label = newLabel_;
	}

	void SetLinkLabelCommand::undo(WarRoomModel& model) {
		WarLink* link = model.getLinkMutable(linkId_);
		if (!link) return;
		link->label = oldLabel_;
	}

	std::string SetLinkLabelCommand::description() const {
		return "修改连接线文字";
	}

} // namespace warroom
