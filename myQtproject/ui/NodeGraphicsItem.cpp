#include "NodeGraphicsItem.h"
#include "CustomTextEdit.h"
#include <qcursor.h>

// ==================== 构造函数 ====================
NodeGraphicsItem::NodeGraphicsItem(const std::string& nodeId, warroom::WarRoomModel& model, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_nodeId(nodeId), m_model(model)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    createAnchors();

    // 初始化预览文档
    initializePreviewDocument();

    // 从模型获取初始位置
    const warroom::WarNode* node = getNode();
    if (node) {
        setPos(node->pos_x, node->pos_y);
    }
}

// ==================== 辅助方法 ====================
const warroom::WarNode* NodeGraphicsItem::getNode() const
{
    return m_model.getNode(m_nodeId);
}

warroom::WarNode* NodeGraphicsItem::getNodeMutable()
{
    return m_model.getNodeMutable(m_nodeId);
}

void NodeGraphicsItem::createInlineEditor() {
    if (m_editorProxy) return;

    /*m_textEdit = new QTextEdit();
    m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_textEdit->document()->setDefaultFont(m_editorFont);
    m_textEdit->document()->setTextWidth(getWidth() - m_textPadding * 2);*/
    auto* customEdit = new CustomTextEdit();  // 使用自定义编辑器
    m_textEdit = customEdit;  // 赋值给 QTextEdit*（多态）
    // 配置
    customEdit->setTransparentMode(true);
    customEdit->setCustomScrollbar(true);

    // 加载现有内容
    const warroom::WarNode* node = getNode();
    if (node) {
        m_textEdit->setMarkdown(QString::fromStdString(node->full_text));
    }

    m_editorProxy = new QGraphicsProxyWidget(this);
    m_editorProxy->setWidget(m_textEdit);
    m_editorProxy->setPos(m_textPadding, m_textPadding);
    m_editorProxy->setZValue(100);

    // 重要：设置代理大小
    m_editorProxy->resize(getWidth() - m_textPadding * 2, getHeight() - m_textPadding * 2);

    m_textEdit->setFocus();

    // 使用事件过滤器监听焦点丢失
    m_textEdit->installEventFilter(this);
}

void NodeGraphicsItem::saveContentToModel() {
    if (!m_textEdit) return;

    warroom::WarNode* node = getNodeMutable();
    if (node) {
        std::string newContent = m_textEdit->toMarkdown().toStdString();
        if (node->full_text != newContent) {
            node->full_text = newContent;
            // 可选：通知外部刷新（如撤销栈）
        }
    }
}

void NodeGraphicsItem::saveAndExitEditMode() {
    if (m_editMode != EditMode::Editing) return;

    saveContentToModel();
    destroyEditor();
    m_editMode = EditMode::Preview;
    refreshPreviewDocument();   // 重新加载模型内容到预览文档
    update();                   // 触发重绘
}

void NodeGraphicsItem::refreshPreviewDocument() {
    const warroom::WarNode* node = getNode();
    if (!node) return;

    m_previewDocument.setDefaultFont(m_editorFont);
    m_previewDocument.setTextWidth(getWidth() - m_textPadding * 2);
    m_previewDocument.setMarkdown(QString::fromStdString(node->full_text));
}

void NodeGraphicsItem::updateEditorGeometry() {
    if (m_editorProxy) {
        m_editorProxy->resize(getWidth() - m_textPadding * 2, getHeight() - m_textPadding * 2);
        if (m_textEdit) {
            m_textEdit->document()->setTextWidth(getWidth() - m_textPadding * 2);
        }
    }
}

void NodeGraphicsItem::initializePreviewDocument() {
    m_previewDocument.setDefaultFont(m_editorFont);
    m_previewDocument.setTextWidth(getWidth() - m_textPadding * 2);
    refreshPreviewDocument();  // 加载内容
}

float NodeGraphicsItem::getWidth() const
{
    const warroom::WarNode* node = getNode();
    return node ? node->width : 160.0f;
}

float NodeGraphicsItem::getHeight() const
{
    const warroom::WarNode* node = getNode();
    return node ? node->height : 60.0f;
}

// ==================== 包围盒 ====================
QRectF NodeGraphicsItem::boundingRect() const
{
    return QRectF(0, 0, getWidth(), getHeight());
}

