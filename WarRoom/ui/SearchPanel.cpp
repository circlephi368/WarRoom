// SearchPanel.cpp
//
// Ctrl+H 浮层搜索面板实现。
// 设计要点：
//   - setAttribute(Qt::WA_ShowWithoutActivating) 确保弹出时不抢主窗口活动状态
//   - 无边框、可拖动（搜索框背景区域为拖动条）
//   - 防抖 300ms 避免极快打字时每帧全量匹配
//   - 搜索范围：WarNode::full_text 大小写不敏感子串匹配
//   - 结果列表用 QListView + 自定义 delegate 绘制匹配字符高亮

#include "SearchPanel.h"
#include "core/warroom/war_room_model.h"
#include "core/warroom/war_node.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QApplication>
#include <QScreen>
#include <QScrollBar>
#include <QFontMetrics>
#include <QStyleOptionViewItem>
#include <algorithm>
#include <cctype>
#include <QGraphicsOpacityEffect>

// ============================================================================
// SearchPanel
// ============================================================================

SearchPanel::SearchPanel(warroom::WarRoomModel* model, QWidget* parent)
	: QFrame(parent)
	, m_model(model)
{
	// 面板自身属性
	setAttribute(Qt::WA_ShowWithoutActivating, true);  // 弹出时不抢主窗口活动状态
	setAttribute(Qt::WA_NoSystemBackground, false);
	setWindowFlags(Qt::FramelessWindowHint | Qt::Widget);  // 无边框、不是独立窗口

	// 透明度（子 Widget 不能用 setWindowOpacity，要用 GraphicsEffect）
	auto* opacity = new QGraphicsOpacityEffect(this);
	opacity->setOpacity(0.95);  // 轻微半透明，有"浮在画布上"的感觉
	setGraphicsEffect(opacity);

	// 深暗色扁平样式
	// 关键：#SearchPanel 选择器让 root widget 有完整的窗口边框
	setObjectName(QStringLiteral("SearchPanel"));
	setStyleSheet(R"(
		#SearchPanel {
			background-color: #2B2B2D;
			border: 1px solid #4A4A4A;
			border-radius: 6px;
			color: #CCCCCC;
		}
		QFrame#SearchPanelTitleBar {
			background-color: #333335;
			border-bottom: 1px solid #3E3E40;
			border-top-left-radius: 6px;
			border-top-right-radius: 6px;
		}
		QLabel#SearchPanelTitleLabel {
			color: #AAAAAA;
			font-size: 12px;
			font-weight: bold;
			padding: 0px 4px;
		}
		QLineEdit {
			background-color: #1E1E1E;
			border: 1px solid #3A3A3A;
			border-radius: 4px;
			padding: 6px 10px;
			font-size: 13px;
			color: #E0E0E0;
			selection-background-color: #264F78;
		}
		QLineEdit:focus {
			border: 1px solid #007ACC;
		}
		QListView {
			background-color: #2B2B2D;
			border: none;
			outline: none;
			font-size: 12px;
			alternate-background-color: #323234;
		}
		QListView::item {
			padding: 4px 8px;
			border-radius: 3px;
		}
		QListView::item:selected {
			background-color: #094771;
			color: #FFFFFF;
		}
		QListView::item:hover:!selected {
			background-color: #3A3D40;
		}
		QPushButton {
			background-color: transparent;
			border: none;
			border-radius: 3px;
			color: #888888;
			font-size: 14px;
			font-weight: bold;
			padding: 2px 6px;
		}
		QPushButton:hover {
			background-color: #4A4A4A;
			color: #FFFFFF;
		}
		QPushButton:pressed {
			background-color: #5A5A5A;
		}
	)");

	buildUI();

	// 防抖计时器：300ms
	m_debounceTimer.setInterval(300);
	m_debounceTimer.setSingleShot(true);
	connect(&m_debounceTimer, &QTimer::timeout, this, [this]() {
		performSearch(m_searchEdit->text());
	});
}

SearchPanel::~SearchPanel() = default;

