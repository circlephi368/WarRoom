// NodeGraphicsItem.h
#pragma once
#include <QGraphicsItem>
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <string>
#include "war_room_model.h"

class NodeGraphicsItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // 构造函数
    NodeGraphicsItem(const std::string& nodeId, const std::string& title,
        const std::string& fullText, const QColor& color,
        warroom::NodeKind kind = warroom::NodeKind::Leaf,     // 新增
        bool isCollapsed = false,                              // 新增
        QGraphicsItem* parent = nullptr)
        : QGraphicsObject(parent), m_nodeId(nodeId), m_title(title),
        m_fullText(fullText), m_color(color),
        m_nodeKind(kind), m_isCollapsed(isCollapsed)         // 新增初始化
    {
        setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
        setAcceptHoverEvents(true);
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

        QRectF rect = boundingRect().adjusted(2, 2, -2, -2);
        painter->setBrush(m_color);
        painter->setPen(QPen(m_color.darker(150), 2));
        painter->drawRoundedRect(rect, 8, 8);

        painter->setPen(Qt::white);
        QFont font("Microsoft YaHei", 10, QFont::Bold);
        painter->setFont(font);

        // 获取实际文本：
        std::string displayText;
        if (m_nodeKind == warroom::NodeKind::Group) {
            // 分组节点：始终显示标题
            displayText = m_title;
        }
        else if (m_isCollapsed) {
            // 叶节点缩略模式：显示标题
            displayText = m_title;
        }
        else {
            // 叶节点展开模式：显示长文本
            displayText = m_fullText.empty() ? m_title : m_fullText;
        }
        if (displayText.empty()) {
            displayText = "未命名";
        }

        QString text = QString::fromStdString(displayText);
        QRectF textRect = rect.adjusted(10, 5, -10, -5);

        // 自动换行 + 省略
        QFontMetrics fm(font);
        QString elided;
        bool needWrap = false;
        // 简单换行：按宽度折行
        QStringList lines;
        QString remaining = text;
        while (!remaining.isEmpty()) {
            QString line = fm.elidedText(remaining, Qt::ElideRight, static_cast<int>(textRect.width()));
            if (line.isEmpty()) break;
            lines.append(line);
            remaining = remaining.mid(line.length());
            if (lines.size() >= 3) { // 最多3行，超出省略
                lines.last() = fm.elidedText(lines.last() + "…", Qt::ElideRight, static_cast<int>(textRect.width()));
                break;
            }
        }

        // 绘制多行文本
        qreal yOffset = textRect.top();
        QFontMetrics fmMulti(font);
        for (int i = 0; i < lines.size(); ++i) {
            painter->drawText(QRectF(textRect.left(), yOffset, textRect.width(), fmMulti.height()),
                Qt::AlignLeft | Qt::AlignTop, lines[i]);
            yOffset += fmMulti.height();
        }

        // 选中高亮
        if (isSelected()) {
            painter->setBrush(Qt::NoBrush);
            QPen highlightPen(QColor(100, 180, 255), 3);
            painter->setPen(highlightPen);
            painter->drawRoundedRect(rect, 8, 8);
        }
    }
    // 在 NodeGraphicsItem 类的 public 部分添加：

    void updateContent(const std::string& newTitle,
        const std::string& newFullText,
        bool isCollapsed) {
        m_title = newTitle;
        m_fullText = newFullText;
        m_isCollapsed = isCollapsed;
        update();//重绘
    }

    void updateColor(const QColor& newColor) {
        m_color = newColor;
        update();
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
    std::string m_fullText;
    QColor m_color;
    float m_width;
    float m_height;
    warroom::NodeKind m_nodeKind = warroom::NodeKind::Leaf;
    bool m_isCollapsed = false;
};