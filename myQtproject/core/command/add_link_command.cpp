#include "core/command/add_link_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

	AnchorSnapshot AnchorSnapshot::fromAnchor(const Anchor* a) {
		AnchorSnapshot s;
		if (!a) {
			s.type = Type::Free;
			s.x = 0.0f;
			s.y = 0.0f;
			return s;
		}
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

	std::unique_ptr<Anchor> AnchorSnapshot::toAnchor() const {
		if (type == Type::Node) {
			auto na = std::make_unique<NodeAnchor>(node_id, offset_x, offset_y, edge);
			return na;
		} else {
			auto fa = std::make_unique<FreeAnchor>(x, y);
			return fa;
		}
	}

	AddLinkCommand::AddLinkCommand(WarLink link)
		: linkId_(link.id.empty() ? generateUuid() : link.id)
		, startAnchor_(AnchorSnapshot::fromAnchor(link.start_anchor.get()))
		, endAnchor_(AnchorSnapshot::fromAnchor(link.end_anchor.get()))
		, type_(link.type)
		, label_(link.label)
		, color_(link.color)
	{
		for (const auto& wp : link.waypoints) {
			waypoints_.push_back(AnchorSnapshot::fromAnchor(wp.get()));
		}
	}

	void AddLinkCommand::execute(WarRoomModel& model) {
		// 根据保存的快照重新构造一个 WarLink，支持多次 execute/undo
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

	void AddLinkCommand::undo(WarRoomModel& model) {
		model.removeLink(linkId_);
	}

	std::string AddLinkCommand::description() const {
		return "添加连线: " + label_;
	}

} // namespace warroom
