#include "WarRoomMainWindow.h"

// 标准库
#include <fstream>
#include <iostream>
#include <functional>

// Qt 头文件
#include <QFileDialog>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QMessageBox>
#include <QToolBar>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSettings>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QUrl>
#include <QDesktopServices>
#include <QFontDialog>

// 项目核心 - 命令
#include "core/command/add_link_command.h"
#include "core/command/add_node_command.h"
#include "core/command/delete_link_command.h"
#include "core/command/delete_node_command.h"
#include "core/command/edit_node_command.h"
#include "core/command/move_node_command.h"
#include "core/command/resize_node_command.h"

// 项目 UI
#include "ui/LinkCreationManager.h"
#include "ui/LinkGraphicsItem.h"
#include "ui/NodeGraphicsItem.h"
#include "ui/warroomview.h"

// 节点模组
#include "mod/builtin/BuiltinMods.h"
#include "mod/builtin/ImageMod.h"
#include "mod/builtin/VideoMod.h"

// ============================================================================
// 静态成员定义
// ============================================================================
QFont WarRoomMainWindow::s_nodeFont{ "Microsoft YaHei", 10, QFont::Normal };
QColor WarRoomMainWindow::s_canvasBackgroundColor{ 30, 30, 30 };

// ============================================================================
// 构造与析构
// ============================================================================

WarRoomMainWindow::WarRoomMainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	// 注册内置节点模组（静态注册，幂等）
	warroom::registerBuiltinMods();

	// 设置无边框窗口
	setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
	setAttribute(Qt::WA_TranslucentBackground, false);  // 不透明背景
	setAttribute(Qt::WA_DeleteOnClose);

	// 构建自定义 UI
	setupCustomUi();

	// 初始化场景
	setupScene();

	// 设置场景连接（你原有的）
	setupSceneConnections();

	// 设置标题栏信号
	setupTitleBar();

	// 设置侧边栏信号
	setupSidebar();

	// ---- 加载字体配置（在填充节点之前，确保新节点使用用户设置的字体）----
	loadNodeFont();

	// ---- 加载背景颜色配置并应用到视图 ----
	loadCanvasBackgroundColor();
	if (m_view) {
		m_view->setBackgroundColor(s_canvasBackgroundColor);
	}

	// 从模型填充初始数据
	populateFromModel();

	// 初始化侧边栏
	refreshSidebarTree();

	// 平台适配（必须在 show 之后调用 winId）
	WindowHelper::setupFramelessWindow(this);

	// ---- 启动时自动加载上次打开的文件 ----
	QString lastPath = readLastOpenFilePath();
	if (!lastPath.isEmpty() && QFile::exists(lastPath)) {
		loadFromFilePath(lastPath);
	}

	resize(800, 600);
}

WarRoomMainWindow::~WarRoomMainWindow()
{
	// 1. 断开所有信号连接，避免析构过程中触发槽函数访问半销毁的对象
	disconnect();

	// 2. 先清理连接管理器（移除事件过滤器），防止后续访问场景
	LinkCreationManager::instance().setScene(nullptr);
	LinkCreationManager::instance().setMainWindow(nullptr);

	// 3. 直接删除场景（会自动 clear 所有图形项，无需手动 clear）
	if (m_scene) {
		delete m_scene;
		m_scene = nullptr;
	}

	// 4. 清空节点映射表（此时所有 NodeGraphicsItem 已由场景删除）
	m_nodeItems.clear();

	// 5. 清空编辑状态（图形项已销毁，无需再调用 setEditMode）
	m_currentEditingNodeId.clear();
}

// ============================================================================
// 公开方法 - 连线操作
// ============================================================================

void WarRoomMainWindow::createLinkBetweenNodes(const std::string& fromId, int fromEdge,
	const std::string& toId, int toEdge)
{
	using warroom::WarLink;
	using warroom::LinkType;

	WarLink link = WarLink::makeNodeToNode(fromId, fromEdge, toId, toEdge, LinkType::Dependency);
	link.label = "";
	link.color = "#3498db";

	auto cmd = std::make_unique<warroom::AddLinkCommand>(std::move(link));

	warroom::Uuid newLinkId = cmd->getLinkId();

	m_undoManager.executeCommand(std::move(cmd), m_model);

	const warroom::WarLink* createdLink = m_model.getLink(newLinkId);
	if (createdLink) {
		auto* linkItem = new LinkGraphicsItem(newLinkId, m_model, this);
		m_scene->addItem(linkItem);
	}
}

