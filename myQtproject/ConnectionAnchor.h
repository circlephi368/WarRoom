// ConnectionAnchor.h
#pragma once

#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
//#include "NodeGraphicsItem.h"
class NodeGraphicsItem;

class ConnectionAnchor : public QGraphicsEllipseItem
{
public:
    ConnectionAnchor(NodeGraphicsItem* parentNode, int edge, QGraphicsItem* parent = nullptr);

    // 边缘方向：0=右,1=下,2=左,3=上
    int edge() const { return m_edge; }
    NodeGraphicsItem* parentNode() const { return m_parentNode; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    NodeGraphicsItem* m_parentNode;
    int m_edge;  // 0:right, 1:bottom, 2:left, 3:top
    bool m_dragging = false;
};