void SearchPanel::buildUI()
{
	auto* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	// ---- 顶部标题栏（拖动条）----
	m_titleBar = new QFrame(this);
	m_titleBar->setObjectName(QStringLiteral("SearchPanelTitleBar"));
	m_titleBar->setFixedHeight(24);
	m_titleBar->setCursor(Qt::SizeAllCursor);  // 暗示可拖动
	m_titleBar->installEventFilter(this);      // 监听鼠标事件做拖动

	auto* titleLayout = new QHBoxLayout(m_titleBar);
	titleLayout->setContentsMargins(8, 0, 4, 0);
	titleLayout->setSpacing(6);

	m_titleLabel = new QLabel(QStringLiteral("查找器"), m_titleBar);
	m_titleLabel->setObjectName(QStringLiteral("SearchPanelTitleLabel"));
	titleLayout->addWidget(m_titleLabel);
	titleLayout->addStretch(1);

	auto* closeBtn = new QPushButton(QStringLiteral("\u00D7"), m_titleBar);  // ×
	closeBtn->setFixedSize(22, 18);
	closeBtn->setToolTip(QStringLiteral("关闭 (Esc)"));
	connect(closeBtn, &QPushButton::clicked, this, &SearchPanel::closePanel);
	titleLayout->addWidget(closeBtn);

	mainLayout->addWidget(m_titleBar);

	// ---- 内容区：搜索框 + 结果列表 ----
	auto* contentLayout = new QVBoxLayout();
	contentLayout->setContentsMargins(10, 8, 10, 10);
	contentLayout->setSpacing(6);

	m_searchEdit = new QLineEdit(this);
	m_searchEdit->setPlaceholderText(QStringLiteral("在所有节点文本中搜索…"));
	m_searchEdit->setClearButtonEnabled(true);
	m_searchEdit->setFocusPolicy(Qt::StrongFocus);

	contentLayout->addWidget(m_searchEdit);

	m_listView = new QListView(this);
	m_listView->setAlternatingRowColors(true);
	m_listView->setFocusPolicy(Qt::NoFocus);  // 键盘焦点始终留在搜索框
	m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
	m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	m_resultModel = new SearchResultModel(this);
	m_delegate = new SearchResultDelegate(this);
	m_listView->setModel(m_resultModel);
	m_listView->setItemDelegate(m_delegate);

	connect(m_listView, &QListView::activated, this, &SearchPanel::onResultActivated);
	connect(m_listView, &QListView::clicked, this, &SearchPanel::onResultClicked);

	contentLayout->addWidget(m_listView, 1);

	mainLayout->addLayout(contentLayout, 1);

	// 搜索框内容变化 → 防抖
	connect(m_searchEdit, &QLineEdit::textChanged, this, &SearchPanel::onTextChanged);

	// 默认尺寸
	resize(560, 360);
}

void SearchPanel::openAt(const QPoint& globalPos)
{
	if (!isVisible()) {
		show();
		raise();
	}

	// 定位：优先用 globalPos，否则在父窗口中央
	if (!globalPos.isNull()) {
		move(globalPos);
	} else if (parentWidget()) {
		QRect pr = parentWidget()->rect();
		move(pr.center().x() - width() / 2, pr.center().y() - height() / 2);
	}

	// 清空上次搜索
	m_searchEdit->clear();
	m_resultModel->clear();

	// 聚焦搜索框
	m_searchEdit->setFocus();
	m_searchEdit->selectAll();
}

void SearchPanel::closePanel()
{
	hide();
	m_debounceTimer.stop();
}

// ============================================================================
// 搜索逻辑
// ============================================================================

void SearchPanel::onTextChanged(const QString& text)
{
	Q_UNUSED(text);
	m_debounceTimer.start();  // 重新计时 300ms
}

void SearchPanel::performSearch(const QString& query)
{
	m_lastQuery = query;

	m_resultModel->clear();
	m_listView->update();

	if (query.trimmed().isEmpty() || !m_model) {
		return;
	}

	// 搜索统一在 QString 上做，确保索引都是 QChar 索引（而不是 UTF-8 字节索引），
	// 这样 delegate 里 mid(start, len) 才能正确对齐，不会截半个中文字符或偏移错位。
	QString needle = query.toLower();
	if (needle.isEmpty()) return;

	std::vector<SearchResultItem> results;
	results.reserve(64);

	constexpr int kCtx = 40;  // 预览截取：匹配位置前后各 40 个 QChar

	const auto& nodes = m_model->getAllNodes();
	for (const auto& kv : nodes) {
		const warroom::WarNode& node = kv.second;

		if (node.full_text.empty()) continue;

		// 一次性转 QString，后续所有操作都在它上面做
		QString fullText = QString::fromStdString(node.full_text);
		if (fullText.isEmpty()) continue;

		// 大小写不敏感子串匹配 → 返回 QChar 索引
		int matchPos = fullText.toLower().indexOf(needle);
		if (matchPos < 0) continue;

		SearchResultItem item;
		item.nodeId = kv.first;
		item.title = QString::fromStdString(node.title);

		int matchLen = needle.size();  // QChar 数

		// 以 QChar 索引裁剪预览窗口，永远切在完整字符边界
		int start = std::max(0, matchPos - kCtx);
		int end = std::min((int)fullText.size(), matchPos + matchLen + kCtx);

		QString previewText = fullText.mid(start, end - start);

		// 匹配在预览中的偏移（QChar 索引，与 delegate 的 mid() 完全对齐）
		item.matchStart = matchPos - start;
		item.matchLength = matchLen;

		// 首尾加省略号（"…" 是 1 个 QChar）
		if (start > 0) {
			previewText.prepend(QStringLiteral("…"));
			item.matchStart += 1;
		}
		if (end < fullText.size()) {
			previewText.append(QStringLiteral("…"));
		}

		// 换行/制表符替换为空格，避免列表出现多行
		previewText.replace('\n', ' ');
		previewText.replace('\r', ' ');
		previewText.replace('\t', ' ');

		item.preview = previewText;

		results.push_back(std::move(item));
	}

	// 最多显示 500 条
	constexpr int kMaxResults = 500;
	if (results.size() > static_cast<size_t>(kMaxResults)) {
		results.resize(kMaxResults);
	}

	m_resultModel->setResults(std::move(results));

	if (m_resultModel->rowCount() > 0) {
		QModelIndex first = m_resultModel->index(0, 0);
		m_listView->setCurrentIndex(first);
		m_listView->scrollTo(first);
	}
}

