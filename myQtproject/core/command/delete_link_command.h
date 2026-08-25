#pragma once
#include "command.h"
#include "core/warroom/warroom_types.h"
#include "core/warroom/war_link.h"
#include <vector>

namespace warroom {

	// 复用 AddLinkCommand 中定义的 AnchorSnapshot 简化实现：
	// 这里做一个独立的简化锚点快照，用于 DeleteLinkCommand。
	struct LinkAnchorSnapshot {
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

		static LinkAnchorSnapshot fromAnchor(const Anchor* a);
		std::unique_ptr<Anchor> toAnchor() const;
	};

	// 删除连线命令
	class DeleteLinkCommand : public Command {
	public:
		explicit DeleteLinkCommand(const Uuid& linkId);

		void execute(WarRoomModel& model) override;
		void undo(WarRoomModel& model) override;
		std::string description() const override;

	private:
		Uuid linkId_;

		// 首次执行时捕获的连线快照
		bool captured_ = false;
		LinkAnchorSnapshot startAnchor_;
		LinkAnchorSnapshot endAnchor_;
		std::vector<LinkAnchorSnapshot> waypoints_;
		LinkType type_ = LinkType::Dependency;
		std::string label_;
		Color color_;
	};

} // namespace warroom
