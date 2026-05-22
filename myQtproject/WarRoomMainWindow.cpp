#include "LinkGraphicsItem.h"
#include "warroomview.h"
#include "WarRoomMainWindow.h"
#include "NodeGraphicsItem.h"
#include "move_node_command.h"
#include "edit_node_command.h"
#include "add_node_command.h"
#include "delete_node_command.h"
#include <QGraphicsScene>
#include <qinputdialog.h>
#include <functional>


WarRoomMainWindow::WarRoomMainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);
	setupScene();
	populateFromModel();
}

WarRoomMainWindow::~WarRoomMainWindow() {}

void WarRoomMainWindow::setupScene()
{
	m_scene = new QGraphicsScene(this);
	m_scene->setSceneRect(-5000, -5000, 10000, 10000);
	m_view = new WarRoomView(m_scene, this);
	setCentralWidget(m_view);
}

void WarRoomMainWindow::populateFromModel()
{
	// 创建测试数据
	using warroom::WarNode;
	using warroom::NodeKind;
	using warroom::Uuid;
	using warroom::WarLink;
	using warroom::LinkType;

	WarNode group = WarNode::makeGroup("主攻方向", 0, -100);
	group.explicit_color = "#e74c3c";
	Uuid groupId = m_model.addNode(std::move(group), m_model.getDocumentRootId());

	WarNode leaf1 = WarNode::makeLeaf("数据库查询优化", -200, 50);
	leaf1.tags = { "进行中" };
	leaf1.explicit_color = "#3498db";
	leaf1.full_text = "test full_text";
	m_model.addNode(std::move(leaf1), groupId);

	WarNode leaf2 = WarNode::makeLeaf("缓存策略调整", 50, 50);
	leaf2.tags = { "未探索" };
	m_model.addNode(std::move(leaf2), groupId);

	WarNode leaf3 = WarNode::makeLeaf("索引重建方案", 300, 50);
	leaf3.tags = { "失败" };
	leaf3.explicit_color = "#95a5a6";
	m_model.addNode(std::move(leaf3), groupId);

	WarNode standalone = WarNode::makeLeaf("网络延迟排查", 500, -100);
	standalone.explicit_color = "#2ecc71";
	m_model.addNode(std::move(standalone), m_model.getDocumentRootId());

	// 获取已创建节点的 ID（这里手工记录，实际项目会维护映射）
	// 简便起见：从模型中查询所有节点，找到我们需要的
	auto topNodes = m_model.getTopLevelNodes();

	// 依赖连线：数据库优化 → 缓存策略
	// 需要从模型中查找标题匹配的节点
	
	// ---- 创建连线（测试数据） ----
	// 先查找已创建节点的 ID
	Uuid dbNode, cacheNode, indexNode, netNode;
	for (const auto& [id, node] : m_model.getAllNodes()) {  // 假设你也有 getAllNodes()，
		// 否则遍历 topLevel + children
		if (node.title == "数据库查询优化") dbNode = id;
		else if (node.title == "缓存策略调整") cacheNode = id;
		else if (node.title == "索引重建方案") indexNode = id;
		else if (node.title == "网络延迟排查") netNode = id;
	}

	// 如果没有 getAllNodes()，用手动查找：
	// （略，用前面已有的变量 groupId 和子节点遍历结果）

	// 添加连线到模型
	if (!dbNode.empty() && !cacheNode.empty()) {
		auto link = warroom::WarLink::makeNodeToNode(dbNode, cacheNode, warroom::LinkType::Dependency);
		link.label = "依赖";
		link.color = "#f39c12";
		m_model.addLink(std::move(link));
	}
	if (!dbNode.empty() && !indexNode.empty()) {
		auto link = warroom::WarLink::makeNodeToNode(dbNode, indexNode, warroom::LinkType::Transformation);
		link.label = "转化为";
		link.color = "#3498db";
		m_model.addLink(std::move(link));
	}
	if (!dbNode.empty() && !netNode.empty()) {
		auto link = warroom::WarLink::makeNodeToNode(dbNode, netNode, warroom::LinkType::Inspiration);
		link.label = "启发";
		link.color = "#9b59b6";
		m_model.addLink(std::move(link));
	}

	// ---- 从模型读取所有连线，创建 LinkGraphicsItem ----
	for (const auto& [linkId, link] : m_model.getAllLinks()) {
		auto* linkItem = new LinkGraphicsItem(linkId, m_model);
		m_scene->addItem(linkItem);
	}
	// 遍历创建图形项
	std::function<void(Uuid)> createItems;
	createItems = [&](Uuid parentId) {
		auto children = m_model.getChildren(parentId);
		for (const Uuid& childId : children) {
			const WarNode* node = m_model.getNode(childId);
			if (!node) continue;

			QColor color(QString::fromStdString(m_model.getEffectiveColor(childId)));

			auto* item = new NodeGraphicsItem(
				childId,
				node->title.empty() ? "未命名" : node->title,
				node->full_text,   // 传入长文本
				color
			);

			item->setPos(node->pos_x, node->pos_y);

			if (node->kind == NodeKind::Group) {
				item->setScale(1.15);
			}

			// ★ 连接信号：拖拽结束 → 更新模型
			QObject::connect(item, &NodeGraphicsItem::positionChanged,
				this, &WarRoomMainWindow::onNodeMoved);
			// moveFinished 连接（用于生成撤销命令）
			QObject::connect(item, &NodeGraphicsItem::moveFinished,
				this, &WarRoomMainWindow::onNodeMoveFinished);

			m_scene->addItem(item);
			createItems(childId);
		}
		};

	createItems(m_model.getDocumentRootId());
}

