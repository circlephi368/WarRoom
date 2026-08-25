#pragma once
#include "command.h"
#include "core/warroom/war_link.h"
#include "core/warroom/warroom_types.h"
#include <memory>

namespace warroom {

	// 简化的锚点描述符（用于 command 内保存锚点信息，深拷贝替代 unique_ptr）
	struct AnchorSnapshot {
		enum class Type { Node, Free };
		Type type = Type::Free;

		// 对于 NodeAnchor
		Uuid node_id;
		float offset_x = 0.0f;
		float offset_y = 0.0f;
		int edge = -1;

		// 对于 FreeAnchor
		float x = 0.0f;
		float y = 0.0f;

		static AnchorSnapshot fromAnchor(const Anchor* a);
		std::unique_ptr<Anchor> toAnchor() const;
	};

	// 添加连线命令
	class AddLinkCommand : public Command {
	public:
		explicit AddLinkCommand(WarLink link);

		void execute(WarRoomModel& model) override;
		void undo(WarRoomModel& model) override;
		std::string description() const override;

		const Uuid& getLinkId() const { return linkId_; }

	private:
		Uuid linkId_;
		AnchorSnapshot startAnchor_;
		AnchorSnapshot endAnchor_;
		std::vector<AnchorSnapshot> waypoints_;
		LinkType type_ = LinkType::Dependency;
		std::string label_;
		Color color_;
	};

} // namespace warroom