void WarRoomMainWindow::deleteLink(const warroom::Uuid& linkId)
{
	qDebug("deleteLink");
	const warroom::WarLink* link = m_model.getLink(linkId);
	if (!link) return;

	warroom::Uuid startNodeId;
	warroom::Uuid endNodeId;

	if (auto* na = dynamic_cast<const warroom::NodeAnchor*>(link->start_anchor.get())) {
		startNodeId = na->node_id;
	}
	if (auto* na = dynamic_cast<const warroom::NodeAnchor*>(link->end_anchor.get())) {
		endNodeId = na->node_id;
	}

	auto cmd = std::make_unique<warroom::DeleteLinkCommand>(
		linkId, startNodeId, endNodeId, link->type, link->label, link->color);
	executeCommand(std::move(cmd));

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

void WarRoomMainWindow::createNodeAndLink(const std::string& fromId, int fromEdge, QPointF scenePos)
{
	using warroom::WarNode;
	using warroom::LinkType;

	// 1. 创建新节点
	WarNode newNode = WarNode::makeLeaf("新节点", scenePos.x(), scenePos.y());
	newNode.full_text = "双击编辑长文本...";

	auto addNodeCmd = std::make_unique<warroom::AddNodeCommand>(
		std::move(newNode), m_model.getDocumentRootId(), -1);
	warroom::Uuid newNodeId = addNodeCmd->getNodeId();

	m_undoManager.executeCommand(std::move(addNodeCmd), m_model);

	// 2. 同步UI，创建节点图形项
	syncAllItemsFromModel();

	// 3. 创建连线（从新节点的对边连到起始节点）
	int toEdge = (fromEdge + 2) % 4;  // 对边：右<->左, 下<->上

	warroom::WarLink link = warroom::WarLink::makeNodeToNode(
		fromId, fromEdge, newNodeId, toEdge, LinkType::Dependency);
	link.label = "";
	link.color = "#3498db";

	auto addLinkCmd = std::make_unique<warroom::AddLinkCommand>(std::move(link));
	warroom::Uuid newLinkId = addLinkCmd->getLinkId();

	m_undoManager.executeCommand(std::move(addLinkCmd), m_model);

	// 4. 创建连线图形项
	const warroom::WarLink* createdLink = m_model.getLink(newLinkId);
	if (createdLink) {
		auto* linkItem = new LinkGraphicsItem(newLinkId, m_model, this);
		m_scene->addItem(linkItem);
	}

	// 5. 刷新连线
	refreshLinks();
}

// ============================================================================
// 私有槽 - 文件操作
// ============================================================================

void WarRoomMainWindow::onNewAction()
{
	if (!maybeSave()) return;

	warroom::WarRoomModel newModel;
	m_model = std::move(newModel);
	m_currentFilePath.clear();
	// 文档目录清空，便于 ImageMod/VideoMod 之类的资源相对路径解析
	warroom::ImageMod::setCurrentDocumentDir(QString());
	warroom::VideoMod::setCurrentDocumentDir(QString());

	clearScene();
	rebuildFromModel();
	onResetView();
}

void WarRoomMainWindow::onSaveAction()
{
	QString path = m_currentFilePath;
	if (path.isEmpty()) {
		path = QFileDialog::getSaveFileName(this, "保存作战图", "",
			"WarRoom文件 (*.warroom)",
			nullptr, QFileDialog::DontConfirmOverwrite);
	}
	if (path.isEmpty()) return;

	if (m_view) {
		warroom::Point2D viewCenter = m_view->getViewCenter();
		m_model.setCameraView(viewCenter, m_view->getZoomLevel());
	}

	if (!path.endsWith(".warroom", Qt::CaseInsensitive)) {
		path += ".warroom";
	}

	// 在写入磁盘之前，把当前文档目录告诉 ImageMod/VideoMod，
	// 这样新选的图片/视频会按相对路径存
	warroom::ImageMod::setCurrentDocumentDir(QFileInfo(path).absolutePath());
	warroom::VideoMod::setCurrentDocumentDir(QFileInfo(path).absolutePath());

	if (m_model.saveToFile(path.toStdString())) {
		m_currentFilePath = path;
		writeLastOpenFilePath(path);
		QMessageBox::information(this, "保存成功", "文件已保存");
	}
	else {
		QMessageBox::warning(this, "保存失败", "无法保存文件");
	}
}

void WarRoomMainWindow::onSaveAsAction()
{
	QString path = QFileDialog::getSaveFileName(this, "保存作战图", "",
		"WarRoom文件 (*.warroom)");
	if (path.isEmpty()) return;

	if (m_view) {
		warroom::Point2D viewCenter = m_view->getViewCenter();
		m_model.setCameraView(viewCenter, m_view->getZoomLevel());
	}

	std::string fullPath = path.toStdString();
	if (fullPath.find(".warroom") == std::string::npos) {
		fullPath += ".warroom";
	}

	QString absDir = QFileInfo(QString::fromStdString(fullPath)).absolutePath();
	warroom::ImageMod::setCurrentDocumentDir(absDir);
	warroom::VideoMod::setCurrentDocumentDir(absDir);

	if (m_model.saveToFile(fullPath)) {
		m_currentFilePath = QString::fromStdString(fullPath);
		writeLastOpenFilePath(QString::fromStdString(fullPath));
		QMessageBox::information(this, "保存成功", "文件已保存");
	}
	else {
		QMessageBox::warning(this, "保存失败", "无法保存文件");
	}
}

void WarRoomMainWindow::onLoadAction()
{
	QString path = QFileDialog::getOpenFileName(this, "加载作战图", "",
		"WarRoom文件 (*.warroom)");
	if (path.isEmpty()) return;

	// 在 loadFromFile 之前先把文档目录告诉 ImageMod/VideoMod，
	// 反序列化时模组的 onNodeLoaded 才能正确解析相对路径
	QString docDir = QFileInfo(path).absolutePath();
	warroom::ImageMod::setCurrentDocumentDir(docDir);
	warroom::VideoMod::setCurrentDocumentDir(docDir);

	warroom::WarRoomModel newModel;
	if (newModel.loadFromFile(path.toStdString())) {
		m_model = std::move(newModel);
		m_currentFilePath = path;
		writeLastOpenFilePath(path);

		m_scene->clear();
		rebuildFromModel();

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

// ============================================================================
// 私有槽 - 导入导出
// ============================================================================

void WarRoomMainWindow::onExportJson()
{
	QString path = QFileDialog::getSaveFileName(this, "导出JSON", "",
		"JSON文件 (*.json)");
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

void WarRoomMainWindow::onImportJson()
{
	QString path = QFileDialog::getOpenFileName(this, "导入JSON", "",
		"JSON文件 (*.json)");
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

// ============================================================================
// 私有槽 - 编辑操作
// ============================================================================

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

// ============================================================================
// 私有槽 - 视图操作
// ============================================================================

void WarRoomMainWindow::onResetView()
{
	if (m_view) {
		m_view->resetTransform();
		m_view->centerOn(0, 0);
	}
}

// ============================================================================
// 私有槽 - 节点操作
// ============================================================================

void WarRoomMainWindow::onNodeSelectedForZBoost(const std::string& nodeId)
{
	warroom::WarNode* node = m_model.getNodeMutable(nodeId);
	if (!node) return;

	// 计算当前所有节点的最高绝对 Z 值
	int max_abs_z = 0;
	for (const auto& [id, n] : m_model.getAllNodes()) {
		int abs_z = m_model.computeAbsoluteZ(id);
		if (abs_z > max_abs_z) max_abs_z = abs_z;
	}

	// 计算当前节点的绝对 Z 值
	int current_abs_z = m_model.computeAbsoluteZ(nodeId);

	// 如果已经是最高的，不需要调整
	if (current_abs_z > max_abs_z) return;

	// 计算需要增加的 relative_z
	int target_abs_z = max_abs_z + 1;
	int delta = target_abs_z - current_abs_z;
	node->relative_z += delta;

	// 更新该节点及其所有子孙的 Z 值
	updateSubtreeZValues(nodeId);
	refreshAllLinksZValue();
	refreshLinks();
}

// ============================================================================
// 私有槽 - 帮助
// ============================================================================

void WarRoomMainWindow::onAbout()
{
	QMessageBox::about(this, "关于作战图",
		"作战图工具\n版本 0.1.0\n\n"
		"功能：\n"
		"• 节点管理（添加、删除、编辑）\n"
		"• 连线管理\n"
		"• 撤销/重做\n"
		"• 保存/加载文件\n"
		"• 导入/导出 JSON");
}

// ============================================================================
// 私有方法 - 场景初始化
// ============================================================================

void WarRoomMainWindow::setupScene()
{
	m_scene = new QGraphicsScene(this);
	m_scene->setSceneRect(-10000, -10000, 20000, 20000);

	// 创建视图并设置到画布区域
	m_view = new WarRoomView(m_scene, m_canvasArea);

	// 确保画布区域的布局正确添加视图
	if (m_canvasArea->layout()) {
		m_canvasArea->layout()->addWidget(m_view);
	}
	else {
		// 如果没有布局，创建一个
		auto* layout = new QVBoxLayout(m_canvasArea);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(m_view);
	}

	// 注意：不要调用 setCentralWidget，因为已经设置了 m_centralContainer

	LinkCreationManager::instance().setMainWindow(this);
	LinkCreationManager::instance().setScene(m_scene);

	setupSceneConnections();
}

void WarRoomMainWindow::populateFromModel()
{
	using warroom::WarNode;
	using warroom::Uuid;

	// ---- 从模型读取连线，创建 LinkGraphicsItem ----
	for (const auto& [linkId, link] : m_model.getAllLinks()) {
		auto* linkItem = new LinkGraphicsItem(linkId, m_model, this);
		m_scene->addItem(linkItem);
	}

	// ---- 递归创建节点图形项 ----
	std::function<void(Uuid)> createItems;
	createItems = [&](Uuid parentId) {
		auto children = m_model.getChildren(parentId);
		for (const Uuid& childId : children) {
			const WarNode* node = m_model.getNode(childId);
			if (!node) continue;

			auto* item = new NodeGraphicsItem(childId, m_model);
			item->setResizingEnabled(!node->is_collapsed);
			item->setPos(node->pos_x, node->pos_y);
			item->refreshFont(s_nodeFont);

			int abs_z = m_model.computeAbsoluteZ(childId);
			item->updateAbsoluteZ(abs_z);

			connectNodeSignals(item);

			m_scene->addItem(item);
			m_nodeItems.insert(QString::fromStdString(childId), item);
			createItems(childId);
		}
		};

	createItems(m_model.getDocumentRootId());
}

void WarRoomMainWindow::rebuildFromModel()
{
	m_nodeItems.clear();

	// 递归添加节点图形项
	std::function<void(warroom::Uuid)> addNodeRecursive;
	addNodeRecursive = [&](warroom::Uuid parentId) {
		auto children = m_model.getChildren(parentId);
		for (const warroom::Uuid& childId : children) {
			const warroom::WarNode* node = m_model.getNode(childId);
			if (!node) continue;

			auto* item = new NodeGraphicsItem(childId, m_model);
			item->setResizingEnabled(!node->is_collapsed);
			item->setPos(node->pos_x, node->pos_y);
			item->refreshFont(s_nodeFont);

			int abs_z = m_model.computeAbsoluteZ(childId);
			item->updateAbsoluteZ(abs_z);

			connectNodeSignals(item);

			m_scene->addItem(item);
			m_nodeItems.insert(QString::fromStdString(childId), item);
			addNodeRecursive(childId);
		}
		};

	addNodeRecursive(m_model.getDocumentRootId());

	// 添加连线图形项
	for (const auto& [linkId, link] : m_model.getAllLinks()) {
		auto* linkItem = new LinkGraphicsItem(linkId, m_model, this);
		m_scene->addItem(linkItem);
	}

	refreshAllLinksZValue();
}

void WarRoomMainWindow::clearScene()
{
	// 先停止所有连接操作
	LinkCreationManager::instance().cancelConnection();

	// 先清理节点映射表（断开信号连接）
	for (auto* item : m_nodeItems) {
		if (item) {
			// 断开所有信号连接
			item->disconnect();
		}
	}
	m_nodeItems.clear();

	// 最后清空场景
	m_scene->clear();
}

// ============================================================================
// 私有方法 - UI 组件设置
// ============================================================================

void WarRoomMainWindow::setupMenuBar()
{
	QMenuBar* menuBar = this->menuBar();
	if (!menuBar) {
		menuBar = new QMenuBar(this);
		this->setMenuBar(menuBar);
	}

	// ---- 文件菜单 ----
	QMenu* fileMenu = menuBar->addMenu("文件(&F)");

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

	// ---- 编辑菜单 ----
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

	// ---- 视图菜单 ----
	QMenu* viewMenu = menuBar->addMenu("视图(&V)");

	QAction* resetViewAction = new QAction("重置视图(&R)", this);
	connect(resetViewAction, &QAction::triggered, this, &WarRoomMainWindow::onResetView);
	viewMenu->addAction(resetViewAction);

	// ---- 帮助菜单 ----
	QMenu* helpMenu = menuBar->addMenu("帮助(&H)");

	QAction* aboutAction = new QAction("关于(&A)", this);
	connect(aboutAction, &QAction::triggered, this, &WarRoomMainWindow::onAbout);
	helpMenu->addAction(aboutAction);
}

void WarRoomMainWindow::setupToolBar()
{
	QToolBar* toolBar = addToolBar("文件");

	QAction* newAction = new QAction(QIcon(), "新建", this);
	connect(newAction, &QAction::triggered, this, &WarRoomMainWindow::onNewAction);
	toolBar->addAction(newAction);

	QAction* openAction = new QAction(QIcon(), "打开", this);
	connect(openAction, &QAction::triggered, this, &WarRoomMainWindow::onLoadAction);
	toolBar->addAction(openAction);

	QAction* saveAction = new QAction(QIcon(), "保存", this);
	connect(saveAction, &QAction::triggered, this, &WarRoomMainWindow::onSaveAction);
	toolBar->addAction(saveAction);

	toolBar->addSeparator();

	QAction* undoAction = new QAction(QIcon(), "撤销", this);
	connect(undoAction, &QAction::triggered, this, &WarRoomMainWindow::onUndo);
	toolBar->addAction(undoAction);

	QAction* redoAction = new QAction(QIcon(), "重做", this);
	connect(redoAction, &QAction::triggered, this, &WarRoomMainWindow::onRedo);
	toolBar->addAction(redoAction);
}

void WarRoomMainWindow::setupSceneConnections()
{
	m_scene->installEventFilter(this);
}

// ============================================================================
// 私有方法 - 文件检查
// ============================================================================

bool WarRoomMainWindow::maybeSave()
{
	QMessageBox::StandardButton reply;
	reply = QMessageBox::question(this, "未保存的更改",
		"当前图表有未保存的更改，是否保存？",
		QMessageBox::Save | QMessageBox::Discard
		| QMessageBox::Cancel);

	if (reply == QMessageBox::Save) {
		onSaveAction();
		return true;
	}
	else if (reply == QMessageBox::Discard) {
		return true;
	}
	else {
		return false;
	}
}

// ============================================================================
// 私有方法 - 节点信号连接
// ============================================================================

void WarRoomMainWindow::connectNodeSignals(NodeGraphicsItem* item)
{
	QObject::connect(item, &NodeGraphicsItem::positionChanged,
		this, &WarRoomMainWindow::onNodeMoved);
	QObject::connect(item, &NodeGraphicsItem::moveFinished,
		this, &WarRoomMainWindow::onNodeMoveFinished);
	QObject::connect(item, &NodeGraphicsItem::sizeChanged,
		this, &WarRoomMainWindow::onNodeSizeChanged);
	QObject::connect(item, &NodeGraphicsItem::resizeFinished,
		this, &WarRoomMainWindow::onNodeResizeFinished);
	QObject::connect(item, &NodeGraphicsItem::selectedForZBoost,
		this, &WarRoomMainWindow::onNodeSelectedForZBoost);
	QObject::connect(item, &NodeGraphicsItem::editRequested, this,
		[this](const std::string& nodeId) {
			if (!m_currentEditingNodeId.empty()
				&& m_currentEditingNodeId != nodeId) {
				auto* oldItem = m_nodeItems.value(
					QString::fromStdString(m_currentEditingNodeId));
				if (oldItem) {
					oldItem->saveAndExitEditMode();
				}
			}
			m_currentEditingNodeId = nodeId;
		});
}

// ============================================================================
// 私有方法 - 拖拽回写
// ============================================================================

void WarRoomMainWindow::onNodeMoved(const std::string& nodeId, float newX, float newY)
{
	warroom::WarNode* node = m_model.getNodeMutable(nodeId);
	if (!node) return;

	node->pos_x = newX;
	node->pos_y = newY;

	const warroom::WarNode* parent = m_model.getNode(node->parent_id);
	if (parent && node->parent_id != m_model.getDocumentRootId()) {
		node->rel_x = newX - parent->pos_x;
		node->rel_y = newY - parent->pos_y;
	}
	else {
		node->rel_x = newX;
		node->rel_y = newY;
	}

	for (const auto& childId : node->children_ids) {
		m_model.updateAbsolutePositionRecursive(childId);
	}
	updateSubtreePositionRecursive(nodeId);
	refreshLinks();
}

void WarRoomMainWindow::onNodeMoveFinished(const std::string& nodeId,
	float oldX, float oldY,
	float newX, float newY)
{
	warroom::WarNode* node = m_model.getNodeMutable(nodeId);
	if (!node) return;

	node->pos_x = newX;
	node->pos_y = newY;

	const warroom::WarNode* parent = m_model.getNode(node->parent_id);
	if (parent && node->parent_id != m_model.getDocumentRootId()) {
		node->rel_x = newX - parent->pos_x;
		node->rel_y = newY - parent->pos_y;
	}
	else {
		node->rel_x = newX;
		node->rel_y = newY;
	}

	for (const auto& childId : node->children_ids) {
		m_model.updateAbsolutePositionRecursive(childId);
	}

	auto cmd = std::make_unique<warroom::MoveNodeCommand>(nodeId, oldX, oldY, newX, newY);
	executeCommand(std::move(cmd));

	// 检查是否需要改变父节点
	NodeGraphicsItem* item = m_nodeItems.value(QString::fromStdString(nodeId));
	if (item) {
		QPointF center = item->sceneBoundingRect().center();
		std::string targetId = findTopmostNodeAtPoint(center, nodeId);

		if (!targetId.empty() && targetId != node->parent_id) {
			// 检查目标是否是当前节点的后代
			bool isDescendant = false;
			warroom::Uuid checkId = targetId;
			while (!checkId.empty() && checkId != m_model.getDocumentRootId()) {
				if (checkId == nodeId) {
					isDescendant = true;
					break;
				}
				const warroom::WarNode* checkNode = m_model.getNode(checkId);
				if (!checkNode) break;
				checkId = checkNode->parent_id;
			}

			if (!isDescendant) {
				reparentNode(nodeId, targetId);
			}
		}
		else if (targetId.empty() && node->parent_id != m_model.getDocumentRootId()) {
			reparentNode(nodeId, m_model.getDocumentRootId());
		}
	}

	refreshLinks();
}

// ============================================================================
// 私有方法 - 尺寸变更回写
// ============================================================================

void WarRoomMainWindow::onNodeSizeChanged(const std::string& nodeId,
	float newWidth, float newHeight)
{
	warroom::WarNode* node = m_model.getNodeMutable(nodeId);
	if (node) {
		node->width = newWidth;
		node->height = newHeight;
	}
	refreshLinks();
}

void WarRoomMainWindow::onNodeResizeFinished(const std::string& nodeId,
	float oldWidth, float oldHeight,
	float newWidth, float newHeight)
{
	auto cmd = std::make_unique<warroom::ResizeNodeCommand>(
		nodeId, oldWidth, oldHeight, newWidth, newHeight);
	executeCommand(std::move(cmd));
}

// ============================================================================
// 私有方法 - 键盘事件
// ============================================================================

void WarRoomMainWindow::keyPressEvent(QKeyEvent* event)
{
	if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Z) {
		if (m_undoManager.canUndo()) {
			m_undoManager.undo(m_model);
			syncAllItemsFromModel();
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

// ============================================================================
// 私有方法 - 视图同步
// ============================================================================

void WarRoomMainWindow::syncAllItemsFromModel()
{
	// 收集模型中所有节点 ID
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

	// 删除映射表中模型已不存在的节点
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
					auto* item = new NodeGraphicsItem(childId, m_model);
					item->refreshFont(s_nodeFont);

					int abs_z = m_model.computeAbsoluteZ(childId);
					item->updateAbsoluteZ(abs_z);

					item->setPos(node->pos_x, node->pos_y);
					item->setNodeSize(node->width, node->height);
					item->setResizingEnabled(!node->is_collapsed);

					connectNodeSignals(item);

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
			nodeItem->refresh();
			nodeItem->blockSignals(false);
		}
	}

	// 同步刷新侧边栏
	refreshSidebarTree();
}

void WarRoomMainWindow::refreshLinks()
{
	QList<QGraphicsItem*> itemsToRemove;

	for (QGraphicsItem* item : m_scene->items()) {
		if (auto* linkItem = dynamic_cast<LinkGraphicsItem*>(item)) {
			if (!m_model.getLink(linkItem->linkId())) {
				itemsToRemove.append(item);
			}
			else {
				linkItem->updatePositions();
			}
		}
	}

	for (QGraphicsItem* item : itemsToRemove) {
		m_scene->removeItem(item);
		delete item;
	}
}

void WarRoomMainWindow::refreshAllLinksZValue()
{
	for (QGraphicsItem* item : m_scene->items()) {
		if (auto* linkItem = dynamic_cast<LinkGraphicsItem*>(item)) {
			linkItem->updateZValueFromNodes();
		}
	}
}

// ============================================================================
// 私有方法 - 节点查找
// ============================================================================

std::string WarRoomMainWindow::findTopmostNodeAtPoint(QPointF scenePos,
	const std::string& excludeId)
{
	int highestZ = -999999;
	std::string resultId;

	std::function<void(const std::string&)> searchRecursive =
		[&](const std::string& nodeId) {
		if (nodeId == excludeId) return;

		NodeGraphicsItem* item = m_nodeItems.value(QString::fromStdString(nodeId));
		if (!item) return;

		if (item->boundingRect().contains(item->mapFromScene(scenePos))) {
			int z = static_cast<int>(item->zValue());
			if (z > highestZ) {
				highestZ = z;
				resultId = nodeId;
			}
		}

		const warroom::WarNode* node = m_model.getNode(nodeId);
		if (node) {
			for (const auto& childId : node->children_ids) {
				searchRecursive(childId);
			}
		}
		};

	std::vector<warroom::Uuid> topLevelNodes = m_model.getTopLevelNodes();
	for (const auto& nodeId : topLevelNodes) {
		searchRecursive(nodeId);
	}

	return resultId;
}

// ============================================================================
// 私有方法 - 父子关系
// ============================================================================

void WarRoomMainWindow::reparentNode(const std::string& nodeId,
	const std::string& newParentId)
{
	warroom::WarNode* node = m_model.getNodeMutable(nodeId);
	if (!node) return;

	std::string oldParentId = node->parent_id;
	if (oldParentId == newParentId) return;

	// 防止循环引用
	warroom::Uuid checkId = newParentId;
	while (!checkId.empty() && checkId != m_model.getDocumentRootId()) {
		if (checkId == nodeId) return;
		const warroom::WarNode* parent = m_model.getNode(checkId);
		if (!parent) break;
		checkId = parent->parent_id;
	}

	float absX = node->pos_x;
	float absY = node->pos_y;

	// 从旧父节点移除
	warroom::WarNode* oldParent = m_model.getNodeMutable(oldParentId);
	if (oldParent) {
		auto& siblings = oldParent->children_ids;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), nodeId),
			siblings.end());
	}

	// 添加到新父节点
	warroom::WarNode* newParent = m_model.getNodeMutable(newParentId);
	if (!newParent && newParentId != m_model.getDocumentRootId()) return;

	if (newParent) {
		newParent->children_ids.push_back(nodeId);
	}
	node->parent_id = newParentId;

	// 根据绝对坐标重新计算相对坐标
	const warroom::WarNode* parentNode = m_model.getNode(newParentId);
	if (parentNode && newParentId != m_model.getDocumentRootId()) {
		node->rel_x = absX - parentNode->pos_x;
		node->rel_y = absY - parentNode->pos_y;
	}
	else {
		node->rel_x = absX;
		node->rel_y = absY;
	}

	refreshLinks();
	rebuildAllBoundingRects();
}

// ============================================================================
// 私有方法 - 子树位置更新
// ============================================================================

void WarRoomMainWindow::updateSubtreePositionRecursive(const std::string& nodeId)
{
	NodeGraphicsItem* item = m_nodeItems.value(QString::fromStdString(nodeId));
	const warroom::WarNode* node = m_model.getNode(nodeId);

	if (item && node) {
		item->blockSignals(true);
		item->setPos(node->pos_x, node->pos_y);
		item->blockSignals(false);
	}

	if (node) {
		for (const auto& childId : node->children_ids) {
			updateSubtreePositionRecursive(childId);
		}
	}
}

// ============================================================================
// 私有方法 - Z 值更新
// ============================================================================

void WarRoomMainWindow::updateSubtreeZValues(const std::string& nodeId)
{
	NodeGraphicsItem* item = m_nodeItems.value(QString::fromStdString(nodeId));
	if (item) {
		int abs_z = m_model.computeAbsoluteZ(nodeId);
		item->updateAbsoluteZ(abs_z);
	}

	const warroom::WarNode* node = m_model.getNode(nodeId);
	if (node) {
		for (const auto& childId : node->children_ids) {
			updateSubtreeZValues(childId);
		}
	}
}

// ============================================================================
// 私有方法 - 包围盒
// ============================================================================

void WarRoomMainWindow::rebuildAllBoundingRects()
{
	NodeGraphicsItem::rebuildAllBoundingRects(m_nodeItems);
}

// ============================================================================
// 私有方法 - 命令执行
// ============================================================================

void WarRoomMainWindow::executeCommand(std::unique_ptr<warroom::Command> cmd)
{
	m_undoManager.executeCommand(std::move(cmd), m_model);
	syncAllItemsFromModel();
	refreshLinks();
}

// ============================================================================
// 私有方法 - 节点操作辅助
// ============================================================================

NodeContext WarRoomMainWindow::captureNodeContext(const warroom::Uuid& nodeId) {
	NodeContext ctx;
	ctx.nodeId = nodeId;
	ctx.index = -1;  // 默认值

	const warroom::WarNode* node = m_model.getNode(nodeId);
	if (!node) {
		return ctx;  // 节点不存在，返回空 context
	}

	ctx.savedNode = *node;
	ctx.parentId = node->parent_id;

	const warroom::WarNode* parent = m_model.getNode(ctx.parentId);
	if (parent) {
		for (size_t i = 0; i < parent->children_ids.size(); ++i) {
			if (parent->children_ids[i] == nodeId) {
				ctx.index = static_cast<int>(i);
				break;
			}
		}
	}
	return ctx;
}

// WarRoomMainWindow.cpp
void WarRoomMainWindow::deleteSelectedNode() {
	QList<QGraphicsItem*> selected = m_scene->selectedItems();

	// 找到第一个选中的节点
	NodeGraphicsItem* selectedItem = nullptr;
	for (QGraphicsItem* item : selected) {
		auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item);
		if (nodeItem) {
			selectedItem = nodeItem;
			break;
		}
	}

	if (!selectedItem) return;

	std::string nodeId = selectedItem->nodeId();
	if (nodeId == m_model.getDocumentRootId()) return;

	// 检查节点是否存在
	const warroom::WarNode* node = m_model.getNode(nodeId);
	if (!node) return;

	// 保存节点上下文
	NodeContext ctx = captureNodeContext(nodeId);

	// 在从模型删除之前，先让图形项进入安全状态
	auto* item = m_nodeItems.value(QString::fromStdString(nodeId));
	if (item) {
		item->prepareForRemoval();  // 新增这一行
	}

	// 删除节点（模型内部会处理子节点的重新挂载）
	m_model.removeNode(nodeId, true);

	// 创建撤销命令
	auto cmd = std::make_unique<warroom::DeleteNodeCommand>(
		ctx.nodeId, ctx.savedNode, ctx.parentId, ctx.index);
	m_undoManager.executeCommand(std::move(cmd), m_model);

	// 同步UI
	syncAllItemsFromModel();
	refreshLinks();
}

void WarRoomMainWindow::addNodeAtPosition(QPointF scenePos)
{
	addNodeAtPosition(scenePos, m_model.getDocumentRootId());
}

void WarRoomMainWindow::addNodeAtPosition(QPointF scenePos,
	const warroom::Uuid& parentId)
{
	warroom::WarNode newNode = warroom::WarNode::makeLeaf("新节点",
		scenePos.x(), scenePos.y());
	newNode.full_text = "双击编辑长文本...";

	auto cmd = std::make_unique<warroom::AddNodeCommand>(
		std::move(newNode), parentId, -1);
	executeCommand(std::move(cmd));
}

// 创建一个绑定了指定主模组的节点（多模态节点入口）
static void seedNodeForMod(warroom::WarNode& node, const std::string& modId)
{
	node.primary_mod_type = modId;
	node.full_text.clear();
	if (modId == "builtin.image") {
		node.title = "图片";
		node.width = 200;
		node.height = 150;
	}
	else if (modId == "builtin.video") {
		node.title = "视频";
		node.width = 240;
		node.height = 180;
	}
	else if (modId == "builtin.web") {
		node.title = "网页";
		node.width = 240;
		node.height = 160;
	}
}

void WarRoomMainWindow::addNodeWithMod(QPointF scenePos,
	const warroom::Uuid& parentId, const std::string& modId)
{
	warroom::WarNode newNode = warroom::WarNode::makeLeaf("",
		scenePos.x(), scenePos.y());
	seedNodeForMod(newNode, modId);

	auto cmd = std::make_unique<warroom::AddNodeCommand>(
		std::move(newNode), parentId, -1);
	executeCommand(std::move(cmd));
}

void WarRoomMainWindow::editNode(const std::string& nodeId)
{
	warroom::WarNode* node = m_model.getNodeMutable(nodeId);
	if (!node) return;

	QString newTitle = QInputDialog::getText(this, "编辑标题", "标题:",
		QLineEdit::Normal,
		QString::fromStdString(node->title));
	QString newFullText = QInputDialog::getMultiLineText(this, "编辑内容", "长文本:",
		QString::fromStdString(node->full_text));

	if (!newTitle.isNull()) {
		auto cmd = std::make_unique<warroom::EditNodeCommand>(
			nodeId,
			node->title, newTitle.toStdString(),
			node->full_text, newFullText.toStdString());
		executeCommand(std::move(cmd));
	}
}

// ============================================================================
// 私有方法 - 右键菜单
// ============================================================================

void WarRoomMainWindow::contextMenuEvent(QContextMenuEvent* event)
{
	QPoint viewPos = m_view->mapFromGlobal(event->globalPos());
	QPointF scenePos = m_view->mapToScene(viewPos);

	QGraphicsItem* item = m_scene->itemAt(scenePos, QTransform());
	QMenu menu(this);

	// ---- 应用深色扁平样式 ----
	menu.setStyleSheet(R"(
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
        QMenu::separator {
            height: 1px;
            background-color: #3A3A3A;
            margin: 4px 8px;
        }
    )");



	auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item);
	if (nodeItem) {
		menu.addAction("删除节点", this, &WarRoomMainWindow::deleteSelectedNode);
		menu.addAction("编辑节点", this, [this, nodeItem]() {
			if (nodeItem) editNode(nodeItem->nodeId());
			});

		// ---- 颜色子菜单 ----
		QMenu* colorMenu = menu.addMenu("修改颜色");

		struct ColorPreset {
			const char* name;
			const char* hexColor;
		};

		std::vector<ColorPreset> presets = {
			{"默认", "#FF888888"},
			{"红色", "#FFE74C3C"},
			{"绿色", "#FF2ECC71"},
			{"蓝色", "#FF3498DB"},
			{"黄色", "#FFF1C40F"},
			{"紫色", "#FF9B59B6"},
			{"橙色", "#FFE67E22"},
			{"青色", "#FF1ABC9C"}
		};

		for (const auto& preset : presets) {
			colorMenu->addAction(preset.name, [this, nodeItem, preset]() {
				if (!nodeItem) return;
				std::string nodeId = nodeItem->nodeId();
				warroom::WarNode* node = m_model.getNodeMutable(nodeId);
				if (node) {
					node->color = preset.hexColor;
					nodeItem->refresh();
				}
				});
		}


		// ---- 透明度子菜单 ----
		QMenu* alphaMenu = menu.addMenu("修改透明度");

		// 透明度预设值（百分比）
		struct AlphaPreset {
			const char* name;
			int percent;
		};

		std::vector<AlphaPreset> alphaPresets = {
			{"0%", 0},
			{"25%", 25},
			{"50%", 50},
			{"75%", 75},
			{"100%", 100}
		};

		for (const auto& preset : alphaPresets) {
			alphaMenu->addAction(preset.name, [this, nodeItem, preset]() {
				if (!nodeItem) return;
				std::string nodeId = nodeItem->nodeId();
				warroom::WarNode* node = m_model.getNodeMutable(nodeId);
				if (node) {
					// 获取当前颜色的 RGB 部分，保留原色相
					QString currentColor = QString::fromStdString(node->color);
					// 格式为 "#AARRGGBB"，提取 RGB
					if (currentColor.length() == 9 && currentColor.startsWith("#")) {
						// 保留 RGB，替换 Alpha 通道
						QString rgb = currentColor.mid(3); // 去掉 "#FF" 保留 "RRGGBB"
						int alphaValue = static_cast<int>(255.0f * preset.percent / 100.0f);
						QString newColor = QString("#%1%2").arg(alphaValue, 2, 16, QChar('0')).arg(rgb);
						node->color = newColor.toStdString();
						nodeItem->refresh();
					}
				}
				});
		}


		menu.addSeparator();
		warroom::Uuid parentId = nodeItem->nodeId();
		menu.addAction("添加子节点", [this, scenePos, parentId]() {
			addNodeAtPosition(scenePos, parentId);
			});

		// ---- 多模态：作为子节点插入媒体 ----
		QMenu* mediaSubMenu = menu.addMenu("插入媒体子节点");
		auto modInfos = warroom::ModManager::instance().getAllModInfo();
		if (modInfos.empty()) {
			mediaSubMenu->addAction("（暂无可用模组）")->setEnabled(false);
		}
		else {
			for (const auto& kv : modInfos) {
				QString label = QString::fromStdString(kv.second.name);
				if (label.isEmpty()) label = QString::fromStdString(kv.first);
				std::string modId = kv.first;
				mediaSubMenu->addAction(label,
					[this, scenePos, parentId, modId]() {
						addNodeWithMod(scenePos, parentId, modId);
					});
			}
		}

		// ---- 节点模组右键菜单 ----
		// 获取节点对应的模组，并调用其 onContextMenu 钩子
		std::string nodeIdStr = nodeItem->nodeId();
		warroom::WarNode* targetNode = m_model.getNodeMutable(nodeIdStr);
		if (targetNode) {
			// 创建刷新回调：用于模组修改数据后请求刷新节点显示
			auto refreshCallback = [this](const std::string& nodeId) {
				NodeGraphicsItem* item = m_nodeItems.value(QString::fromStdString(nodeId));
				if (item) {
					item->refresh();
				}
			};

			// 主模组的右键菜单
			if (!targetNode->primary_mod_type.empty()) {
				warroom::NodeMod* primaryMod = warroom::ModManager::instance().getMod(targetNode->primary_mod_type);
				void* primaryData = warroom::ModManager::instance().getNodePrivate(targetNode, targetNode->primary_mod_type);
				if (primaryMod) {
					warroom::ModMenuContext ctx;
					ctx.menu = &menu;
					ctx.parent = this;
					ctx.node = targetNode;
					ctx.modData = primaryData;
					ctx.nodeId = nodeIdStr;
					ctx.requestNodeRefresh = refreshCallback;
					bool addedSeparator = primaryMod->onContextMenu(ctx, primaryData);
					if (addedSeparator) {
						menu.addSeparator();
					}
				}
			}

			// 辅助模组的右键菜单
			for (const auto& auxModType : targetNode->auxiliary_mod_types) {
				warroom::NodeMod* auxMod = warroom::ModManager::instance().getMod(auxModType);
				void* auxData = warroom::ModManager::instance().getNodePrivate(targetNode, auxModType);
				if (auxMod) {
					warroom::ModMenuContext ctx;
					ctx.menu = &menu;
					ctx.parent = this;
					ctx.node = targetNode;
					ctx.modData = auxData;
					ctx.nodeId = nodeIdStr;
					ctx.requestNodeRefresh = refreshCallback;
					bool addedSeparator = auxMod->onContextMenu(ctx, auxData);
					if (addedSeparator) {
						menu.addSeparator();
					}
				}
			}
		}
	}
	else {
		menu.addAction("添加节点", this, [this, scenePos]() {
			addNodeAtPosition(scenePos);
			});

		// ---- 多模态：在空白处插入媒体 ----
		QMenu* mediaMenu = menu.addMenu("插入媒体节点");
		auto modInfos = warroom::ModManager::instance().getAllModInfo();
		if (modInfos.empty()) {
			mediaMenu->addAction("（暂无可用模组）")->setEnabled(false);
		}
		else {
			warroom::Uuid rootId = m_model.getDocumentRootId();
			for (const auto& kv : modInfos) {
				QString label = QString::fromStdString(kv.second.name);
				if (label.isEmpty()) label = QString::fromStdString(kv.first);
				std::string modId = kv.first;
				mediaMenu->addAction(label,
					[this, scenePos, rootId, modId]() {
						addNodeWithMod(scenePos, rootId, modId);
					});
			}
		}
	}

	menu.exec(event->globalPos());
}

