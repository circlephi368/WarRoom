// war_link.h
#pragma once
#include "warroom_types.h"

namespace warroom {

	// 前置声明
	class WarRoomModel;

	// 锚点类型
	enum class AnchorType {
		Node,   // 锚定在节点上
		Free    // 自由画布坐标
	};

	// 锚点基类
	struct Anchor {
		AnchorType anchor_type;
		virtual ~Anchor() = default;
		virtual Point2D resolvePosition(const WarRoomModel& model) const = 0;
	};

	// 节点锚点
	struct NodeAnchor : Anchor {
		Uuid node_id;
		float offset_x = 0.0f;
		float offset_y = 0.0f;
		int edge = -1;  // -1 表示未指定（兼容旧数据），0=右, 1=下, 2=左, 3=上

		NodeAnchor() { anchor_type = AnchorType::Node; }
		NodeAnchor(Uuid id, float ox = 0, float oy = 0, int edge = -1)
			: node_id(std::move(id)), offset_x(ox), offset_y(oy), edge(edge){
			anchor_type = AnchorType::Node;
		}

		Point2D resolvePosition(const WarRoomModel& model) const override;
	};

	// 自由锚点
	struct FreeAnchor : Anchor {
		float x = 0.0f;
		float y = 0.0f;

		FreeAnchor() { anchor_type = AnchorType::Free; }
		FreeAnchor(float x, float y) : x(x), y(y) {
			anchor_type = AnchorType::Free;
		}

		Point2D resolvePosition(const WarRoomModel&) const override {
			return { x, y };
		}
	};

	// 连线类型
	enum class LinkType {
		Dependency, // 依赖
		Contradiction, // 矛盾
		Transformation, // 转化
		Inspiration, // 启发
		Negation,    // 否定
		UsingMethod  // 使用方法（D2）
	};

	// 连线
	struct WarLink {
		Uuid id;
		std::unique_ptr<Anchor> start_anchor;
		std::unique_ptr<Anchor> end_anchor;
		std::vector<std::unique_ptr<Anchor>> waypoints;
		LinkType type = LinkType::Dependency;
		std::string label;
		Color color = kDefaultLinkColor;

		static WarLink makeNodeToNode(Uuid src, int srcEdge, Uuid dst, int dstEdge, LinkType type = LinkType::Dependency) {
			WarLink link;
			link.id = generateUuid();
			link.start_anchor = std::make_unique<NodeAnchor>(std::move(src), 0, 0, srcEdge);
			link.end_anchor = std::make_unique<NodeAnchor>(std::move(dst), 0, 0, dstEdge);
			link.type = type;
			return link;
		}

		static WarLink makeNodeToFree(Uuid src, float x, float y) {
			WarLink link;
			link.id = generateUuid();
			link.start_anchor = std::make_unique<NodeAnchor>(std::move(src));
			link.end_anchor = std::make_unique<FreeAnchor>(x, y);
			link.type = LinkType::Inspiration;
			return link;
		}

		static WarLink makeFreeLine(float x1, float y1, float x2, float y2,
			const std::vector<Point2D>& waypoints = {}) {
			WarLink link;
			link.id = generateUuid();
			link.start_anchor = std::make_unique<FreeAnchor>(x1, y1);
			link.end_anchor = std::make_unique<FreeAnchor>(x2, y2);
			for (auto& wp : waypoints) {
				link.waypoints.push_back(std::make_unique<FreeAnchor>(wp.x, wp.y));
			}
			return link;
		}
	};

} // namespace warroom