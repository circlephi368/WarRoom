#include "NodeGraphicsItem.h"
#include "CustomTextEdit.h"
#include "WarRoomMainWindow.h"
#include "mod/ModManager.h"
#include <qcursor.h>
#include <QTextBlock>
#include <QMenu>
#include <QInputDialog>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QStyleOption>
#include <QRectF>

// ==================== 构造函数 ====================
NodeGraphicsItem::NodeGraphicsItem(const std::string& nodeId, warroom::WarRoomModel* model, QGraphicsItem* parent)
	: QGraphicsObject(parent), m_nodeId(nodeId), m_model(model)
{
	setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
	setAcceptHoverEvents(true);
	setAcceptDrops(true);
	createAnchors();
	
	// 使用全局配置的字体（从 settings.ini 读取）
	m_editorFont = WarRoomMainWindow::getNodeFont();
	// 使用全局配置的节点文本颜色
	m_textColor = WarRoomMainWindow::getNodeTextColor();

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
		// 自动附加 shouldAutoAttach 返回 true 的辅助模组
		warroom::ModManager::instance().autoAttachAuxiliaryMods(node, this);
	}
}

NodeGraphicsItem::~NodeGraphicsItem()
{
	// [DESTDBG] 记录析构入口，含 this、nodeId、关键成员地址
	// 若同一 this 出现两次 -> 双重析构
	qDebug().nospace().noquote()
		<< "[DESTDBG] >>> ~NodeGraphicsItem ENTER this=" << static_cast<void*>(this)
		<< " nodeId=" << QString::fromStdString(m_nodeId)
		<< " | m_model=" << static_cast<void*>(m_model)
		<< " | m_editorProxy=" << static_cast<void*>(m_editorProxy)
		<< " | m_textEdit=" << static_cast<void*>(m_textEdit)
		<< " | m_browserProxy=" << static_cast<void*>(m_browserProxy)
		<< " | anchors=" << m_anchors.size();

	// 标记正在删除，防止任何后续操作
	m_pendingRemoval = true;

	// 先断开所有信号槽，防止外部回调到正在析构的对象
	qDebug() << "[DESTDBG]   ~NodeGraphicsItem step1: disconnect()";
	disconnect();

	// 清理浏览器 widget（如果存在）
	if (m_browserProxy) {
		destroyBrowserWidget();
	}

	// 注意：不需要手动删除 m_anchors，因为 Qt 父子关系会自动处理
	// 注意：不需要手动删除 m_editorProxy 和 m_textEdit，因为 QGraphicsProxyWidget 设置了父项

	// 如果编辑器还存在，只移除事件过滤器，不主动删除（Qt 会处理）
	if (m_textEdit) {
		qDebug() << "[DESTDBG]   ~NodeGraphicsItem step2: removeEventFilter from m_textEdit";
		m_textEdit->removeEventFilter(this);
	}

	// 清理模组私有数据（只通过 nodeId 调用，不访问 m_model 指针）
	// 这里不依赖 m_model，直接通过 ModManager 的 nodeId 版本清理
	qDebug() << "[DESTDBG]   ~NodeGraphicsItem step3: cleanupNodeModData for nodeId="
			 << QString::fromStdString(m_nodeId);
	try {
		warroom::ModManager::instance().cleanupNodeModData(m_nodeId);
	} catch (...) {
		qDebug() << "[DESTDBG]   ~NodeGraphicsItem step3: EXCEPTION caught in cleanupNodeModData";
		// 析构函数中不抛出异常
	}
	qDebug() << "[DESTDBG]   ~NodeGraphicsItem step3: cleanupNodeModData done";

	// 将所有指针置空（仅为了调试时清晰，非必需）
	m_editorProxy = nullptr;
	m_textEdit = nullptr;
	m_browserProxy = nullptr;
	m_model = nullptr;

	qDebug().nospace().noquote()
		<< "[DESTDBG] <<< ~NodeGraphicsItem EXIT this=" << static_cast<void*>(this);
}

// ==================== 辅助方法 ====================
const warroom::WarNode* NodeGraphicsItem::getNode() const
{
	if (!m_model) return nullptr;
	return m_model->getNode(m_nodeId);
}

