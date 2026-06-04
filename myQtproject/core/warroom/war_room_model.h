// war_room_model.h
#pragma once
#include <fstream>
#include "warroom_types.h"
#include "war_node.h"
#include "war_link.h"
#include "war_zone.h"
#include "scout_action.h"
#include "nlohmann/json.hpp"

namespace warroom {

	// 时间轴条目
	struct TimelineEntry {
		Timestamp timestamp;
		std::string description;
	};

	class WarRoomModel {
	public:
		// ---- 构造 ----
		WarRoomModel();

		// ---- 文档根 ----
		Uuid getDocumentRootId() const { return document_root_id_; }

		// ---- 节点操作 ----
		const WarNode* getNode(Uuid id) const;
		WarNode* getNodeMutable(Uuid id);
		Uuid addNode(WarNode node, Uuid parent_id, int index = -1);
		bool removeNode(Uuid id);  // 级联删除子树和关联连线
		void setNodeParent(Uuid node_id, Uuid new_parent_id, int index = -1);
		const std::unordered_map<Uuid, WarNode>& getAllNodes() const { return nodes_; }
		void setNodeColor(Uuid node_id, Color color);
		void setNodesize(Uuid node_id, float width, float height);
		void rebuildRelativeCoordinates();
		void updateAbsolutePositionRecursive(Uuid node_id);
		void updateAbsolutePosition(Uuid node_id);  // 更新单个节点（基于父节点）

		// ---- 查询 ----
		std::vector<Uuid> getChildren(Uuid parent_id) const;
		std::vector<Uuid> getTopLevelNodes() const;  // 文档根的直接子节点
		Color getEffectiveColor(Uuid node_id) const;
		float getNodewidth(Uuid id) const;
		float getNodeheight(Uuid id) const;
		Color getNodeColor(Uuid id)const;
		int computeAbsoluteZ(Uuid node_id) const;  // 计算绝对Z值

		// ---- 连线操作 ----
		const WarLink* getLink(Uuid id) const;
		Uuid addLink(WarLink link);
		bool removeLink(Uuid id);
		std::vector<Uuid> getLinksForNode(Uuid node_id) const;
		const std::unordered_map<Uuid, WarLink>& getAllLinks() const { return links_; }

		// ---- 战区操作 ----
		const WarZone* getZone(Uuid id) const;
		Uuid addZone(WarZone zone);
		bool removeZone(Uuid id);
		void addNodeToZone(Uuid node_id, Uuid zone_id);
		void removeNodeFromZone(Uuid node_id, Uuid zone_id);

		// ---- 侦察记录 ----
		void addScoutAction(ScoutAction action);
		const std::vector<ScoutAction>& getScoutLog() const { return scout_log_; }

		// ---- 时间轴 ----
		void addTimelineEntry(std::string description);
		const std::vector<TimelineEntry>& getTimeline() const { return timeline_; }

		// ---- 视图状态 ----
		Point2D camera_position;
		float zoom_level = 1.0f;
		// ---- 序列化 ----
		nlohmann::json toJson() const;
		bool fromJson(const nlohmann::json& j);
		bool saveToFile(const std::string& filepath) const;
		bool loadFromFile(const std::string& filepath);

		// 获取视图状态（用于保存/恢复）
		void setCameraView(const Point2D& pos, float zoom) {
			camera_position = pos;
			zoom_level = zoom;
		}

		void getCameraView(Point2D& pos, float& zoom) const {
			pos = camera_position;
			zoom = zoom_level;
		}
	private:
		Uuid document_root_id_;
		std::unordered_map<Uuid, WarNode> nodes_;
		std::unordered_map<Uuid, WarLink> links_;
		std::unordered_map<Uuid, WarZone> zones_;
		std::vector<ScoutAction> scout_log_;
		std::vector<TimelineEntry> timeline_;

		// 索引：节点 -> 连线
		std::unordered_multimap<Uuid, Uuid> links_by_node_;

		// 内部辅助
		void collectSubtreeIds(Uuid node_id, std::unordered_set<Uuid>& out_ids) const;
	};

} // namespace warroom