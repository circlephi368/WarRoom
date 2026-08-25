#include "core/command/delete_link_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

	LinkAnchorSnapshot LinkAnchorSnapshot::fromAnchor(const Anchor* a) {
		LinkAnchorSnapshot s;
		if (!a) return s;
		s.type = (a->anchor_type == AnchorType::Node) ? Type::Node : Type::Free;
		if (s.type == Type::Node) {
			if (const NodeAnchor* na = dynamic_cast<const NodeAnchor*>(a)) {
				s.node_id = na->node_id;
				s.offset_x = na->offset_x;
				s.offset_y = na->offset_y;
				s.edge = na->edge;
			}
		} else {
			if (const FreeAnchor* fa = dynamic_cast<const FreeAnchor*>(a)) {
				s.x = fa->x;
				s.y = fa->y;
			}
		}
		return s;
	}

	std::unique_ptr<Anchor> LinkAnchorSnapshot::toAnchor() const {
		if (type == Type::Node) {
			return std::make_unique<NodeAnchor>(node_id, offset_x, offset_y, edge);
		} else {
			return std::make_unique<FreeAnchor>(x, y);
		}
	}

	DeleteLinkCommand::DeleteLinkCommand(const Uuid& linkId)
		: linkId_(linkId)
	{}

	void DeleteLinkCommand::execute(WarRoomModel& model) {
		// 首次执行时捕获完整的连线快照（包括锚点和路点信息）
		if (!captured_) {
			const WarLink* link = model.getLink(linkId_);
			if (!link) return;
			startAnchor_ = LinkAnchorSnapshot::fromAnchor(link->start_anchor.get());
			endAnchor_ = LinkAnchorSnapshot::fromAnchor(link->end_anchor.get());
			waypoints_.clear();
			for (const auto& wp : link->waypoints) {
				waypoints_.push_back(LinkAnchorSnapshot::fromAnchor(wp.get()));
			}
			type_ = link->type;
			label_ = link->label;
			color_ = link->color;
			captured_ = true;
		}

		model.removeLink(linkId_);
	}

	void DeleteLinkCommand::undo(WarRoomModel& model) {
		if (!captured_) return;

		WarLink link;
		link.id = linkId_;
		link.start_anchor = startAnchor_.toAnchor();
		link.end_anchor = endAnchor_.toAnchor();
		link.type = type_;
		link.label = label_;
		link.color = color_;
		for (const auto& wp : waypoints_) {
			link.waypoints.push_back(wp.toAnchor());
		}
		model.addLink(std::move(link));
	}

	std::string DeleteLinkCommand::description() const {
		return "删除连线: " + label_;
	}

} // namespace warroom
