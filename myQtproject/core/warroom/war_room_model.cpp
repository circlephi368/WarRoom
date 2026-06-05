// war_room_model.cpp
#include "war_room_model.h"
#include <algorithm>
#include <cassert>

namespace warroom {

	// 前置：NodeAnchor::resolvePosition 需要访问模型
	Point2D NodeAnchor::resolvePosition(const WarRoomModel& model) const {
		const WarNode* node = model.getNode(node_id);
		if (!node) return { 0, 0 };

		// 如果指定了 edge，计算对应边中点
		if (edge >= 0 && edge <= 3) {
			float w = node->width;
			float h = node->height;
			switch (edge) {
			case 0: return { node->pos_x + w,         node->pos_y + h / 2.0f };  // 右中
			case 1: return { node->pos_x + w / 2.0f,  node->pos_y + h };        // 下中
			case 2: return { node->pos_x,             node->pos_y + h / 2.0f };  // 左中
			case 3: return { node->pos_x + w / 2.0f,  node->pos_y };            // 上中
			}
		}

		// 无 edge（兼容旧数据）→ 回退到 offset 模式（左上角 + 偏移）
		return { node->pos_x + offset_x, node->pos_y + offset_y };
	}

	WarRoomModel::WarRoomModel() {
		// 创建隐式文档根节点
		WarNode root;
		root.id = generateUuid();
		root.kind = NodeKind::Group;
		root.title = "Document Root";
		root.color = kDefaultNodeColor;//根节点默认设置颜色
		root.pos_x = 0;
		root.pos_y = 0;
		root.is_collapsed = false;
		document_root_id_ = root.id;
		nodes_[root.id] = std::move(root);
	}

	const WarNode* WarRoomModel::getNode(Uuid id) const {
		auto it = nodes_.find(id);
		return (it != nodes_.end()) ? &it->second : nullptr;
	}

	WarNode* WarRoomModel::getNodeMutable(Uuid id) {
		auto it = nodes_.find(id);
		return (it != nodes_.end()) ? &it->second : nullptr;
	}

	Uuid WarRoomModel::addNode(WarNode node, Uuid parent_id, int index) {
		Uuid node_id = node.id;
		if (node_id.empty()) node_id = generateUuid();
		node.id = node_id;

		// 确保父节点存在
		if (parent_id.empty() || nodes_.find(parent_id) == nodes_.end()) {
			parent_id = document_root_id_;
		}
		node.parent_id = parent_id;

		// 初始化相对坐标：如果父节点是根节点，则 rel = abs；否则 rel = abs - parent_abs
		const WarNode* parent = getNode(parent_id);
		if (parent && parent_id != document_root_id_) {
			node.rel_x = node.pos_x - parent->pos_x;
			node.rel_y = node.pos_y - parent->pos_y;
		}
		else {
			node.rel_x = node.pos_x;
			node.rel_y = node.pos_y;
		}

		// 添加到父节点的 children_ids
		auto& parentNode = nodes_[parent_id];
		if (index < 0 || index >= static_cast<int>(parentNode.children_ids.size())) {
			parentNode.children_ids.push_back(node_id);
		}
		else {
			parentNode.children_ids.insert(parentNode.children_ids.begin() + index, node_id);
		}

		nodes_[node_id] = std::move(node);
		return node_id;
	}

	bool WarRoomModel::removeNode(Uuid id) {
		if (id == document_root_id_) return false;

		auto it = nodes_.find(id);
		if (it == nodes_.end()) return false;

		// 级联收集整个子树
		std::unordered_set<Uuid> to_remove;
		collectSubtreeIds(id, to_remove);

		// 删除关联连线
		for (Uuid node_id : to_remove) {
			auto range = links_by_node_.equal_range(node_id);
			for (auto lit = range.first; lit != range.second; ++lit) {
				links_.erase(lit->second);
			}
			links_by_node_.erase(node_id);
		}

		// 从父节点的 children_ids 中移除
		Uuid parent_id = it->second.parent_id;
		auto& siblings = nodes_[parent_id].children_ids;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());

		// 删除所有子树节点
		for (Uuid node_id : to_remove) {
			nodes_.erase(node_id);
		}

