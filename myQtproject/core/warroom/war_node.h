// war_node.h
#pragma once
#include "warroom_types.h"

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
        std::vector<std::string> tags;  // 如 "未探索"、"进行中"、"已验证"、"失败"
        int priority = 0;               // 0-10

        //长宽（包围盒属性）
        float width=160;
        float height=60;

        //颜色
        Color color;//十六进制颜色
        
        // ---- 外观覆写（optional 表示未显式设定，走继承） ----
        std::optional<Color> explicit_color;
        std::optional<float> explicit_size;

        // ---- 空间位置 ----
        float pos_x = 0.0f;
        float pos_y = 0.0f;

        // ---- 分组专属 ----
        bool is_collapsed = false;
        GroupDisplayMode collapsed_display = GroupDisplayMode::CountBadge;

        // ---- 工具节点专属 ----
        std::string tool_category;
        std::string tool_summary;

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