// ============================================================================
// 结果项激活
// ============================================================================

void SearchPanel::onResultActivated(const QModelIndex& index)
{
	if (!index.isValid()) return;
	QVariant v = index.data(Qt::UserRole);
	if (!v.isValid()) return;
	emit navigateToNodeRequested(v.toString().toStdString());
	// 跳转后保持面板打开——用户可以继续点别的条目浏览
}

void SearchPanel::onResultClicked(const QModelIndex& index)
{
	onResultActivated(index);
}

// ============================================================================
// 键盘 & 事件
// ============================================================================

void SearchPanel::keyPressEvent(QKeyEvent* event)
{
	switch (event->key()) {
	case Qt::Key_Escape:
		closePanel();
		return;
	case Qt::Key_Return:
	case Qt::Key_Enter: {
		QModelIndex cur = m_listView->currentIndex();
		if (!cur.isValid() && m_resultModel->rowCount() > 0) {
			cur = m_resultModel->index(0, 0);
		}
		if (cur.isValid()) {
			onResultActivated(cur);
		}
		return;
	}
	case Qt::Key_Down: {
		int row = m_listView->currentIndex().row();
		if (row < m_resultModel->rowCount() - 1) {
			QModelIndex next = m_resultModel->index(row + 1, 0);
			m_listView->setCurrentIndex(next);
			m_listView->scrollTo(next);
		}
		return;
	}
	case Qt::Key_Up: {
		int row = m_listView->currentIndex().row();
		if (row > 0) {
			QModelIndex prev = m_resultModel->index(row - 1, 0);
			m_listView->setCurrentIndex(prev);
			m_listView->scrollTo(prev);
		}
		return;
	}
	default:
		break;
	}
	QWidget::keyPressEvent(event);
}

bool SearchPanel::event(QEvent* event)
{
	// 搜索框/结果列表没有吃掉的 Esc → 关闭
	if (event->type() == QEvent::ShortcutOverride) {
		auto* se = static_cast<QKeyEvent*>(event);
		if (se->key() == Qt::Key_Escape) {
			closePanel();
			event->accept();
			return true;
		}
	}
	return QWidget::event(event);
}

// ============================================================================
// 拖动支持：eventFilter 监听标题栏的鼠标事件
// ============================================================================

bool SearchPanel::eventFilter(QObject* obj, QEvent* event)
{
	if (obj == m_titleBar) {
		switch (event->type()) {
		case QEvent::MouseButtonPress: {
			auto* me = static_cast<QMouseEvent*>(event);
			if (me->button() == Qt::LeftButton) {
				m_dragging = true;
				m_dragStartGlobal = me->globalPosition().toPoint();
				m_dragStartPos = pos();
				// 捕获鼠标，让拖动过程中不会被其他控件抢走
				m_titleBar->grabMouse();
				return true;
			}
			break;
		}
		case QEvent::MouseMove: {
			auto* me = static_cast<QMouseEvent*>(event);
			if (m_dragging && me->buttons() & Qt::LeftButton) {
				QPoint delta = me->globalPosition().toPoint() - m_dragStartGlobal;
				move(m_dragStartPos + delta);
				return true;
			}
			break;
		}
		case QEvent::MouseButtonRelease: {
			if (m_dragging) {
				m_dragging = false;
				m_titleBar->releaseMouse();
				return true;
			}
			break;
		}
		default:
			break;
		}
	}
	return QWidget::eventFilter(obj, event);
}