// ============================================================================
// 保护方法 - 事件过滤器
// ============================================================================

bool WarRoomMainWindow::eventFilter(QObject* watched, QEvent* event)
{
	// 保持左下角设置按钮始终位于左下角
	if (m_centralContainer && m_settingsCorner && watched == m_centralContainer) {
		if (event->type() == QEvent::Resize) {
			QSize containerSize = m_centralContainer->size();
			int cornerW = 100;
			int cornerH = 90;
			m_settingsCorner->setGeometry(
				0, containerSize.height() - cornerH, cornerW, cornerH);
			m_settingsCorner->raise();
		}
	}

	if (watched == m_scene && event->type() == QEvent::GraphicsSceneMousePress) {
		QGraphicsSceneMouseEvent* mouseEvent =
			static_cast<QGraphicsSceneMouseEvent*>(event);

		std::string clickedNodeId = findTopmostNodeAtPoint(mouseEvent->scenePos(), "");

		if (!m_currentEditingNodeId.empty()) {
			if (clickedNodeId != m_currentEditingNodeId) {
				auto* editingItem = m_nodeItems.value(
					QString::fromStdString(m_currentEditingNodeId));
				if (editingItem) {
					editingItem->saveAndExitEditMode();
				}
				m_currentEditingNodeId.clear();
			}
		}
	}

	return QMainWindow::eventFilter(watched, event);
}

