#include "NodeGraphicsItem.h"
#include "CustomTextEdit.h"
#include "WarRoomMainWindow.h"
#include "mod/ModManager.h"
#include <qcursor.h>

// ==================== 构造函数 ====================
NodeGraphicsItem::NodeGraphicsItem(const std::string& nodeId, warroom::WarRoomModel& model, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_nodeId(nodeId), m_model(model)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    createAnchors();

    // 使用全局配置的字体（从 settings.ini 读取）
    m_editorFont = WarRoomMainWindow::getNodeFont();

    // 初始化预览文档
    initializePreviewDocument();

    // 从模型获取初始位置
    warroom::WarNode* node = getNodeMutable();
    if (node) {
        setPos(node->pos_x, node->pos_y);

        // 初始化节点上绑定的模组（若有）
        // 注意：相同 nodeId 重复创建图形项时（例如撤销/重做），会先清掉旧的
        warroom::ModManager::instance().cleanupNodeModData(node->id);
        warroom::ModManager::instance().initNodeModData(node, this);
    }
}

NodeGraphicsItem::~NodeGraphicsItem()
{
    // 标记正在删除，防止任何后续操作
    m_pendingRemoval = true;

    // 注意：不需要手动删除 m_anchors，因为 Qt 父子关系会自动处理
    // 注意：不需要手动删除 m_editorProxy 和 m_textEdit，因为 QGraphicsProxyWidget 设置了父项

    // 如果编辑器还存在，只移除事件过滤器，不主动删除（Qt 会处理）
    if (m_textEdit) {
        m_textEdit->removeEventFilter(this);
    }

    // 将所有指针置空（仅为了调试时清晰，非必需）
    m_editorProxy = nullptr;
    m_textEdit = nullptr;
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

    auto* customEdit = new CustomTextEdit();
    m_textEdit = customEdit;

    // 配置
    customEdit->setTransparentMode(true);
    customEdit->setCustomScrollbar(true);

    //// 关键：设置文档边距为0，与预览一致
    //customEdit->document()->setDocumentMargin(0);

    // 设置字体
    customEdit->document()->setDefaultFont(m_editorFont);

    // 设置文本宽度（与预览一致）
    customEdit->document()->setTextWidth(getWidth() - m_textPadding * 2);

    // 设置文本选项（与预览一致）
    QTextOption option = customEdit->document()->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    option.setTabStopDistance(40);
    customEdit->document()->setDefaultTextOption(option);

    // 加载现有内容
    const warroom::WarNode* node = getNode();
    if (node) {
        customEdit->setMarkdown(QString::fromStdString(node->full_text));
    }

    m_editorProxy = new QGraphicsProxyWidget(this);
    m_editorProxy->setWidget(customEdit);
    m_editorProxy->setPos(m_textPadding, m_textPadding);
    m_editorProxy->setZValue(100);
    m_editorProxy->resize(getWidth() - m_textPadding * 2, getHeight() - m_textPadding * 2);

    customEdit->setFocus();
    customEdit->installEventFilter(this);
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

void NodeGraphicsItem::saveAndExitEditMode()
{
    if (m_editMode != EditMode::Editing) return;

    // 先保存内容到模型
    saveContentToModel();

    // 在销毁编辑器之前，先移除事件过滤器（避免析构时还在接收事件）
    if (m_textEdit) {
        m_textEdit->removeEventFilter(this);
    }

    // 销毁编辑器（QGraphicsProxyWidget 会自动删除其 widget）
    destroyEditor();

    m_editMode = EditMode::Preview;
    refreshPreviewDocument();
    update();
}

void NodeGraphicsItem::prepareForRemoval()
{
    if (m_pendingRemoval) return;
    m_pendingRemoval = true;

    // 1. 如果正在编辑，先保存并退出（这会触发编辑器销毁）
    if (m_editMode == EditMode::Editing) {
        saveAndExitEditMode();  // 这里会调用 destroyEditor
    }

    // 2. 如果编辑器仍然存在（异常情况），强制销毁
    if (m_editorProxy) {
        destroyEditor();
    }

    // 3. 隐藏并禁用所有锚点
    for (auto* anchor : m_anchors) {
        if (anchor) {
            anchor->hide();
            anchor->setAcceptedMouseButtons(Qt::NoButton);
        }
    }

    // 4. 清除选中状态
    setSelected(false);

    // 5. 阻止进一步的事件处理
    setAcceptHoverEvents(false);
    setFlag(ItemIsSelectable, false);
    setFlag(ItemIsMovable, false);

    // 6. 断开所有信号连接，避免外部继续引用
    disconnect();  // 断开所有 Qt 信号槽连接
}

void NodeGraphicsItem::refreshPreviewDocument() {
    const warroom::WarNode* node = getNode();
    if (!node) return;

    // 确保文档设置与编辑器一致
    //m_previewDocument.setDocumentMargin(0);
    m_previewDocument.setDefaultFont(m_editorFont);
    m_previewDocument.setTextWidth(getWidth() - m_textPadding * 2);

    // 复制编辑器的文本选项（如果编辑器存在）
    if (m_textEdit) {
        QTextOption option = m_textEdit->document()->defaultTextOption();
        m_previewDocument.setDefaultTextOption(option);
    }
    else {
        // 使用默认设置
        QTextOption option = m_previewDocument.defaultTextOption();
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
        option.setTabStopDistance(40);
        m_previewDocument.setDefaultTextOption(option);
    }

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
    // 设置与编辑器完全一致的文档边距
    //m_previewDocument.setDocumentMargin(0);
    m_previewDocument.setDefaultFont(m_editorFont);
    m_previewDocument.setTextWidth(getWidth() - m_textPadding * 2);

    // 设置文档的默认样式，与 QTextEdit 默认保持一致
    // QTextEdit 默认使用 QTextDocument 的默认样式
    // 但为了确保一致性，我们显式设置一些参数
    QTextOption option = m_previewDocument.defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // 设置行距为默认值（与 QTextEdit 一致）
    option.setTabStopDistance(40);  // QTextEdit 默认制表符宽度
    m_previewDocument.setDefaultTextOption(option);

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

    // ---- 主模组绘制（若主模组返回 true，则跳过默认背景/文本绘制） ----
    bool primaryHandled = false;
    if (!node->primary_mod_type.empty()) {
        auto& mm = warroom::ModManager::instance();
        if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
            void* data = mm.getPrimaryPrivate(node);
            warroom::ModRenderContext ctx{
                painter, rect, node, 1.0f,
                isSelected(), false
            };
            primaryHandled = mod->onPaint(ctx, data);
        }
    }

    if (!primaryHandled) {
        // ---- 默认绘制（原版纯文本节点行为） ----
        QColor color(QString::fromStdString(node->color));
        painter->setBrush(color);
        painter->setPen(QPen(color.darker(150), 2));
        painter->drawRoundedRect(rect, 8, 8);

        if (m_editMode == EditMode::Preview) {
            painter->save();
            painter->translate(m_textPadding, m_textPadding);
            QRectF clipRect(0, 0, getWidth() - m_textPadding * 2, getHeight() - m_textPadding * 2);
            m_previewDocument.drawContents(painter, clipRect);
            painter->restore();
        }
    }

    // ---- 辅助模组叠绘（不影响主模组返回值） ----
    if (!node->auxiliary_mod_types.empty()) {
        auto& mm = warroom::ModManager::instance();
        for (const auto& modType : node->auxiliary_mod_types) {
            if (warroom::NodeMod* mod = mm.getMod(modType)) {
                void* data = mm.getNodePrivate(node, modType);
                warroom::ModRenderContext ctx{
                    painter, rect, node, 1.0f,
                    isSelected(), false
                };
                mod->onPaint(ctx, data);
            }
        }
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
void NodeGraphicsItem::refreshFont(const QFont& font)
{
    m_editorFont = font;
    // 同步预览文档字体
    m_previewDocument.setDefaultFont(font);
    // 如果编辑器已打开，同步编辑器字体
    if (m_textEdit) {
        m_textEdit->document()->setDefaultFont(font);
    }
    update(); // 重绘
}

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

void NodeGraphicsItem::destroyEditor()
{
    if (!m_editorProxy) return;

    // 先通知编辑器即将被销毁，阻止后续事件
    if (m_textEdit) {
        auto* customEdit = dynamic_cast<CustomTextEdit*>(m_textEdit);
        if (customEdit) {
            customEdit->prepareForDestruction();
        }
        // 移除事件过滤器（再次确保）
        m_textEdit->removeEventFilter(this);
    }

    // 删除 proxy（会自动删除 m_textEdit）
    delete m_editorProxy;
    m_editorProxy = nullptr;
    m_textEdit = nullptr;
}

// ==================== 悬停事件 ====================
void NodeGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    if (m_pendingRemoval) {
        event->ignore();
        return;
    }

    for (auto* anchor : m_anchors) {
        if (anchor) anchor->show();
    }
    QGraphicsObject::hoverEnterEvent(event);
}

void NodeGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    if (m_pendingRemoval) {
        event->ignore();
        return;
    }

    for (auto* anchor : m_anchors) {
        if (anchor) anchor->hide();
    }
    QGraphicsObject::hoverLeaveEvent(event);
}

void NodeGraphicsItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_pendingRemoval) {
        event->ignore();
        return;
    }

    // 优先让模组处理
    const warroom::WarNode* node = getNode();
    if (node) {
        auto& mm = warroom::ModManager::instance();
        // 主模组
        if (!node->primary_mod_type.empty()) {
            if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
                void* data = mm.getPrimaryPrivate(node);
                auto r = mod->onMouseDoubleClick(event, node, data);
                if (r != warroom::ModInteractionResult::Ignored) {
                    if (r == warroom::ModInteractionResult::Consumed) {
                        event->accept();
                        return;
                    }
                    // Handled：不阻止默认行为后续逻辑——但这里已经够用，accept 退出
                    event->accept();
                    return;
                }
            }
        }
        // 辅助模组
        for (const auto& modType : node->auxiliary_mod_types) {
            if (warroom::NodeMod* mod = mm.getMod(modType)) {
                void* data = mm.getNodePrivate(node, modType);
                auto r = mod->onMouseDoubleClick(event, node, data);
                if (r == warroom::ModInteractionResult::Consumed) {
                    event->accept();
                    return;
                }
            }
        }
    }

    if (m_editMode == EditMode::Preview) {
        emit editRequested(m_nodeId);
        m_editMode = EditMode::Editing;
        createInlineEditor();
        event->accept();
        return;
    }
    QGraphicsObject::mouseDoubleClickEvent(event);
}

bool NodeGraphicsItem::eventFilter(QObject* watched, QEvent* event) {
    // 如果节点已被标记删除，忽略所有事件
    if (m_pendingRemoval) {
        return false;
    }

    if (watched == m_textEdit && event->type() == QEvent::FocusOut) {
        // 检查 m_textEdit 和当前对象是否仍然有效
        if (!m_textEdit || m_pendingRemoval) {
            return false;
        }

        // 使用 QMetaObject::invokeMethod 时，确保目标对象仍然有效
        // 注意：这里使用了 QueuedConnection，但需要确保 this 在调用时仍然有效
        // 更好的做法：使用 QPointer 捕获 this

        // 改用 QPointer 来安全调用
        QPointer<NodeGraphicsItem> self(this);
        QMetaObject::invokeMethod(this, [self]() {
            if (self && !self->m_pendingRemoval) {
                self->saveAndExitEditMode();
            }
            }, Qt::QueuedConnection);

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