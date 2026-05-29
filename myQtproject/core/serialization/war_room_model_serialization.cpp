// war_room_model_serialization.cpp
#include "core/serialization/war_room_model_serialization.h"
#include "core/warroom/war_room_model.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace warroom {

    // ---- 时间戳 ISO 8601 转换 ----
    std::string timestampToIsoString(const Timestamp& tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z";
        return ss.str();
    }

    Timestamp isoStringToTimestamp(const std::string& iso) {
        std::tm tm = {};
        int ms = 0;
        std::stringstream ss(iso);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (ss.fail()) {
            // 尝试解析不带毫秒的格式
            ss.clear();
            ss.str(iso);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        }
        else {
            char dot;
            if (ss >> dot && dot == '.') {
                ss >> ms;
            }
        }
        auto time_t = std::mktime(&tm);
        return std::chrono::system_clock::from_time_t(time_t) +
            std::chrono::milliseconds(ms);
    }

    // ---- Point2D 序列化 ----
    nlohmann::json toJson(const Point2D& p) {
        return { {"x", p.x}, {"y", p.y} };
    }

    Point2D fromJsonPoint(const nlohmann::json& j) {
        return { j.value("x", 0.0f), j.value("y", 0.0f) };
    }

    // ---- Rect 序列化 ----
    nlohmann::json toJson(const Rect& r) {
        return { {"x", r.x}, {"y", r.y}, {"width", r.width}, {"height", r.height} };
    }

    Rect fromJsonRect(const nlohmann::json& j) {
        return { j.value("x", 0.0f), j.value("y", 0.0f),
                j.value("width", 0.0f), j.value("height", 0.0f) };
    }

    // ---- 锚点多态序列化 ----
    nlohmann::json anchorToJson(const Anchor& anchor) {
        nlohmann::json j;
        j["type"] = anchorTypeToString(anchor.anchor_type);

        if (auto* na = dynamic_cast<const NodeAnchor*>(&anchor)) {
            j["node_id"] = na->node_id;
            j["offset_x"] = na->offset_x;
            j["offset_y"] = na->offset_y;
        }
        else if (auto* fa = dynamic_cast<const FreeAnchor*>(&anchor)) {
            j["x"] = fa->x;
            j["y"] = fa->y;
        }
        return j;
    }

    std::unique_ptr<Anchor> anchorFromJson(const nlohmann::json& j)
    {
        if (j.is_null() || !j.contains("type")) {
            // 返回默认的 NodeAnchor
            auto anchor = std::make_unique<NodeAnchor>();
            anchor->anchor_type = AnchorType::Node;  // 关键：设置类型
            anchor->node_id = "";
            anchor->offset_x = 0.0f;
            anchor->offset_y = 0.0f;
            return anchor;
        }

        std::string type = j.value("type", "node");
        if (type == "free") {
            auto anchor = std::make_unique<FreeAnchor>();
            anchor->anchor_type = AnchorType::Free;  // 关键：设置类型
            anchor->x = j.value("x", 0.0f);
            anchor->y = j.value("y", 0.0f);
            return anchor;
        }
        else {  // node type
            auto anchor = std::make_unique<NodeAnchor>();
            anchor->anchor_type = AnchorType::Node;  // 关键：设置类型
            anchor->node_id = j.value("node_id", "");
            anchor->offset_x = j.value("offset_x", 0.0f);
            anchor->offset_y = j.value("offset_y", 0.0f);
            return anchor;
        }
    }

    // ---- WarNode 序列化 ----
    nlohmann::json toJson(const WarNode& node) {
        nlohmann::json j;
        j["id"] = node.id;
        j["parent_id"] = node.parent_id;

        // 转为有序列表保证稳定性
        nlohmann::json children = nlohmann::json::array();
        for (const auto& childId : node.children_ids) {
            children.push_back(childId);
        }
        j["children_ids"] = children;

        j["kind"] = nodeKindToString(node.kind);
        j["title"] = node.title;
        j["full_text"] = node.full_text;

        nlohmann::json tags = nlohmann::json::array();
        for (const auto& tag : node.tags) {
            tags.push_back(tag);
        }
        j["tags"] = tags;
        j["priority"] = node.priority;

        if (node.explicit_color.has_value()) {
            j["explicit_color"] = node.explicit_color.value();
        }
        if (node.explicit_size.has_value()) {
            j["explicit_size"] = node.explicit_size.value();
        }

        j["pos_x"] = node.pos_x;
        j["pos_y"] = node.pos_y;
        j["is_collapsed"] = node.is_collapsed;
        j["collapsed_display"] = groupDisplayModeToString(node.collapsed_display);
        j["tool_category"] = node.tool_category;
        j["tool_summary"] = node.tool_summary;

        return j;
    }

    WarNode fromJsonWarNode(const nlohmann::json& j) {
        WarNode node;
        node.id = j.value("id", "");
        node.parent_id = j.value("parent_id", "");

        if (j.contains("children_ids")) {
            for (const auto& child : j["children_ids"]) {
                node.children_ids.push_back(child.get<std::string>());
            }
        }

        node.kind = nodeKindFromString(j.value("kind", "leaf"));
        node.title = j.value("title", "");
        node.full_text = j.value("full_text", "");

        if (j.contains("tags")) {
            for (const auto& tag : j["tags"]) {
                node.tags.push_back(tag.get<std::string>());
            }
        }

        node.priority = j.value("priority", 0);

        if (j.contains("explicit_color")) {
            node.explicit_color = j["explicit_color"].get<std::string>();
        }
        if (j.contains("explicit_size")) {
            node.explicit_size = j["explicit_size"].get<float>();
        }

        node.pos_x = j.value("pos_x", 0.0f);
        node.pos_y = j.value("pos_y", 0.0f);
        node.is_collapsed = j.value("is_collapsed", false);
        node.collapsed_display = groupDisplayModeFromString(j.value("collapsed_display", "count_badge"));
        node.tool_category = j.value("tool_category", "");
        node.tool_summary = j.value("tool_summary", "");

        return node;
    }

    // ---- WarLink 序列化 ----
    nlohmann::json toJson(const WarLink& link) {
        nlohmann::json j;
        j["id"] = link.id;
        j["start_anchor"] = anchorToJson(*link.start_anchor);
        j["end_anchor"] = anchorToJson(*link.end_anchor);

        nlohmann::json waypoints = nlohmann::json::array();
        for (const auto& wp : link.waypoints) {
            waypoints.push_back(anchorToJson(*wp));
        }
        j["waypoints"] = waypoints;

        j["type"] = linkTypeToString(link.type);
        j["label"] = link.label;
        j["color"] = link.color;

        return j;
    }

    WarLink fromJsonWarLink(const nlohmann::json& j) {
        WarLink link;
        link.id = j.value("id", "");
        link.start_anchor = anchorFromJson(j["start_anchor"]);
        link.end_anchor = anchorFromJson(j["end_anchor"]);

        if (j.contains("waypoints")) {
            for (const auto& wp : j["waypoints"]) {
                link.waypoints.push_back(anchorFromJson(wp));
            }
        }

        link.type = linkTypeFromString(j.value("type", "dependency"));
        link.label = j.value("label", "");
        link.color = j.value("color", kDefaultLinkColor);

        return link;
    }

    // ---- WarZone 序列化 ----
    nlohmann::json toJson(const WarZone& zone) {
        nlohmann::json j;
        j["id"] = zone.id;
        j["name"] = zone.name;
        j["background_color"] = zone.background_color;
        j["border_color"] = zone.border_color;
        j["boundary"] = toJson(zone.boundary);

        nlohmann::json members = nlohmann::json::array();
        for (const auto& member : zone.member_ids) {
            members.push_back(member);
        }
        j["member_ids"] = members;
        j["collapsed"] = zone.collapsed;

        return j;
    }

    WarZone fromJsonWarZone(const nlohmann::json& j) {
        WarZone zone;
        zone.id = j.value("id", "");
        zone.name = j.value("name", "");
        zone.background_color = j.value("background_color", "#ffffff00");
        zone.border_color = j.value("border_color", "#cccccc");
        zone.boundary = fromJsonRect(j["boundary"]);

        if (j.contains("member_ids")) {
            for (const auto& member : j["member_ids"]) {
                zone.member_ids.push_back(member.get<std::string>());
            }
        }

        zone.collapsed = j.value("collapsed", false);
        return zone;
    }

    // ---- ScoutAction 序列化 ----
    nlohmann::json toJson(const ScoutAction& action) {
        nlohmann::json j;
        j["id"] = action.id;
        j["timestamp"] = timestampToIsoString(action.timestamp);
        j["related_node_id"] = action.related_node_id;
        j["hypothesis"] = action.hypothesis;
        j["action_desc"] = action.action_desc;
        j["result"] = scoutResultToString(action.result);
        j["failure_reason"] = action.failure_reason;
        return j;
    }

    ScoutAction fromJsonScoutAction(const nlohmann::json& j) {
        ScoutAction action;
        action.id = j.value("id", "");
        action.timestamp = isoStringToTimestamp(j.value("timestamp", ""));
        action.related_node_id = j.value("related_node_id", "");
        action.hypothesis = j.value("hypothesis", "");
        action.action_desc = j.value("action_desc", "");
        action.result = scoutResultFromString(j.value("result", "success"));
        action.failure_reason = j.value("failure_reason", "");
        return action;
    }

    // ---- TimelineEntry 序列化 ----
    nlohmann::json toJson(const TimelineEntry& entry) {
        return { {"timestamp", timestampToIsoString(entry.timestamp)},
                {"description", entry.description} };
    }

    TimelineEntry fromJsonTimelineEntry(const nlohmann::json& j) {
        return { isoStringToTimestamp(j.value("timestamp", "")),
                j.value("description", "") };
    }

    // ---- 主序列化方法 ----
    nlohmann::json WarRoomModel::toJson() const {
        nlohmann::json j;

        // 文档根节点 ID
        j["document_root_id"] = document_root_id_;

        // 节点 - 直接遍历
        nlohmann::json nodes = nlohmann::json::array();
        for (const auto& pair : nodes_) {
            nodes.push_back(::warroom::toJson(pair.second));
        }
        j["nodes"] = nodes;

        // 连线 - 直接遍历（避免拷贝不可拷贝类型）
        nlohmann::json links = nlohmann::json::array();
        for (const auto& pair : links_) {
            links.push_back(::warroom::toJson(pair.second));
        }
        j["links"] = links;

        // 战区 - 直接遍历
        nlohmann::json zones = nlohmann::json::array();
        for (const auto& pair : zones_) {
            zones.push_back(::warroom::toJson(pair.second));
        }
        j["zones"] = zones;

        // 侦察日志
        nlohmann::json scoutLog = nlohmann::json::array();
        for (const auto& action : scout_log_) {
            scoutLog.push_back(::warroom::toJson(action));
        }
        j["scout_log"] = scoutLog;

        // 时间轴
        nlohmann::json timeline = nlohmann::json::array();
        for (const auto& entry : timeline_) {
            timeline.push_back(::warroom::toJson(entry));
        }
        j["timeline"] = timeline;

        // 视图状态
        j["camera_position"] = ::warroom::toJson(camera_position);
        j["zoom_level"] = zoom_level;

        return j;
    }

    // ---- 主反序列化方法 ----
    bool WarRoomModel::fromJson(const nlohmann::json& j) {
        try {
            // 清空现有数据
            nodes_.clear();
            links_.clear();
            zones_.clear();
            scout_log_.clear();
            timeline_.clear();
            links_by_node_.clear();

            // 文档根
            document_root_id_ = j.value("document_root_id", generateUuid());

            // 节点
            if (j.contains("nodes") && j["nodes"].is_array()) {
                for (const auto& nodeJson : j["nodes"]) {
                    WarNode node = fromJsonWarNode(nodeJson);
                    if (!node.id.empty()) {
                        nodes_[node.id] = std::move(node);
                    }
                }
            }

            // 确保文档根节点存在
            if (nodes_.find(document_root_id_) == nodes_.end()) {
                WarNode root;
                root.id = document_root_id_;
                root.kind = NodeKind::Group;
                root.title = "Document Root";
                nodes_[document_root_id_] = std::move(root);
            }

            // 连线
            if (j.contains("links") && j["links"].is_array()) {
                for (const auto& linkJson : j["links"]) {
                    WarLink link = fromJsonWarLink(linkJson);
                    if (!link.id.empty()) {
                        std::string linkId = link.id;
                        links_[linkId] = std::move(link);
                        // 重建索引
                        const auto& linkRef = links_[linkId];
                        if (auto* na = dynamic_cast<const NodeAnchor*>(linkRef.start_anchor.get())) {
                            links_by_node_.emplace(na->node_id, link.id);
                        }
                        if (auto* na = dynamic_cast<const NodeAnchor*>(linkRef.end_anchor.get())) {
                            links_by_node_.emplace(na->node_id, link.id);
                        }
                    }
                }
            }

            // 战区
            if (j.contains("zones") && j["zones"].is_array()) {
                for (const auto& zoneJson : j["zones"]) {
                    WarZone zone = fromJsonWarZone(zoneJson);
                    if (!zone.id.empty()) {
                        zones_[zone.id] = std::move(zone);
                    }
                }
            }

            // 侦察日志
            if (j.contains("scout_log") && j["scout_log"].is_array()) {
                for (const auto& actionJson : j["scout_log"]) {
                    scout_log_.push_back(fromJsonScoutAction(actionJson));
                }
            }

            // 时间轴
            if (j.contains("timeline") && j["timeline"].is_array()) {
                for (const auto& entryJson : j["timeline"]) {
                    timeline_.push_back(fromJsonTimelineEntry(entryJson));
                }
            }

            // 视图状态
            if (j.contains("camera_position")) {
                camera_position = fromJsonPoint(j["camera_position"]);
            }
            zoom_level = j.value("zoom_level", 1.0f);

            return true;
        }
        catch (const std::exception& e) {
            // 记录错误日志
            return false;
        }
    }

    // 便捷方法：保存到文件
    bool WarRoomModel::saveToFile(const std::string& filepath) const {
        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << toJson().dump(2);  // 缩进2空格
        return file.good();
    }

    // 便捷方法：从文件加载
    bool WarRoomModel::loadFromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;
        nlohmann::json j;
        file >> j;
        return fromJson(j);
    }

} // namespace warroom