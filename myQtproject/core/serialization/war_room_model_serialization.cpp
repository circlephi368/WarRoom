// war_room_model_serialization.cpp

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/serialization/war_room_model_serialization.h"
#include "core/warroom/war_room_model.h"
#include "mod/ModManager.h"
#include "mod/builtin/ImageMod.h"
#include "mod/builtin/VideoMod.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <codecvt>
#include <locale>
#include <iostream>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QString>

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
			j["edge"] = na->edge;
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
			anchor->edge = j.value("edge", -1);
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
		j["text_display_mode"] = node.text_display_mode;

		nlohmann::json tags = nlohmann::json::array();
		for (const auto& tag : node.tags) {
			tags.push_back(tag);
		}
		j["tags"] = tags;
		j["priority"] = node.priority;
		j["relative_z"] = node.relative_z;

		if (node.explicit_color.has_value()) {
			j["explicit_color"] = node.explicit_color.value();
		}
		if (node.explicit_size.has_value()) {
			j["explicit_size"] = node.explicit_size.value();
		}
		j["color"] = node.color;

		j["pos_x"] = node.pos_x;
		j["pos_y"] = node.pos_y;

		j["width"] = node.width;
		j["height"] = node.height;

		j["is_collapsed"] = node.is_collapsed;
		j["collapsed_display"] = groupDisplayModeToString(node.collapsed_display);
		j["tool_category"] = node.tool_category;
		j["tool_summary"] = node.tool_summary;

		// 待办字段
		j["todo_state"] = todoStateToString(node.todo_state);
		if (node.todo_state != TodoState::None) {
			j["todo_created_at"] = timestampToIsoString(node.todo_created_at);
		}

		// ---- 节点模组字段 ----
		// 主模组
		if (!node.primary_mod_type.empty()) {
			j["primary_mod_type"] = node.primary_mod_type;
			if (!node.primary_mod_data.is_null()) {
				j["primary_mod_data"] = node.primary_mod_data;
			}
		}
		// 辅助模组
		if (!node.auxiliary_mod_types.empty()) {
			nlohmann::json auxTypes = nlohmann::json::array();
			for (const auto& t : node.auxiliary_mod_types) auxTypes.push_back(t);
			j["auxiliary_mod_types"] = auxTypes;
		}
		if (!node.auxiliary_mod_data.empty()) {
			nlohmann::json auxData = nlohmann::json::object();
			for (const auto& kv : node.auxiliary_mod_data) {
				auxData[kv.first] = kv.second;
			}
			j["auxiliary_mod_data"] = auxData;
		}

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
		node.text_display_mode = j.value("text_display_mode", std::string{"markdown"});

		if (j.contains("tags")) {
			for (const auto& tag : j["tags"]) {
				node.tags.push_back(tag.get<std::string>());
			}
		}

		node.priority = j.value("priority", 0);
		node.relative_z = j.value("relative_z", 1);

		if (j.contains("explicit_color")) {
			node.explicit_color = j["explicit_color"].get<std::string>();
		}
		if (j.contains("explicit_size")) {
			node.explicit_size = j["explicit_size"].get<float>();
		}
		node.color = j.value("color", kDefaultNodeColor);

		node.pos_x = j.value("pos_x", 0.0f);
		node.pos_y = j.value("pos_y", 0.0f);

		node.width = j.value("width", 160.0f);
		node.height = j.value("height", 60.0f);

		node.is_collapsed = j.value("is_collapsed", false);
		node.collapsed_display = groupDisplayModeFromString(j.value("collapsed_display", "count_badge"));
		node.tool_category = j.value("tool_category", "");
		node.tool_summary = j.value("tool_summary", "");

		// 待办字段（向后兼容：旧存档没有这些字段，默认为 None）
		node.todo_state = todoStateFromString(j.value("todo_state", "none"));
		if (node.todo_state != TodoState::None && j.contains("todo_created_at")) {
			node.todo_created_at = isoStringToTimestamp(j["todo_created_at"].get<std::string>());
		}

		// ---- 节点模组字段（向后兼容：旧存档没有这些字段） ----
		node.primary_mod_type = j.value("primary_mod_type", std::string{});
		if (j.contains("primary_mod_data")) {
			node.primary_mod_data = j["primary_mod_data"];
		}
		if (j.contains("auxiliary_mod_types") && j["auxiliary_mod_types"].is_array()) {
			for (const auto& t : j["auxiliary_mod_types"]) {
				node.auxiliary_mod_types.push_back(t.get<std::string>());
			}
		}
		if (j.contains("auxiliary_mod_data") && j["auxiliary_mod_data"].is_object()) {
			for (auto it = j["auxiliary_mod_data"].begin();
			     it != j["auxiliary_mod_data"].end(); ++it) {
				node.auxiliary_mod_data[it.key()] = it.value();
			}
		}

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
			// 在序列化前，把模组运行时数据回写到 WarNode 的 mod 字段
			// （需要 const_cast，因为 toJson 是 const 但模组钩子要写 WarNode）
			WarNode* mutable_node = const_cast<WarNode*>(&pair.second);
			ModManager::instance().saveNodeModData(mutable_node);
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
			todo_list_.clear();

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
			
			// 重建所有节点的相对坐标
			rebuildRelativeCoordinates();

			// 重建待办列表：收集所有 todo_state != None 的节点
			todo_list_.clear();
			for (const auto& pair : nodes_) {
				if (pair.second.todo_state != TodoState::None) {
					todo_list_.push_back(pair.first);
				}
			}
			// 按标记时间排序
			std::sort(todo_list_.begin(), todo_list_.end(),
				[this](const Uuid& a, const Uuid& b) {
					const WarNode* na = getNode(a);
					const WarNode* nb = getNode(b);
					if (!na || !nb) return false;
					return na->todo_created_at < nb->todo_created_at;
				});

			return true;
		}
		catch (const std::exception& e) {
			// 记录错误日志
			return false;
		}
	}

	// 便捷方法：保存到文件（支持 Unicode 路径）
	bool WarRoomModel::saveToFile(const std::string& filepath) const {
#ifdef _WIN32
		// Windows: 转换为 wstring 以支持 Unicode 路径
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), (int)filepath.size(), NULL, 0);
		std::wstring wpath(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), (int)filepath.size(), &wpath[0], size_needed);

		std::ofstream file(wpath);
