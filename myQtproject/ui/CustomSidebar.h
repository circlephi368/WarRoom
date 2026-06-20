#pragma once

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>

// 树节点（轻量数据结构，不依赖 Qt 图形项）
struct TreeNodeData {
	std::string id;
	std::string parentId;
	std::string text;          // 显示文本
	int depth = 0;             // 缩进层级
	bool expanded = true;      // 是否展开
	bool hasChildren = false;  // 是否有子节点
	bool selected = false;     // 是否选中
};

class CustomSidebar : public QWidget
{
	Q_OBJECT

public:
	explicit CustomSidebar(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setFixedWidth(220);
		setMinimumWidth(160);
		setMouseTracking(true);

		// 滚动条
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

	~CustomSidebar() {
		if (m_scrollBar) {
			disconnect(m_scrollBar, &QScrollBar::valueChanged, this, nullptr);
		}
	}

	// 设置节点树数据（外部调用）
	void setTreeData(const std::vector<TreeNodeData>& nodes) {
		m_nodes = nodes;

		// 构建快速查找映射：nodeId -> index in m_nodes
		m_nodeMap.clear();
		for (size_t i = 0; i < nodes.size(); ++i) {
			m_nodeMap[nodes[i].id] = static_cast<int>(i);
		}

		// 计算可见节点
		computeVisibleIndices();

		updateScrollBar();
		update();
	}

	// 通过 ID 选中节点（仅侧边栏内部高亮，不影响画布节点选中状态）
	void selectNode(const std::string& nodeId) {
		for (auto& node : m_nodes) {
			node.selected = (node.id == nodeId);
		}
		update();
	}

	// 获取指定节点的箭头起点位置（在 CustomSidebar 自身坐标系中）
	// 返回右侧边缘中点（因为 CustomSidebar 在左侧，箭头从右边出发朝向画布）
	// 若找不到对应项或节点被折叠隐藏则返回无效点
	QPoint getNodeArrowOrigin(const std::string& nodeId) const {
		auto mapIt = m_nodeMap.find(nodeId);
		if (mapIt == m_nodeMap.end()) return QPoint(-1, -1);
		int nodeIdx = mapIt->second;
		// 在可见列表中查找
		for (int vi = 0; vi < static_cast<int>(m_visibleIndices.size()); ++vi) {
			if (m_visibleIndices[vi] == nodeIdx) {
				int y = vi * m_rowHeight - m_scrollBar->value() + m_rowHeight / 2;
				return QPoint(width(), y);  // 右边缘
			}
		}
		return QPoint(-1, -1);
	}

	// 清空
	void clear() {
		m_nodes.clear();
		m_nodeMap.clear();
		m_visibleIndices.clear();
		m_collapsedNodes.clear();
		m_scrollBar->setRange(0, 0);
		update();
	}

signals:
	// 单击节点：请求聚焦（仅居中视图，不选中节点）
	void nodeFocused(const std::string& nodeId);
	// 双击节点：请求聚焦并放大
	void nodeDoubleClicked(const std::string& nodeId);

protected:
	void paintEvent(QPaintEvent*) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);

		// 背景
		p.fillRect(rect(), QColor(36, 36, 36));

		// 右侧细线分隔
		p.setPen(QPen(QColor(50, 50, 50), 1));
		p.drawLine(width() - 1, 0, width() - 1, height());

		if (m_visibleIndices.empty()) {
			// 空状态提示
			p.setPen(QColor(100, 100, 100));
			p.setFont(QFont("Microsoft YaHei", 10));
			p.drawText(rect().adjusted(16, 16, -16, -16),
				Qt::AlignTop | Qt::AlignHCenter,
				"暂无节点");
			return;
		}

		// 计算可见范围（只绘制屏幕内的行）
		int scrollOffset = m_scrollBar->value();
		int visibleCount = static_cast<int>(m_visibleIndices.size());
		int startVisible = scrollOffset / m_rowHeight;
		int endVisible = std::min(
			visibleCount - 1,
			(scrollOffset + height()) / m_rowHeight);

