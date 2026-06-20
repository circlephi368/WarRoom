// ConnectionAnchor.h
#pragma once

#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QPointer>

class NodeGraphicsItem;

class ConnectionAnchor : public QGraphicsEllipseItem
{
public:
	ConnectionAnchor(NodeGraphicsItem* parentNode, int edge, QGraphicsItem* parent = nullptr);
	~ConnectionAnchor() = default;

	// 边缘方向：0=右,1=下,2=左,3=上
	int edge() const { return m_edge; }
	NodeGraphicsItem* parentNode() const { return m_parentNode; }

	// 安全地获取父节点（可能为 nullptr）
	NodeGraphicsItem* safeParentNode() const { return m_parentNode; }

	// 重写命中检测相关接口，提供比视觉更大的可点击区域
	QRectF boundingRect() const override;
	QPainterPath shape() const override;
	bool contains(const QPointF& point) const override;

protected:
	void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
	void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
	void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
	void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
	QPointer<NodeGraphicsItem> m_parentNode;  // 使用 QPointer 防止野指针
	int m_edge;  // 0:right, 1:bottom, 2:left, 3:top
	bool m_dragging = false;

	static constexpr qreal VISUAL_RADIUS = 6.0;   // 视觉半径
	static constexpr qreal HIT_RADIUS = 20.0;     // 命中检测半径（比视觉大很多，便于点击）
};