// ============================================================
// setupCustomUi - 构建自绘主窗口布局
// ============================================================
void WarRoomMainWindow::setupCustomUi()
{
	// ---- 中央容器 ----
	m_centralContainer = new QWidget(this);
	m_centralContainer->setObjectName("centralContainer");
	setCentralWidget(m_centralContainer);

	// 主垂直布局：标题栏 + 工具栏 + 内容区
	auto* mainLayout = new QVBoxLayout(m_centralContainer);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	// ---- 自绘标题栏 ----
	m_titleBar = new CustomTitleBar(m_centralContainer);
	m_titleBar->setTitle("WarRoom");
	mainLayout->addWidget(m_titleBar);

	// ---- 工具栏容器（使用 QToolBar 或自定义容器）----
	QWidget* toolBarContainer = new QWidget(m_centralContainer);
	toolBarContainer->setFixedHeight(36);
	toolBarContainer->setStyleSheet("background-color: #2A2A2A;");
	auto* toolBarLayout = new QHBoxLayout(toolBarContainer);
	toolBarLayout->setContentsMargins(8, 4, 8, 4);
	toolBarLayout->setSpacing(8);

	// 创建按钮
	QPushButton* newBtn = new QPushButton("新建", toolBarContainer);
	QPushButton* openBtn = new QPushButton("打开", toolBarContainer);
	QPushButton* saveBtn = new QPushButton("保存", toolBarContainer);
	QPushButton* undoBtn = new QPushButton("撤销", toolBarContainer);
	QPushButton* redoBtn = new QPushButton("重做", toolBarContainer);

	// 按钮样式
	QString btnStyle = R"(
		QPushButton {
			background: #3A3A3A;
			border: none;
			padding: 4px 12px;
			border-radius: 3px;
			color: #CCCCCC;
			font-size: 12px;
		}
		QPushButton:hover {
			background: #4A4A4A;
		}
		QPushButton:pressed {
			background: #2A2A2A;
		}
	)";

	newBtn->setStyleSheet(btnStyle);
	openBtn->setStyleSheet(btnStyle);
	saveBtn->setStyleSheet(btnStyle);
	undoBtn->setStyleSheet(btnStyle);
	redoBtn->setStyleSheet(btnStyle);

	connect(newBtn, &QPushButton::clicked, this, &WarRoomMainWindow::onNewAction);
	connect(openBtn, &QPushButton::clicked, this, &WarRoomMainWindow::onLoadAction);
	connect(saveBtn, &QPushButton::clicked, this, &WarRoomMainWindow::onSaveAction);
	connect(undoBtn, &QPushButton::clicked, this, &WarRoomMainWindow::onUndo);
	connect(redoBtn, &QPushButton::clicked, this, &WarRoomMainWindow::onRedo);

	toolBarLayout->addWidget(newBtn);
	toolBarLayout->addWidget(openBtn);
	toolBarLayout->addWidget(saveBtn);
	toolBarLayout->addWidget(undoBtn);
	toolBarLayout->addWidget(redoBtn);
	toolBarLayout->addStretch();

	// 添加一个简单的视图控制
	QPushButton* resetViewBtn = new QPushButton("重置视图", toolBarContainer);
	resetViewBtn->setStyleSheet(btnStyle);
	connect(resetViewBtn, &QPushButton::clicked, this, &WarRoomMainWindow::onResetView);
	toolBarLayout->addWidget(resetViewBtn);

	mainLayout->addWidget(toolBarContainer);

	// ---- 内容区：侧边栏 + 画布 ----
	auto* contentLayout = new QHBoxLayout();
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);

	// 左侧边栏
	m_sidebar = new CustomSidebar(m_centralContainer);
	m_sidebar->setVisible(true);
	contentLayout->addWidget(m_sidebar);

	// 画布区域
	m_canvasArea = new QWidget(m_centralContainer);
	m_canvasArea->setObjectName("canvasArea");
	auto* canvasLayout = new QVBoxLayout(m_canvasArea);
	canvasLayout->setContentsMargins(0, 0, 0, 0);
	canvasLayout->setSpacing(0);

	contentLayout->addWidget(m_canvasArea, 1);

	mainLayout->addLayout(contentLayout, 1);

	// ---- 全局背景样式 ----
	m_centralContainer->setStyleSheet(R"(
		#centralContainer {
			background-color: #1E1E1E;
		}
		#canvasArea {
			background-color: #252525;
			border: none;
		}
	)");

	// ---- 左下角悬浮容器：侧边栏折叠按钮（上） + 设置按钮（下） ----
	m_settingsCorner = new QWidget(m_centralContainer);
	m_settingsCorner->setAttribute(Qt::WA_TransparentForMouseEvents, false);
	auto* cornerLayout = new QVBoxLayout(m_settingsCorner);
	cornerLayout->setContentsMargins(10, 5, 0, 5);
	cornerLayout->setSpacing(5);
	cornerLayout->setAlignment(Qt::AlignLeft | Qt::AlignBottom);

	// 侧边栏折叠按钮（上）
	m_sidebarCollapseBtn = new QPushButton(QChar(0x2630), m_settingsCorner);
	m_sidebarCollapseBtn->setFixedSize(32, 32);
	m_sidebarCollapseBtn->setToolTip("折叠/展开侧边栏");
	m_sidebarCollapseBtn->setStyleSheet(R"(
		QPushButton {
			background-color: rgba(50, 50, 50, 200);
			color: #CCCCCC;
			border: 1px solid #3A3A3A;
			border-radius: 16px;
			font-size: 14px;
		}
		QPushButton:hover {
			background-color: rgba(70, 70, 70, 220);
			color: #FFFFFF;
		}
		QPushButton:pressed {
			background-color: rgba(40, 40, 40, 220);
		}
	)");
	cornerLayout->addWidget(m_sidebarCollapseBtn, 0, Qt::AlignLeft);
	connect(m_sidebarCollapseBtn, &QPushButton::clicked,
		this, &WarRoomMainWindow::onToggleSidebarCollapse);

	// 设置按钮（下）
	m_settingsButton = new QPushButton(QChar(0x2699), m_settingsCorner);
	m_settingsButton->setFixedSize(32, 32);
	m_settingsButton->setToolTip("设置");
	m_settingsButton->setStyleSheet(R"(
		QPushButton {
			background-color: rgba(50, 50, 50, 200);
			color: #CCCCCC;
			border: 1px solid #3A3A3A;
			border-radius: 16px;
			font-size: 16px;
		}
		QPushButton:hover {
			background-color: rgba(70, 70, 70, 220);
			color: #FFFFFF;
		}
		QPushButton:pressed {
			background-color: rgba(40, 40, 40, 220);
		}
	)");
	cornerLayout->addWidget(m_settingsButton, 0, Qt::AlignLeft);
	connect(m_settingsButton, &QPushButton::clicked,
		this, &WarRoomMainWindow::onSettingsClicked);

	// 将按钮容器固定在左下角
	m_settingsCorner->setGeometry(0, 0, 100, 90);
	m_settingsCorner->raise();

	// 监听中央容器的大小变化，保持按钮在左下角
	m_centralContainer->installEventFilter(this);

	// 不调用 setupMenuBar()，避免布局冲突
	// 如果需要菜单功能，可以通过右键菜单或工具栏按钮实现
}