// ==================== 绘制 ====================
void NodeGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
    QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    const warroom::WarNode* node = getNode();
    if (!node) return;

    painter->setRenderHint(QPainter::Antialiasing);

    QRectF rect = boundingRect().adjusted(2, 2, -2, -2);

    // 绘制背景
    QColor color(QString::fromStdString(node->color));
    painter->setBrush(color);
    painter->setPen(QPen(color.darker(150), 2));
    painter->drawRoundedRect(rect, 8, 8);

    //// 绘制标题（始终显示）
    //painter->setPen(Qt::white);
    //QFont font("Microsoft YaHei", 10, QFont::Bold);
    //painter->setFont(font);

    //QString title = QString::fromStdString(node->title);
    //if (title.isEmpty()) title = "未命名";

    //QRectF titleRect = rect.adjusted(10, 5, -10, -25);
    //painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignTop, title);

    // 预览模式下绘制内容
    if (m_editMode == EditMode::Preview) {
        painter->save();
        painter->translate(m_textPadding, m_textPadding);  // 偏移标题高度
        QRectF clipRect(0, 0, getWidth() - m_textPadding * 2, getHeight() - m_textPadding * 2 - 20);
        m_previewDocument.drawContents(painter, clipRect);
        painter->restore();
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

    // 创建四个锚点
    auto* rightAnchor = new ConnectionAnchor(this, 0, this);
    auto* bottomAnchor = new ConnectionAnchor(this, 1, this);
    auto* leftAnchor = new ConnectionAnchor(this, 2, this);
    auto* topAnchor = new ConnectionAnchor(this, 3, this);

    m_anchors.append(rightAnchor);
    m_anchors.append(bottomAnchor);
    m_anchors.append(leftAnchor);
    m_anchors.append(topAnchor);

    for (auto* anchor : m_anchors) {
        anchor->setFlag(QGraphicsItem::ItemStacksBehindParent, false);
        anchor->setZValue(100);
        anchor->hide();  // 初始隐藏
    }

    // 设置位置
    updateAnchorsPosition();
}

void NodeGraphicsItem::updateAnchorsPosition()
{
    qreal w = getWidth();
    qreal h = getHeight();

    if (m_anchors.size() >= 4) {
		// 右边缘 (索引0)
        m_anchors[0]->setPos(w, h / 2.0);
		// 下边缘 (索引1)
        m_anchors[1]->setPos(w / 2.0, h);
		// 左边缘 (索引2)
        m_anchors[2]->setPos(0, h / 2.0);
		// 上边缘 (索引3)
        m_anchors[3]->setPos(w / 2.0, 0);
    }
}

// ==================== 刷新显示 ====================
void NodeGraphicsItem::refresh()
{
    const warroom::WarNode* node = getNode();
    if (node) {
        // 如果位置变了，更新位置
        if (!qFuzzyCompare(static_cast<float>(pos().x()), node->pos_x) ||
            !qFuzzyCompare(static_cast<float>(pos().y()), node->pos_y)) {
            setPos(node->pos_x, node->pos_y);
        }
        // 刷新预览文档内容
        refreshPreviewDocument();
        // 如果大小变了，需要更新锚点位置
        updateAnchorsPosition();
    }
    update();  // 重绘
}

// ==================== 设置节点大小 ====================
void NodeGraphicsItem::setNodeSize(float width, float height)
{
    warroom::WarNode* node = getNodeMutable();
    if (!node) return;

    if (qFuzzyCompare(node->width, width) && qFuzzyCompare(node->height, height))
        return;

    prepareGeometryChange();
    node->width = width;
    node->height = height;

    // 更新预览文档宽度
    m_previewDocument.setTextWidth(width - m_textPadding * 2);

    // 如果编辑器存在，也更新编辑器大小
    if (m_editorProxy) {
        m_editorProxy->resize(width - m_textPadding * 2, height - m_textPadding * 2);
        if (m_textEdit) {
            m_textEdit->document()->setTextWidth(width - m_textPadding * 2);
        }
    }

    updateAnchorsPosition();
    update();
    emit sizeChanged(m_nodeId, width, height);
}

void NodeGraphicsItem::setEditMode(EditMode mode) {
    if (m_editMode == mode) return;

    if (mode == EditMode::Editing) {
        createInlineEditor();
    }
    else {
        saveAndExitEditMode();
    }
}

void NodeGraphicsItem::destroyEditor() {
    if (m_editorProxy) {
        delete m_editorProxy;  // 会自动删除 m_textEdit
        m_editorProxy = nullptr;
        m_textEdit = nullptr;
    }
}

// ==================== 悬停事件 ====================
void NodeGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    for (auto* anchor : m_anchors) {
        if (anchor) anchor->show();
    }
    QGraphicsObject::hoverEnterEvent(event);
}

void NodeGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
	// 隐藏所有锚点
    for (auto* anchor : m_anchors) {
        if (anchor) anchor->hide();
    }
    QGraphicsObject::hoverLeaveEvent(event);
}

void NodeGraphicsItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (m_editMode == EditMode::Preview) {
        emit editRequested(m_nodeId);  // 通知 MainWindow 先清理其他编辑器
        m_editMode = EditMode::Editing;
        createInlineEditor();
        event->accept();
        return;
    }
    QGraphicsObject::mouseDoubleClickEvent(event);
}