#else
		std::ofstream file(filepath);
#endif
		if (!file.is_open()) return false;

		// 写入 UTF-8 BOM（可选，但推荐）
		file << "\xEF\xBB\xBF";
		file << toJson().dump(2);
		return file.good();
	}

	// 便捷方法：从文件加载（支持 Unicode 路径）
	bool WarRoomModel::loadFromFile(const std::string& filepath) {
#ifdef _WIN32
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), (int)filepath.size(), NULL, 0);
		std::wstring wpath(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), (int)filepath.size(), &wpath[0], size_needed);

		std::ifstream file(wpath);
#else
		std::ifstream file(filepath);
#endif
		if (!file.is_open()) return false;

		// 跳过可能的 UTF-8 BOM
		char bom[3];
		file.read(bom, 3);
		if (!(bom[0] == (char)0xEF && bom[1] == (char)0xBB && bom[2] == (char)0xBF)) {
			file.seekg(0);
		}

		nlohmann::json j;
		file >> j;
		return fromJson(j);
	}

	// ============================================================
	// 文件夹存档（唯一格式）
	// ============================================================
	// 内部辅助：把 std::string 路径转为 QString
	static QString qstr(const std::string& s) {
		return QString::fromUtf8(s.c_str(), (int)s.size());
	}

	// 内部辅助：判断路径是否指向一个已存在的目录
	static bool isDir(const QString& p) {
		return QFileInfo(p).isDir();
	}

	// 内部辅助：递归创建目录
	static bool mkpath(const QString& p) {
		QDir dir(p);
		if (dir.exists()) return true;
		return dir.mkpath(".");
	}

	bool WarRoomModel::saveToFolder(const std::string& folder) const {
		QString baseDir = QFileInfo(qstr(folder)).absoluteFilePath();
		if (!mkpath(baseDir)) return false;

		QString folderName = QFileInfo(baseDir).fileName();
		QString boardFilePath = baseDir + "/" + folderName + ".warroom";
		QString modDataRoot = baseDir + "/mod_data";

		auto& mm = ModManager::instance();
		for (const auto& pair : nodes_) {
			const WarNode& node = pair.second;
			if (node.primary_mod_type.empty()) continue;

			NodeMod* mod = mm.getMod(node.primary_mod_type);
			if (!mod) continue;

			WarNode* mutable_node = const_cast<WarNode*>(&node);
			QString subdir = QString::fromStdString(node.primary_mod_type);
			mod->setArchiveBaseDir(baseDir, modDataRoot + "/" + subdir);

			void* modData = mm.getPrimaryPrivate(&node);
			QStringList extFiles = mod->collectExternalFiles(&node, modData);
			if (extFiles.isEmpty()) continue;

			QString targetDir = modDataRoot + "/" + subdir;
			mkpath(targetDir);

			QString nodeIdPrefix = QString::fromStdString(node.id);
			for (const QString& src : extFiles) {
				QFileInfo fi(src);
				QString base = fi.fileName();
				QString dst;

				// 如果源文件已经在目标目录（mod_data/<modId>/）下，直接用原文件名
				// 避免每次保存都重复添加 nodeId 前缀
				if (fi.absolutePath() == QDir(targetDir).absolutePath()) {
					dst = targetDir + "/" + base;
					// 即便不复制，也确保 storedPath 是相对路径
					QString relFromBase = QDir(baseDir).relativeFilePath(dst);
					if (auto* imgMod = dynamic_cast<ImageMod*>(mod)) {
						auto* pd = static_cast<ImageMod::PrivateData*>(modData);
						if (pd) pd->storedPath = relFromBase;
					}
					else if (auto* vidMod = dynamic_cast<VideoMod*>(mod)) {
						auto* pd = static_cast<VideoMod::PrivateData*>(modData);
						if (pd) pd->storedPath = relFromBase;
					}
					continue;
				}

				// 外部文件：加 nodeId 前缀避免重名
				dst = targetDir + "/" + nodeIdPrefix + "_" + base;

				if (QFile::exists(dst)) QFile::remove(dst);
				if (!QFile::copy(src, dst)) continue;

				QString relFromBase = QDir(baseDir).relativeFilePath(dst);
				if (auto* imgMod = dynamic_cast<ImageMod*>(mod)) {
					auto* pd = static_cast<ImageMod::PrivateData*>(modData);
					if (pd) pd->storedPath = relFromBase;
				}
				else if (auto* vidMod = dynamic_cast<VideoMod*>(mod)) {
					auto* pd = static_cast<VideoMod::PrivateData*>(modData);
					if (pd) pd->storedPath = relFromBase;
				}
			}

			mm.saveNodeModData(mutable_node);
		}

		nlohmann::json j = toJson();
#ifdef _WIN32
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, boardFilePath.toStdString().c_str(), (int)boardFilePath.toStdString().size(), NULL, 0);
		std::wstring wpath(size_needed, 0);
		std::string boardUtf8 = boardFilePath.toStdString();
		MultiByteToWideChar(CP_UTF8, 0, boardUtf8.c_str(), (int)boardUtf8.size(), &wpath[0], size_needed);
		std::ofstream file(wpath);