// ============================================================
// setupTitleBar - 标题栏信号连接
// ============================================================
void WarRoomMainWindow::setupTitleBar()
{
	connect(m_titleBar, &CustomTitleBar::minimizeClicked,
		this, &WarRoomMainWindow::onTitleBarMinimize);
	connect(m_titleBar, &CustomTitleBar::maximizeClicked,
		this, &WarRoomMainWindow::onTitleBarMaximize);
	connect(m_titleBar, &CustomTitleBar::closeClicked,
		this, &WarRoomMainWindow::onTitleBarClose);
	connect(m_titleBar, &CustomTitleBar::titleBarDoubleClicked,
		this, &WarRoomMainWindow::onTitleBarMaximize);

	// 窗口拖拽
	connect(m_titleBar, &CustomTitleBar::startWindowDrag,
		this, [this](const QPoint& globalPos) {
			m_windowDragging = true;
			m_dragGlobalStart = globalPos;
		});
	connect(m_titleBar, &CustomTitleBar::windowDrag,
		this, [this](const QPoint& globalPos) {
			if (m_windowDragging && !isMaximized()) {
				QPoint delta = globalPos - m_dragGlobalStart;
				move(pos() + delta);
				m_dragGlobalStart = globalPos;
			}
		});
}

// ============================================================
// setupSidebar - 侧边栏信号连接
// ============================================================
void WarRoomMainWindow::setupSidebar()
{
	connect(m_sidebar, &CustomSidebar::nodeFocused,
		this, &WarRoomMainWindow::onSidebarNodeFocused);
	connect(m_sidebar, &CustomSidebar::nodeDoubleClicked,
		this, &WarRoomMainWindow::onSidebarNodeDoubleClicked);
}

