#include "NodeGraphicsItem.h"

// ==================== 构造函数 ====================
NodeGraphicsItem::NodeGraphicsItem(const std::string& nodeId, const std::string& title,
    const std::string& fullText, const QColor& color,
    warroom::NodeKind kind, bool isCollapsed, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_nodeId(nodeId), m_title(title),
    m_fullText(fullText), m_color(color),
    m_nodeKind(kind), m_isCollapsed(isCollapsed)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    m_width = 160;
    m_height = 60;
    createAnchors();
    setAcceptHoverEvents(true);
}

// ==================== 包围盒 ====================
QRectF NodeGraphicsItem::boundingRect() const
{
    return QRectF(0, 0, m_width, m_height);
}

// ==================== 绘制 ====================
void NodeGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
    QWidget* widget)
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

    // 获取实际文本
    std::string displayText;
    if (m_nodeKind == warroom::NodeKind::Group) {
        displayText = m_title;
    }
    else if (m_isCollapsed) {
        displayText = m_title;
    }
    else {
        displayText = m_fullText.empty() ? m_title : m_fullText;
    }
    if (displayText.empty()) {
        displayText = "未命名";
    }

    QString text = QString::fromStdString(displayText);
    QRectF textRect = rect.adjusted(10, 5, -10, -5);

    // 自动换行 + 省略
    QFontMetrics fm(font);
    QStringList lines;
    QString remaining = text;
    while (!remaining.isEmpty()) {
        QString line = fm.elidedText(remaining, Qt::ElideRight, static_cast<int>(textRect.width()));
        if (line.isEmpty()) break;
        lines.append(line);
        remaining = remaining.mid(line.length());
        if (lines.size() >= 3) {
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

// ==================== 创建锚点 ====================
void NodeGraphicsItem::createAnchors()
{
    // 清除旧锚点
    for (auto* anchor : m_anchors) {
        delete anchor;
    }
    m_anchors.clear();

    // 获取节点实际尺寸（考虑缩放）
    qreal w = boundingRect().width();
    qreal h = boundingRect().height();

    // 右边缘 (x = w, y = h/2) - 中点
    auto* rightAnchor = new ConnectionAnchor(this, 0, this);
    rightAnchor->setPos(w, h / 2.0);
    rightAnchor->hide();
    m_anchors.append(rightAnchor);

    // 下边缘 (x = w/2, y = h) - 中点
    auto* bottomAnchor = new ConnectionAnchor(this, 1, this);
    bottomAnchor->setPos(w / 2.0, h);
    bottomAnchor->hide();
    m_anchors.append(bottomAnchor);

    // 左边缘 (x = 0, y = h/2) - 中点
    auto* leftAnchor = new ConnectionAnchor(this, 2, this);
    leftAnchor->setPos(0, h / 2.0);
    leftAnchor->hide();
    m_anchors.append(leftAnchor);

    // 上边缘 (x = w/2, y = 0) - 中点
    auto* topAnchor = new ConnectionAnchor(this, 3, this);
    topAnchor->setPos(w / 2.0, 0);
    topAnchor->hide();
    m_anchors.append(topAnchor);

    for (auto* anchor : m_anchors) {
        anchor->setFlag(QGraphicsItem::ItemStacksBehindParent, false);
        anchor->setZValue(100);
    }
}

// ==================== 悬停进入事件 ====================
void NodeGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    // 显示所有锚点
    for (auto* anchor : m_anchors) {
        if (anchor) anchor->show();
    }
    QGraphicsObject::hoverEnterEvent(event);
}

// ==================== 悬停离开事件 ====================
void NodeGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    // 隐藏所有锚点
    for (auto* anchor : m_anchors) {
        if (anchor) anchor->hide();
    }
    QGraphicsObject::hoverLeaveEvent(event);
}

// ==================== 项目变化事件 ====================
QVariant NodeGraphicsItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged) {
        emit positionChanged(m_nodeId,
            static_cast<float>(pos().x()),
            static_cast<float>(pos().y()));
    }
    return QGraphicsItem::itemChange(change, value);
}

// ==================== 鼠标按下事件 ====================
void NodeGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartX = static_cast<float>(pos().x());
        m_dragStartY = static_cast<float>(pos().y());
    }
    QGraphicsObject::mousePressEvent(event);
}

// ==================== 鼠标释放事件 ====================
void NodeGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    float newX = static_cast<float>(pos().x());
    float newY = static_cast<float>(pos().y());
    if (m_dragStartX != newX || m_dragStartY != newY) {
        emit moveFinished(m_nodeId, m_dragStartX, m_dragStartY, newX, newY);
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

// ==================== 更新内容 ====================
void NodeGraphicsItem::updateContent(const std::string& newTitle,
    const std::string& newFullText,
    bool isCollapsed)
{
    m_title = newTitle;
    m_fullText = newFullText;
    m_isCollapsed = isCollapsed;
    update(); // 重绘
}

// ==================== 更新颜色 ====================
void NodeGraphicsItem::updateColor(const QColor& newColor)
{
    m_color = newColor;
    update();
}

// ==================== 获取锚点位置 ====================
QPointF NodeGraphicsItem::getAnchorPos(int edge) const
{
    qreal w = boundingRect().width();
    qreal h = boundingRect().height();

    switch (edge) {
    case 0: return QPointF(w, h / 2.0);      // 右 - 中点
    case 1: return QPointF(w / 2.0, h);      // 下 - 中点
    case 2: return QPointF(0, h / 2.0);      // 左 - 中点
    case 3: return QPointF(w / 2.0, 0);      // 上 - 中点
    default: return QPointF(w / 2.0, h / 2.0);
    }
}