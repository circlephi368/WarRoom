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
		bool removeNode(Uuid id, bool reparentChildren = true);  // 级联删除子树和关联连线
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

		// ---- 全局最大 Z 值 ----
		// 维护当前所有节点中最大的 absolute_z（用于置顶操作，避免全树遍历）
		// 每次 normalizeZValues 后等于节点总数（压缩后的最大 rank）
		int getMaxAbsZ() const { return g_max_abs_z_; }
		void setMaxAbsZ(int z) { g_max_abs_z_ = z; }

		// ---- Z 值归一化（排名压缩）----
		// 按当前 absolute_z 排序后，用排名（从1开始）重新分配，
		// 然后反向推出新的 relative_z，保证所有 relative_z 为正整数且不改变视觉层级。
		// 归一化后 g_max_abs_z_ = 节点总数。
		void normalizeZValues();

		// ---- 连线操作 ----
		const WarLink* getLink(Uuid id) const;
		WarLink* getLinkMutable(Uuid id);
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

		// ---- 只读模式 ----
		bool isReadOnly() const { return is_read_only_; }
		void setReadOnly(bool readOnly) { is_read_only_ = readOnly; }

		// ---- 待办列表 ----
		// 设置节点的待办状态，自动维护 todo_list_
		void setNodeTodoState(Uuid id, TodoState state);
		TodoState getNodeTodoState(Uuid id) const;
		// 获取所有待办节点 ID（按添加时间排序）
		const std::vector<Uuid>& getTodoList() const { return todo_list_; }
		// ---- 序列化 ----
        nlohmann::json toJson() const;
        bool fromJson(const nlohmann::json& j);
        bool saveToFile(const std::string& filepath) const;
        bool loadFromFile(const std::string& filepath);

        // ---- 文件夹存档 ----
        // 把整个白板保存到 <folder> 目录：
        //   <folder>/board.json       ← 与旧 .warroom 同样的 JSON
        //   <folder>/mod_data/<modId>/ ← 模组资源（图片、视频等）
        // 若 folder 指向 .warroom 单文件（已存在），会被视为已存在的存档目录并直接写入。
        // folder 不存在时自动创建。
        bool saveToFolder(const std::string& folder) const;

        // 从一个存档目录加载：
        //   读取 <folder>/board.json
        //   告知所有主模组"基础目录 = <folder>"，使其能解析 mod_data/ 下的相对路径
        bool loadFromFolder(const std::string& folder);

        // 从任意路径加载（自动识别是 .warroom 单文件还是 .warroom/ 目录）
        bool loadFromAuto(const std::string& path);

		// 获取视图状态（用于保存/恢复）
		void setCameraView(const Point2D& pos, float zoom) {
			camera_position = pos;
			zoom_level = zoom;
		}

		void getCameraView(Point2D& pos, float& zoom) const {
			pos = camera_position;
			zoom = zoom_level;
		}

		// 计算子树中所有节点的绝对 Z 值最大值（用于置顶后更新 g_max_abs_z_）
		int computeSubtreeMaxAbsZ(Uuid node_id) const;
	private:
		Uuid document_root_id_;
		std::unordered_map<Uuid, WarNode> nodes_;
		std::unordered_map<Uuid, WarLink> links_;
		std::unordered_map<Uuid, WarZone> zones_;
		std::vector<ScoutAction> scout_log_;
		std::vector<TimelineEntry> timeline_;

		// 索引：节点 -> 连线
		std::unordered_multimap<Uuid, Uuid> links_by_node_;

		// 全局最大 absolute_z（用于置顶操作，避免每次全树遍历）
		int g_max_abs_z_ = 0;

		// 只读模式标记
		bool is_read_only_ = false;

		// 待办列表：存放所有 todo_state != None 的节点 ID
		std::vector<Uuid> todo_list_;

		// 内部辅助
		void collectSubtreeIds(Uuid node_id, std::unordered_set<Uuid>& out_ids) const;
		
	};

} // namespace warroom