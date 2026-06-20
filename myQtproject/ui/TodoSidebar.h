// TodoSidebar.h
//
// 右侧待办侧边栏：按时间顺序展示所有标记为待办的节点。
// 支持勾选完成、点击聚焦到画布对应节点。
//
#pragma once

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QDateTime>
#include <vector>
#include <string>
#include <algorithm>
#include "core/warroom/warroom_types.h"

// 待办项数据（轻量，供侧边栏显示用）
struct TodoItemData {
	std::string nodeId;
	std::string title;
	bool done = false;           // true=已完成, false=待完成
	warroom::Timestamp createdAt;
};

class TodoSidebar : public QWidget
{
	Q_OBJECT

public:
	explicit TodoSidebar(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setFixedWidth(240);
		setMouseTracking(true);

		m_scrollBar = new QScrollBar(Qt::Vertical, this);
		m_scrollBar->setStyleSheet(R"(
			QScrollBar:vertical {
				background: #1E1E1E;
				width: 6px;
				margin: 0;
			}
			QScrollBar::handle:vertical {
				background: #555555;
				border-radius: 3px;
				min-height: 20px;
			}
			QScrollBar::handle:vertical:hover {
				background: #777777;
			}
			QScrollBar::add-line:vertical,
			QScrollBar::sub-line:vertical {
				height: 0;
			}
			QScrollBar::add-page:vertical,
			QScrollBar::sub-page:vertical {
				background: transparent;
			}
		)");

		connect(m_scrollBar, &QScrollBar::valueChanged,
			this, [this]() { update(); });
	}

	~TodoSidebar() {
		if (m_scrollBar) {
			disconnect(m_scrollBar, &QScrollBar::valueChanged, this, nullptr);
		}
	}

	// 设置待办列表数据（外部调用）
	// 数据应已按 createdAt 排序
	void setTodoData(const std::vector<TodoItemData>& items) {
		m_items = items;
		updateScrollBar();
		update();
	}

	// 通过 ID 选中项（仅高亮，不影响画布）
	void selectItem(const std::string& nodeId) {
		m_selectedId = nodeId;
		update();
	}

	// 获取指定项的箭头起点位置（在 TodoSidebar 自身坐标系中）
	// 返回左侧边缘中点（因为 TodoSidebar 在右侧，箭头从左边出发朝向画布）
	// 若找不到对应项则返回无效点
	QPoint getItemArrowOrigin(const std::string& nodeId) const {
		for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
			if (m_items[i].nodeId == nodeId) {
				int y = m_headerHeight + i * m_rowHeight - m_scrollBar->value()
					+ m_rowHeight / 2;
				return QPoint(0, y);  // 左边缘
			}
		}
		return QPoint(-1, -1);
	}

	// 清空
	void clear() {
		m_items.clear();
		m_selectedId.clear();
		m_scrollBar->setRange(0, 0);
		update();
	}

signals:
	// 单击项：请求聚焦画布上对应节点
	void itemFocused(const std::string& nodeId);
	// 双击项：请求聚焦并在缩放过小时放大
	void itemDoubleClicked(const std::string& nodeId);
	// 勾选/取消勾选完成状态
	void itemToggled(const std::string& nodeId, bool done);
	// 右键菜单请求（移除待办）
	void itemRemoveRequested(const std::string& nodeId);

