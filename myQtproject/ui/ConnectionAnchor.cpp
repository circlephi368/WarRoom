// ConnectionAnchor.cpp
#include "ConnectionAnchor.h"
#include "NodeGraphicsItem.h"
#include "LinkCreationManager.h"
#include <QCursor>
#include <QGraphicsScene>
#include <cmath>

ConnectionAnchor::ConnectionAnchor(NodeGraphicsItem* parentNode, int edge, QGraphicsItem* parent)
    : QGraphicsEllipseItem(parent)
    , m_parentNode(parentNode)
    , m_edge(edge)
{
    // 视觉矩形保持较小，绘制时只画视觉区域
    setRect(-VISUAL_RADIUS, -VISUAL_RADIUS, VISUAL_RADIUS * 2, VISUAL_RADIUS * 2);
    setBrush(QColor(100, 150, 255, 180));
    setPen(QPen(Qt::white, 1));
    setAcceptHoverEvents(true);
    setZValue(100);
    hide();  // 默认隐藏，hover时显示
}

QRectF ConnectionAnchor::boundingRect() const
{
    // 返回包含命中区域的包围盒，确保事件系统能检测到锚点
    return QRectF(-HIT_RADIUS, -HIT_RADIUS, HIT_RADIUS * 2, HIT_RADIUS * 2);
}

QPainterPath ConnectionAnchor::shape() const
{
    // 命中区域为比视觉大得多的圆形，便于用户点击和拖拽
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), HIT_RADIUS, HIT_RADIUS);
    return path;
}

bool ConnectionAnchor::contains(const QPointF& point) const
{
    // 使用更大的命中半径进行点包含检测
    return std::hypot(point.x(), point.y()) <= HIT_RADIUS;
}

void ConnectionAnchor::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    // 检查父节点是否仍然有效
    if (!m_parentNode) {
        hide();
        QGraphicsEllipseItem::hoverEnterEvent(event);
        return;
    }

    show();
    setBrush(QColor(100, 150, 255, 255));
    QGraphicsEllipseItem::hoverEnterEvent(event);
}

void ConnectionAnchor::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    // 检查父节点是否仍然有效
    if (!m_parentNode) {
        hide();
        QGraphicsEllipseItem::hoverLeaveEvent(event);
        return;
    }

    if (!m_dragging) {
        hide();
    }
    setBrush(QColor(100, 150, 255, 180));
    QGraphicsEllipseItem::hoverLeaveEvent(event);
}

void ConnectionAnchor::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    // 检查父节点是否仍然有效
    if (!m_parentNode) {
        event->ignore();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        setCursor(Qt::CrossCursor);
        setBrush(QColor(100, 150, 255, 255));

        // 启动连接，后续鼠标移动由场景事件过滤器处理
        LinkCreationManager::instance().startConnection(this, event->scenePos());

        // 必须 accept 以阻止事件继续传递（避免触发框选或连接线选中）
        event->accept();
        return;
    }
    QGraphicsEllipseItem::mousePressEvent(event);
}

void ConnectionAnchor::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    // 不再需要，由场景事件过滤器处理
    event->accept();
}

void ConnectionAnchor::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
        setBrush(QColor(100, 150, 255, 180));

        // 连接结束由场景事件过滤器处理
        event->accept();
        return;
    }
    QGraphicsEllipseItem::mouseReleaseEvent(event);
}