#include "core/command/delete_node_command.h"
#include "core/warroom/war_room_model.h"

namespace warroom {

	DeleteNodeCommand::DeleteNodeCommand(const Uuid& nodeId)
		: nodeId_(nodeId)
	{}

	NodeLinkSnapshot::AnchorSnap DeleteNodeCommand::captureAnchor(const Anchor* a) {
		NodeLinkSnapshot::AnchorSnap s;
		if (!a) return s;
		s.type = (a->anchor_type == AnchorType::Node)
			? NodeLinkSnapshot::AnchorType::Node
			: NodeLinkSnapshot::AnchorType::Free;
		if (s.type == NodeLinkSnapshot::AnchorType::Node) {
			if (auto* na = dynamic_cast<const NodeAnchor*>(a)) {
				s.node_id = na->node_id;
				s.offset_x = na->offset_x;
				s.offset_y = na->offset_y;
				s.edge = na->edge;
			}
		} else {
			if (auto* fa = dynamic_cast<const FreeAnchor*>(a)) {
				s.x = fa->x;
				s.y = fa->y;
			}
		}
		return s;
	}

	std::unique_ptr<Anchor> DeleteNodeCommand::restoreAnchor(const NodeLinkSnapshot::AnchorSnap& s) {
		if (s.type == NodeLinkSnapshot::AnchorType::Node) {
			return std::make_unique<NodeAnchor>(s.node_id, s.offset_x, s.offset_y, s.edge);
		} else {
			return std::make_unique<FreeAnchor>(s.x, s.y);
		}
	}

	void DeleteNodeCommand::execute(WarRoomModel& model) {
		// 首次执行时捕获节点快照（包含子节点列表、颜色、位置、模组数据等）
		// 以及与该节点关联的所有连线快照
		if (!captured_) {
			const WarNode* node = model.getNode(nodeId_);
			if (!node) return; // 节点不存在，无法删除
			savedNode_ = *node;
			parentId_ = node->parent_id;

			// 记录该节点在父节点 children_ids 中的位置
			index_ = -1;
			const WarNode* parent = model.getNode(parentId_);
			if (parent) {
				for (size_t i = 0; i < parent->children_ids.size(); ++i) {
					if (parent->children_ids[i] == nodeId_) {
						index_ = static_cast<int>(i);
						break;
					}
				}
			}

			// 捕获与该节点关联的所有连线快照（removeNode 会删除这些连线）
			auto allLinks = model.getLinksForNode(nodeId_);
			for (const auto& linkId : allLinks) {
				const WarLink* link = model.getLink(linkId);
				if (!link) continue;
				NodeLinkSnapshot snap;
				snap.linkId = linkId;
				snap.startAnchor = captureAnchor(link->start_anchor.get());
				snap.endAnchor = captureAnchor(link->end_anchor.get());
				for (const auto& wp : link->waypoints) {
					snap.waypoints.push_back(captureAnchor(wp.get()));
				}
				snap.type = link->type;
				snap.label = link->label;
				snap.color = link->color;
				savedLinks_.push_back(std::move(snap));
			}

			captured_ = true;
		}

		// 删除节点：不把它的子节点提升到根节点，保持结构一致
		model.removeNode(nodeId_, false);
	}

	void DeleteNodeCommand::undo(WarRoomModel& model) {
		if (!captured_) return;

		// 1. 恢复节点（使用快照的完整数据，包括子节点列表、颜色、位置等）
		WarNode copy = savedNode_;
		model.addNode(std::move(copy), parentId_, index_);

		// 2. 恢复与该节点关联的所有连线
		for (const auto& linkSnap : savedLinks_) {
			// 检查节点引用是否都存在：只有当两端节点（对于 NodeAnchor）都存在时才恢复
			// 实际上，我们是在删除节点的 undo，节点已经恢复。但有些连线可能连接到
			// 其他节点（外部节点），这些节点应该仍然存在。
			// 安全检查：如果两端的节点锚点对应的节点不存在，就不恢复这条连线
			bool nodesValid = true;
			if (linkSnap.startAnchor.type == NodeLinkSnapshot::AnchorType::Node) {
				if (!model.getNode(linkSnap.startAnchor.node_id)) nodesValid = false;
			}
			if (linkSnap.endAnchor.type == NodeLinkSnapshot::AnchorType::Node) {
				if (!model.getNode(linkSnap.endAnchor.node_id)) nodesValid = false;
			}
			for (const auto& wp : linkSnap.waypoints) {
				if (wp.type == NodeLinkSnapshot::AnchorType::Node) {
					if (!model.getNode(wp.node_id)) {
						nodesValid = false;
						break;
					}
				}
			}

			if (!nodesValid) continue;

			WarLink link;
			link.id = linkSnap.linkId;
			link.start_anchor = restoreAnchor(linkSnap.startAnchor);
			link.end_anchor = restoreAnchor(linkSnap.endAnchor);
			link.type = linkSnap.type;
			link.label = linkSnap.label;
			link.color = linkSnap.color;
			for (const auto& wp : linkSnap.waypoints) {
				link.waypoints.push_back(restoreAnchor(wp));
			}
			model.addLink(std::move(link));
		}
	}

	std::string DeleteNodeCommand::description() const {
		return "删除节点: " + savedNode_.title;
	}

} // namespace warroom
