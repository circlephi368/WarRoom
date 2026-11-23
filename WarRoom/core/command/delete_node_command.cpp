#include "core/command/delete_node_command.h"
#include "core/warroom/war_room_model.h"
#include <functional>
#include <unordered_set>

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
		// 首次执行时捕获整棵子树快照（所有后代节点 + 子树关联的所有连线）
		if (!captured_) {
			const WarNode* root = model.getNode(nodeId_);
			if (!root) return;
			parentId_ = root->parent_id;

			// 记录根节点在原父节点 children_ids 中的位置
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

			// 先根遍历捕获子树：每个节点记录其原父节点与在父节点中的序号
			// 注意根节点的 parentId_/index_ 已单独记录，子节点的 parentId 是其真实父节点
			std::function<void(const Uuid&, const Uuid&, int)> captureNode =
				[&](const Uuid& nid, const Uuid& pid, int idx) {
				const WarNode* n = model.getNode(nid);
				if (!n) return;

				SubtreeNodeSnapshot snap;
				snap.id = nid;
				snap.node = *n;
				snap.parentId = pid;
				snap.index = idx;
				subtree_.push_back(std::move(snap));

				for (size_t i = 0; i < n->children_ids.size(); ++i) {
					captureNode(n->children_ids[i], nid, static_cast<int>(i));
				}
			};
			captureNode(nodeId_, parentId_, index_);

			// 捕获子树中任意节点关联的所有连线（去重）
			std::unordered_set<Uuid> seenLinks;
			for (const auto& s : subtree_) {
				auto links = model.getLinksForNode(s.id);
				for (const auto& linkId : links) {
					if (!seenLinks.insert(linkId).second) continue;
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
			}

			captured_ = true;
		}

		// 删除整棵子树（removeNode(false) 会级联删除所有后代节点及其连线）
		model.removeNode(nodeId_, false);
	}

	void DeleteNodeCommand::undo(WarRoomModel& model) {
		if (!captured_) return;

		// 按先根后子的顺序恢复节点：父节点先存在，子节点 addNode 时才能正确挂载
		// addNode 会重算 rel_x/rel_y 并把节点登记到父节点的 children_ids 中
		for (const auto& s : subtree_) {
			WarNode copy = s.node;
			model.addNode(std::move(copy), s.parentId, s.index);
		}

		// 恢复与子树关联的连线：只有当所有 NodeAnchor 引用的节点都存在时才恢复
		for (const auto& linkSnap : savedLinks_) {
			bool nodesValid = true;
			auto checkNode = [&](const NodeLinkSnapshot::AnchorSnap& a) {
				if (a.type == NodeLinkSnapshot::AnchorType::Node
					&& !model.getNode(a.node_id)) {
					nodesValid = false;
				}
			};
			checkNode(linkSnap.startAnchor);
			checkNode(linkSnap.endAnchor);
			for (const auto& wp : linkSnap.waypoints) checkNode(wp);
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
		if (!subtree_.empty()) {
			return "删除节点: " + subtree_.front().node.title;
		}
		return "删除节点";
	}

} // namespace warroom