bool NodeGraphicsItem::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_textEdit && event->type() == QEvent::FocusOut) {
        // 使用延迟调用，避免在事件处理中直接删除对象
        QMetaObject::invokeMethod(this, "saveAndExitEditMode", Qt::QueuedConnection);
        return false;
    }
    return QGraphicsObject::eventFilter(watched, event);
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
		// 如果节点尚未被选中，或者点击时改变选中状态
        if (!isSelected()) {
            emit selectedForZBoost(m_nodeId);
        }

		// 调整节点大小
        ResizeHandle handle = handleAt(event->scenePos());
        if (handle != Handle_None) {
			// 开始调整大小
            m_resizing = true;
            m_activeHandle = handle;
			// 关键修改：记录相对于节点的本地坐标
            m_resizeStartLocalPos = mapFromScene(event->scenePos());
            m_resizeStartWidth = getWidth();
            m_resizeStartHeight = getHeight();
            event->accept();
            return;
        }
		// 拖拽节点位置
        m_dragStartX = static_cast<float>(pos().x());
        m_dragStartY = static_cast<float>(pos().y());
    }
    QGraphicsObject::mousePressEvent(event);
}

// ==================== 鼠标移动事件 ====================
void NodeGraphicsItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_resizing) {
        QPointF currentLocalPos = mapFromScene(event->scenePos());
        QPointF delta = currentLocalPos - m_resizeStartLocalPos;

        float newWidth = m_resizeStartWidth;
        float newHeight = m_resizeStartHeight;
        QPointF newPos = pos();

        const float MIN_W = 80;
        const float MIN_H = 50;

		// 根据手柄方向独立计算宽高和位置
        switch (m_activeHandle) {
        case Handle_Right:
            newWidth = qMax(MIN_W, m_resizeStartWidth + delta.x());
            break;
        case Handle_Bottom:
            newHeight = qMax(MIN_H, m_resizeStartHeight + delta.y());
            break;
        case Handle_BottomRight:
            newWidth = qMax(MIN_W, m_resizeStartWidth + delta.x());
            newHeight = qMax(MIN_H, m_resizeStartHeight + delta.y());
            break;
        case Handle_Left:
            newWidth = qMax(MIN_W, m_resizeStartWidth - delta.x());
            newPos.setX(pos().x() + (m_resizeStartWidth - newWidth));
            break;
        case Handle_Top:
            newHeight = qMax(MIN_H, m_resizeStartHeight - delta.y());
            newPos.setY(pos().y() + (m_resizeStartHeight - newHeight));
            break;
        case Handle_TopLeft:
            newWidth = qMax(MIN_W, m_resizeStartWidth - delta.x());
            newHeight = qMax(MIN_H, m_resizeStartHeight - delta.y());
            newPos.setX(pos().x() + (m_resizeStartWidth - newWidth));
            newPos.setY(pos().y() + (m_resizeStartHeight - newHeight));
            break;
        case Handle_TopRight:
            newWidth = qMax(MIN_W, m_resizeStartWidth + delta.x());
            newHeight = qMax(MIN_H, m_resizeStartHeight - delta.y());
            newPos.setY(pos().y() + (m_resizeStartHeight - newHeight));
            break;
        case Handle_BottomLeft:
            newWidth = qMax(MIN_W, m_resizeStartWidth - delta.x());
            newHeight = qMax(MIN_H, m_resizeStartHeight + delta.y());
            newPos.setX(pos().x() + (m_resizeStartWidth - newWidth));
            break;
        default:
            break;
        }

        bool posChanged = false;
        if (newPos != pos()) {
            setPos(newPos);
            posChanged = true;
        }

        // 直接修改模型中的尺寸
        warroom::WarNode* node = getNodeMutable();
        if (node) {
            if (!qFuzzyCompare(node->width, newWidth) || !qFuzzyCompare(node->height, newHeight)) {
                prepareGeometryChange();
                node->width = newWidth;
                node->height = newHeight;

                // 如果正在编辑，同步更新编辑器大小
                if (m_editorProxy) {
                    updateEditorGeometry();
                }

                updateAnchorsPosition();
                update();
                emit sizeChanged(m_nodeId, newWidth, newHeight);
            }
        }

        if (posChanged) {
            QPointF newCurrentLocalPos = mapFromScene(event->scenePos());
			// 更新起始本地坐标
            m_resizeStartLocalPos = newCurrentLocalPos;
            m_resizeStartWidth = getWidth();
            m_resizeStartHeight = getHeight();
        }

        event->accept();
        return;
    }

    QGraphicsObject::mouseMoveEvent(event);
}

