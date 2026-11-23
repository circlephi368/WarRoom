// SearchPanel.h
//
// Ctrl+H 浮层搜索面板：
//   - 非模态、无边框、可拖动、不阻塞画布操作
//   - 搜索范围：WarNode::full_text 大小写不敏感子串匹配
//   - 结果列表显示匹配片段预览（匹配字符高亮）+ 节点标题
//   - ↑↓ 移动选中、Enter 跳转、Esc 关闭
//   - 点击结果项直接跳转
//
// 未来 Mod 扩展点（预留）：
//   通过 WarRoomModel::registerSearchProvider 注册额外可搜索文本，
//   各 Mod 负责返回自己贡献的搜索片段。

#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QListView>
#include <QTimer>
#include <QModelIndex>
#include <QPoint>
#include <QFrame>
#include <QLabel>
#include <vector>
#include <string>
#include <QStyledItemDelegate>

namespace warroom {
	class WarRoomModel;
}

class SearchResultModel;
class SearchResultDelegate;

class SearchPanel : public QFrame
{
	Q_OBJECT

public:
	explicit SearchPanel(warroom::WarRoomModel* model, QWidget* parent = nullptr);
	~SearchPanel() override;

	// 弹出面板：聚焦搜索框、显示、raise
	void openAt(const QPoint& globalPos = {});

	// 关闭面板（停防抖计时器 + hide）
	void closePanel();

signals:
	// 用户选中某条结果：请求主窗口导航到该节点
	void navigateToNodeRequested(const std::string& nodeId);

protected:
	void keyPressEvent(QKeyEvent* event) override;
	bool event(QEvent* event) override;
	bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
	void onTextChanged(const QString& text);
	void onResultActivated(const QModelIndex& index);
	void onResultClicked(const QModelIndex& index);

private:
	void buildUI();
	void performSearch(const QString& query);

	// 拖动支持
	bool m_dragging = false;
	QPoint m_dragStartGlobal;
	QPoint m_dragStartPos;

	warroom::WarRoomModel* m_model = nullptr;

	// UI 组件
	QFrame* m_titleBar = nullptr;    // 顶部标题栏（拖动条）
	QLabel* m_titleLabel = nullptr;
	QLineEdit* m_searchEdit = nullptr;
	QListView* m_listView = nullptr;
	SearchResultModel* m_resultModel = nullptr;
	SearchResultDelegate* m_delegate = nullptr;

	// 搜索防抖计时器（300ms）
	QTimer m_debounceTimer;

	// 最近一次查询（避免重复跑相同 query）
	QString m_lastQuery;
};

// ---------------------------------------------------------------------------
// SearchResultModel —— 结果列表的数据模型
// ---------------------------------------------------------------------------

struct SearchResultItem {
	std::string nodeId;
	QString title;          // 节点标题（可能是空或 "新节点"）
	QString preview;        // 匹配片段预览（"…前40字[高亮]后40字…"）
	int matchStart = 0;     // 匹配在 preview 中的起始位置（用于 delegate 绘制高亮）
	int matchLength = 0;    // 匹配长度（用于 delegate 绘制高亮）
};

class SearchResultModel : public QAbstractListModel
{
	Q_OBJECT

public:
	explicit SearchResultModel(QObject* parent = nullptr)
		: QAbstractListModel(parent) {}

	int rowCount(const QModelIndex& parent = QModelIndex()) const override {
		if (parent.isValid()) return 0;
		return static_cast<int>(m_items.size());
	}

	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
		if (!index.isValid() || index.row() < 0
			|| index.row() >= static_cast<int>(m_items.size())) {
			return {};
		}
		const SearchResultItem& item = m_items[static_cast<size_t>(index.row())];
		switch (role) {
		case Qt::UserRole:    return QString::fromStdString(item.nodeId);
		case Qt::UserRole + 1: return item.title;
		case Qt::UserRole + 2: return item.preview;
		case Qt::UserRole + 3: return item.matchStart;
		case Qt::UserRole + 4: return item.matchLength;
		default: return {};
		}
	}

	void setResults(std::vector<SearchResultItem>&& items) {
		beginResetModel();
		m_items = std::move(items);
		endResetModel();
	}

	void clear() {
		beginResetModel();
		m_items.clear();
		endResetModel();
	}

	const SearchResultItem& at(int row) const {
		return m_items[static_cast<size_t>(row)];
	}

private:
	std::vector<SearchResultItem> m_items;
};

// ---------------------------------------------------------------------------
// SearchResultDelegate —— 自定义绘制：匹配字符高亮（橙红色）
// ---------------------------------------------------------------------------

class SearchResultDelegate : public QStyledItemDelegate
{
	Q_OBJECT

public:
	explicit SearchResultDelegate(QObject* parent = nullptr)
		: QStyledItemDelegate(parent) {}

	void paint(QPainter* painter,
		const QStyleOptionViewItem& option,
		const QModelIndex& index) const override;

	QSize sizeHint(const QStyleOptionViewItem& option,
		const QModelIndex& index) const override;
};