protected:
	void paintEvent(QPaintEvent*) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);

		// 背景
		p.fillRect(rect(), QColor(36, 36, 36));

		// 左侧细线分隔
		p.setPen(QPen(QColor(50, 50, 50), 1));
		p.drawLine(0, 0, 0, height());

		// 标题栏
		p.fillRect(QRect(0, 0, width(), m_headerHeight), QColor(32, 32, 32));
		p.setPen(QColor(200, 200, 200));
		QFont titleFont("Microsoft YaHei", 10, QFont::Bold);
		p.setFont(titleFont);
		p.drawText(QRect(12, 0, width() - 24, m_headerHeight),
			Qt::AlignVCenter | Qt::AlignLeft, "待办事项");

		// 统计
		int pendingCount = 0;
		for (const auto& item : m_items) {
			if (!item.done) ++pendingCount;
		}
		QString countText = QString("%1 待办 / %2 完成")
			.arg(pendingCount).arg(static_cast<int>(m_items.size()) - pendingCount);
		p.setPen(QColor(120, 120, 120));
		QFont countFont("Microsoft YaHei", 8);
		p.setFont(countFont);
		p.drawText(QRect(12, 0, width() - 24, m_headerHeight),
			Qt::AlignVCenter | Qt::AlignRight, countText);

		if (m_items.empty()) {
			p.setPen(QColor(100, 100, 100));
			p.setFont(QFont("Microsoft YaHei", 10));
			p.drawText(rect().adjusted(16, m_headerHeight + 16, -16, -16),
				Qt::AlignTop | Qt::AlignHCenter,
				"暂无待办\n\n右键节点可添加待办");
			return;
		}

		// 绘制列表项
		int scrollOffset = m_scrollBar->value();
		int listTop = m_headerHeight;
		int visibleHeight = height() - listTop;
		int startIdx = scrollOffset / m_rowHeight;
		int endIdx = std::min(static_cast<int>(m_items.size()) - 1,
			(scrollOffset + visibleHeight) / m_rowHeight);

		p.setFont(QFont("Microsoft YaHei", 9));

		for (int i = startIdx; i <= endIdx; ++i) {
			if (i < 0 || i >= static_cast<int>(m_items.size())) continue;
			int y = listTop + i * m_rowHeight - scrollOffset;
			drawRow(p, m_items[i], y, i);
		}
	}

	void mousePressEvent(QMouseEvent* event) override {
		int idx = hitTest(event->pos());
		if (idx < 0 || idx >= static_cast<int>(m_items.size())) return;

		const auto& item = m_items[idx];

		// 检查是否点击了复选框区域
		int checkboxX = 12;
		int checkboxY = m_headerHeight + idx * m_rowHeight - m_scrollBar->value()
			+ (m_rowHeight - m_checkboxSize) / 2;
		QRect checkboxRect(checkboxX, checkboxY, m_checkboxSize, m_checkboxSize);

		if (checkboxRect.contains(event->pos())) {
			emit itemToggled(item.nodeId, !item.done);
			return;
		}

		// 点击行：高亮 + 聚焦信号
		if (event->button() == Qt::LeftButton) {
			m_selectedId = item.nodeId;
			emit itemFocused(item.nodeId);
			update();
		}
	}

	void mouseDoubleClickEvent(QMouseEvent* event) override {
		int idx = hitTest(event->pos());
		if (idx < 0 || idx >= static_cast<int>(m_items.size())) return;

		// 检查是否点击了复选框区域：若是则阻断，不触发任何聚焦/移动
		int checkboxX = 12;
		int checkboxY = m_headerHeight + idx * m_rowHeight - m_scrollBar->value()
			+ (m_rowHeight - m_checkboxSize) / 2;
		QRect checkboxRect(checkboxX, checkboxY, m_checkboxSize, m_checkboxSize);
		if (checkboxRect.contains(event->pos())) {
			return;
		}

		// 双击非复选框区域：触发双击聚焦（带放大逻辑）
		emit itemDoubleClicked(m_items[idx].nodeId);
	}

	void wheelEvent(QWheelEvent* event) override {
		m_scrollBar->setValue(m_scrollBar->value() - event->angleDelta().y() / 2);
		event->accept();
	}

	void resizeEvent(QResizeEvent* event) override {
		m_scrollBar->setGeometry(width() - 6, m_headerHeight, 6, height() - m_headerHeight);
		updateScrollBar();
		QWidget::resizeEvent(event);
	}

