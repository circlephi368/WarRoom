// NodeGraphicsItem.h
#pragma once
#include <QGraphicsItem>
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <string>
#include "war_room_model.h"
#include "ConnectionAnchor.h"

class NodeGraphicsItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // 构造函数声明
    NodeGraphicsItem(const std::string& nodeId, const std::string& title,
        const std::string& fullText, const QColor& color,
        warroom::NodeKind kind = warroom::NodeKind::Leaf,
        bool isCollapsed = false,
        QGraphicsItem* parent = nullptr);

    // 包围盒
    QRectF boundingRect() const override;

    // 绘制
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override;

    // 获取四个边缘锚点的位置（相对于节点）
    QPointF getAnchorPos(int edge) const;

    // 创建锚点
    void createAnchors();

    // 获取锚点列表
    QList<ConnectionAnchor*> anchors() const { return m_anchors; }

    // 更新内容
    void updateContent(const std::string& newTitle,
        const std::string& newFullText,
        bool isCollapsed);

    // 更新颜色
    void updateColor(const QColor& newColor);

    // 获取节点ID
    const std::string& nodeId() const { return m_nodeId; }

signals:
    void positionChanged(const std::string& nodeId, float newX, float newY);
    void moveFinished(const std::string& nodeId, float oldX, float oldY, float newX, float newY);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    float m_dragStartX = 0.0f;
    float m_dragStartY = 0.0f;
    std::string m_nodeId;
    std::string m_title;
    std::string m_fullText;
    QColor m_color;
    float m_width;
    float m_height;
    warroom::NodeKind m_nodeKind = warroom::NodeKind::Leaf;
    bool m_isCollapsed = false;
    QList<ConnectionAnchor*> m_anchors;
};