void WarRoomMainWindow::onNodeMoved(const std::string& nodeId, float newX, float newY)
{
	warroom::WarNode* node = m_model.getNodeMutable(nodeId);
	if (!node) return;
	node->pos_x = newX;
	node->pos_y = newY;
	// 拖拽过程中也刷新连线，保持实时跟随
	refreshLinks();
}
void WarRoomMainWindow::onNodeMoveFinished(const std::string& nodeId,
	float oldX, float oldY,
	float newX, float newY)
{
	using warroom::MoveNodeCommand;
	auto cmd = std::make_unique<MoveNodeCommand>(nodeId, oldX, oldY, newX, newY);
	m_undoManager.executeCommand(std::move(cmd), m_model);

	// 刷新连线
	refreshLinks();
}
void WarRoomMainWindow::keyPressEvent(QKeyEvent* event)
{
	if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Z) {
		if (m_undoManager.canUndo()) {
			m_undoManager.undo(m_model);
			syncAllItemsFromModel();  // 待实现
			refreshLinks();
		}
		return;
	}
	if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Y) {
		if (m_undoManager.canRedo()) {
			m_undoManager.redo(m_model);
			syncAllItemsFromModel();
			refreshLinks();
		}
		return;
	}
	QMainWindow::keyPressEvent(event);
}
void WarRoomMainWindow::syncAllItemsFromModel()
{
	// 收集场景中已有的节点ID
	std::unordered_set<std::string> existingItemIds;
	for (QGraphicsItem* item : m_scene->items()) {
		auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item);
		if (nodeItem) {
			existingItemIds.insert(nodeItem->nodeId());
		}
	}

	// 递归遍历模型，为缺失的节点创建图形项
	std::function<void(warroom::Uuid)> createMissingItems;
	createMissingItems = [&](warroom::Uuid parentId) {
		auto children = m_model.getChildren(parentId);
		for (const warroom::Uuid& childId : children) {
			if (existingItemIds.find(childId) == existingItemIds.end()) {
				const warroom::WarNode* node = m_model.getNode(childId);
				if (node) {
					QColor color(QString::fromStdString(m_model.getEffectiveColor(childId)));
					auto* item = new NodeGraphicsItem(
						childId,
						node->title.empty() ? "未命名" : node->title,
						node->full_text,
						color
					);
					item->setPos(node->pos_x, node->pos_y);
					if (node->kind == warroom::NodeKind::Group) {
						item->setScale(1.15);
					}
					QObject::connect(item, &NodeGraphicsItem::positionChanged,
						this, &WarRoomMainWindow::onNodeMoved);
					QObject::connect(item, &NodeGraphicsItem::moveFinished,
						this, &WarRoomMainWindow::onNodeMoveFinished);
					m_scene->addItem(item);
					existingItemIds.insert(childId);
				}
			}
			createMissingItems(childId);
		}
		};

	createMissingItems(m_model.getDocumentRootId());

	// 同步已有节点的位置和内容
	for (QGraphicsItem* item : m_scene->items()) {
		auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item);
		if (!nodeItem) continue;

		const warroom::WarNode* node = m_model.getNode(nodeItem->nodeId());
		if (node) {
			nodeItem->blockSignals(true);

			// 同步位置
			nodeItem->setPos(node->pos_x, node->pos_y);

			// 同步标题和文本内容（需要给 NodeGraphicsItem 添加更新方法）
			nodeItem->updateContent(
				node->title.empty() ? "未命名" : node->title,
				node->full_text
			);

			// 同步颜色
			QColor color(QString::fromStdString(m_model.getEffectiveColor(nodeItem->nodeId())));
			nodeItem->updateColor(color);

			nodeItem->blockSignals(false);
		}
	}
}