		p.setFont(QFont("Microsoft YaHei", 9));

		for (int vi = startVisible; vi <= endVisible; ++vi) {
			if (vi < 0 || vi >= visibleCount) continue;
			int nodeIdx = m_visibleIndices[vi];
			if (nodeIdx < 0 || nodeIdx >= static_cast<int>(m_nodes.size())) continue;

			const auto& node = m_nodes[nodeIdx];
			int y = vi * m_rowHeight - scrollOffset;
			drawRow(p, node, y);
		}
	}

	void mousePressEvent(QMouseEvent* event) override {
		int visibleIndex = hitTest(event->pos());
		if (visibleIndex < 0 || visibleIndex >= static_cast<int>(m_visibleIndices.size())) return;

		int nodeIdx = m_visibleIndices[visibleIndex];
		if (nodeIdx < 0 || nodeIdx >= static_cast<int>(m_nodes.size())) return;

		const auto& node = m_nodes[nodeIdx];

		// 检查是否点击了折叠/展开箭头
		int arrowX = 8 + node.depth * m_indentWidth;
		int arrowY = visibleIndex * m_rowHeight - m_scrollBar->value();
		QRect arrowRect(arrowX, arrowY + (m_rowHeight - 16) / 2, 16, 16);

		if (node.hasChildren && arrowRect.contains(event->pos())) {
			// 点击箭头：切换折叠状态
			bool currentlyCollapsed = (m_collapsedNodes.find(node.id) != m_collapsedNodes.end())
				&& m_collapsedNodes[node.id];
			m_collapsedNodes[node.id] = !currentlyCollapsed;
			computeVisibleIndices();
			updateScrollBar();
			update();
			return;
		}

		// 点击行：只做侧边栏内部高亮 + 聚焦信号（不影响画布节点选中状态）
		for (auto& n : m_nodes) {
			n.selected = (n.id == node.id);
		}
		emit nodeFocused(node.id);
		update();
	}

	void mouseDoubleClickEvent(QMouseEvent* event) override {
		int visibleIndex = hitTest(event->pos());
		if (visibleIndex < 0 || visibleIndex >= static_cast<int>(m_visibleIndices.size())) return;

		int nodeIdx = m_visibleIndices[visibleIndex];
		if (nodeIdx >= 0 && nodeIdx < static_cast<int>(m_nodes.size())) {
			emit nodeDoubleClicked(m_nodes[nodeIdx].id);
		}
	}

	void wheelEvent(QWheelEvent* event) override {
		m_scrollBar->setValue(m_scrollBar->value() - event->angleDelta().y() / 2);
		event->accept();
	}

	void resizeEvent(QResizeEvent* event) override {
		m_scrollBar->setGeometry(width() - 6, 0, 6, height());
		updateScrollBar();
		QWidget::resizeEvent(event);
	}