private:
	void drawRow(QPainter& p, const TodoItemData& item, int y, int rowIdx) {
		bool selected = (item.nodeId == m_selectedId);

		// 选中高亮
		if (selected) {
			p.fillRect(QRect(0, y, width(), m_rowHeight), QColor(60, 60, 70));
			p.fillRect(QRect(width() - 3, y, 3, m_rowHeight), QColor(100, 140, 220));
		}

		// 复选框
		int cbX = 12;
		int cbY = y + (m_rowHeight - m_checkboxSize) / 2;
		QRect cbRect(cbX, cbY, m_checkboxSize, m_checkboxSize);
		QPointF cbCenter = cbRect.center();

		if (item.done) {
			// 已完成：实心圆 + 勾
			p.setPen(QPen(QColor(80, 180, 100), 1.5));
			p.setBrush(QColor(80, 180, 100));
			p.drawEllipse(cbCenter, m_checkboxSize / 2.0, m_checkboxSize / 2.0);
			// 白色勾
			p.setPen(QPen(QColor(255, 255, 255), 2));
			p.drawLine(cbRect.left() + 4, cbRect.center().y(),
				cbRect.center().x() - 1, cbRect.bottom() - 4);
			p.drawLine(cbRect.center().x() - 1, cbRect.bottom() - 4,
				cbRect.right() - 3, cbRect.top() + 4);
		}
		else {
			// 未完成：空心圆
			p.setPen(QPen(QColor(140, 140, 140), 1.5));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(cbCenter, m_checkboxSize / 2.0, m_checkboxSize / 2.0);
		}

		// 文本
		int textX = cbX + m_checkboxSize + 8;
		QRect textRect(textX, y, width() - textX - 12, m_rowHeight);

		if (item.done) {
			p.setPen(QColor(100, 100, 100));
		}
		else {
			p.setPen(selected ? QColor(220, 220, 220) : QColor(180, 180, 180));
		}

		QString displayText = QString::fromStdString(item.title);
		if (displayText.isEmpty()) displayText = "未命名节点";
		if (displayText.length() > 28) displayText = displayText.left(26) + "…";

		// 已完成项添加删除线效果（用中线）
		if (item.done) {
			p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, displayText);
			QFontMetrics fm(p.font());
			int tw = fm.horizontalAdvance(displayText);
			int textY = textRect.center().y();
			p.setPen(QPen(QColor(100, 100, 100), 1));
			p.drawLine(textX, textY, textX + std::min(tw, textRect.width()), textY);
		}
		else {
			p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, displayText);
		}

		// 时间戳（小字，灰色）
		auto time_t = std::chrono::system_clock::to_time_t(item.createdAt);
		QString timeStr = QDateTime::fromSecsSinceEpoch(time_t).toString("MM-dd HH:mm");
		p.setPen(QColor(90, 90, 90));
		QFont smallFont("Microsoft YaHei", 7);
		p.setFont(smallFont);
		p.drawText(textRect, Qt::AlignBottom | Qt::AlignLeft, timeStr);
		p.setFont(QFont("Microsoft YaHei", 9));
	}

	int hitTest(const QPoint& pos) const {
		if (pos.x() < 0 || pos.x() > width() - m_scrollBar->width()) return -1;
		if (pos.y() < m_headerHeight) return -1;
		int idx = (pos.y() - m_headerHeight + m_scrollBar->value()) / m_rowHeight;
		if (idx < 0 || idx >= static_cast<int>(m_items.size())) return -1;
		return idx;
	}

	void updateScrollBar() {
		int totalHeight = static_cast<int>(m_items.size()) * m_rowHeight;
		int visibleHeight = height() - m_headerHeight;
		if (totalHeight > visibleHeight) {
			m_scrollBar->setRange(0, totalHeight - visibleHeight);
			m_scrollBar->setPageStep(visibleHeight);
			m_scrollBar->show();
		}
		else {
			m_scrollBar->setRange(0, 0);
			m_scrollBar->hide();
		}
	}

	std::vector<TodoItemData> m_items;
	std::string m_selectedId;
	QScrollBar* m_scrollBar = nullptr;

	static constexpr int m_headerHeight = 36;
	static constexpr int m_rowHeight = 40;
	static constexpr int m_checkboxSize = 16;
};