warroom::WarNode* NodeGraphicsItem::getNodeMutable()
{
	if (!m_model) return nullptr;
	return m_model->getNodeMutable(m_nodeId);
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

	// 加载现有内容（用 setPlainText 保留原始文本，避免 toMarkdown 的转义累积问题）
	const warroom::WarNode* node = getNode();
	if (node) {
		customEdit->setPlainText(QString::fromStdString(node->full_text));
	}

	// 用 QTextCursor 全选并强制应用字体和颜色（setDefaultFont 仅对新输入生效，
	// 对已有内容特别是 Markdown 解析后的内容无效，需要 cursor 方式覆盖）
	{
		QTextCursor cursor(customEdit->document());
		cursor.select(QTextCursor::Document);
		QTextCharFormat fmt;
		fmt.setFont(m_editorFont);
		fmt.setForeground(QBrush(m_textColor));
		cursor.mergeCharFormat(fmt);
	}

	// 设置 palette 文本颜色，确保删除全部文本后重新输入也使用设置的颜色
	// （mergeCharFormat 仅影响已有文本，新输入的默认色由 palette 决定）
	// 注意：必须在 setWidget() 之后设置，否则 proxy 会覆盖 palette；
	// 且需显式设置 viewport 的 palette，光标颜色由 viewport palette 决定
	// 此调用在下方 setWidget 之后执行

	// 统一编辑器所有文本块的行距和段落间距，与预览模式保持一致
	{
		QTextBlockFormat blockFmt;
		blockFmt.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
		blockFmt.setTopMargin(0);
		blockFmt.setBottomMargin(4);   // 段落间距：段后 4px
		for (QTextBlock block = customEdit->document()->begin(); block.isValid(); block = block.next()) {
			QTextCursor cursor(block);
			cursor.mergeBlockFormat(blockFmt);
		}
	}

	m_editorProxy = new QGraphicsProxyWidget(this);
	m_editorProxy->setWidget(customEdit);
	m_editorProxy->setPos(m_textPadding, m_textPadding);
	m_editorProxy->resize(getWidth() - m_textPadding * 2, getHeight() - m_textPadding * 2);
	m_editorProxy->setZValue(100);

	// setWidget 之后显式设置 palette（含 viewport），确保光标颜色正确
	{
		QPalette pal = customEdit->palette();
		pal.setColor(QPalette::Text, m_textColor);
		customEdit->setPalette(pal);
		customEdit->viewport()->setPalette(pal);
	}

	customEdit->setFocus();
	customEdit->installEventFilter(this);

	// 右键菜单请求：CustomTextEdit::contextMenuEvent 转发到这里
	connect(customEdit, &CustomTextEdit::customContextMenuRequested,
			this, &NodeGraphicsItem::showEditorContextMenu);
}

