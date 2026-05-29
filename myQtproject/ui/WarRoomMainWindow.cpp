#include "ui/LinkGraphicsItem.h"
#include "ui/warroomview.h"
#include "WarRoomMainWindow.h"
#include "ui/NodeGraphicsItem.h"
#include "core/command/move_node_command.h"
#include "core/command/edit_node_command.h"
#include "core/command/add_node_command.h"
#include "core/command/add_link_command.h"
#include "core/command/delete_node_command.h"
#include "ui/LinkCreationManager.h"
#include "core/command/delete_link_command.h"
#include <QGraphicsScene>
#include <qinputdialog.h>
#include <functional>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <fstream>
#include <iostream>
#include <qtoolbar.h>


WarRoomMainWindow::WarRoomMainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);
	setupMenuBar();
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

	// 初始化连接管理器
	LinkCreationManager::instance().setMainWindow(this);
	LinkCreationManager::instance().setScene(m_scene);
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
	group.color= "#e74c3c";
	//group.explicit_color = "#e74c3c";
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
	leaf3.width = 300;
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
	for (const auto& [id, node] : m_model.getAllNodes()) {
		// 否则遍历 topLevel + children
		if (node.title == "数据库查询优化") dbNode = id;
		else if (node.title == "缓存策略调整") cacheNode = id;
		else if (node.title == "索引重建方案") indexNode = id;
		else if (node.title == "网络延迟排查") netNode = id;
	}
	// 添加连线到模型
	if (!dbNode.empty() && !cacheNode.empty()) {
		auto link = warroom::WarLink::makeNodeToNode(dbNode, 0, cacheNode, 0, warroom::LinkType::Dependency);
		link.label = "依赖";
		link.color = "#f39c12";
		m_model.addLink(std::move(link));
	}
	if (!dbNode.empty() && !indexNode.empty()) {
		auto link = warroom::WarLink::makeNodeToNode(dbNode, 1, indexNode, 2, warroom::LinkType::Transformation);
		link.label = "转化为";
		link.color = "#3498db";
		m_model.addLink(std::move(link));
	}
	if (!dbNode.empty() && !netNode.empty()) {
		auto link = warroom::WarLink::makeNodeToNode(dbNode, 2, netNode, 3, warroom::LinkType::Inspiration);
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
				childId, *node,
				node->title.empty() ? "未命名" : node->title,
				node->full_text,   // 传入长文本
				color
			);

			item->setPos(node->pos_x, node->pos_y);

			// ★ 连接信号：拖拽结束 → 更新模型
			QObject::connect(item, &NodeGraphicsItem::positionChanged,
				this, &WarRoomMainWindow::onNodeMoved);
			// moveFinished 连接（用于生成撤销命令）
			QObject::connect(item, &NodeGraphicsItem::moveFinished,
				this, &WarRoomMainWindow::onNodeMoveFinished);

			m_scene->addItem(item);
			m_nodeItems.insert(QString::fromStdString(childId), item);
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
	executeCommand(std::move(cmd));

	
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
	// 收集模型中所有节点ID
	std::unordered_set<std::string> modelNodeIds;
	std::function<void(warroom::Uuid)> collectIds;
	collectIds = [&](warroom::Uuid parentId) {
		auto children = m_model.getChildren(parentId);
		for (const auto& childId : children) {
			modelNodeIds.insert(childId);
			collectIds(childId);
		}
		};
	collectIds(m_model.getDocumentRootId());

	// 删除映射表中模型里已不存在的节点
	QList<QString> idsToRemove;
	for (auto it = m_nodeItems.begin(); it != m_nodeItems.end(); ++it) {
		if (modelNodeIds.find(it.key().toStdString()) == modelNodeIds.end()) {
			idsToRemove.append(it.key());
		}
	}
	for (const auto& id : idsToRemove) {
		auto* item = m_nodeItems.take(id);
		m_scene->removeItem(item);
		delete item;
	}

	// 为模型中有但映射表中没有的节点创建图形项
	std::function<void(warroom::Uuid)> createMissingItems;
	createMissingItems = [&](warroom::Uuid parentId) {
		auto children = m_model.getChildren(parentId);
		for (const auto& childId : children) {
			QString key = QString::fromStdString(childId);
			if (!m_nodeItems.contains(key)) {
				const warroom::WarNode* node = m_model.getNode(childId);
				if (node) {
					QColor color(QString::fromStdString(node->color));
					auto* item = new NodeGraphicsItem(
						childId, *node,
						node->title.empty() ? "未命名" : node->title,
						node->full_text,
						color,
						node->kind,
						node->is_collapsed
					);
					item->setPos(node->pos_x, node->pos_y);
					QObject::connect(item, &NodeGraphicsItem::positionChanged,
						this, &WarRoomMainWindow::onNodeMoved);
					QObject::connect(item, &NodeGraphicsItem::moveFinished,
						this, &WarRoomMainWindow::onNodeMoveFinished);
					m_scene->addItem(item);
					m_nodeItems.insert(key, item);
				}
			}
			createMissingItems(childId);
		}
		};
	createMissingItems(m_model.getDocumentRootId());

	// 同步已有节点的位置和内容
	for (auto it = m_nodeItems.begin(); it != m_nodeItems.end(); ++it) {
		NodeGraphicsItem* nodeItem = it.value();
		const warroom::WarNode* node = m_model.getNode(it.key().toStdString());
		if (node) {
			nodeItem->blockSignals(true);
			nodeItem->setPos(node->pos_x, node->pos_y);
			nodeItem->updateContent(
				node->title.empty() ? "未命名" : node->title,
				node->full_text,
				node->is_collapsed
			);
			QColor color(QString::fromStdString(node->color));
			nodeItem->updateColor(color);
			nodeItem->blockSignals(false);
			
		}
	}
}