		return true;
	}

	void WarRoomModel::setNodeParent(Uuid node_id, Uuid new_parent_id, int index) {
		WarNode* node = getNodeMutable(node_id);
		if (!node) return;
		if (new_parent_id.empty() || nodes_.find(new_parent_id) == nodes_.end()) {
			new_parent_id = document_root_id_;
		}

		// 从旧父节点移除
		Uuid old_parent = node->parent_id;
		auto& old_siblings = nodes_[old_parent].children_ids;
		old_siblings.erase(std::remove(old_siblings.begin(), old_siblings.end(), node_id),
			old_siblings.end());

		// 设置新父节点
		node->parent_id = new_parent_id;
		auto& new_siblings = nodes_[new_parent_id].children_ids;
		if (index < 0 || index >= static_cast<int>(new_siblings.size())) {
			new_siblings.push_back(node_id);
		}
		else {
			new_siblings.insert(new_siblings.begin() + index, node_id);
		}
	}

	void WarRoomModel::setNodeColor(Uuid node_id, Color color){
		getNodeMutable(node_id)->color = color;
	}

	void WarRoomModel::setNodesize(Uuid node_id, float width, float height){
		getNodeMutable(node_id)->width = width;
		getNodeMutable(node_id)->height = height;
	}

	void WarRoomModel::updateAbsolutePosition(Uuid node_id) {
		WarNode* node = getNodeMutable(node_id);
		if (!node) return;

		if (node->parent_id.empty() || node->parent_id == document_root_id_) {
			// 顶层节点：绝对坐标 = 相对坐标
			node->pos_x = node->rel_x;
			node->pos_y = node->rel_y;
		}
		else {
			const WarNode* parent = getNode(node->parent_id);
			if (parent) {
				node->pos_x = parent->pos_x + node->rel_x;
				node->pos_y = parent->pos_y + node->rel_y;
			}
		}
	}
	// 从绝对坐标重建相对坐标
	void WarRoomModel::rebuildRelativeCoordinates() {
		std::function<void(const Uuid&)> rebuildRecursive = [&](const Uuid& nodeId) {
			WarNode* node = getNodeMutable(nodeId);
			if (!node) return;

			const WarNode* parent = getNode(node->parent_id);
			if (parent && node->parent_id != document_root_id_) {
				// 有父节点：相对坐标 = 绝对坐标 - 父节点绝对坐标
				node->rel_x = node->pos_x - parent->pos_x;
				node->rel_y = node->pos_y - parent->pos_y;
			}
			else {
				// 根节点或顶层节点：相对坐标 = 绝对坐标
				node->rel_x = node->pos_x;
				node->rel_y = node->pos_y;
			}

			// 递归处理子节点
			for (const auto& childId : node->children_ids) {
				rebuildRecursive(childId);
			}
			};

		rebuildRecursive(document_root_id_);
	}
	void WarRoomModel::updateAbsolutePositionRecursive(Uuid node_id) {
		updateAbsolutePosition(node_id);

		WarNode* node = getNodeMutable(node_id);
		if (!node) return;

		for (const Uuid& child_id : node->children_ids) {
			updateAbsolutePositionRecursive(child_id);
		}
	}

	std::vector<Uuid> WarRoomModel::getChildren(Uuid parent_id) const {
		const WarNode* node = getNode(parent_id);
		if (!node) return {};
		return node->children_ids;
	}

	std::vector<Uuid> WarRoomModel::getTopLevelNodes() const {
		return getChildren(document_root_id_);
	}

	Color WarRoomModel::getEffectiveColor(Uuid node_id) const {
		const WarNode* node = getNode(node_id);
		if (!node) return kDefaultNodeColor;
		if (node->explicit_color.has_value()) return node->explicit_color.value();

		// 递归向父节点查找
		if (!node->parent_id.empty()) {
			return getEffectiveColor(node->parent_id);
		}
		return kDefaultNodeColor;
	}

	float WarRoomModel::getNodewidth(Uuid id) const
	{
		return getNode(id)->width;
	}
	float WarRoomModel::getNodeheight(Uuid id) const
	{
		return getNode(id)->height;
	}

	Color WarRoomModel::getNodeColor(Uuid id) const
	{
		return getNode(id)->color;
	}

	const WarLink* WarRoomModel::getLink(Uuid id) const {
		auto it = links_.find(id);
		return (it != links_.end()) ? &it->second : nullptr;
	}

	WarLink* WarRoomModel::getLinkMutable(Uuid id) {
		auto it = links_.find(id);
		return (it != links_.end()) ? &it->second : nullptr;
	}

	Uuid WarRoomModel::addLink(WarLink link) {
		Uuid link_id = link.id;
		if (link_id.empty()) link_id = generateUuid();
		link.id = link_id;

		// 更新索引
		if (auto* na = dynamic_cast<NodeAnchor*>(link.start_anchor.get())) {
			links_by_node_.emplace(na->node_id, link_id);
		}
		if (auto* na = dynamic_cast<NodeAnchor*>(link.end_anchor.get())) {
			links_by_node_.emplace(na->node_id, link_id);
		}
		for (auto& wp : link.waypoints) {
			if (auto* na = dynamic_cast<NodeAnchor*>(wp.get())) {
				links_by_node_.emplace(na->node_id, link_id);
			}
		}

		links_[link_id] = std::move(link);
		return link_id;
	}

	bool WarRoomModel::removeLink(Uuid id) {
		auto it = links_.find(id);
		if (it == links_.end()) return false;

		// 清理索引
		const WarLink& link = it->second;

		auto removeFromIndex = [&](const std::unique_ptr<Anchor>& anchor) {
			if (auto* na = dynamic_cast<NodeAnchor*>(anchor.get())) {
				auto range = links_by_node_.equal_range(na->node_id);
				for (auto lit = range.first; lit != range.second; ++lit) {
					if (lit->second == id) {
						links_by_node_.erase(lit);
						return;
					}
				}
			}
			};

		removeFromIndex(link.start_anchor);
		removeFromIndex(link.end_anchor);
		for (const auto& wp : link.waypoints) {
			removeFromIndex(wp);
		}

		links_.erase(it);
		return true;
	}

	std::vector<Uuid> WarRoomModel::getLinksForNode(Uuid node_id) const {
		std::vector<Uuid> result;
		auto range = links_by_node_.equal_range(node_id);
		for (auto it = range.first; it != range.second; ++it) {
			result.push_back(it->second);
		}
		return result;
	}

	const WarZone* WarRoomModel::getZone(Uuid id) const {
		auto it = zones_.find(id);
		return (it != zones_.end()) ? &it->second : nullptr;
	}

	Uuid WarRoomModel::addZone(WarZone zone) {
		Uuid zone_id = zone.id.empty() ? generateUuid() : zone.id;
		zone.id = zone_id;
		zones_[zone_id] = std::move(zone);
		return zone_id;
	}

	bool WarRoomModel::removeZone(Uuid id) {
		return zones_.erase(id) > 0;
	}

	void WarRoomModel::addNodeToZone(Uuid node_id, Uuid zone_id) {
		auto it = zones_.find(zone_id);
		if (it == zones_.end()) return;
		auto& members = it->second.member_ids;
		if (std::find(members.begin(), members.end(), node_id) == members.end()) {
			members.push_back(node_id);
		}
	}

	void WarRoomModel::removeNodeFromZone(Uuid node_id, Uuid zone_id) {
		auto it = zones_.find(zone_id);
		if (it == zones_.end()) return;
		auto& members = it->second.member_ids;
		members.erase(std::remove(members.begin(), members.end(), node_id), members.end());
	}

	void WarRoomModel::addScoutAction(ScoutAction action) {
		if (action.id.empty()) action.id = generateUuid();
		scout_log_.push_back(std::move(action));
	}

	void WarRoomModel::addTimelineEntry(std::string description) {
		timeline_.push_back({ std::chrono::system_clock::now(), std::move(description) });
	}

	void WarRoomModel::collectSubtreeIds(Uuid node_id, std::unordered_set<Uuid>& out_ids) const {
		out_ids.insert(node_id);
		const WarNode* node = getNode(node_id);
		if (!node) return;
		for (const Uuid& child_id : node->children_ids) {
			collectSubtreeIds(child_id, out_ids);
		}
	}
	int WarRoomModel::computeAbsoluteZ(Uuid node_id) const {
		const WarNode* node = getNode(node_id);
		if (!node) return 0;

		int abs_z = node->relative_z;
		Uuid current_parent = node->parent_id;

		// 向上累加，直到根节点（document_root_id_ 本身不是可见节点，不计入）
		while (!current_parent.empty() && current_parent != document_root_id_) {
			const WarNode* parent = getNode(current_parent);
			if (!parent) break;
			abs_z += parent->relative_z;
			current_parent = parent->parent_id;
		}

		return abs_z;
	}
} // namespace warroom