void NodeGraphicsItem::saveContentToModel() {
	if (!m_textEdit) return;

	// 只读模式下不保存修改（但允许用户进入编辑模式以便复制内容）
	if (m_model && m_model->isReadOnly()) {
		return;
	}

	warroom::WarNode* node = getNodeMutable();
	if (node) {
		std::string newContent = m_textEdit->toPlainText().toStdString();
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

	emit editFinished(m_nodeId);
}

QString NodeGraphicsItem::getSelectedText() const
{
	if (m_editMode != EditMode::Editing || !m_textEdit) return {};
	return m_textEdit->textCursor().selectedText();
}

void NodeGraphicsItem::setTitleFromString(const std::string& newTitle)
{
	warroom::WarNode* n = getNodeMutable();
	if (n) {
		n->title = newTitle;
		emit titleChanged(m_nodeId);
	}
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

	// 3. 如果浏览器 widget 存在，销毁它
	if (m_browserProxy) {
		destroyBrowserWidget();
	}

	// 4. 隐藏并禁用所有锚点
	for (auto* anchor : m_anchors) {
		if (anchor) {
			anchor->hide();
			anchor->setAcceptedMouseButtons(Qt::NoButton);
		}
	}

	// 5. 清除选中状态
	setSelected(false);

	// 6. 阻止进一步的事件处理
	setAcceptHoverEvents(false);
	setFlag(ItemIsSelectable, false);
	setFlag(ItemIsMovable, false);

	// 7. 断开所有信号连接，避免外部继续引用
	disconnect();  // 断开所有 Qt 信号槽连接
}

void NodeGraphicsItem::refreshPreviewDocument() {
	const warroom::WarNode* node = getNode();
	if (!node) return;

	// 内容变更时递增版本号并失效缓存
	m_currentVersion++;
	invalidateCache();

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

	// 根据节点的 text_display_mode 选择渲染方式
	if (node->text_display_mode == "plain") {
		// 纯文本模式：直接显示原始文本，不做 Markdown 解析
		m_previewDocument.setPlainText(QString::fromStdString(node->full_text));
	}
	else {
		// Markdown 模式（默认）：预处理换行后解析
		// 将纯文本的每个换行都转为 Markdown 段落分隔
		// 用户按一次回车即视为一个新段落（有段落间距），与编辑器行为一致
		// Markdown 中单 \n 不换行（视为空格），需 \n\n 才是段落分隔
		QString text = QString::fromStdString(node->full_text);
		// 先保护已有的段落分隔（连续空行），避免重复替换
		text.replace("\n\n", "\x01\x01");
		// 单 \n 转为段落分隔（每个行都是一个独立段落）
		text.replace("\n", "\n\n");
		// 恢复原有段落分隔
		text.replace("\x01\x01", "\n\n");
		m_previewDocument.setMarkdown(text);
	}

	// 用 QTextCursor 全选并应用字体族名和颜色
	// 注意：只用 setFontFamily() 而非 setFont()，以保留 Markdown 设置的
	// 标题字号、粗体字重等格式（setFont 会覆盖这些）
	{
		QTextCursor cursor(&m_previewDocument);
		cursor.select(QTextCursor::Document);
		QTextCharFormat fmt;
		fmt.setFontFamily(m_editorFont.family());
		fmt.setForeground(QBrush(m_textColor));
		cursor.mergeCharFormat(fmt);
	}

	// 统一预览文档所有文本块的行距和段落间距，与编辑器保持一致
	{
		QTextBlockFormat blockFmt;
		blockFmt.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
		blockFmt.setTopMargin(0);
		blockFmt.setBottomMargin(4);   // 段落间距：段后 4px
		for (QTextBlock block = m_previewDocument.begin(); block.isValid(); block = block.next()) {
			QTextCursor cursor(block);
			cursor.mergeBlockFormat(blockFmt);
		}
	}
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

// ==================== 文字渲染缓存实现 ====================
bool NodeGraphicsItem::cacheIsValid(qreal zoom, qreal dpr) const
{
	if (m_cachedPixmap.isNull()) return false;
	if (m_cacheVersion != m_currentVersion) return false;
	if (!qFuzzyCompare(m_cachedZoom, zoom)) return false;
	if (!qFuzzyCompare(m_cachedDPR, dpr)) return false;
	return true;
}

void NodeGraphicsItem::invalidateCache()
{
	m_cachedPixmap = QPixmap();
	m_cachedZoom = 0.0;
	m_cachedDPR = 0.0;
	m_cacheVersion = -1;
}

void NodeGraphicsItem::rebuildCachePixmap(qreal zoom, qreal dpr)
{
	int nodeW = static_cast<int>(getWidth());
	int nodeH = static_cast<int>(getHeight());
	int textW = nodeW - m_textPadding * 2;
	int textH = nodeH - m_textPadding * 2;

	if (textW <= 0 || textH <= 0) return;

	// 缓存 pixmap 的逻辑尺寸 = textW × textH（与绘制位置匹配）
	// 物理像素尺寸 = textW * zoom * dpr（保证缩放后的清晰度）
	int physW = static_cast<int>(textW * zoom * dpr);
	int physH = static_cast<int>(textH * zoom * dpr);

	if (physW <= 0 || physH <= 0) return;

	m_cachedPixmap = QPixmap(physW, physH);
	// 设置 DPR 使逻辑尺寸 = textW × textH
	// 物理尺寸 = textW * zoom * dpr, 逻辑尺寸 = 物理尺寸 / DPR
	// 要使逻辑尺寸 = textW, 则 DPR = 物理尺寸 / textW = zoom * dpr
	m_cachedPixmap.setDevicePixelRatio(zoom * dpr);
	m_cachedPixmap.fill(Qt::transparent);

	QPainter cachePainter(&m_cachedPixmap);
	cachePainter.setRenderHint(QPainter::Antialiasing);
	cachePainter.setRenderHint(QPainter::TextAntialiasing);

	// pixmap 逻辑尺寸 = textW × textH，直接绘制内容即可
	m_previewDocument.drawContents(&cachePainter, QRectF(0, 0, textW, textH));
	cachePainter.end();

	m_cachedZoom = zoom;
	m_cachedDPR = dpr;
	m_cacheVersion = m_currentVersion;
}

// ==================== 绘制 ====================
void NodeGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
	QWidget* widget)
{
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
			// 获取当前实际缩放等级：优先从 option->levelOfDetail，备选从 view transform
			qreal currentZoom = 1.0;
			//if (option) {
			//	currentZoom = option->levelOfDetail;
			//} else 
			if (scene() && !scene()->views().isEmpty()) {
				currentZoom = scene()->views().first()->transform().m11();
			}
			qreal currentDPR = painter->device()->devicePixelRatioF();

			if (cacheIsValid(currentZoom, currentDPR)) {
				// 缓存有效：直接绘制缓存的 pixmap（使用 int 重载避免 QRectF 重载问题）
				painter->drawPixmap(m_textPadding, m_textPadding, m_cachedPixmap);
			} else {
				// 缓存无效：检查缩放是否稳定
				// 只有缩放稳定时才重建缓存，避免动画过程中频繁重建
				constexpr qreal kZoomStableThreshold = 0.05;
				bool zoomStable = qFuzzyIsNull(m_cachedZoom) ||
					std::abs(currentZoom - m_cachedZoom) < kZoomStableThreshold;

				if (zoomStable) {
					// 缩放稳定：重建缓存并使用
					rebuildCachePixmap(currentZoom, currentDPR);
					if (!m_cachedPixmap.isNull()) {
						painter->drawPixmap(m_textPadding, m_textPadding, m_cachedPixmap);
					} else {
						painter->save();
						painter->translate(m_textPadding, m_textPadding);
						QRectF clipRect(0, 0, getWidth() - m_textPadding * 2, getHeight() - m_textPadding * 2);
						m_previewDocument.drawContents(painter, clipRect);
						painter->restore();
					}
				} else {
					// 缩放不稳定（动画进行中）：直接绘制，不重建缓存
					painter->save();
					painter->translate(m_textPadding, m_textPadding);
					QRectF clipRect(0, 0, getWidth() - m_textPadding * 2, getHeight() - m_textPadding * 2);
					m_previewDocument.drawContents(painter, clipRect);
					painter->restore();
				}
			}
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
	// 字体变更时递增版本号并失效缓存
	m_currentVersion++;
	invalidateCache();
	// 重新刷新预览文档（内部会用 cursor 强制应用字体）
	refreshPreviewDocument();
	// 如果编辑器已打开，同步编辑器字体和颜色
	if (m_textEdit) {
		m_textEdit->document()->setDefaultFont(font);
		QTextCursor cursor(m_textEdit->document());
		cursor.select(QTextCursor::Document);
		QTextCharFormat fmt;
		fmt.setFont(font);
		fmt.setForeground(QBrush(m_textColor));
		cursor.mergeCharFormat(fmt);
		QPalette pal = m_textEdit->palette();
		pal.setColor(QPalette::Text, m_textColor);
		m_textEdit->setPalette(pal);
		m_textEdit->viewport()->setPalette(pal);
	}
	update(); // 重绘
}

void NodeGraphicsItem::refreshTextColor(const QColor& color)
{
	m_textColor = color;
	// 颜色变更时递增版本号并失效缓存
	m_currentVersion++;
	invalidateCache();
	// 重新刷新预览文档以应用新颜色
	refreshPreviewDocument();
	// 如果编辑器已打开，同步编辑器字体和颜色
	if (m_textEdit) {
		QTextCursor cursor(m_textEdit->document());
		cursor.select(QTextCursor::Document);
		QTextCharFormat fmt;
		fmt.setFont(m_editorFont);
		fmt.setForeground(QBrush(m_textColor));
		cursor.mergeCharFormat(fmt);
		QPalette pal = m_textEdit->palette();
		pal.setColor(QPalette::Text, m_textColor);
		m_textEdit->setPalette(pal);
		m_textEdit->viewport()->setPalette(pal);
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
		// 如果浏览器 widget 存在，更新几何信息
		if (m_browserProxy) {
			updateBrowserGeometry();
		}
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

	// 尺寸变更时递增版本号并失效缓存
	m_currentVersion++;
	invalidateCache();

	// 更新预览文档宽度
	m_previewDocument.setTextWidth(width - m_textPadding * 2);

	// 如果编辑器存在，也更新编辑器大小
	if (m_editorProxy) {
		m_editorProxy->resize(width - m_textPadding * 2, height - m_textPadding * 2);
		if (m_textEdit) {
			m_textEdit->document()->setTextWidth(width - m_textPadding * 2);
		}
	}

	// 如果浏览器 widget 存在，更新其几何信息
	if (m_browserProxy) {
		updateBrowserGeometry();
	}

	updateAnchorsPosition();
	update();
	emit sizeChanged(m_nodeId, width, height);
}

void NodeGraphicsItem::setEditMode(EditMode mode) {
	if (m_editMode == mode) return;

	if (mode == EditMode::Editing) {
		invalidateCache();  // 进入编辑模式时清除预览缓存
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

// ==================== 浏览器嵌入相关 ====================
void NodeGraphicsItem::createBrowserWidget()
{
	if (m_browserProxy) {
		destroyBrowserWidget();
	}

	const warroom::WarNode* node = getNode();
	if (!node) return;

	auto& mm = warroom::ModManager::instance();

	warroom::NodeMod* activeMod = nullptr;
	void* modData = nullptr;

	if (!node->primary_mod_type.empty()) {
		if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
			void* data = mm.getPrimaryPrivate(node);
			if (mod->hasEmbeddedWidget() && mod->isEmbeddedWidgetActive(data)) {
				activeMod = mod;
				modData = data;
			}
		}
	}

	if (!activeMod) {
		for (const auto& modType : node->auxiliary_mod_types) {
			if (warroom::NodeMod* mod = mm.getMod(modType)) {
				void* data = mm.getNodePrivate(node, modType);
				if (mod->hasEmbeddedWidget() && mod->isEmbeddedWidgetActive(data)) {
					activeMod = mod;
					modData = data;
					break;
				}
			}
		}
	}

	if (!activeMod) return;

	m_browserProxy = new QGraphicsProxyWidget(this);

	// QGraphicsProxyWidget 继承自 QGraphicsObject 而非 QWidget，
	// 所以不能作为 QWidget* parent。先传 nullptr，再通过 setWidget() 接管。
	QWidget* browserWidget = activeMod->createEmbeddedWidget(
		const_cast<warroom::WarNode*>(node), modData, nullptr);

	if (!browserWidget) {
		delete m_browserProxy;
		m_browserProxy = nullptr;
		return;
	}

	m_browserProxy->setWidget(browserWidget);
	m_browserProxy->setZValue(50);

	// 安装事件过滤器以捕获 ESC 键退出浏览模式
	browserWidget->installEventFilter(this);

	updateBrowserGeometry();
	update();
}

void NodeGraphicsItem::destroyBrowserWidget()
{
	if (!m_browserProxy) return;

	// 移除事件过滤器
	if (QWidget* w = m_browserProxy->widget()) {
		w->removeEventFilter(this);
	}

	const warroom::WarNode* node = getNode();
	auto& mm = warroom::ModManager::instance();

	if (node) {
		if (!node->primary_mod_type.empty()) {
			if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
				void* data = mm.getPrimaryPrivate(node);
				if (mod->hasEmbeddedWidget()) {
					QWidget* w = m_browserProxy->widget();
					if (w) {
						mod->destroyEmbeddedWidget(w, data);
					}
				}
			}
		}
		for (const auto& modType : node->auxiliary_mod_types) {
			if (warroom::NodeMod* mod = mm.getMod(modType)) {
				void* data = mm.getNodePrivate(node, modType);
				if (mod->hasEmbeddedWidget()) {
					QWidget* w = m_browserProxy->widget();
					if (w) {
						mod->destroyEmbeddedWidget(w, data);
					}
				}
			}
		}
	}

	delete m_browserProxy;
	m_browserProxy = nullptr;
	update();
}

void NodeGraphicsItem::updateBrowserGeometry()
{
	if (!m_browserProxy) return;

	int margin = 2;
	int navBarHeight = 28;
	int w = getWidth() - margin * 2;
	int h = getHeight() - margin * 2 - navBarHeight;

	if (w > 0 && h > 0) {
		m_browserProxy->setPos(margin, margin + navBarHeight);
		m_browserProxy->resize(w, h);
	}
}

bool NodeGraphicsItem::isInBrowseMode() const
{
	if (!m_browserProxy) return false;

	const warroom::WarNode* node = getNode();
	if (!node) return false;

	auto& mm = warroom::ModManager::instance();

	if (!node->primary_mod_type.empty()) {
		if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
			void* data = mm.getPrimaryPrivate(node);
			return mod->isEmbeddedWidgetActive(data);
		}
	}

	for (const auto& modType : node->auxiliary_mod_types) {
		if (warroom::NodeMod* mod = mm.getMod(modType)) {
			void* data = mm.getNodePrivate(node, modType);
			if (mod->isEmbeddedWidgetActive(data)) return true;
		}
	}

	return false;
}

void NodeGraphicsItem::keyPressEvent(QKeyEvent* event)
{
	if (m_pendingRemoval) {
		event->ignore();
		return;
	}

	const warroom::WarNode* node = getNode();
	if (node) {
		auto& mm = warroom::ModManager::instance();
		// 主模组优先
		if (!node->primary_mod_type.empty()) {
			if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
				void* data = mm.getPrimaryPrivate(node);
				auto r = mod->onKeyPress(event, node, data);
				if (r == warroom::ModInteractionResult::Consumed) {
					if (!mod->isEmbeddedWidgetActive(data)) {
						destroyBrowserWidget();
					}
					event->accept();
					return;
				}
			}
		}
		// 辅助模组
		for (const auto& modType : node->auxiliary_mod_types) {
			if (warroom::NodeMod* mod = mm.getMod(modType)) {
				void* data = mm.getNodePrivate(node, modType);
				auto r = mod->onKeyPress(event, node, data);
				if (r == warroom::ModInteractionResult::Consumed) {
					if (!mod->isEmbeddedWidgetActive(data)) {
						destroyBrowserWidget();
					}
					event->accept();
					return;
				}
			}
		}
	}

	QGraphicsObject::keyPressEvent(event);
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

	// 只读模式下禁止模组双击操作（如换图、换视频等）
	// 但允许进入文本编辑模式以便复制内容（保存时会检查只读）
	const warroom::WarNode* node = getNode();
	if (node && m_model && !m_model->isReadOnly()) {
		// 优先让辅助模组处理（上层图形先接收交互），再主模组
		auto& mm = warroom::ModManager::instance();
		// 辅助模组
		for (const auto& modType : node->auxiliary_mod_types) {
			if (warroom::NodeMod* mod = mm.getMod(modType)) {
				void* data = mm.getNodePrivate(node, modType);
				auto r = mod->onMouseDoubleClick(event, node, data);
				if (r == warroom::ModInteractionResult::Consumed) {
					if (mod->isEmbeddedWidgetActive(data)) {
						createBrowserWidget();
					}
					event->accept();
					return;
				}
			}
		}
		// 主模组
		if (!node->primary_mod_type.empty()) {
			if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
				void* data = mm.getPrimaryPrivate(node);
				auto r = mod->onMouseDoubleClick(event, node, data);
				if (r != warroom::ModInteractionResult::Ignored) {
					if (mod->isEmbeddedWidgetActive(data)) {
						createBrowserWidget();
					}
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

	// 浏览器 widget 的事件过滤（捕获 ESC 退出浏览模式）
	if (m_browserProxy && watched == m_browserProxy->widget()) {
		if (event->type() == QEvent::KeyPress) {
			QKeyEvent* ke = static_cast<QKeyEvent*>(event);
			if (ke->key() == Qt::Key_Escape) {
				// 通知模组退出浏览模式
				const warroom::WarNode* node = getNode();
				auto& mm = warroom::ModManager::instance();
				if (node) {
					if (!node->primary_mod_type.empty()) {
						if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
							void* data = mm.getPrimaryPrivate(node);
							mod->onKeyPress(ke, node, data);
						}
					}
				}
				destroyBrowserWidget();
				update();
				event->accept();
				return true;
			}
		}
	}

	if (watched == m_textEdit) {
		if (event->type() == QEvent::FocusOut) {
			qDebug() << "[CTXDBG] FocusOut on m_textEdit, m_inContextMenu =" << m_inContextMenu;

			// 右键菜单显示期间忽略 FocusOut，避免编辑器被提前销毁
			if (m_inContextMenu) {
				qDebug() << "[CTXDBG] FocusOut ignored (in context menu)";
				return true;   // 消费事件，不触发 saveAndExitEditMode
			}

			// 检查 m_textEdit 和当前对象是否仍然有效
			if (!m_textEdit || m_pendingRemoval) {
				return false;
			}

			// 改用 QPointer 来安全调用
			QPointer<NodeGraphicsItem> self(this);
			QMetaObject::invokeMethod(this, [self]() {
				if (self && !self->m_pendingRemoval) {
					self->saveAndExitEditMode();
				}
				}, Qt::QueuedConnection);

			return false;
		}
	}
	return QGraphicsObject::eventFilter(watched, event);
}

void NodeGraphicsItem::showEditorContextMenu(const QPoint& globalPos) {
	if (!m_textEdit || m_pendingRemoval) return;
	if (m_model && m_model->isReadOnly()) return;

	qDebug() << "[CTXDBG] showEditorContextMenu called";

	// 构建菜单（与 WarRoomMainWindow 右键菜单相同的深色扁平样式）
	QMenu* menu = new QMenu();
	menu->setAttribute(Qt::WA_DeleteOnClose);
	menu->setStyleSheet(R"(
		QMenu {
			background-color: #2D2D2D;
			border: 1px solid #3A3A3A;
			border-radius: 4px;
			padding: 4px 0px;
		}
		QMenu::item {
			background-color: transparent;
			color: #CCCCCC;
			padding: 6px 28px 6px 20px;
			border: none;
			margin: 0px 4px;
			border-radius: 2px;
		}
		QMenu::item:selected {
			background-color: #4A4A4A;
			color: #FFFFFF;
		}
		QMenu::item:pressed {
			background-color: #3A6A9A;
			color: #FFFFFF;
		}
		QMenu::item:disabled {
			color: #666666;
		}
		QMenu::separator {
			height: 1px;
			background-color: #3A3A3A;
			margin: 4px 8px;
		}
	)");

	// 标准编辑动作（复制/剪切/粘贴/全选/撤销/重做）
	QAction* actUndo = menu->addAction("撤销");
	actUndo->setEnabled(m_textEdit->document()->isUndoAvailable());
	QAction* actRedo = menu->addAction("重做");
	actRedo->setEnabled(m_textEdit->document()->isRedoAvailable());
	menu->addSeparator();
	QAction* actCut = menu->addAction("剪切");
	QAction* actCopy = menu->addAction("复制");
	QAction* actPaste = menu->addAction("粘贴");
	menu->addSeparator();
	QAction* actSelectAll = menu->addAction("全选");

	QTextCursor cursor = m_textEdit->textCursor();
	bool hasSel = cursor.hasSelection();
	actCut->setEnabled(hasSel);
	actCopy->setEnabled(hasSel);

	// ---- 标题相关 ----
	menu->addSeparator();

	QAction* actSelToTitle = menu->addAction("将选中文字设为标题");
	actSelToTitle->setEnabled(hasSel);

	QString currentTitle;
	const warroom::WarNode* node = getNode();
	if (node) currentTitle = QString::fromStdString(node->title);
	QAction* actRenameTitle = menu->addAction("重命名标题…");

	QPointer<NodeGraphicsItem> self(this);
	QString selText = hasSel ? cursor.selectedText() : QString();
	QString capturedTitle = currentTitle;

	connect(actUndo, &QAction::triggered, m_textEdit, &QTextEdit::undo);
	connect(actRedo, &QAction::triggered, m_textEdit, &QTextEdit::redo);
	connect(actCut, &QAction::triggered, m_textEdit, &QTextEdit::cut);
	connect(actCopy, &QAction::triggered, m_textEdit, &QTextEdit::copy);
	connect(actPaste, &QAction::triggered, m_textEdit, &QTextEdit::paste);
	connect(actSelectAll, &QAction::triggered, m_textEdit, &QTextEdit::selectAll);

	connect(actSelToTitle, &QAction::triggered, menu, [self, selText]() {
		if (self) self->setTitleFromString(selText.toStdString());
	});
	connect(actRenameTitle, &QAction::triggered, menu, [self, capturedTitle]() {
		if (!self) return;
		bool ok = false;
		QString text = QInputDialog::getText(
			nullptr, "重命名标题", "标题：",
			QLineEdit::Normal, capturedTitle, &ok);
		if (ok) self->setTitleFromString(text.toStdString());
	});

	// 关键：exec 期间 menu 会抢焦点触发 m_textEdit 的 FocusOut，
	// 设置标志抑制 FocusOut 导致的 saveAndExitEditMode
	m_inContextMenu = true;
	qDebug() << "[CTXDBG] before menu->exec, m_inContextMenu = true";
	menu->exec(globalPos);
	qDebug() << "[CTXDBG] after menu->exec, m_inContextMenu = false";
	m_inContextMenu = false;

	// exec 返回后主动让编辑器重新获焦，避免因失焦退出编辑
	if (m_textEdit) {
		m_textEdit->setFocus();
	}
}

// ==================== 项目变化事件 ====================
QVariant NodeGraphicsItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
	if (change == ItemPositionHasChanged) {
		emit positionChanged(m_nodeId,
			static_cast<float>(pos().x()),
			static_cast<float>(pos().y()));
		if (m_browserProxy) {
			updateBrowserGeometry();
		}
	}
	return QGraphicsItem::itemChange(change, value);
}

// ==================== 鼠标按下事件 ====================
void NodeGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
	// 只读模式下禁止操作
	if (m_model && m_model->isReadOnly()) {
		event->ignore();
		return;
	}

	// 优先让辅助模组处理（上层图形先接收交互），再主模组
	const warroom::WarNode* node = getNode();
	if (node && m_model && event->button() == Qt::LeftButton) {
		auto& mm = warroom::ModManager::instance();
		// 辅助模组
		for (const auto& modType : node->auxiliary_mod_types) {
			if (warroom::NodeMod* mod = mm.getMod(modType)) {
				void* data = mm.getNodePrivate(node, modType);
				auto r = mod->onMousePress(event, node, data);
				if (r == warroom::ModInteractionResult::Consumed) {
					event->accept();
					return;
				}
			}
		}
		// 主模组
		if (!node->primary_mod_type.empty()) {
			if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
				void* data = mm.getPrimaryPrivate(node);
				auto r = mod->onMousePress(event, node, data);
				if (r == warroom::ModInteractionResult::Consumed) {
					event->accept();
					return;
				}
			}
		}
	}

	if (event->button() == Qt::LeftButton) {
		// 如果节点尚未被选中，或者点击时改变选中状态
		if (!isSelected()) {
			emit selectedForZBoost(m_nodeId);
		}

		// 调整节点大小（浏览模式下仍允许调整大小）
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

		// 浏览模式下禁止拖拽节点
		if (isInBrowseMode()) {
			event->accept();
			return;
		}

		// 拖拽节点位置
		m_dragStartX = static_cast<float>(pos().x());
		m_dragStartY = static_cast<float>(pos().y());
	}
	QGraphicsObject::mousePressEvent(event);
}

