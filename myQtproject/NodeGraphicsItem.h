// NodeGraphicsItem.h
#pragma once
#include <QGraphicsItem>
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <string>

class NodeGraphicsItem : public QGraphicsObject
{
    Q_OBJECT

public:
    NodeGraphicsItem(const std::string& nodeId, const std::string& title,
        const QColor& color, QGraphicsItem* parent = nullptr)
        : QGraphicsObject(parent), m_nodeId(nodeId), m_title(title), m_color(color)
    {
        // 使此项可选中、可移动
        setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
        setAcceptHoverEvents(true);

        // 设置固定大小
        m_width = 160;
        m_height = 60;
    }

    // 包围盒
    QRectF boundingRect() const override
    {
        return QRectF(0, 0, m_width, m_height);
    }

    // 绘制
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override
    {
        Q_UNUSED(option);
        Q_UNUSED(widget);

        painter->setRenderHint(QPainter::Antialiasing);

        // 背景圆角矩形
        QRectF rect = boundingRect().adjusted(2, 2, -2, -2);
        painter->setBrush(m_color);
        painter->setPen(QPen(m_color.darker(150), 2));
        painter->drawRoundedRect(rect, 8, 8);

        // 标题文字（白色，截断过长文本）
        painter->setPen(Qt::white);
        QFont font("Microsoft YaHei", 10, QFont::Bold);
        painter->setFont(font);
        QString text = QString::fromStdString(m_title);
        QRectF textRect = rect.adjusted(10, 5, -10, -5);
        QString elided = painter->fontMetrics().elidedText(text, Qt::ElideRight,
            static_cast<int>(textRect.width()));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);

        // 如果被选中，绘制高亮边框
        if (isSelected()) {
            painter->setBrush(Qt::NoBrush);
            QPen highlightPen(QColor(100, 180, 255), 3);
            painter->setPen(highlightPen);
            painter->drawRoundedRect(rect, 8, 8);
        }
    }

    // 拖拽后更新模型坐标（后续接入模型时使用）
    const std::string& nodeId() const { return m_nodeId; }

signals:
    void positionChanged(const std::string& nodeId, float newX, float newY);
    void moveFinished(const std::string& nodeId, float oldX, float oldY, float newX, float newY);
    
protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
    {
        if (change == ItemPositionHasChanged) {
            emit positionChanged(m_nodeId,
                static_cast<float>(pos().x()),
                static_cast<float>(pos().y()));
        }
        return QGraphicsItem::itemChange(change, value);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        m_dragStartX = static_cast<float>(pos().x());
        m_dragStartY = static_cast<float>(pos().y());
        QGraphicsObject::mousePressEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        float newX = static_cast<float>(pos().x());
        float newY = static_cast<float>(pos().y());
        if (m_dragStartX != newX || m_dragStartY != newY) {
            emit moveFinished(m_nodeId, m_dragStartX, m_dragStartY, newX, newY);
        }
        QGraphicsObject::mouseReleaseEvent(event);
    }

private:
    float m_dragStartX = 0.0f;
    float m_dragStartY = 0.0f;
    std::string m_nodeId;
    std::string m_title;
    QColor m_color;
    float m_width;
    float m_height;
};