// ============================================================
// buildSidebarData - 从模型构建侧边栏树数据
// ============================================================
void WarRoomMainWindow::buildSidebarData(std::vector<TreeNodeData>& outNodes) const
{
	// 递归遍历节点树
	std::function<void(warroom::Uuid, int)> traverse;
	traverse = [&](warroom::Uuid parentId, int depth) {
		auto children = m_model.getChildren(parentId);
		for (const auto& childId : children) {
			const warroom::WarNode* node = m_model.getNode(childId);
			if (!node) continue;

			TreeNodeData data;
			data.id = childId;
			data.parentId = parentId;
			data.text = node->title.empty() ? "未命名" : node->title;
			data.depth = depth;
			data.hasChildren = !m_model.getChildren(childId).empty();
			data.selected = (childId == m_currentEditingNodeId);

			outNodes.push_back(data);
			traverse(childId, depth + 1);
		}
		};

	traverse(m_model.getDocumentRootId(), 0);
}

// ============================================================
// refreshSidebarTree - 刷新侧边栏
// ============================================================
void WarRoomMainWindow::refreshSidebarTree()
{
	std::vector<TreeNodeData> nodes;
	buildSidebarData(nodes);
	m_sidebar->setTreeData(nodes);
}

void WarRoomMainWindow::showEvent(QShowEvent* event)
{
	QMainWindow::showEvent(event);
	static bool done = false;
	if (!done) {
		done = true;
		WindowHelper::setupFramelessWindow(this);
	}
}


// ============================================================
// 侧边栏回调
// ============================================================
void WarRoomMainWindow::onSidebarNodeFocused(const std::string& nodeId)
{
	// 只居中视图使节点可见，不修改画布节点的选中状态
	auto it = m_nodeItems.find(QString::fromStdString(nodeId));
	if (it != m_nodeItems.end()) {
		auto* item = it.value();
		m_view->centerOn(item);
	}
	m_sidebar->selectNode(nodeId);
}

void WarRoomMainWindow::onToggleSidebarCollapse()
{
	if (!m_sidebar) return;

	// 切换可见性
	bool currentlyVisible = m_sidebar->isVisible();
	m_sidebar->setVisible(!currentlyVisible);

	// 切换按钮的图标和提示文字（折叠状态显示右箭头，展开状态显示三线）
	if (currentlyVisible) {
		m_sidebarCollapseBtn->setText(QChar(0x25B6));  // 右箭头
		m_sidebarCollapseBtn->setToolTip("展开侧边栏");
	}
	else {
		m_sidebarCollapseBtn->setText(QChar(0x2630));  // 三线
		m_sidebarCollapseBtn->setToolTip("折叠侧边栏");
	}
}

void WarRoomMainWindow::onSidebarNodeDoubleClicked(const std::string& nodeId)
{
	// 双击聚焦：居中并适当放大，但不修改画布节点选中状态
	auto it = m_nodeItems.find(QString::fromStdString(nodeId));
	if (it != m_nodeItems.end()) {
		auto* item = it.value();
		m_view->centerOn(item);

		float currentZoom = m_view->getZoomLevel();
		if (currentZoom < 0.5f) {
			warroom::Point2D center = m_view->getViewCenter();
			m_view->setViewCenter(center, 0.6f);
		}

		m_sidebar->selectNode(nodeId);
	}
}

// ============================================================
// 标题栏回调
// ============================================================
void WarRoomMainWindow::onTitleBarMinimize()
{
	showMinimized();
}

void WarRoomMainWindow::onTitleBarMaximize()
{
	if (isMaximized()) {
		showNormal();
		m_titleBar->setMaximized(false);
	}
	else {
		showMaximized();
		m_titleBar->setMaximized(true);
	}
}

void WarRoomMainWindow::onTitleBarClose()
{
	if (maybeSave()) {
		close();
	}
}

// ============================================================
// 无边框窗口：鼠标事件（边缘拖拽缩放）
// ============================================================
void WarRoomMainWindow::mousePressEvent(QMouseEvent* event)
{
	// 标题栏的拖拽由 CustomTitleBar 内部处理
	// 这里处理窗口边缘缩放（由 nativeEvent 配合）
	QMainWindow::mousePressEvent(event);
}

