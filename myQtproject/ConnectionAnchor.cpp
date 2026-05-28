// ConnectionAnchor.cpp
#include "ConnectionAnchor.h"
#include "NodeGraphicsItem.h"
#include "LinkCreationManager.h"
#include <QCursor>
#include <QGraphicsScene>

ConnectionAnchor::ConnectionAnchor(NodeGraphicsItem* parentNode, int edge, QGraphicsItem* parent)
    : QGraphicsEllipseItem(parent)
    , m_parentNode(parentNode)
    , m_edge(edge)
{
    setRect(-6, -6, 12, 12);
    setBrush(QColor(100, 150, 255, 180));
    setPen(QPen(Qt::white, 1));
    setAcceptHoverEvents(true);
    setZValue(100);
    hide();  // 默认隐藏，hover时显示
}

void ConnectionAnchor::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    show();
    setBrush(QColor(100, 150, 255, 255));
    QGraphicsEllipseItem::hoverEnterEvent(event);
}

void ConnectionAnchor::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    if (!m_dragging) {
        hide();
    }
    setBrush(QColor(100, 150, 255, 180));
    QGraphicsEllipseItem::hoverLeaveEvent(event);
}

void ConnectionAnchor::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        setCursor(Qt::CrossCursor);
        setBrush(QColor(100, 150, 255, 255));

        event->accept();

        // 启动连接，后续鼠标移动由场景事件过滤器处理
        LinkCreationManager::instance().startConnection(this, event->scenePos());
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
    }
    QGraphicsEllipseItem::mouseReleaseEvent(event);
}