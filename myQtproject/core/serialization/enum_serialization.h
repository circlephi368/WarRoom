// enum_serialization.h
#pragma once
#include <string>
#include <unordered_map>
#include "core/warroom/war_link.h"
#include "core/warroom/war_node.h"
#include "core/warroom/scout_action.h"

namespace warroom {

    // ---- NodeKind 序列化 ----
    inline std::string nodeKindToString(NodeKind kind) {
        switch (kind) {
        case NodeKind::Leaf:   return "leaf";
        case NodeKind::Group:  return "group";
        case NodeKind::Tool:   return "tool";
        }
        return "leaf";
    }

    inline NodeKind nodeKindFromString(const std::string& str) {
        static const std::unordered_map<std::string, NodeKind> map = {
            {"leaf", NodeKind::Leaf},
            {"group", NodeKind::Group},
            {"tool", NodeKind::Tool}
        };
        auto it = map.find(str);
        return (it != map.end()) ? it->second : NodeKind::Leaf;
    }

    // ---- GroupDisplayMode 序列化 ----
    inline std::string groupDisplayModeToString(GroupDisplayMode mode) {
        switch (mode) {
        case GroupDisplayMode::MiniIcon:   return "mini_icon";
        case GroupDisplayMode::CountBadge: return "count_badge";
        case GroupDisplayMode::ColorBlock: return "color_block";
        }
        return "count_badge";
    }

    inline GroupDisplayMode groupDisplayModeFromString(const std::string& str) {
        static const std::unordered_map<std::string, GroupDisplayMode> map = {
            {"mini_icon", GroupDisplayMode::MiniIcon},
            {"count_badge", GroupDisplayMode::CountBadge},
            {"color_block", GroupDisplayMode::ColorBlock}
        };
        auto it = map.find(str);
        return (it != map.end()) ? it->second : GroupDisplayMode::CountBadge;
    }

    // ---- LinkType 序列化 ----
    inline std::string linkTypeToString(LinkType type) {
        switch (type) {
        case LinkType::Dependency:     return "dependency";
        case LinkType::Contradiction:  return "contradiction";
        case LinkType::Transformation: return "transformation";
        case LinkType::Inspiration:    return "inspiration";
        case LinkType::Negation:       return "negation";
        case LinkType::UsingMethod:    return "using_method";
        }
        return "dependency";
    }

    inline LinkType linkTypeFromString(const std::string& str) {
        static const std::unordered_map<std::string, LinkType> map = {
            {"dependency", LinkType::Dependency},
            {"contradiction", LinkType::Contradiction},
            {"transformation", LinkType::Transformation},
            {"inspiration", LinkType::Inspiration},
            {"negation", LinkType::Negation},
            {"using_method", LinkType::UsingMethod}
        };
        auto it = map.find(str);
        return (it != map.end()) ? it->second : LinkType::Dependency;
    }

    // ---- AnchorType 序列化 ----
    inline std::string anchorTypeToString(AnchorType type) {
        switch (type) {
        case AnchorType::Node: return "node";
        case AnchorType::Free: return "free";
        }
        return "node";
    }

    inline AnchorType anchorTypeFromString(const std::string& str) {
        return (str == "free") ? AnchorType::Free : AnchorType::Node;
    }

    // ---- ScoutResult 序列化 ----
    inline std::string scoutResultToString(ScoutResult result) {
        switch (result) {
        case ScoutResult::Success:        return "success";
        case ScoutResult::PartialSuccess: return "partial_success";
        case ScoutResult::Failure:        return "failure";
        }
        return "success";
    }

    inline ScoutResult scoutResultFromString(const std::string& str) {
        static const std::unordered_map<std::string, ScoutResult> map = {
            {"success", ScoutResult::Success},
            {"partial_success", ScoutResult::PartialSuccess},
            {"failure", ScoutResult::Failure}
        };
        auto it = map.find(str);
        return (it != map.end()) ? it->second : ScoutResult::Success;
    }

    // ---- TodoState 序列化 ----
    inline std::string todoStateToString(TodoState state) {
        switch (state) {
        case TodoState::None:    return "none";
        case TodoState::Pending: return "pending";
        case TodoState::Done:    return "done";
        }
        return "none";
    }

    inline TodoState todoStateFromString(const std::string& str) {
        static const std::unordered_map<std::string, TodoState> map = {
            {"none", TodoState::None},
            {"pending", TodoState::Pending},
            {"done", TodoState::Done}
        };
        auto it = map.find(str);
        return (it != map.end()) ? it->second : TodoState::None;
    }

} // namespace warroom