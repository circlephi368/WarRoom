// war_room_model_serialization.cpp
#include "war_room_model_serialization.h"
#include "war_room_model.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace warroom {

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