// ==================== 鼠标释放事件 ====================
void NodeGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_resizing) {
        emit resizeFinished(m_nodeId,
            m_resizeStartWidth, m_resizeStartHeight,
            getWidth(), getHeight());
        m_resizing = false;
        m_activeHandle = Handle_None;
        event->accept();
        return;
    }

    float newX = static_cast<float>(pos().x());
    float newY = static_cast<float>(pos().y());
    if (m_dragStartX != newX || m_dragStartY != newY) {
        emit moveFinished(m_nodeId, m_dragStartX, m_dragStartY, newX, newY);
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

// ==================== 获取锚点位置 ====================
QPointF NodeGraphicsItem::getAnchorPos(int edge) const
{
    qreal w = getWidth();
    qreal h = getHeight();

    switch (edge) {
    case 0: return QPointF(w, h / 2.0);
    case 1: return QPointF(w / 2.0, h);
    case 2: return QPointF(0, h / 2.0);
    case 3: return QPointF(w / 2.0, 0);
    default: return QPointF(w / 2.0, h / 2.0);
    }
}

// 用于拖动调整节点size
NodeGraphicsItem::ResizeHandle NodeGraphicsItem::handleAt(const QPointF& scenePos) const
{
    if (!m_resizingEnabled) return Handle_None;

    QPointF localPos = mapFromScene(scenePos);
    QRectF rect = boundingRect().adjusted(2, 2, -2, -2);

    auto pointInHandle = [&](const QPointF& handleCenter) -> bool {
        return QRectF(handleCenter.x() - HANDLE_SIZE / 2,
            handleCenter.y() - HANDLE_SIZE / 2,
            HANDLE_SIZE, HANDLE_SIZE).contains(localPos);
        };

	// 优先判断四角
    if (pointInHandle(rect.topLeft()))     return Handle_TopLeft;
    if (pointInHandle(rect.topRight()))    return Handle_TopRight;
    if (pointInHandle(rect.bottomLeft()))  return Handle_BottomLeft;
    if (pointInHandle(rect.bottomRight())) return Handle_BottomRight;

	// 四边整条边判定
	// 顶边
    if (QRectF(rect.left(), rect.top() - HANDLE_SIZE / 2,
        rect.width(), HANDLE_SIZE).contains(localPos))
        return Handle_Top;

	// 底边
    if (QRectF(rect.left(), rect.bottom() - HANDLE_SIZE / 2,
        rect.width(), HANDLE_SIZE).contains(localPos))
        return Handle_Bottom;

	// 左边
    if (QRectF(rect.left() - HANDLE_SIZE / 2, rect.top(),
        HANDLE_SIZE, rect.height()).contains(localPos))
        return Handle_Left;

	// 右边
    if (QRectF(rect.right() - HANDLE_SIZE / 2, rect.top(),
        HANDLE_SIZE, rect.height()).contains(localPos))
        return Handle_Right;

    return Handle_None;
}

void NodeGraphicsItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    ResizeHandle handle = handleAt(event->scenePos());

	// 根据手柄设置光标
    switch (handle) {
    case Handle_TopLeft:
    case Handle_BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case Handle_TopRight:
    case Handle_BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case Handle_Top:
    case Handle_Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case Handle_Left:
    case Handle_Right:
        setCursor(Qt::SizeHorCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }

    QGraphicsObject::hoverMoveEvent(event);
}

// 刷新绝对z值
void NodeGraphicsItem::updateAbsoluteZ(int absolute_z) {
    setZValue(absolute_z);
}

void NodeGraphicsItem::updateCustomBoundingRect()
{
    // 自身包围盒（从 Qt 的 boundingRect 获取）
    QRectF selfRect = boundingRect();

    // 如果是叶子节点或没有子节点图形项，直接使用自身
    // 注意：这里需要从外部获取子节点的图形项，所以需要传入映射表
    // 实际调用时通过 rebuildAllBoundingRects 统一处理更方便
    m_customBoundingRect = selfRect;
}

void NodeGraphicsItem::rebuildAllBoundingRects(QHash<QString, NodeGraphicsItem*>& nodeItems)
{
    // 后序遍历：先计算子节点，再计算父节点
    std::function<void(NodeGraphicsItem*)> computeRecursive = [&](NodeGraphicsItem* item) {
        if (!item) return;

        QRectF rect = item->boundingRect();

        // 合并所有子节点的包围盒
        for (auto* childItem : item->childItems()) {
            auto* nodeChild = dynamic_cast<NodeGraphicsItem*>(childItem);
            if (nodeChild) {
                computeRecursive(nodeChild);
                rect = rect.united(nodeChild->getCustomBoundingRect());
            }
        }

        item->m_customBoundingRect = rect;
        };

    // 遍历所有顶层节点
    for (auto* item : nodeItems) {
        NodeGraphicsItem* nodeItem = item;
        if (nodeItem && !nodeItem->parentItem()) {  // 顶层节点
            computeRecursive(nodeItem);
        }
    }
}