void WarRoomMainWindow::mouseMoveEvent(QMouseEvent* event)
{
	QMainWindow::mouseMoveEvent(event);
}

void WarRoomMainWindow::mouseReleaseEvent(QMouseEvent* event)
{
	m_windowDragging = false;
	QMainWindow::mouseReleaseEvent(event);
}

void WarRoomMainWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
	// 双击标题栏切换最大化（由 CustomTitleBar 的信号处理）
	QMainWindow::mouseDoubleClickEvent(event);
}

// ============================================================
// 无边框窗口：平台原生事件（边框缩放）
// ============================================================
bool WarRoomMainWindow::nativeEvent(const QByteArray& eventType,
	void* message, qintptr* result)
{
	// 窗口有效性检查
	if (!isVisible() || !winId()) {
		return false;
	}

	return WindowHelper::handleNativeWindowEvent(
		this, eventType, message, result, 6);
}

// ============================================================
// 配置管理：配置目录与文件路径
// ============================================================
QString WarRoomMainWindow::getConfigDir()
{
	// 使用可执行文件所在目录 + config 子目录（与 WarRoom 同级）
	// 便于用户直接访问
	QString exeDir = QCoreApplication::applicationDirPath();
	QString cfgDir = exeDir + QLatin1String("/config");

	QDir dir(cfgDir);
	if (!dir.exists()) {
		dir.mkpath(cfgDir);
	}
	return cfgDir;
}

QString WarRoomMainWindow::getConfigFilePath()
{
	return getConfigDir() + QLatin1String("/settings.ini");
}

// ============================================================
// 配置管理：读写上次打开的文件路径
// ============================================================
QString WarRoomMainWindow::readLastOpenFilePath()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) return QString();

	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("General");
	QString path = settings.value("lastOpenFile", QString()).toString();
	settings.endGroup();
	return path;
}

void WarRoomMainWindow::writeLastOpenFilePath(const QString& path)
{
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("General");
	settings.setValue("lastOpenFile", path);
	settings.setValue("lastOpenTime",
		QDateTime::currentDateTime().toString(Qt::ISODate));
	settings.endGroup();
	settings.sync();
}

// ============================================================
// 从指定路径加载 .warroom 文件（共享给启动逻辑与按钮逻辑）
// ============================================================
void WarRoomMainWindow::loadFromFilePath(const QString& path)
{
	if (path.isEmpty()) return;

	QString docDir = QFileInfo(path).absolutePath();
	warroom::ImageMod::setCurrentDocumentDir(docDir);
	warroom::VideoMod::setCurrentDocumentDir(docDir);

	warroom::WarRoomModel newModel;
	if (newModel.loadFromFile(path.toStdString())) {
		m_model = std::move(newModel);
		m_currentFilePath = path;
		writeLastOpenFilePath(path);

		m_scene->clear();
		rebuildFromModel();

		if (m_view) {
			warroom::Point2D viewPos;
			float zoom;
			m_model.getCameraView(viewPos, zoom);
			m_view->restoreViewState(viewPos, zoom);
		}
	}
}

