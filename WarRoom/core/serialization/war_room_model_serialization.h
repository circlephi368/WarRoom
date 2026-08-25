// war_room_model_serialization.h
#pragma once

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <string>
#include <memory>

#include "core/warroom/war_room_model.h"
#include "core/serialization/enum_serialization.h"
#include "nlohmann/json.hpp"
#include <chrono>

namespace warroom {

	// ---- 时间戳 ISO 8601 转换 ----
	std::string timestampToIsoString(const Timestamp& tp);
	Timestamp isoStringToTimestamp(const std::string& iso);

	// ---- Point2D 序列化 ----
	nlohmann::json toJson(const Point2D& p);
	Point2D fromJsonPoint(const nlohmann::json& j);

	// ---- Rect 序列化 ----
	nlohmann::json toJson(const Rect& r);
	Rect fromJsonRect(const nlohmann::json& j);

	// ---- 锚点多态序列化 ----
	nlohmann::json anchorToJson(const Anchor& anchor);
	std::unique_ptr<Anchor> anchorFromJson(const nlohmann::json& j);

	// ---- WarNode 序列化 ----
	nlohmann::json toJson(const WarNode& node);
	WarNode fromJsonWarNode(const nlohmann::json& j);

	// ---- WarLink 序列化 ----
	nlohmann::json toJson(const WarLink& link);
	WarLink fromJsonWarLink(const nlohmann::json& j);

	// ---- WarZone 序列化 ----
	nlohmann::json toJson(const WarZone& zone);
	WarZone fromJsonWarZone(const nlohmann::json& j);

	// ---- ScoutAction 序列化 ----
	nlohmann::json toJson(const ScoutAction& action);
	ScoutAction fromJsonScoutAction(const nlohmann::json& j);

	// ---- TimelineEntry 序列化 ----
	nlohmann::json toJson(const TimelineEntry& entry);
	TimelineEntry fromJsonTimelineEntry(const nlohmann::json& j);

} // namespace warroom