#else
		std::ofstream file(boardFilePath.toStdString());
#endif
		if (!file.is_open()) return false;
		file << "\xEF\xBB\xBF";
		file << j.dump(2);
		return file.good();
	}

	bool WarRoomModel::loadFromFolder(const std::string& folder) {
		QString baseDir = QFileInfo(qstr(folder)).absoluteFilePath();
		if (!isDir(baseDir)) return false;

		QString folderName = QFileInfo(baseDir).fileName();
		QString boardFilePath = baseDir + "/" + folderName + ".warroom";
		if (!QFile::exists(boardFilePath)) return false;

		nlohmann::json j;
#ifdef _WIN32
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, boardFilePath.toStdString().c_str(), (int)boardFilePath.toStdString().size(), NULL, 0);
		std::wstring wpath(size_needed, 0);
		std::string boardUtf8 = boardFilePath.toStdString();
		MultiByteToWideChar(CP_UTF8, 0, boardUtf8.c_str(), (int)boardUtf8.size(), &wpath[0], size_needed);
		std::ifstream file(wpath);
#else
		std::ifstream file(boardFilePath.toStdString());
#endif
		if (!file.is_open()) return false;
		char bom[3];
		file.read(bom, 3);
		if (!(bom[0] == (char)0xEF && bom[1] == (char)0xBB && bom[2] == (char)0xBF)) {
			file.seekg(0);
		}
		file >> j;

		if (!fromJson(j)) return false;

		auto& mm = ModManager::instance();
		QDir baseDirObj(baseDir);
		for (const auto& pair : nodes_) {
			const WarNode& node = pair.second;
			if (node.primary_mod_type.empty()) continue;
			NodeMod* mod = mm.getMod(node.primary_mod_type);
			if (!mod) continue;

			QString subdir = QString::fromStdString(node.primary_mod_type);
			QString modDataSubdir = baseDir + "/mod_data/" + subdir;
			mod->setArchiveBaseDir(baseDir, modDataSubdir);
		}

		return true;
	}

	bool WarRoomModel::loadFromAuto(const std::string& path) {
		QString p = qstr(path);
		QFileInfo fi(p);
		if (fi.isDir()) {
			return loadFromFolder(path);
		}
		if (fi.isFile() && fi.suffix().toLower() == "warroom") {
			QString fileBaseName = fi.completeBaseName();
			QString parentDirName = fi.dir().dirName();
			if (fileBaseName == parentDirName) {
				return loadFromFolder(fi.absolutePath().toStdString());
			}
		}
		return false;
	}

} // namespace warroom