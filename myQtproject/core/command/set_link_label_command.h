#pragma once
#include "command.h"
#include "core/warroom/warroom_types.h"
#include <string>

namespace warroom {

	// 修改连接线文字的命令
	class SetLinkLabelCommand : public Command {
	public:
		SetLinkLabelCommand(Uuid linkId, std::string newLabel);

		void execute(WarRoomModel& model) override;
		void undo(WarRoomModel& model) override;
		std::string description() const override;

	private:
		Uuid linkId_;
		std::string oldLabel_; // 首次执行时从模型读取
		std::string newLabel_;
		bool captured_ = false;
	};

} // namespace warroom