void WarRoomMainWindow::refreshLinks()
{
	for (QGraphicsItem* item : m_scene->items()) {
		if (auto* linkItem = dynamic_cast<LinkGraphicsItem*>(item)) {
			linkItem->updatePositions();
		}
	}
}

NodeContext WarRoomMainWindow::captureNodeContext(const warroom::Uuid& nodeId) {
	NodeContext ctx;
	ctx.nodeId = nodeId;

	const warroom::WarNode* node = m_model.getNode(nodeId);
	if (node) {
		ctx.savedNode = *node;  // 拷贝保存
		ctx.parentId = node->parent_id;

		// 查找在父节点 children 中的索引
		const warroom::WarNode* parent = m_model.getNode(ctx.parentId);
		if (parent) {
			for (size_t i = 0; i < parent->children_ids.size(); ++i) {
				if (parent->children_ids[i] == nodeId) {
					ctx.index = static_cast<int>(i);
					break;
				}
			}
		}
	}
	return ctx;
}

// 删除当前选中的节点
void WarRoomMainWindow::deleteSelectedNode() {
	QList<QGraphicsItem*> selected = m_scene->selectedItems();
	for (QGraphicsItem* item : selected) {
		auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item);
		if (!nodeItem) continue;

		std::string nodeId = nodeItem->nodeId();

		// 不能删除根节点（可选检查）
		if (nodeId == m_model.getDocumentRootId()) continue;

		NodeContext ctx = captureNodeContext(nodeId);

		auto cmd = std::make_unique<warroom::DeleteNodeCommand>(
			ctx.nodeId, ctx.savedNode, ctx.parentId, ctx.index
		);
		m_undoManager.executeCommand(std::move(cmd), m_model);

		// 删除图形项
		delete nodeItem;
		break;  // 一次只删一个
	}

	refreshLinks();  // 刷新连线
}
//添加节点
void WarRoomMainWindow::addNodeAtPosition(QPointF scenePos) {
	warroom::WarNode newNode = warroom::WarNode::makeLeaf("新节点", scenePos.x(), scenePos.y());
	newNode.full_text = "双击编辑长文本...";

	auto cmd = std::make_unique<warroom::AddNodeCommand>(
		std::move(newNode),
		m_model.getDocumentRootId(),
		-1
	);
	m_undoManager.executeCommand(std::move(cmd), m_model);

	syncAllItemsFromModel();  // 现在会创建新节点的图形项
	refreshLinks();
}

// 编辑节点（双击时调用）
void WarRoomMainWindow::editNode(const std::string& nodeId) {
	warroom::WarNode* node = m_model.getNodeMutable(nodeId);
	if (!node) return;

	QString newTitle = QInputDialog::getText(this, "编辑标题", "标题:",
		QLineEdit::Normal,
		QString::fromStdString(node->title));
	QString newFullText = QInputDialog::getMultiLineText(this, "编辑内容", "长文本:",
		QString::fromStdString(node->full_text));

	if (!newTitle.isNull()) {  // 用户未取消
		auto cmd = std::make_unique<warroom::EditNodeCommand>(
			nodeId,
			node->title, newTitle.toStdString(),
			node->full_text, newFullText.toStdString()
		);
		m_undoManager.executeCommand(std::move(cmd), m_model);

		// 更新图形项的显示
		syncAllItemsFromModel();
	}
}
void WarRoomMainWindow::contextMenuEvent(QContextMenuEvent* event) {
	QPointF scenePos = m_view->mapToScene(event->pos());
	QGraphicsItem* item = m_scene->itemAt(scenePos, QTransform());

	QMenu menu(this);

	if (dynamic_cast<NodeGraphicsItem*>(item)) {
		menu.addAction("删除节点", this, &WarRoomMainWindow::deleteSelectedNode);
		menu.addAction("编辑节点", this, [this, item]() {
			auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item);
			if (nodeItem) editNode(nodeItem->nodeId());
			});
		menu.addSeparator();
		menu.addAction("添加子节点", this, [this, scenePos]() {
			addNodeAtPosition(scenePos);
			});
	}
	else {
		menu.addAction("添加节点", this, [this, scenePos]() {
			addNodeAtPosition(scenePos);
			});
	}

	menu.exec(event->globalPos());
}