// ==================== 拖放事件（转发给模组）====================
void NodeGraphicsItem::dragEnterEvent(QGraphicsSceneDragDropEvent* event)
{
	if (m_pendingRemoval) { event->ignore(); return; }
	const QMimeData* mime = event->mimeData();
	if (!mime) { event->ignore(); return; }

	const warroom::WarNode* node = getNode();
	if (!node) { event->ignore(); return; }

	auto& mm = warroom::ModManager::instance();
	// 辅助模组优先（上层图形先接收交互），再主模组
	for (const auto& modType : node->auxiliary_mod_types) {
		if (warroom::NodeMod* mod = mm.getMod(modType)) {
			void* data = mm.getNodePrivate(node, modType);
			if (mod->canAcceptDrop(mime, node, data)) {
				event->acceptProposedAction();
				return;
			}
		}
	}
	if (!node->primary_mod_type.empty()) {
		if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
			void* data = mm.getPrimaryPrivate(node);
			if (mod->canAcceptDrop(mime, node, data)) {
				event->acceptProposedAction();
				return;
			}
		}
	}
	event->ignore();
}

void NodeGraphicsItem::dragMoveEvent(QGraphicsSceneDragDropEvent* event)
{
	if (m_pendingRemoval) { event->ignore(); return; }
	const QMimeData* mime = event->mimeData();
	if (!mime) { event->ignore(); return; }

	const warroom::WarNode* node = getNode();
	if (!node) { event->ignore(); return; }

	auto& mm = warroom::ModManager::instance();
	for (const auto& modType : node->auxiliary_mod_types) {
		if (warroom::NodeMod* mod = mm.getMod(modType)) {
			void* data = mm.getNodePrivate(node, modType);
			if (mod->canAcceptDrop(mime, node, data)) {
				event->acceptProposedAction();
				return;
			}
		}
	}
	if (!node->primary_mod_type.empty()) {
		if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
			void* data = mm.getPrimaryPrivate(node);
			if (mod->canAcceptDrop(mime, node, data)) {
				event->acceptProposedAction();
				return;
			}
		}
	}
	event->ignore();
}