// ============================================================
// 设置按钮：简单设置对话框
// ============================================================
void WarRoomMainWindow::onSettingsClicked()
{
	QDialog* dialog = new QDialog(this);
	dialog->setWindowTitle("设置");
	dialog->setMinimumWidth(420);
	dialog->setStyleSheet(R"(
		QDialog {
			background-color: #252525;
		}
		QLabel {
			color: #CCCCCC;
			font-size: 12px;
		}
		QLineEdit {
			background-color: #1E1E1E;
			color: #FFFFFF;
			border: 1px solid #3A3A3A;
			border-radius: 3px;
			padding: 4px 6px;
			selection-background-color: #3A6A9A;
		}
		QPushButton {
			background-color: #3A3A3A;
			color: #CCCCCC;
			border: none;
			padding: 6px 16px;
			border-radius: 3px;
			minimum-width: 80px;
		}
		QPushButton:hover {
			background-color: #4A4A4A;
			color: #FFFFFF;
		}
		QPushButton:pressed {
			background-color: #2A2A2A;
		}
		QTabWidget {
			background-color: #2A2A2A;
			border: none;
		}
		QTabWidget::pane {
			border: 1px solid #3A3A3A;
			background-color: #252525;
		}
		QTabBar::tab {
			background-color: #2A2A2A;
			color: #AAAAAA;
			padding: 8px 16px;
			border: none;
		}
		QTabBar::tab:selected {
			background-color: #3A3A3A;
			color: #FFFFFF;
		}
		QTabBar::tab:hover {
			background-color: #4A4A4A;
		}
		QScrollArea {
			background-color: #252525;
			border: none;
		}
	)");

	auto* tabWidget = new QTabWidget(dialog);

	// ========== 通用设置页面 ==========
	QWidget* generalPage = new QWidget();
	auto* generalLayout = new QVBoxLayout(generalPage);
	generalLayout->setContentsMargins(16, 16, 16, 12);
	generalLayout->setSpacing(10);

	// ---- 字体设置 ----
	QLabel* fontTitle = new QLabel("字体设置", generalPage);
	fontTitle->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold;");
	generalLayout->addWidget(fontTitle);

	// 字体预览行
	QWidget* fontRow = new QWidget(generalPage);
	auto* fontRowLayout = new QHBoxLayout(fontRow);
	fontRowLayout->setContentsMargins(0, 0, 0, 0);
	fontRowLayout->setSpacing(6);

	QLineEdit* fontPreview = new QLineEdit(generalPage);
	fontPreview->setReadOnly(true);
	fontPreview->setFixedHeight(28);
	fontPreview->setText(QString("%1, %2pt").arg(getNodeFont().family()).arg(getNodeFont().pointSize()));
	fontRowLayout->addWidget(fontPreview, 1);

	// 点击"选择字体" -> 弹对话框 -> 确认后直接应用并保存到配置
	QPushButton* fontBtn = new QPushButton("选择字体", generalPage);
	fontBtn->setFixedHeight(28);
	fontBtn->setStyleSheet(R"(
		QPushButton {
			background-color: #2A5A8A;
			color: #CCCCCC;
			border: none;
			padding: 4px 14px;
			border-radius: 3px;
		}
		QPushButton:hover {
			background-color: #3A6A9A;
			color: #FFFFFF;
		}
		QPushButton:pressed {
			background-color: #1A4A7A;
		}
	)");
	fontRowLayout->addWidget(fontBtn);

	generalLayout->addWidget(fontRow);

	// 点击"选择字体" -> 弹对话框 -> 确认后直接应用并保存到配置
	QObject::connect(fontBtn, &QPushButton::clicked, [fontPreview, dialog, this]() {
		bool ok = false;
		QFont current = getNodeFont();
		QFont selected = QFontDialog::getFont(&ok, current, dialog, "选择节点字体");
		if (ok) {
			saveNodeFont(selected);
			fontPreview->setText(QString("%1, %2pt")
				.arg(selected.family()).arg(selected.pointSize()));
		}
	});

	generalLayout->addSpacing(6);

	// ---- 背景颜色设置 ----
	QLabel* bgTitle = new QLabel("背景颜色", generalPage);
	bgTitle->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold;");
	generalLayout->addWidget(bgTitle);

	// 预设背景颜色
	QWidget* bgColorRow = new QWidget(generalPage);
	auto* bgColorLayout = new QHBoxLayout(bgColorRow);
	bgColorLayout->setContentsMargins(0, 0, 0, 0);
	bgColorLayout->setSpacing(8);

	// 预设颜色列表（深色 + 浅色）
	struct BgColorPreset {
		const char* name;
		QColor color;
	};
	std::vector<BgColorPreset> bgPresets = {
		{"深灰", QColor(30, 30, 30)},
		{"炭灰", QColor(45, 45, 48)},
		{"墨蓝", QColor(25, 32, 40)},
		{"暗紫", QColor(35, 30, 40)},
		{"米白", QColor(240, 238, 230)},
		{"浅灰", QColor(220, 220, 220)},
		{"淡蓝", QColor(230, 240, 250)},
		{"奶黄", QColor(250, 245, 230)}
	};

	// 颜色选择按钮
	std::vector<QPushButton*> colorBtns;
	for (size_t i = 0; i < bgPresets.size(); ++i) {
		QPushButton* colorBtn = new QPushButton(bgPresets[i].name, bgColorRow);
		colorBtn->setFixedSize(50, 28);
		colorBtn->setCheckable(true);

		// 设置按钮背景色
		QString btnStyle = QString(
			"QPushButton {"
			"	background-color: %1;"
			"	color: %2;"
			"	border: 2px solid %3;"
			"	border-radius: 4px;"
			"	font-size: 10px;"
			"}"
			"QPushButton:checked {"
			"	border-color: #FFFFFF;"
			"	border-width: 3px;"
			"}"
			"QPushButton:hover {"
			"	border-color: #888888;"
			"}"
		).arg(bgPresets[i].color.name())
		.arg(bgPresets[i].color.lightness() > 128 ? "#333333" : "#CCCCCC")
		.arg(bgPresets[i].color.lightness() > 128 ? "#666666" : "#555555");

		colorBtn->setStyleSheet(btnStyle);

		// 检查是否是当前颜色
		if (bgPresets[i].color == s_canvasBackgroundColor) {
			colorBtn->setChecked(true);
		}

		bgColorLayout->addWidget(colorBtn);
		colorBtns.push_back(colorBtn);

		// 连接点击事件
		QObject::connect(colorBtn, &QPushButton::clicked, [this, i, &bgPresets, colorBtn, &colorBtns]() {
			QColor selectedColor = bgPresets[i].color;
			saveCanvasBackgroundColor(selectedColor);
			if (m_view) {
				m_view->setBackgroundColor(selectedColor);
			}
			// 更新按钮选中状态
			for (QPushButton* btn : colorBtns) {
				btn->setChecked(btn == colorBtn);
			}
		});
	}

	generalLayout->addWidget(bgColorRow);
	generalLayout->addSpacing(6);

	// ---- 最近打开的文件 ----
	QLabel* lastFileLabel = new QLabel("最近打开的文件：", generalPage);
	generalLayout->addWidget(lastFileLabel);

	QLineEdit* lastFileEdit = new QLineEdit(generalPage);
	lastFileEdit->setReadOnly(true);
	lastFileEdit->setText(readLastOpenFilePath());
	generalLayout->addWidget(lastFileEdit);

	// ---- 配置目录 ----
	QLabel* configDirLabel = new QLabel("配置文件目录：", generalPage);
	generalLayout->addWidget(configDirLabel);

	QLineEdit* configDirEdit = new QLineEdit(generalPage);
	configDirEdit->setReadOnly(true);
	configDirEdit->setText(getConfigDir());
	generalLayout->addWidget(configDirEdit);

	generalLayout->addSpacing(6);
	generalLayout->addStretch();

	// ========== 模组设置页面 ==========
	QWidget* modPage = new QWidget();
	auto* modLayout = new QVBoxLayout(modPage);
	modLayout->setContentsMargins(16, 16, 16, 12);
	modLayout->setSpacing(10);

	QLabel* modTitle = new QLabel("模组设置", modPage);
	modTitle->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold;");
	modLayout->addWidget(modTitle);

	// 模组列表区域
	QScrollArea* modScrollArea = new QScrollArea(modPage);
	modScrollArea->setWidgetResizable(true);
	modScrollArea->setMinimumHeight(200);

	QWidget* modListWidget = new QWidget();
	auto* modListLayout = new QVBoxLayout(modListWidget);
	modListLayout->setContentsMargins(0, 0, 0, 0);
	modListLayout->setSpacing(8);

	// 获取已注册的模组信息
	auto modInfos = warroom::ModManager::instance().getAllModInfo();
	if (modInfos.empty()) {
		QLabel* noModsLabel = new QLabel("（暂无已加载的模组）", modListWidget);
		noModsLabel->setStyleSheet("color: #888888; font-style: italic;");
		noModsLabel->setAlignment(Qt::AlignCenter);
		modListLayout->addWidget(noModsLabel);
	}
	else {
		for (const auto& kv : modInfos) {
			QWidget* modItem = new QWidget(modListWidget);
			auto* modItemLayout = new QHBoxLayout(modItem);
			modItemLayout->setContentsMargins(8, 6, 8, 6);
			modItemLayout->setSpacing(10);

			modItem->setStyleSheet(R"(
				QWidget {
					background-color: #2A2A2A;
					border: 1px solid #3A3A3A;
					border-radius: 4px;
				}
			)");

			// 模组名称
			QString modName = QString::fromStdString(kv.second.name);
			if (modName.isEmpty()) modName = QString::fromStdString(kv.first);
			QLabel* nameLabel = new QLabel(modName, modItem);
			nameLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");
			modItemLayout->addWidget(nameLabel, 1);

			// 模组ID
			QLabel* idLabel = new QLabel(QString("ID: %1").arg(QString::fromStdString(kv.first)), modItem);
			idLabel->setStyleSheet("color: #888888; font-size: 10px;");
			modItemLayout->addWidget(idLabel);

			// 占位按钮（后续功能）
			QPushButton* settingsBtn = new QPushButton("设置", modItem);
			settingsBtn->setFixedSize(50, 24);
			settingsBtn->setEnabled(false);  // 暂时禁用
			settingsBtn->setStyleSheet(R"(
				QPushButton {
					background-color: #3A3A3A;
					color: #888888;
					border: none;
					padding: 2px 8px;
					border-radius: 3px;
				}
			)");
			modItemLayout->addWidget(settingsBtn);

			// 卸载按钮（暂时禁用）
			QPushButton* unloadBtn = new QPushButton("卸载", modItem);
			unloadBtn->setFixedSize(50, 24);
			unloadBtn->setEnabled(false);  // 暂时禁用
			unloadBtn->setStyleSheet(R"(
				QPushButton {
					background-color: #5A3030;
					color: #888888;
					border: none;
					padding: 2px 8px;
					border-radius: 3px;
				}
			)");
			modItemLayout->addWidget(unloadBtn);

			modListLayout->addWidget(modItem);
		}
	}

	modListLayout->addStretch();
	modScrollArea->setWidget(modListWidget);
	modLayout->addWidget(modScrollArea);

	// 添加模组按钮（暂时禁用）
	QPushButton* addModBtn = new QPushButton("加载模组", modPage);
	addModBtn->setEnabled(false);  // 暂时禁用
	addModBtn->setStyleSheet(R"(
		QPushButton {
			background-color: #3A3A3A;
			color: #888888;
			border: none;
			padding: 8px 16px;
			border-radius: 3px;
		}
	)");
	modLayout->addWidget(addModBtn);

	// 说明文字
	QLabel* modNote = new QLabel("提示：模组功能开发中，后续版本将支持自定义模组加载。", modPage);
	modNote->setStyleSheet("color: #666666; font-size: 11px; font-style: italic;");
	modLayout->addWidget(modNote);

	// 添加页面到 TabWidget
	tabWidget->addTab(generalPage, "通用设置");
	tabWidget->addTab(modPage, "模组设置");

	// 主布局
	auto* mainLayout = new QVBoxLayout(dialog);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);
	mainLayout->addWidget(tabWidget);

	// 底部按钮
	auto* buttonRow = new QWidget(dialog);
	auto* buttonLayout = new QHBoxLayout(buttonRow);
	buttonLayout->setContentsMargins(16, 12, 16, 12);
	buttonLayout->setSpacing(8);

	QPushButton* openCfgBtn = new QPushButton("打开配置目录", dialog);
	buttonLayout->addWidget(openCfgBtn);

	buttonLayout->addStretch();

	QPushButton* okBtn = new QPushButton("确定", dialog);
	okBtn->setDefault(true);
	buttonLayout->addWidget(okBtn);

	mainLayout->addWidget(buttonRow);

	QObject::connect(openCfgBtn, &QPushButton::clicked, [dialog, this]() {
		QString dir = getConfigDir();
		QUrl url = QUrl::fromLocalFile(dir);
		QDesktopServices::openUrl(url);
	});
	QObject::connect(okBtn, &QPushButton::clicked,
		dialog, &QDialog::accept);

	dialog->exec();
	dialog->deleteLater();
}

// ============================================================
// 字体配置：加载 / 保存 / 获取
// ============================================================
void WarRoomMainWindow::loadNodeFont()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) {
		s_nodeFont = QFont("Microsoft YaHei", 10, QFont::Normal);
		return;
	}
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Font");
	QString family = settings.value("family", "Microsoft YaHei").toString();
	int pointSize = settings.value("pointSize", 10).toInt();
	int weight = settings.value("weight", QFont::Normal).toInt();
	bool bold = settings.value("bold", false).toBool();
	bool italic = settings.value("italic", false).toBool();
	settings.endGroup();

	s_nodeFont = QFont(family, pointSize, weight);
	s_nodeFont.setBold(bold);
	s_nodeFont.setItalic(italic);
}

void WarRoomMainWindow::saveNodeFont(const QFont& font)
{
	s_nodeFont = font;
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Font");
	settings.setValue("family", font.family());
	settings.setValue("pointSize", font.pointSize() > 0 ? font.pointSize() : 10);
	settings.setValue("weight", font.weight());
	settings.setValue("bold", font.bold());
	settings.setValue("italic", font.italic());
	settings.endGroup();
	settings.sync();

	// 立即刷新所有节点的字体显示
	refreshAllNodeFonts();
}
QFont WarRoomMainWindow::getNodeFont()
{
	return s_nodeFont;
}

// ============================================================
// 背景颜色配置：加载 / 保存 / 获取
// ============================================================
QColor WarRoomMainWindow::getCanvasBackgroundColor()
{
	return s_canvasBackgroundColor;
}

void WarRoomMainWindow::saveCanvasBackgroundColor(const QColor& color)
{
	s_canvasBackgroundColor = color;
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	settings.setValue("backgroundColor", color.name());
	settings.endGroup();
	settings.sync();
}

void WarRoomMainWindow::loadCanvasBackgroundColor()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) {
		s_canvasBackgroundColor = QColor(30, 30, 30);
		return;
	}
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	QString colorName = settings.value("backgroundColor", "#1E1E1E").toString();
	settings.endGroup();

	QColor loadedColor(colorName);
	if (loadedColor.isValid()) {
		s_canvasBackgroundColor = loadedColor;
	}
	else {
		s_canvasBackgroundColor = QColor(30, 30, 30);
	}
}

// ============================================================
// 刷新所有节点的字体（字体变更后调用）
// ============================================================
void WarRoomMainWindow::refreshAllNodeFonts()
{
	// 通知所有 NodeGraphicsItem 刷新字体
	for (QGraphicsItem* item : m_scene->items()) {
		if (auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item)) {
			nodeItem->refreshFont(s_nodeFont);
		}
	}

	// 刷新连线标签（重绘即可，连线项会在下一次 update 时使用新字体）
	for (QGraphicsItem* item : m_scene->items()) {
		if (auto* linkItem = dynamic_cast<LinkGraphicsItem*>(item)) {
			linkItem->update();
		}
	}
}