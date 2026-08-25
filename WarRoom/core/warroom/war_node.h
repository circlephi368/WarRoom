// war_node.h
#pragma once
#include "warroom_types.h"
#include <nlohmann/json.hpp>

namespace warroom {

	enum class NodeKind {
		Leaf,   // 普通叶节点
		Group,  // 分组节点（可收纳子节点、可折叠）
		Tool    // 工具节点（D2需求预留）
	};

	enum class GroupDisplayMode {
		MiniIcon,   // 折叠后显示迷你图标
		CountBadge, // 折叠后显示子节点计数
		ColorBlock  // 折叠后显示缩略色块
	};

	// 待办状态
	enum class TodoState {
		None = 0,    // 未启用待办
		Pending = 1, // 待办中（未完成）
		Done = 2     // 已完成
	};

	struct WarNode {
		// ---- 身份与树结构 ----
		Uuid id;
		Uuid parent_id;                 // 空字符串表示挂载在文档根下
		std::vector<Uuid> children_ids; // 有序子节点列表

		// ---- 类型 ----
		NodeKind kind = NodeKind::Leaf;

		// ---- 通用属性 ----
		std::string title;
		std::string full_text;          // 长文本，支持 Markdown
		std::string text_display_mode = "markdown";  // "markdown" 或 "plain"，决定预览渲染方式
		std::vector<std::string> tags;  // 如 "未探索"、"进行中"、"已验证"、"失败"
		int priority = 0;               // 0-10

		int relative_z = 1;

		//长宽（包围盒属性）
		float width=160;
		float height=60;

		//颜色
		Color color= kDefaultNodeColor;//十六进制颜色
		
		// ---- 外观覆写（optional 表示未显式设定，走继承） ----
		std::optional<Color> explicit_color;
		std::optional<float> explicit_size;

		// ---- 空间位置 ----
		float pos_x = 0.0f;
		float pos_y = 0.0f;
		//相对坐标
		float rel_x = 0.0f;   // 相对于父节点的 X
		float rel_y = 0.0f;   // 相对于父节点的 Y

		// ---- 分组专属 ----
		bool is_collapsed = false;
		GroupDisplayMode collapsed_display = GroupDisplayMode::CountBadge;

		// ---- 工具节点专属 ----
		std::string tool_category;
		std::string tool_summary;

		// ---- 待办 ----
		TodoState todo_state = TodoState::None;
		Timestamp todo_created_at;  // 标记为待办的时间，用于排序

		// ---- 节点模组（多模态扩展） ----
		// 主模组类型 ID（空字符串表示无主模组，节点行为同原版纯文本节点）。
		// 一个节点最多绑定一个主模组，主模组负责节点的主要外观/交互。
		std::string primary_mod_type;
		// 主模组的可序列化数据（由对应 NodeMod::serialize / deserialize 维护）
		nlohmann::json primary_mod_data;

		// 辅助模组：可叠加多个，仅做附加渲染/交互（如徽章、状态指示等）
		std::vector<std::string> auxiliary_mod_types;
		// 每个辅助模组对应的可序列化数据，键为模组 id
		std::unordered_map<std::string, nlohmann::json> auxiliary_mod_data;

		// 便捷方法
		void setRelativeZ(int z) { relative_z = z; }
		int getRelativeZ() const { return relative_z; }

		// 便捷工厂方法
		static WarNode makeLeaf(const std::string& title, float x = 0, float y = 0) {
			WarNode node;
			node.id = generateUuid();
			node.kind = NodeKind::Leaf;
			node.title = title;
			node.pos_x = x;
			node.pos_y = y;
			return node;
		}

		static WarNode makeGroup(const std::string& title, float x = 0, float y = 0) {
			WarNode node;
			node.id = generateUuid();
			node.kind = NodeKind::Group;
			node.title = title;
			node.pos_x = x;
			node.pos_y = y;
			return node;
		}
	};

} // namespace warroom