private:
	// 根据折叠状态构建可见节点索引列表
	void computeVisibleIndices() {
		m_visibleIndices.clear();
		m_visibleIndices.reserve(m_nodes.size());

		for (size_t i = 0; i < m_nodes.size(); ++i) {
			// 检查是否有折叠的祖先节点
			if (isHiddenByCollapsedAncestor(static_cast<int>(i))) {
				continue;
			}
			m_visibleIndices.push_back(static_cast<int>(i));
		}
	}

	// 判断某个节点是否因为祖先被折叠而应该隐藏
	bool isHiddenByCollapsedAncestor(int nodeIdx) {
		if (nodeIdx < 0 || nodeIdx >= static_cast<int>(m_nodes.size())) return true;
		const auto& node = m_nodes[nodeIdx];
		std::string currentParentId = node.parentId;
		while (!currentParentId.empty()) {
			auto it = m_nodeMap.find(currentParentId);
			if (it == m_nodeMap.end()) break;

			// 检查此父节点是否被折叠
			auto cit = m_collapsedNodes.find(currentParentId);
			if (cit != m_collapsedNodes.end() && cit->second) {
				return true;
			}

			// 继续向上追溯
			int parentIdx = it->second;
			if (parentIdx >= 0 && parentIdx < static_cast<int>(m_nodes.size())) {
				currentParentId = m_nodes[parentIdx].parentId;
			}
			else {
				break;
			}
		}
		return false;
	}

	void drawRow(QPainter& p, const TreeNodeData& node, int y) {
		// 选中高亮（仅侧边栏视觉）
		if (node.selected) {
			p.fillRect(QRect(0, y, width(), m_rowHeight), QColor(60, 60, 70));
			p.fillRect(QRect(0, y, 3, m_rowHeight), QColor(100, 140, 220));
		}

		int indent = node.depth * m_indentWidth;
		int x = 8 + indent;

		// 折叠/展开箭头
		if (node.hasChildren) {
			bool collapsed = (m_collapsedNodes.find(node.id) != m_collapsedNodes.end())
				&& m_collapsedNodes[node.id];
			QRect arrowRect(x, y + (m_rowHeight - 16) / 2, 16, 16);

			p.setPen(Qt::NoPen);
			p.setBrush(QColor(140, 140, 140));
			QPolygon triangle;
			if (collapsed) {
				// 右三角
				triangle << QPoint(arrowRect.x() + 4, arrowRect.y() + 2)
					<< QPoint(arrowRect.x() + 4, arrowRect.y() + 14)
					<< QPoint(arrowRect.x() + 12, arrowRect.y() + 8);
			}
			else {
				// 下三角
				triangle << QPoint(arrowRect.x() + 2, arrowRect.y() + 4)
					<< QPoint(arrowRect.x() + 14, arrowRect.y() + 4)
					<< QPoint(arrowRect.x() + 8, arrowRect.y() + 12);
			}
			p.drawPolygon(triangle);
		}

		x += 16;

		// 节点图标
		p.setPen(Qt::NoPen);
		p.setBrush(node.hasChildren ? QColor(120, 160, 220) : QColor(180, 180, 180));
		p.drawEllipse(QPointF(x + 6, y + m_rowHeight / 2), 3, 3);
		x += 14;

		// 节点文本
		QRect textRect(x, y, width() - x - 12, m_rowHeight);
		p.setPen(node.selected ? QColor(220, 220, 220) : QColor(180, 180, 180));
		QString displayText = QString::fromStdString(node.text);
		if (displayText.isEmpty()) {
			displayText = "未命名节点";
		}
		if (displayText.length() > 40) {
			displayText = displayText.left(38) + "…";
		}
		p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, displayText);
	}

	// 返回点击位置对应的"可见节点"索引（不是 m_nodes 原始索引）
	int hitTest(const QPoint& pos) const {
		if (pos.x() < 0 || pos.x() > width() - m_scrollBar->width()) {
			return -1;
		}
		int visibleIndex = (pos.y() + m_scrollBar->value()) / m_rowHeight;
		if (visibleIndex < 0 || visibleIndex >= static_cast<int>(m_visibleIndices.size())) {
			return -1;
		}
		return visibleIndex;
	}

	// 滚动条基于可见节点数
	void updateScrollBar() {
		int totalHeight = static_cast<int>(m_visibleIndices.size()) * m_rowHeight;
		int visibleHeight = height();
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

	std::vector<TreeNodeData> m_nodes;
	std::unordered_map<std::string, int> m_nodeMap;     // nodeId -> index
	std::vector<int> m_visibleIndices;                    // visible indices into m_nodes
	std::unordered_map<std::string, bool> m_collapsedNodes; // nodeId -> 是否折叠

	QScrollBar* m_scrollBar = nullptr;

	static constexpr int m_rowHeight = 28;
	static constexpr int m_indentWidth = 16;
};