void WarRoomMainWindow::refreshLinks()
{
	// 收集待删除的无效连线项（不能在遍历时直接删除）
	QList<QGraphicsItem*> itemsToRemove;

	for (QGraphicsItem* item : m_scene->items()) {
		if (auto* linkItem = dynamic_cast<LinkGraphicsItem*>(item)) {
			// 检查模型中的连线是否还存在
			if (!m_model.getLink(linkItem->linkId())) {
				itemsToRemove.append(item);
			}
			else {
				linkItem->updatePositions();
			}
		}
	}

	// 清理无效连线图形项
	for (QGraphicsItem* item : itemsToRemove) {
		m_scene->removeItem(item);
		delete item;
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
		executeCommand(std::move(cmd));

		//// 删除图形项
		//m_nodeItems.remove(QString::fromStdString(nodeId));
		//delete nodeItem;
		break;  // 一次只删一个
	}

	
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
	executeCommand(std::move(cmd));

	
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
		executeCommand(std::move(cmd));

		
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
void WarRoomMainWindow::onSaveAction() {
	QString path = m_currentFilePath;
	if (path.isEmpty()) {
		path = QFileDialog::getSaveFileName(this, "保存作战图", "", "WarRoom文件 (*.warroom)");
	}
	if (path.isEmpty()) return;

	// 先同步视图状态到模型
	if (m_view) {
		warroom::Point2D viewCenter = m_view->getViewCenter();
		m_model.setCameraView(viewCenter, m_view->getZoomLevel());
	}

	std::string fullPath = path.toStdString();
	if (fullPath.find(".warroom") == std::string::npos) {
		fullPath += ".warroom";
	}

	if (m_model.saveToFile(fullPath)) {
		m_currentFilePath = QString::fromStdString(fullPath);
		QMessageBox::information(this, "保存成功", "文件已保存");
	}
	else {
		QMessageBox::warning(this, "保存失败", "无法保存文件");
	}
}

void WarRoomMainWindow::onLoadAction() {
	QString path = QFileDialog::getOpenFileName(this, "加载作战图", "", "WarRoom文件 (*.warroom)");
	if (path.isEmpty()) return;

	warroom::WarRoomModel newModel;
	if (newModel.loadFromFile(path.toStdString())) {
		// 替换当前模型
		m_model = std::move(newModel);
		m_currentFilePath = path;

		// 清空场景并重建
		m_scene->clear();
		rebuildFromModel();  // 需要实现这个方法

		// 恢复视图
		if (m_view) {
			warroom::Point2D viewPos;
			float zoom;
			m_model.getCameraView(viewPos, zoom);
			m_view->restoreViewState(viewPos, zoom);
		}

		QMessageBox::information(this, "加载成功", "文件已加载");
	}
	else {
		QMessageBox::warning(this, "加载失败", "无法加载文件");
	}
}

void WarRoomMainWindow::onExportJson() {
	QString path = QFileDialog::getSaveFileName(this, "导出JSON", "", "JSON文件 (*.json)");
	if (path.isEmpty()) return;

	std::ofstream file(path.toStdString());
	if (file.is_open()) {
		file << m_model.toJson().dump(2);
		file.close();
		QMessageBox::information(this, "导出成功", "JSON已导出");
	}
	else {
		QMessageBox::warning(this, "导出失败", "无法创建文件");
	}
}

void WarRoomMainWindow::onImportJson() {
	QString path = QFileDialog::getOpenFileName(this, "导入JSON", "", "JSON文件 (*.json)");
	if (path.isEmpty()) return;

	std::ifstream file(path.toStdString());
	if (file.is_open()) {
		nlohmann::json j;
		file >> j;
		if (m_model.fromJson(j)) {
			m_scene->clear();
			rebuildFromModel();
			QMessageBox::information(this, "导入成功", "JSON已导入");
		}
		else {
			QMessageBox::warning(this, "导入失败", "JSON格式错误");
		}
		file.close();
	}
	else {
		QMessageBox::warning(this, "导入失败", "无法打开文件");
	}
}

// 辅助方法：从模型重建场景
void WarRoomMainWindow::rebuildFromModel() {
	// 清空映射表
	//qDeleteAll(m_nodeItems);
	m_nodeItems.clear();
	// 收集所有节点 ID
	std::function<void(warroom::Uuid)> addNodeRecursive;
	addNodeRecursive = [&](warroom::Uuid parentId) {
		auto children = m_model.getChildren(parentId);
		for (const warroom::Uuid& childId : children) {
			const warroom::WarNode* node = m_model.getNode(childId);
			if (!node) continue;

			QColor color(QString::fromStdString(m_model.getEffectiveColor(childId)));

			auto* item = new NodeGraphicsItem(
				childId, *node,
				node->title.empty() ? "未命名" : node->title,
				node->full_text,
				color
			);

			item->setPos(node->pos_x, node->pos_y);

			QObject::connect(item, &NodeGraphicsItem::positionChanged,
				this, &WarRoomMainWindow::onNodeMoved);
			QObject::connect(item, &NodeGraphicsItem::moveFinished,
				this, &WarRoomMainWindow::onNodeMoveFinished);

			m_scene->addItem(item);
			m_nodeItems.insert(QString::fromStdString(childId), item);
			addNodeRecursive(childId);
		}
		};

	addNodeRecursive(m_model.getDocumentRootId());

	// 添加连线
	for (const auto& [linkId, link] : m_model.getAllLinks()) {
		auto* linkItem = new LinkGraphicsItem(linkId, m_model);
		m_scene->addItem(linkItem);
	}
}
void WarRoomMainWindow::setupToolBar()
{
	QToolBar* toolBar = addToolBar("文件");

	// 新建按钮
	QAction* newAction = new QAction(QIcon(), "新建", this);
	connect(newAction, &QAction::triggered, this, &WarRoomMainWindow::onNewAction);
	toolBar->addAction(newAction);

	// 打开按钮
	QAction* openAction = new QAction(QIcon(), "打开", this);
	connect(openAction, &QAction::triggered, this, &WarRoomMainWindow::onLoadAction);
	toolBar->addAction(openAction);

	// 保存按钮
	QAction* saveAction = new QAction(QIcon(), "保存", this);
	connect(saveAction, &QAction::triggered, this, &WarRoomMainWindow::onSaveAction);
	toolBar->addAction(saveAction);

	toolBar->addSeparator();

	// 撤销按钮
	QAction* undoAction = new QAction(QIcon(), "撤销", this);
	connect(undoAction, &QAction::triggered, this, &WarRoomMainWindow::onUndo);
	toolBar->addAction(undoAction);

	// 重做按钮
	QAction* redoAction = new QAction(QIcon(), "重做", this);
	connect(redoAction, &QAction::triggered, this, &WarRoomMainWindow::onRedo);
	toolBar->addAction(redoAction);
}
// 检查是否需要保存当前修改（简单实现）
bool WarRoomMainWindow::maybeSave()
{
	// 如果没有修改过，直接返回 true
	// 这里可以添加 dirty flag 来追踪是否有未保存的修改

	QMessageBox::StandardButton reply;
	reply = QMessageBox::question(this, "未保存的更改",
		"当前图表有未保存的更改，是否保存？",
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

	if (reply == QMessageBox::Save) {
		onSaveAction();
		return true;  // 假设保存成功
	}
	else if (reply == QMessageBox::Discard) {
		return true;
	}
	else {
		return false;  // 取消操作
	}
}

void WarRoomMainWindow::onNewAction()
{
	if (!maybeSave()) return;

	// 创建新模型
	warroom::WarRoomModel newModel;
	m_model = std::move(newModel);
	m_currentFilePath.clear();

	// 清空场景并重建
	clearScene();
	rebuildFromModel();

	// 重置视图
	onResetView();
}

void WarRoomMainWindow::onSaveAsAction()
{
	QString path = QFileDialog::getSaveFileName(this, "保存作战图", "", "WarRoom文件 (*.warroom)");
	if (path.isEmpty()) return;

	// 同步视图状态到模型
	if (m_view) {
		warroom::Point2D viewCenter = m_view->getViewCenter();
		m_model.setCameraView(viewCenter, m_view->getZoomLevel());
	}

	std::string fullPath = path.toStdString();
	if (fullPath.find(".warroom") == std::string::npos) {
		fullPath += ".warroom";
	}

	if (m_model.saveToFile(fullPath)) {
		m_currentFilePath = QString::fromStdString(fullPath);
		QMessageBox::information(this, "保存成功", "文件已保存");
	}
	else {
		QMessageBox::warning(this, "保存失败", "无法保存文件");
	}
}

void WarRoomMainWindow::onUndo()
{
	if (m_undoManager.canUndo()) {
		m_undoManager.undo(m_model);
		syncAllItemsFromModel();
		refreshLinks();
	}
}

void WarRoomMainWindow::onRedo()
{
	if (m_undoManager.canRedo()) {
		m_undoManager.redo(m_model);
		syncAllItemsFromModel();
		refreshLinks();
	}
}

void WarRoomMainWindow::onResetView()
{
	if (m_view) {
		m_view->resetTransform();
		m_view->centerOn(0, 0);
	}
}

void WarRoomMainWindow::onAbout()
{
	QMessageBox::about(this, "关于作战图",
		"作战图工具\n版本 1.0\n\n"
		"功能：\n"
		"• 节点管理（添加、删除、编辑）\n"
		"• 连线管理\n"
		"• 撤销/重做\n"
		"• 保存/加载文件\n"
		"• 导入/导出 JSON");
}

void WarRoomMainWindow::clearScene()
{
	// 清空映射表（场景清空会自动删除图形项）
	m_nodeItems.clear();
	// 清空场景中的所有项
	m_scene->clear();
}
void WarRoomMainWindow::executeCommand(std::unique_ptr<warroom::Command> cmd)
{
	m_undoManager.executeCommand(std::move(cmd), m_model);
	syncAllItemsFromModel();
	refreshLinks();
}
void WarRoomMainWindow::setupMenuBar()
{
	// 获取窗口的菜单栏，如果不存在则创建
	QMenuBar* menuBar = this->menuBar();
	if (!menuBar) {
		menuBar = new QMenuBar(this);
		this->setMenuBar(menuBar);
	}

	// 创建文件菜单
	QMenu* fileMenu = menuBar->addMenu("文件(&F)");  // &F 表示 Alt+F 快捷键

	QAction* newAction = new QAction("新建(&N)", this);
	newAction->setShortcut(QKeySequence::New);
	connect(newAction, &QAction::triggered, this, &WarRoomMainWindow::onNewAction);
	fileMenu->addAction(newAction);

	fileMenu->addSeparator();

	QAction* openAction = new QAction("打开(&O)...", this);
	openAction->setShortcut(QKeySequence::Open);
	connect(openAction, &QAction::triggered, this, &WarRoomMainWindow::onLoadAction);
	fileMenu->addAction(openAction);

	QAction* saveAction = new QAction("保存(&S)", this);
	saveAction->setShortcut(QKeySequence::Save);
	connect(saveAction, &QAction::triggered, this, &WarRoomMainWindow::onSaveAction);
	fileMenu->addAction(saveAction);

	QAction* saveAsAction = new QAction("另存为(&A)...", this);
	saveAsAction->setShortcut(QKeySequence::SaveAs);
	connect(saveAsAction, &QAction::triggered, this, &WarRoomMainWindow::onSaveAsAction);
	fileMenu->addAction(saveAsAction);

	fileMenu->addSeparator();

	QAction* importAction = new QAction("导入 JSON(&I)...", this);
	connect(importAction, &QAction::triggered, this, &WarRoomMainWindow::onImportJson);
	fileMenu->addAction(importAction);

	QAction* exportAction = new QAction("导出 JSON(&E)...", this);
	connect(exportAction, &QAction::triggered, this, &WarRoomMainWindow::onExportJson);
	fileMenu->addAction(exportAction);

	fileMenu->addSeparator();

	QAction* exitAction = new QAction("退出(&X)", this);
	exitAction->setShortcut(QKeySequence::Quit);
	connect(exitAction, &QAction::triggered, this, &QWidget::close);
	fileMenu->addAction(exitAction);

	// 编辑菜单
	QMenu* editMenu = menuBar->addMenu("编辑(&E)");

	QAction* undoAction = new QAction("撤销(&U)", this);
	undoAction->setShortcut(QKeySequence::Undo);
	connect(undoAction, &QAction::triggered, this, &WarRoomMainWindow::onUndo);
	editMenu->addAction(undoAction);

	QAction* redoAction = new QAction("重做(&R)", this);
	redoAction->setShortcut(QKeySequence::Redo);
	connect(redoAction, &QAction::triggered, this, &WarRoomMainWindow::onRedo);
	editMenu->addAction(redoAction);

	editMenu->addSeparator();

	QAction* deleteAction = new QAction("删除(&D)", this);
	deleteAction->setShortcut(QKeySequence::Delete);
	connect(deleteAction, &QAction::triggered, this, &WarRoomMainWindow::deleteSelectedNode);
	editMenu->addAction(deleteAction);

	// 视图菜单
	QMenu* viewMenu = menuBar->addMenu("视图(&V)");

	QAction* resetViewAction = new QAction("重置视图(&R)", this);
	connect(resetViewAction, &QAction::triggered, this, &WarRoomMainWindow::onResetView);
	viewMenu->addAction(resetViewAction);

	// 帮助菜单
	QMenu* helpMenu = menuBar->addMenu("帮助(&H)");

	QAction* aboutAction = new QAction("关于(&A)", this);
	connect(aboutAction, &QAction::triggered, this, &WarRoomMainWindow::onAbout);
	helpMenu->addAction(aboutAction);
}
void WarRoomMainWindow::createLinkBetweenNodes(const std::string& fromId,int fromEdge, const std::string& toId, int toEdge)
{
	using warroom::WarLink;
	using warroom::LinkType;

	WarLink link = WarLink::makeNodeToNode(fromId, fromEdge, toId, toEdge, LinkType::Dependency);
	link.label = "";
	link.color = "#3498db";

	auto cmd = std::make_unique<warroom::AddLinkCommand>(std::move(link));

	// 保存 linkId 以便创建图形项
	warroom::Uuid newLinkId = cmd->getLinkId();

	// 执行命令
	m_undoManager.executeCommand(std::move(cmd), m_model);

	// 为新增连线创建图形项
	const warroom::WarLink* createdLink = m_model.getLink(newLinkId);
	if (createdLink) {
		auto* linkItem = new LinkGraphicsItem(newLinkId, m_model);
		m_scene->addItem(linkItem);
	}
}
void WarRoomMainWindow::deleteLink(const warroom::Uuid& linkId)
{
	const warroom::WarLink* link = m_model.getLink(linkId);
	if (!link) return;

	// 提取锚点中的节点ID
	warroom::Uuid startNodeId;
	warroom::Uuid endNodeId;

	if (auto* na = dynamic_cast<const warroom::NodeAnchor*>(link->start_anchor.get())) {
		startNodeId = na->node_id;
	}
	if (auto* na = dynamic_cast<const warroom::NodeAnchor*>(link->end_anchor.get())) {
		endNodeId = na->node_id;
	}

	auto cmd = std::make_unique<warroom::DeleteLinkCommand>(
		linkId, startNodeId, endNodeId, link->type, link->label, link->color
	);
	executeCommand(std::move(cmd));

	// 删除场景中的图形项
	for (QGraphicsItem* item : m_scene->items()) {
		if (auto* linkItem = dynamic_cast<LinkGraphicsItem*>(item)) {
			if (linkItem->linkId() == linkId) {
				m_scene->removeItem(linkItem);
				delete linkItem;
				break;
			}
		}
	}
}