// ============================================================================
// SearchResultDelegate —— 自定义绘制
// ============================================================================

void SearchResultDelegate::paint(QPainter* painter,
	const QStyleOptionViewItem& option,
	const QModelIndex& index) const
{
	painter->save();

	// 背景
	if (option.state & QStyle::State_Selected) {
		painter->fillRect(option.rect, QColor(9, 71, 113));  // #094771
	} else if (option.state & QStyle::State_MouseOver) {
		painter->fillRect(option.rect, QColor(42, 45, 46));  // #2A2D2E
	} else if (index.row() % 2 == 1) {
		painter->fillRect(option.rect, QColor(45, 45, 45));  // #2D2D2D 交替行
	}

	// 读数据
	QString title = index.data(Qt::UserRole + 1).toString();
	QString preview = index.data(Qt::UserRole + 2).toString();
	int matchStart = index.data(Qt::UserRole + 3).toInt();
	int matchLen = index.data(Qt::UserRole + 4).toInt();

	if (title.isEmpty()) title = QStringLiteral("(无标题)");

	// 字体
	QFont titleFont = option.font;
	titleFont.setPointSize(12);
	titleFont.setBold(true);
	QFont previewFont = option.font;
	previewFont.setPointSize(11);

	// 颜色
	QColor titleColor = (option.state & QStyle::State_Selected)
		? QColor(255, 255, 255) : QColor(224, 224, 224);       // #E0E0E0
	QColor previewColor = (option.state & QStyle::State_Selected)
		? QColor(220, 230, 240) : QColor(160, 160, 160);    // #A0A0A0
	QColor matchColor = (option.state & QStyle::State_Selected)
		? QColor(255, 200, 100) : QColor(255, 140, 70);       // 橙红高亮

	// 区域划分
	QRect rect = option.rect;
	int padding = 8;
	QRect titleRect(rect.x() + padding, rect.y() + padding / 2,
		rect.width() - padding * 2, 18);
	QRect previewRect(rect.x() + padding, titleRect.bottom() + 1,
		rect.width() - padding * 2, 14);

	// 绘制 title
	painter->setFont(titleFont);
	painter->setPen(titleColor);
	QString titleElided = QFontMetrics(titleFont).elidedText(
		title, Qt::ElideRight, titleRect.width());
	painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, titleElided);

	// 绘制 preview + 高亮
	painter->setFont(previewFont);

	if (matchLen > 0 && matchStart >= 0 && matchStart + matchLen <= preview.size()) {
		// 三段：before / match / after
		// 统一用点基线绘制，避免 drawText(rect, AlignVCenter) 模式触发 Qt 自动多行换行
		QString before = preview.left(matchStart);
		QString match = preview.mid(matchStart, matchLen);
		QString after = preview.mid(matchStart + matchLen);

		QFontMetrics fm(previewFont);
		// 与 Qt AlignVCenter 内部公式一致：
		// baseY = rect.y() + (rect.height() - fm.height()) / 2 + fm.ascent()
		int baseY = previewRect.y()
			+ (previewRect.height() - fm.height()) / 2
			+ fm.ascent();
		int matchW = fm.horizontalAdvance(match);
		int remainingForBefore = previewRect.width() - matchW;  // 留给 before 的最大宽度

		// before 太长时从左侧省略（保留紧贴匹配点的上下文更有用）
		QString beforeDraw;
		if (fm.horizontalAdvance(before) <= remainingForBefore) {
			beforeDraw = before;
		} else {
			beforeDraw = fm.elidedText(before, Qt::ElideLeft, remainingForBefore);
		}

		int x = previewRect.x();

		// 画 before
		painter->setPen(previewColor);
		painter->drawText(QPoint(x, baseY), beforeDraw);
		x += fm.horizontalAdvance(beforeDraw);

		// 画 match（高亮）
		painter->setPen(matchColor);
		painter->drawText(QPoint(x, baseY), match);
		x += matchW;

		// 画 after（截断到剩余宽度）
		painter->setPen(previewColor);
		int afterMaxW = previewRect.right() - x - 2;
		if (afterMaxW > 0) {
			QString afterElided = fm.elidedText(after, Qt::ElideRight, afterMaxW);
			painter->drawText(QPoint(x, baseY), afterElided);
		}
	} else {
		painter->setPen(previewColor);
		painter->drawText(previewRect,
			Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, preview);
	}

	painter->restore();
}

QSize SearchResultDelegate::sizeHint(const QStyleOptionViewItem& option,
	const QModelIndex& index) const
{
	Q_UNUSED(index);
	// 每两行（title + preview）+ padding
	return QSize(option.rect.width(), 40);
}