void NodeGraphicsItem::dropEvent(QGraphicsSceneDragDropEvent* event)
{
	if (m_pendingRemoval) { event->ignore(); return; }
	const QMimeData* mime = event->mimeData();
	if (!mime) { event->ignore(); return; }

	const warroom::WarNode* node = getNode();
	if (!node) { event->ignore(); return; }

	auto& mm = warroom::ModManager::instance();
	// 辅助模组优先（上层图形先接收交互），再主模组
	for (const auto& modType : node->auxiliary_mod_types) {
		if (warroom::NodeMod* mod = mm.getMod(modType)) {
			void* data = mm.getNodePrivate(node, modType);
			if (mod->canAcceptDrop(mime, node, data)) {
				mod->onDrop(mime, m_model->getNodeMutable(node->id), data);
				event->acceptProposedAction();
				update();
				return;
			}
		}
	}
	if (!node->primary_mod_type.empty()) {
		if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
			void* data = mm.getPrimaryPrivate(node);
			if (mod->canAcceptDrop(mime, node, data)) {
				mod->onDrop(mime, m_model->getNodeMutable(node->id), data);
				event->acceptProposedAction();
				update();
				return;
			}
		}
	}
	event->ignore();
}

// ==================== 鼠标移动事件 ====================
void NodeGraphicsItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
	// 只读模式下禁止拖动和调整大小
	if (m_model && m_model->isReadOnly()) {
		event->ignore();
		return;
	}

	// 优先让辅助模组处理（上层图形先接收交互），再主模组
	const warroom::WarNode* node = getNode();
	if (node && m_model && event->button() == Qt::NoButton) {
		auto& mm = warroom::ModManager::instance();
		// 辅助模组
		for (const auto& modType : node->auxiliary_mod_types) {
			if (warroom::NodeMod* mod = mm.getMod(modType)) {
				void* data = mm.getNodePrivate(node, modType);
				auto r = mod->onMouseMove(event, node, data);
				if (r == warroom::ModInteractionResult::Consumed) {
					event->accept();
					update();
					return;
				}
			}
		}
		// 主模组
		if (!node->primary_mod_type.empty()) {
			if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
				void* data = mm.getPrimaryPrivate(node);
				auto r = mod->onMouseMove(event, node, data);
				if (r == warroom::ModInteractionResult::Consumed) {
					event->accept();
					update();
					return;
				}
			}
		}
	}

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
	// 优先让辅助模组处理（上层图形先接收交互），再主模组
	const warroom::WarNode* node = getNode();
	if (node && m_model) {
		auto& mm = warroom::ModManager::instance();
		// 辅助模组
		for (const auto& modType : node->auxiliary_mod_types) {
			if (warroom::NodeMod* mod = mm.getMod(modType)) {
				void* data = mm.getNodePrivate(node, modType);
				auto r = mod->onMouseRelease(event, node, data);
				if (r == warroom::ModInteractionResult::Consumed) {
					event->accept();
					update();
					return;
				}
			}
		}
		// 主模组
		if (!node->primary_mod_type.empty()) {
			if (warroom::NodeMod* mod = mm.getMod(node->primary_mod_type)) {
				void* data = mm.getPrimaryPrivate(node);
				auto r = mod->onMouseRelease(event, node, data);
				if (r == warroom::ModInteractionResult::Consumed) {
					event->accept();
					update();
					return;
				}
			}
		}
	}

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