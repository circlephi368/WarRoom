#pragma once
#include "command.h"
#include "core/warroom/warroom_types.h"

namespace warroom {

	// 修改节点颜色 / 透明度的命令
	// - 保存修改前的颜色（用于 undo）和修改后的颜色（用于 redo / execute）
	class SetNodeColorCommand : public Command {
	public:
		SetNodeColorCommand(Uuid nodeId, Color newColor);

		void execute(WarRoomModel& model) override;
		void undo(WarRoomModel& model) override;
		std::string description() const override;

	private:
		Uuid nodeId_;
		Color oldColor_; // 首次执行时从模型读取
		Color newColor_;
		bool captured_ = false;
	};

} // namespace warroom
