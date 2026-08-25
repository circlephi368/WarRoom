#include "WarRoomMainWindow.h"
#include "ColorPickerDialog.h"

// 标准库
#include <fstream>
#include <iostream>
#include <functional>
#include <algorithm>

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
#include <QToolButton>
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
#include <QKeySequenceEdit>
#include <QDialogButtonBox>
#include <QButtonGroup>

// 项目核心 - 命令
#include "core/command/add_link_command.h"
#include "core/command/add_node_command.h"
#include "core/command/delete_link_command.h"
#include "core/command/delete_node_command.h"
#include "core/command/edit_node_command.h"
#include "core/command/move_node_command.h"
#include "core/command/resize_node_command.h"
#include "core/command/set_node_color_command.h"
#include "core/command/set_link_label_command.h"

// 项目 UI
#include "ui/LinkCreationManager.h"
#include "ui/LinkGraphicsItem.h"
#include "ui/NodeGraphicsItem.h"
#include "ui/warroomview.h"
#include "ui/CameraAnimator.h"
#include "ui/HighlightOverlay.h"

// 节点模组
#include "mod/builtin/BuiltinMods.h"
#include "mod/builtin/ImageMod.h"
#include "mod/builtin/VideoMod.h"
#include "mod/KeyBinding.h"

// ============================================================================
// 静态成员定义
// ============================================================================
QFont WarRoomMainWindow::s_nodeFont{ "Microsoft YaHei", 10, QFont::Normal };
QColor WarRoomMainWindow::s_canvasBackgroundColor{ 30, 30, 30 };
int WarRoomMainWindow::s_canvasBackgroundStyle = 0; // 0=Dots, 1=Grid, 2=Image
QString WarRoomMainWindow::s_canvasBackgroundImagePath;
int WarRoomMainWindow::s_canvasBackgroundImageMode = 0; // 0=Tiled, 1=Stretch
QColor WarRoomMainWindow::s_nodeTextColor{ 240, 240, 240 }; // 默认浅色文本
QColor WarRoomMainWindow::s_linkColor{ 150, 150, 150 };     // 默认灰色连线
int WarRoomMainWindow::s_canvasPanMode = 0;       // 默认中键拖动
int WarRoomMainWindow::s_canvasKeyPanMode = 0;    // 默认方向键移动

// ============================================================================
// 构造与析构
// ============================================================================

WarRoomMainWindow::WarRoomMainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	// 注册内置节点模组（静态注册，幂等）
	warroom::registerBuiltinMods();

	// ---- 注册主程序键位并加载用户配置 ----
	registerAppKeyBindings();
	loadKeyBindings();

	// 设置无边框窗口
	setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
	setAttribute(Qt::WA_TranslucentBackground, false);  // 不透明背景
	//setAttribute(Qt::WA_DeleteOnClose);

	// 构建自定义 UI
	setupCustomUi();

	// 应用用户自定义键位（action 已创建）
	applyKeyBindings();

	// 初始化场景
	setupScene();

	// 将主程序 action 注册到视图 viewport，确保快捷键在视图获得焦点时也能触发
	// （Qt 的 shortcut 分发只检查焦点 widget 自身的 shortcut map）
	qDebug() << "[KEYDBG] constructor: registering actions to m_view. m_view =" << m_view;
	if (m_view) {
		int added = 0;
		if (m_newAction) { m_view->addAction(m_newAction); ++added; }
		if (m_openAction) { m_view->addAction(m_openAction); ++added; }
		if (m_saveAction) { m_view->addAction(m_saveAction); ++added; }
		if (m_saveAsAction) { m_view->addAction(m_saveAsAction); ++added; }
		if (m_exitAction) { m_view->addAction(m_exitAction); ++added; }
		if (m_undoAction) { m_view->addAction(m_undoAction); ++added; }
		if (m_redoAction) { m_view->addAction(m_redoAction); ++added; }
		if (m_deleteAction) { m_view->addAction(m_deleteAction); ++added; }
		qDebug() << "[KEYDBG] constructor: actually added" << added << "actions to m_view (expected 8 if all created)";
	}

	// 设置场景连接（你原有的）
	setupSceneConnections();

	// ---- 创建相机动画器 ----
	m_cameraAnimator = new CameraAnimator(m_view, this);

	// 将动画器注入视图，用于键盘平移和滚轮缩放的平滑动画
	m_view->setAnimator(m_cameraAnimator);

	// 用户拖动/滚轮时中止相机动画
	connect(m_view, &WarRoomView::userPanStarted,
		this, &WarRoomMainWindow::abortCameraAnimation);

	// ---- 创建高亮覆盖层 ----
	// 覆盖在 m_centralContainer 之上，鼠标事件穿透
	m_highlightOverlay = new HighlightOverlay(m_centralContainer);
	m_highlightOverlay->show();
	m_highlightOverlay->raise();

	// 将视图传递给 HighlightOverlay（必须在 m_view 创建后）
	if (m_view) {
		m_highlightOverlay->setView(m_view);
	}

	// 初始化焦点指示器
	m_highlightOverlay->setFocusState(FocusState::CanvasFocus);
	updateCanvasAreaForOverlay();

	// 用户拖动/滚轮时中止相机动画
	connect(m_view, &WarRoomView::userPanStarted,
		this, &WarRoomMainWindow::abortCameraAnimation);

	// 设置标题栏信号
	setupTitleBar();

	// 设置侧边栏信号
	setupSidebar();

	// 设置待办侧边栏信号
	setupTodoSidebar();

	// ---- 加载字体配置（在填充节点之前，确保新节点使用用户设置的字体）----
	loadNodeFont();

	// ---- 加载背景颜色配置并应用到视图 ----
	loadCanvasBackgroundColor();
	if (m_view) {
		m_view->setBackgroundColor(s_canvasBackgroundColor);
	}

	// ---- 加载背景样式（点阵/网格）并应用到视图 ----
	loadCanvasBackgroundStyle();
	if (m_view) {
		m_view->setBackgroundStyle(
			static_cast<WarRoomView::BackgroundStyle>(s_canvasBackgroundStyle));
	}

	// ---- 加载图片背景配置并应用到视图 ----
	loadCanvasBackgroundImage();
	if (m_view && !s_canvasBackgroundImagePath.isEmpty()) {
		m_view->setBackgroundImage(s_canvasBackgroundImagePath,
			static_cast<WarRoomView::ImageMode>(s_canvasBackgroundImageMode));
	}

	// ---- 加载节点文本颜色和连线颜色配置 ----
	loadNodeTextColor();
	loadLinkColor();

	// ---- 加载画布拖动 / 移动模式并应用到视图 ----
	loadCanvasPanMode();
	loadCanvasKeyPanMode();
	if (m_view) {
		m_view->setPanMode(static_cast<WarRoomView::PanMode>(s_canvasPanMode));
		m_view->setKeyPanMode(static_cast<WarRoomView::KeyPanMode>(s_canvasKeyPanMode));
	}

	// 从模型填充初始数据
	populateFromModel();

	// 初始化侧边栏
	refreshSidebarTree();
	refreshTodoSidebar();

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
	// [DESTDBG] 关键诊断：析构函数被调用时记录 this 地址、各关键成员地址
	// 如果该地址被打印两次，说明同一对象被析构两次（double-free）
	qDebug().nospace().noquote()
		<< "[DESTDBG] >>> ~WarRoomMainWindow ENTER this=" << static_cast<void*>(this)
		<< " | m_scene=" << static_cast<void*>(m_scene)
		<< " | m_view=" << static_cast<void*>(m_view)
		<< " | m_centralContainer=" << static_cast<void*>(m_centralContainer)
		<< " | m_titleBar=" << static_cast<void*>(m_titleBar)
		<< " | m_sidebar=" << static_cast<void*>(m_sidebar)
		<< " | m_canvasArea=" << static_cast<void*>(m_canvasArea)
		<< " | nodeItems=" << m_nodeItems.size();

	// 1. 断开所有信号连接，避免析构过程中触发槽函数访问半销毁的对象
	qDebug() << "[DESTDBG]   ~WarRoomMainWindow step1: disconnect()";
	disconnect();

	// 2. 先清理连接管理器（移除事件过滤器），防止后续访问场景
	qDebug() << "[DESTDBG]   ~WarRoomMainWindow step2: LinkCreationManager shutdown";
	LinkCreationManager::instance().setScene(nullptr);
	LinkCreationManager::instance().setMainWindow(nullptr);

	// 3. 直接删除场景（会自动 clear 所有图形项，无需手动 clear）
	qDebug() << "[DESTDBG]   ~WarRoomMainWindow step3: deleting m_scene =" << static_cast<void*>(m_scene);
	if (m_scene) {
		delete m_scene;
		m_scene = nullptr;
	}
	qDebug() << "[DESTDBG]   ~WarRoomMainWindow step3: m_scene deleted, now =" << static_cast<void*>(m_scene);

	// 4. 清空节点映射表（此时所有 NodeGraphicsItem 已由场景删除）
	qDebug() << "[DESTDBG]   ~WarRoomMainWindow step4: clearing m_nodeItems (size=" << m_nodeItems.size() << ")";
	m_nodeItems.clear();

	// 5. 清空编辑状态（图形项已销毁，无需再调用 setEditMode）
	qDebug() << "[DESTDBG]   ~WarRoomMainWindow step5: clearing edit state";
	m_currentEditingNodeId.clear();

	qDebug().nospace().noquote()
		<< "[DESTDBG] <<< ~WarRoomMainWindow EXIT this=" << static_cast<void*>(this)
		<< " (基类析构链随后运行，将删除 QObject 子对象)";
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
		auto* linkItem = new LinkGraphicsItem(newLinkId, &m_model);
		setupLinkItemConnections(linkItem);
		m_scene->addItem(linkItem);
	}
}

void WarRoomMainWindow::deleteLink(const warroom::Uuid& linkId)
{
	if (m_model.isReadOnly()) return;
	qDebug("deleteLink");
	const warroom::WarLink* link = m_model.getLink(linkId);
	if (!link) return;

	// 使用新的 DeleteLinkCommand：构造时只需 linkId，命令内部会自动捕获快照
	auto cmd = std::make_unique<warroom::DeleteLinkCommand>(linkId);
	executeCommand(std::move(cmd));
}

void WarRoomMainWindow::onLinkLabelEditRequested(const warroom::Uuid& linkId)
{
	if (m_model.isReadOnly()) return;

	// 找到对应的 LinkGraphicsItem
	for (auto* item : m_scene->items()) {
		if (auto* linkItem = dynamic_cast<LinkGraphicsItem*>(item)) {
			if (linkItem->linkId() == linkId) {
				QString newLabel = linkItem->pendingLabel();
				linkItem->clearPendingLabel();

				qDebug() << "[LINKDBG] onLinkLabelEditRequested:"
					<< "linkId=" << QString::fromStdString(linkId)
					<< "newLabel=" << newLabel;

				// 使用 SetLinkLabelCommand 支持 undo/redo
				auto cmd = std::make_unique<warroom::SetLinkLabelCommand>(
					linkId, newLabel.toStdString());
				executeCommand(std::move(cmd));

				//// 验证模型已更新
				//const warroom::WarLink* link = m_model.getLink(linkId);
				//if (link) {
				//	qDebug() << "[LINKDBG] link->label after command:"
				//		<< QString::fromStdString(link->label);
				//}

				// 刷新连线显示
				//linkItem->prepareGeometryChange();
				linkItem->update();
				break;
			}
		}
	}
}

void WarRoomMainWindow::onLinkColorChangeRequested(const warroom::Uuid& linkId, const QString& newColor)
{
	if (m_model.isReadOnly()) return;

	// 直接修改颜色（后续可改为 command 支持 undo/redo）
	warroom::WarLink* link = m_model.getLinkMutable(linkId);
	if (link) {
		link->color = newColor.toStdString();

		// 刷新所有连线（因为共享颜色缓存）
		for (auto* item : m_scene->items()) {
			if (auto* linkItem = dynamic_cast<LinkGraphicsItem*>(item)) {
				linkItem->update();
			}
		}
	}
}

void WarRoomMainWindow::setupLinkItemConnections(LinkGraphicsItem* linkItem)
{
	if (!linkItem) return;

	connect(linkItem, &LinkGraphicsItem::deletionRequested,
		this, &WarRoomMainWindow::deleteLink);
	connect(linkItem, &LinkGraphicsItem::labelEditRequested,
		this, &WarRoomMainWindow::onLinkLabelEditRequested);
	connect(linkItem, &LinkGraphicsItem::colorChangeRequested,
		this, &WarRoomMainWindow::onLinkColorChangeRequested);
}

void WarRoomMainWindow::createNodeAndLink(const std::string& fromId, int fromEdge, QPointF scenePos)
{
	if (m_model.isReadOnly()) return;
	using warroom::WarNode;
	using warroom::LinkType;

	// 1. 创建新节点
	WarNode newNode = WarNode::makeLeaf("新节点", scenePos.x(), scenePos.y());
	newNode.full_text = "";

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
		auto* linkItem = new LinkGraphicsItem(newLinkId, &m_model);
		setupLinkItemConnections(linkItem);
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
	qDebug() << "[KEYDBG-SLOT] onNewAction triggered";
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
	refreshSidebarTree();
	refreshTodoSidebar();
}

// 静态辅助：判断 .warroom 文件是否为新版格式（文件所在目录名 == 文件名去后缀）
static bool isNewFormatWarroom(const QString& warroomFilePath) {
	QFileInfo fi(warroomFilePath);
	if (!fi.isFile()) return false;
	if (fi.suffix().toLower() != "warroom") return false;
	QString fileBaseName = fi.completeBaseName();
	QString parentDirName = fi.dir().dirName();
	return fileBaseName == parentDirName;
}

// 静态辅助：从 .warroom 文件路径提取存档文件夹路径
static QString getArchiveFolderFromWarroom(const QString& warroomFilePath) {
	QFileInfo fi(warroomFilePath);
	return fi.absolutePath();
}

// 静态辅助：从存档文件夹路径得到内部 .warroom 文件路径
static QString getWarroomFromFolder(const QString& folderPath) {
	QFileInfo fi(folderPath);
	QString folderName = fi.fileName();
	return folderPath + "/" + folderName + ".warroom";
}

void WarRoomMainWindow::onSaveAction()
{
	qDebug() << "[KEYDBG-SLOT] onSaveAction triggered | m_currentFilePath =" << m_currentFilePath;
	if (m_view) {
		warroom::Point2D viewCenter = m_view->getViewCenter();
		m_model.setCameraView(viewCenter, m_view->getZoomLevel());
	}

	if (m_currentFilePath.isEmpty()) {
		qDebug() << "[KEYDBG-SLOT] onSaveAction -> redirect to onSaveAsAction";
		onSaveAsAction();
		return;
	}

	QString folderPath = isNewFormatWarroom(m_currentFilePath)
		? getArchiveFolderFromWarroom(m_currentFilePath)
		: m_currentFilePath;

	QString absDir = QFileInfo(folderPath).absoluteFilePath();
	warroom::ImageMod::setCurrentDocumentDir(absDir);
	warroom::VideoMod::setCurrentDocumentDir(absDir);

	if (m_model.saveToFolder(folderPath.toStdString())) {
		m_currentFilePath = getWarroomFromFolder(folderPath);
		writeLastOpenFilePath(m_currentFilePath);
		QMessageBox::information(this, "保存成功",
			QString("已保存到：\n%1").arg(m_currentFilePath));
	}
	else {
		QMessageBox::warning(this, "保存失败", "无法保存文件");
	}
}

void WarRoomMainWindow::onSaveAsAction()
{
	qDebug() << "[KEYDBG-SLOT] onSaveAsAction triggered";
	QString chosen = QFileDialog::getExistingDirectory(this,
		"选择保存位置", QString(),
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	if (chosen.isEmpty()) return;

	bool ok = false;
	QString name = QInputDialog::getText(this, "存档名称",
		"输入文件夹名称（不含后缀）：",
		QLineEdit::Normal, "Untitled", &ok);
	if (!ok || name.isEmpty()) return;

	// 去除可能的 .warroom 后缀
	if (name.endsWith(".warroom", Qt::CaseInsensitive)) {
		name = name.left(name.size() - 8);
	}
	QString folderPath = QFileInfo(chosen).absoluteFilePath() + "/" + name;

	if (m_view) {
		warroom::Point2D viewCenter = m_view->getViewCenter();
		m_model.setCameraView(viewCenter, m_view->getZoomLevel());
	}

	QString absDir = QFileInfo(folderPath).absoluteFilePath();
	warroom::ImageMod::setCurrentDocumentDir(absDir);
	warroom::VideoMod::setCurrentDocumentDir(absDir);

	if (m_model.saveToFolder(folderPath.toStdString())) {
		// 统一 m_currentFilePath 为 .warroom 文件路径
		m_currentFilePath = getWarroomFromFolder(folderPath);
		writeLastOpenFilePath(m_currentFilePath);
		QMessageBox::information(this, "保存成功",
			QString("已保存到：\n%1").arg(m_currentFilePath));
	}
	else {
		QMessageBox::warning(this, "保存失败", "无法保存文件");
	}
}

void WarRoomMainWindow::onLoadAction()
{
	qDebug() << "[KEYDBG-SLOT] onLoadAction triggered";
	QString path = QFileDialog::getOpenFileName(this, "打开作战图", "",
		"WarRoom文件 (*.warroom)");
	if (path.isEmpty()) return;

	QString docDir = isNewFormatWarroom(path)
		? getArchiveFolderFromWarroom(path)
		: QFileInfo(path).absolutePath();

	warroom::ImageMod::setCurrentDocumentDir(docDir);
	warroom::VideoMod::setCurrentDocumentDir(docDir);

	warroom::WarRoomModel newModel;
	if (newModel.loadFromAuto(path.toStdString())) {
		m_model = std::move(newModel);
		m_currentFilePath = path;
		writeLastOpenFilePath(path);

		m_model.normalizeZValues();

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
	qDebug() << "[KEYDBG-SLOT] onUndo triggered | canUndo =" << m_undoManager.canUndo();
	if (m_undoManager.canUndo()) {
		m_undoManager.undo(m_model);
		syncAllItemsFromModel();
		refreshLinks();
	}
}

void WarRoomMainWindow::onRedo()
{
	qDebug() << "[KEYDBG-SLOT] onRedo triggered | canRedo =" << m_undoManager.canRedo();
	if (m_undoManager.canRedo()) {
		m_undoManager.redo(m_model);
		syncAllItemsFromModel();
		refreshLinks();
	}
}

// ============================================================================
// 私有槽 - 视图操作
// ============================================================================

void WarRoomMainWindow::onToggleReadOnly()
{
	bool isReadOnly = !m_model.isReadOnly();
	m_model.setReadOnly(isReadOnly);

	if (isReadOnly) {
		m_readOnlyBtn->setText("只读");
	}
	else {
		m_readOnlyBtn->setText("编辑");
	}
}

void WarRoomMainWindow::onResetView()
{
	if (m_view && m_cameraAnimator) {
		m_cameraAnimator->focusOn(QPointF(0, 0), 1.0f);
	}
}

// ============================================================================
// 私有槽 - 节点操作
// ============================================================================

void WarRoomMainWindow::onNodeSelectedForZBoost(const std::string& nodeId)
{
	if (m_model.isReadOnly()) return;
	warroom::WarNode* node = m_model.getNodeMutable(nodeId);
	if (!node) return;

	// 获取父节点的 absolute_z（document_root 的子节点：父节点 absolute_z = 0）
	warroom::Uuid parentId = node->parent_id;
	int parentAbsZ = 0;
	if (!parentId.empty() && parentId != m_model.getDocumentRootId()) {
		parentAbsZ = m_model.computeAbsoluteZ(parentId);
	}

	// 目标 absolute_z = 全局最大 + 1（保证全局置顶）
	int targetAbsZ = m_model.getMaxAbsZ() + 1;

	// 设置 relative_z，使得 sum(路径) = targetAbsZ
	node->relative_z = targetAbsZ - parentAbsZ;

	// 更新该节点及其所有子孙的 Z 值（使视图立即反映变化，避免闪烁）
	updateSubtreeZValues(nodeId);
	refreshAllLinksZValue();
	refreshLinks();

	// 维护全局最大 z：必须取子树（含自身）的最大 absolute_z，
	// 因为子节点的 absolute_z = node.absolute_z + descendant_relative_z之和
	// 可能大于 node.absolute_z 本身
	int subtreeMax = m_model.computeSubtreeMaxAbsZ(
		warroom::Uuid(nodeId));
	m_model.setMaxAbsZ(subtreeMax);

	// 更新焦点指示器
	auto* item = m_nodeItems.value(QString::fromStdString(nodeId));
	if (item) {
		updateFocusOnNode(item);
	}

	// 如果超过阈值，触发归一化
	if (m_model.getMaxAbsZ() > 1000000) {
		m_model.normalizeZValues();
		// 归一化后全量刷新视图
		updateSubtreeZValues(m_model.getDocumentRootId());
		refreshAllLinksZValue();
		refreshLinks();
	}
}

// ============================================================================
// 焦点指示器更新
// ============================================================================

void WarRoomMainWindow::updateFocusOnNode(NodeGraphicsItem* item)
{
	if (!m_highlightOverlay || !item || !m_view) return;
	m_highlightOverlay->setFocusState(FocusState::NodeFocus, item);
	updateCanvasAreaForOverlay();
}

void WarRoomMainWindow::updateFocusOnCanvas()
{
	if (!m_highlightOverlay) return;
	m_highlightOverlay->setFocusState(FocusState::CanvasFocus);
	updateCanvasAreaForOverlay();
}

void WarRoomMainWindow::updateFocusNoFocus(const QString& reason)
{
	if (!m_highlightOverlay) return;
	m_highlightOverlay->setFocusState(FocusState::NoFocus);
	m_highlightOverlay->setExternalFocusName(reason);
}

void WarRoomMainWindow::updateCanvasAreaForOverlay()
{
	if (!m_highlightOverlay || !m_view) return;

	QRect viewRect = m_view->rect();
	QPoint tl = m_view->mapTo(m_highlightOverlay->parentWidget(), viewRect.topLeft());
	QPoint br = m_view->mapTo(m_highlightOverlay->parentWidget(), viewRect.bottomRight());
	m_highlightOverlay->setCanvasArea(QRect(tl, br).normalized());
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
	// 扩展画布范围：±100000（宽度 200000），确保点阵/网格范围足够大
	m_scene->setSceneRect(-100000, -100000, 200000, 200000);

	// 关键：禁用 BSP 树索引。大画布 + 节点少时，BSP 索引的空分区反而拖慢渲染。
	// NoIndex 在 1000 个节点以内的场景中，速度与 BSP 相当甚至更快，
	// 且不会出现"背景只画一半"的优化截断问题。
	m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);

	// 创建视图并设置到画布区域
	m_view = new WarRoomView(m_scene, m_canvasArea);
	m_view->setAcceptDrops(true);

	// 拖放文件到空白处 → 创建对应类型的新节点
	connect(m_view, &WarRoomView::dropToCreateNode,
		this, &WarRoomMainWindow::addNodeFromDrop);

	// 视图失去焦点时更新焦点指示器
	connect(m_view, &WarRoomView::viewFocusLost, this, [this]() {
		if (m_highlightOverlay) {
			updateFocusNoFocus(tr("画布"));
		}
	});

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
		auto* linkItem = new LinkGraphicsItem(linkId, &m_model);
		setupLinkItemConnections(linkItem);
		m_scene->addItem(linkItem);
	}

	// ---- 递归创建节点图形项 ----
	std::function<void(Uuid)> createItems;
	createItems = [&](Uuid parentId) {
		auto children = m_model.getChildren(parentId);
		for (const Uuid& childId : children) {
			const WarNode* node = m_model.getNode(childId);
			if (!node) continue;

			auto* item = new NodeGraphicsItem(childId, &m_model);
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

			auto* item = new NodeGraphicsItem(childId, &m_model);
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
		auto* linkItem = new LinkGraphicsItem(linkId, &m_model);
		setupLinkItemConnections(linkItem);
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

	m_newAction = new QAction("新建(&N)", this);
	m_newAction->setShortcut(QKeySequence::New);
	connect(m_newAction, &QAction::triggered, this, &WarRoomMainWindow::onNewAction);
	fileMenu->addAction(m_newAction);

	fileMenu->addSeparator();

	m_openAction = new QAction("打开(&O)...", this);
	m_openAction->setShortcut(QKeySequence::Open);
	connect(m_openAction, &QAction::triggered, this, &WarRoomMainWindow::onLoadAction);
	fileMenu->addAction(m_openAction);

	m_saveAction = new QAction("保存(&S)", this);
	m_saveAction->setShortcut(QKeySequence::Save);
	connect(m_saveAction, &QAction::triggered, this, &WarRoomMainWindow::onSaveAction);
	fileMenu->addAction(m_saveAction);

	m_saveAsAction = new QAction("另存为(&A)...", this);
	m_saveAsAction->setShortcut(QKeySequence::SaveAs);
	connect(m_saveAsAction, &QAction::triggered, this, &WarRoomMainWindow::onSaveAsAction);
	fileMenu->addAction(m_saveAsAction);

	fileMenu->addSeparator();

	QAction* importAction = new QAction("导入 JSON(&I)...", this);
	connect(importAction, &QAction::triggered, this, &WarRoomMainWindow::onImportJson);
	fileMenu->addAction(importAction);

	QAction* exportAction = new QAction("导出 JSON(&E)...", this);
	connect(exportAction, &QAction::triggered, this, &WarRoomMainWindow::onExportJson);
	fileMenu->addAction(exportAction);

	fileMenu->addSeparator();

	m_exitAction = new QAction("退出(&X)", this);
	m_exitAction->setShortcut(QKeySequence::Quit);
	connect(m_exitAction, &QAction::triggered, this, &QWidget::close);
	fileMenu->addAction(m_exitAction);

	// ---- 编辑菜单 ----
	QMenu* editMenu = menuBar->addMenu("编辑(&E)");

	m_undoAction = new QAction("撤销(&U)", this);
	m_undoAction->setShortcut(QKeySequence::Undo);
	connect(m_undoAction, &QAction::triggered, this, &WarRoomMainWindow::onUndo);
	editMenu->addAction(m_undoAction);

	m_redoAction = new QAction("重做(&R)", this);
	m_redoAction->setShortcut(QKeySequence::Redo);
	connect(m_redoAction, &QAction::triggered, this, &WarRoomMainWindow::onRedo);
	editMenu->addAction(m_redoAction);

	editMenu->addSeparator();

	m_deleteAction = new QAction("删除(&D)", this);
	m_deleteAction->setShortcut(QKeySequence::Delete);
	connect(m_deleteAction, &QAction::triggered, this, &WarRoomMainWindow::deleteSelectedNode);
	editMenu->addAction(m_deleteAction);

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

	if (!m_newAction) {
		m_newAction = new QAction(QIcon(), "新建", this);
		connect(m_newAction, &QAction::triggered, this, &WarRoomMainWindow::onNewAction);
	}
	toolBar->addAction(m_newAction);

	if (!m_openAction) {
		m_openAction = new QAction(QIcon(), "打开", this);
		connect(m_openAction, &QAction::triggered, this, &WarRoomMainWindow::onLoadAction);
	}
	toolBar->addAction(m_openAction);

	if (!m_saveAction) {
		m_saveAction = new QAction(QIcon(), "保存", this);
		connect(m_saveAction, &QAction::triggered, this, &WarRoomMainWindow::onSaveAction);
	}
	toolBar->addAction(m_saveAction);

	toolBar->addSeparator();

	if (!m_undoAction) {
		m_undoAction = new QAction(QIcon(), "撤销", this);
		connect(m_undoAction, &QAction::triggered, this, &WarRoomMainWindow::onUndo);
	}
	toolBar->addAction(m_undoAction);

	if (!m_redoAction) {
		m_redoAction = new QAction(QIcon(), "重做", this);
		connect(m_redoAction, &QAction::triggered, this, &WarRoomMainWindow::onRedo);
	}
	toolBar->addAction(m_redoAction);
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
			if (m_view) {
				m_view->setIsEditing(true);
			}
		});
	QObject::connect(item, &NodeGraphicsItem::editFinished, this,
		[this](const std::string&) {
			m_currentEditingNodeId.clear();
			if (m_view) {
				m_view->setIsEditing(false);
			}
		});
	QObject::connect(item, &NodeGraphicsItem::titleChanged, this,
		[this](const std::string&) {
			refreshSidebarTree();
			refreshTodoSidebar();
		});
}

// ============================================================================
// 私有方法 - 拖拽回写
// ============================================================================

void WarRoomMainWindow::onNodeMoved(const std::string& nodeId, float newX, float newY)
{
	if (m_model.isReadOnly()) return;
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
	if (m_model.isReadOnly()) return;
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
	if (m_model.isReadOnly()) return;
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
// 注意：撤销/重做等快捷键通过 QAction::setShortcut 注册到 m_undoAction/m_redoAction，
// Qt 会自动通过 QApplication::notify 分发，这里不需要手动处理。
// 之前在这里硬编码 Ctrl+Z/Ctrl+Y 并 return，导致其他 action 快捷键（如 Ctrl+S/Ctrl+N）
// 在此 widget 获得焦点时全部失效（return 阻断了 QMainWindow::keyPressEvent 转发）。
// 移除后所有 action 快捷键由 Qt 自身的事件分发机制处理，包括用户自定义键位。

void WarRoomMainWindow::keyPressEvent(QKeyEvent* event)
{
	qDebug() << "[KEYDBG] MainWindow::keyPressEvent key =" << event->key()
		<< "modifiers =" << event->modifiers()
		<< "focusWidget =" << QApplication::focusWidget();
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
					auto* item = new NodeGraphicsItem(childId, &m_model);
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
	refreshTodoSidebar();
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
	if (m_model.isReadOnly()) return;
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
	qDebug() << "[KEYDBG-SLOT] deleteSelectedNode triggered | readOnly =" << m_model.isReadOnly();
	if (m_model.isReadOnly()) return;
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

	// 交给 DeleteNodeCommand 处理：构造命令并通过 UndoManager 执行
	// DeleteNodeCommand 会在 execute 时自动捕获快照，undo 时恢复
	auto cmd = std::make_unique<warroom::DeleteNodeCommand>(nodeId);
	executeCommand(std::move(cmd));
}

void WarRoomMainWindow::addNodeAtPosition(QPointF scenePos)
{
	addNodeAtPosition(scenePos, m_model.getDocumentRootId());
}

void WarRoomMainWindow::addNodeAtPosition(QPointF scenePos,
	const warroom::Uuid& parentId)
{
	if (m_model.isReadOnly()) return;
	warroom::WarNode newNode = warroom::WarNode::makeLeaf("新节点",
		scenePos.x(), scenePos.y());
	newNode.full_text = "";

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
	if (m_model.isReadOnly()) return;
	warroom::WarNode newNode = warroom::WarNode::makeLeaf("",
		scenePos.x(), scenePos.y());
	seedNodeForMod(newNode, modId);

	auto cmd = std::make_unique<warroom::AddNodeCommand>(
		std::move(newNode), parentId, -1);
	executeCommand(std::move(cmd));
}

void WarRoomMainWindow::addNodeFromDrop(QPointF scenePos, const QMimeData* mimeData)
{
	if (m_model.isReadOnly() || !mimeData) return;

	// 遍历主模组，找到第一个能处理的
	auto& mm = warroom::ModManager::instance();
	for (const std::string& modId : mm.getPrimaryMods()) {
		if (warroom::NodeMod* mod = mm.getMod(modId)) {
			if (!mod->canCreateNodeFromDrop(mimeData)) continue;

			// 创建节点（先取默认尺寸用于居中偏移）
			warroom::WarNode newNode = warroom::WarNode::makeLeaf("", 0, 0);
			seedNodeForMod(newNode, modId);
			// 让节点中央对齐鼠标位置（节点坐标是左上角）
			newNode.pos_x = scenePos.x() - newNode.width / 2.0f;
			newNode.pos_y = scenePos.y() - newNode.height / 2.0f;

			auto cmd = std::make_unique<warroom::AddNodeCommand>(
				std::move(newNode), m_model.getDocumentRootId(), -1);
			// 在 move 前保存 nodeId，executeCommand 后用它取 modData
			std::string nodeId = cmd->getNodeId();
			executeCommand(std::move(cmd));

			// syncAllItemsFromModel 已在 executeCommand 中完成，
			// NodeGraphicsItem 构造时已调用 initNodeModData（onCreateNode + onNodeLoaded）
			// 现在拿到节点的 modData 调用 onDropToNewNode 填充数据
			void* modData = mm.getPrimaryPrivate(m_model.getNode(nodeId));
			if (modData) {
				mod->onDropToNewNode(mimeData,
					m_model.getNodeMutable(nodeId), modData);
			}
			break;
		}
	}
}

void WarRoomMainWindow::editNode(const std::string& nodeId)
{
	if (m_model.isReadOnly()) return;
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

		// ---- 标题相关 ----
		// 预览模式只保留"重命名标题"（选词设标题仅编辑模式下通过编辑器内右键菜单可用）
		{
			std::string titleNodeId = nodeItem->nodeId();
			warroom::WarNode* titleNode = m_model.getNodeMutable(titleNodeId);
			QString currentTitle = titleNode ? QString::fromStdString(titleNode->title) : QString();
			menu.addAction("重命名标题…", [this, titleNodeId, currentTitle]() {
				bool ok = false;
				QString text = QInputDialog::getText(this, "重命名标题",
					"标题：", QLineEdit::Normal, currentTitle, &ok);
				if (ok) {
					warroom::WarNode* n = m_model.getNodeMutable(titleNodeId);
					if (n) {
						n->title = text.toStdString();
						refreshSidebarTree();
					}
				}
			});
		}

		menu.addSeparator();

		// ---- 预览模式子菜单 ----
		QMenu* previewModeMenu = menu.addMenu("预览模式");
		std::string nodeIdStrPreview = nodeItem->nodeId();
		warroom::WarNode* previewNode = m_model.getNodeMutable(nodeIdStrPreview);
		if (previewNode) {
			bool isPlain = (previewNode->text_display_mode == "plain");
			QAction* mdAction = previewModeMenu->addAction("Markdown 渲染");
			mdAction->setCheckable(true);
			mdAction->setChecked(!isPlain);
			QAction* plainAction = previewModeMenu->addAction("纯文本显示");
			plainAction->setCheckable(true);
			plainAction->setChecked(isPlain);

			auto switchMode = [this, nodeItem](const std::string& mode) {
				if (!nodeItem) return;
				std::string nid = nodeItem->nodeId();
				warroom::WarNode* n = m_model.getNodeMutable(nid);
				if (!n) return;
				n->text_display_mode = mode;
				NodeGraphicsItem* item = m_nodeItems.value(QString::fromStdString(nid));
				if (item) item->refresh();
			};
			connect(mdAction, &QAction::triggered, [switchMode]() { switchMode("markdown"); });
			connect(plainAction, &QAction::triggered, [switchMode]() { switchMode("plain"); });
		}

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
				// 通过 command 系统修改颜色，支持 undo/redo
				auto cmd = std::make_unique<warroom::SetNodeColorCommand>(
					nodeId, std::string(preset.hexColor));
				executeCommand(std::move(cmd));
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
				if (!node) return;
				// 获取当前颜色的 RGB 部分，保留原色相
				QString currentColor = QString::fromStdString(node->color);
				// 格式为 "#AARRGGBB"，提取 RGB
				if (currentColor.length() == 9 && currentColor.startsWith("#")) {
					// 保留 RGB，替换 Alpha 通道
					QString rgb = currentColor.mid(3);
					int alphaValue = static_cast<int>(255.0f * preset.percent / 100.0f);
					QString newColor = QString("#%1%2")
						.arg(alphaValue, 2, 16, QChar('0'))
						.arg(rgb);
					// 使用 command 系统修改颜色（支持 undo/redo）
					auto cmd = std::make_unique<warroom::SetNodeColorCommand>(
						nodeId, newColor.toStdString());
					executeCommand(std::move(cmd));
				}
				});
		}

		// ---- 待办功能 ----
		{
			std::string todoNodeId = nodeItem->nodeId();
			warroom::TodoState currentTodo = m_model.getNodeTodoState(todoNodeId);

			if (currentTodo == warroom::TodoState::None) {
				menu.addAction("添加待办", [this, todoNodeId]() {
					m_model.setNodeTodoState(todoNodeId, warroom::TodoState::Pending);
					refreshTodoSidebar();
				});
			}
			else {
				if (currentTodo == warroom::TodoState::Pending) {
					menu.addAction("标记完成", [this, todoNodeId]() {
						m_model.setNodeTodoState(todoNodeId, warroom::TodoState::Done);
						refreshTodoSidebar();
					});
				}
				else {
					menu.addAction("取消完成", [this, todoNodeId]() {
						m_model.setNodeTodoState(todoNodeId, warroom::TodoState::Pending);
						refreshTodoSidebar();
					});
				}
				menu.addAction("移除待办", [this, todoNodeId]() {
					m_model.setNodeTodoState(todoNodeId, warroom::TodoState::None);
					refreshTodoSidebar();
				});
			}
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

			// 辅助模组的右键菜单（已启用的）
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

			// 未启用的辅助模组：调用 onContextMenuForNode（可显示"启用"入口）
			auto allAuxMods = warroom::ModManager::instance().getAuxiliaryMods();
			for (const auto& modId : allAuxMods) {
				// 跳过已启用的
				bool enabled = false;
				for (const auto& t : targetNode->auxiliary_mod_types) {
					if (t == modId) { enabled = true; break; }
				}
				if (enabled) continue;
				if (warroom::NodeMod* mod = warroom::ModManager::instance().getMod(modId)) {
					warroom::ModMenuContext ctx;
					ctx.menu = &menu;
					ctx.parent = this;
					ctx.node = targetNode;
					ctx.modData = nullptr;
					ctx.nodeId = nodeIdStr;
					ctx.requestNodeRefresh = refreshCallback;
					bool addedSeparator = mod->onContextMenuForNode(ctx);
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

	menu.addSeparator();
	menu.addAction("刷新", this, [this]() {
		if (m_scene) m_scene->update();
		if (m_view) m_scene->update(m_view->sceneRect());
	});

	menu.exec(event->globalPos());
}

// ============================================================================
// 保护方法 - 事件过滤器
// ============================================================================

bool WarRoomMainWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent* ke = static_cast<QKeyEvent*>(event);
		qDebug() << "[KEYDBG] eventFilter KeyPress watched =" << watched
			<< "key =" << ke->key() << "modifiers =" << ke->modifiers();
	}

	// 保持左下角设置按钮始终位于左下角
	if (m_centralContainer && m_settingsCorner && watched == m_centralContainer) {
		if (event->type() == QEvent::Resize) {
			QSize containerSize = m_centralContainer->size();
			int cornerW = 100;
			int cornerH = 90;
			m_settingsCorner->setGeometry(
				0, containerSize.height() - cornerH, cornerW, cornerH);
			m_settingsCorner->raise();

			// 同步高亮覆盖层大小
			if (m_highlightOverlay) {
				m_highlightOverlay->syncGeometry();
			}
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
			}
		}

		// 更新焦点指示器
		if (!clickedNodeId.empty()) {
			auto* item = m_nodeItems.value(QString::fromStdString(clickedNodeId));
			if (item) {
				updateFocusOnNode(item);
			}
		} else {
			updateFocusOnCanvas();
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

	// 创建按钮（同时创建对应的 QAction 成员，用于快捷键注册）
	// 每个工具栏按钮关联一个 QAction：按钮 clicked -> action trigger
	// QAction 是快捷键注册的载体，applyKeyBindings() 会把 QKeySequence 应用到这些 action 上
	QPushButton* newBtn = new QPushButton("新建", toolBarContainer);
	QPushButton* openBtn = new QPushButton("打开", toolBarContainer);
	QPushButton* saveBtn = new QPushButton("保存", toolBarContainer);
	QPushButton* undoBtn = new QPushButton("撤销", toolBarContainer);
	QPushButton* redoBtn = new QPushButton("重做", toolBarContainer);
	QPushButton* deleteBtn = new QPushButton("删除", toolBarContainer);

	// 创建 6 个 QAction 成员（m_saveAsAction/m_exitAction 在文件菜单处创建）
	if (!m_newAction) {
		m_newAction = new QAction("新建", this);
		m_newAction->setShortcut(QKeySequence::New);
		connect(m_newAction, &QAction::triggered, this, &WarRoomMainWindow::onNewAction);
	}
	if (!m_openAction) {
		m_openAction = new QAction("打开", this);
		m_openAction->setShortcut(QKeySequence::Open);
		connect(m_openAction, &QAction::triggered, this, &WarRoomMainWindow::onLoadAction);
	}
	if (!m_saveAction) {
		m_saveAction = new QAction("保存", this);
		m_saveAction->setShortcut(QKeySequence::Save);
		connect(m_saveAction, &QAction::triggered, this, &WarRoomMainWindow::onSaveAction);
	}
	if (!m_undoAction) {
		m_undoAction = new QAction("撤销", this);
		m_undoAction->setShortcut(QKeySequence::Undo);
		connect(m_undoAction, &QAction::triggered, this, &WarRoomMainWindow::onUndo);
	}
	if (!m_redoAction) {
		m_redoAction = new QAction("重做", this);
		m_redoAction->setShortcut(QKeySequence::Redo);
		connect(m_redoAction, &QAction::triggered, this, &WarRoomMainWindow::onRedo);
	}
	if (!m_deleteAction) {
		m_deleteAction = new QAction("删除", this);
		m_deleteAction->setShortcut(QKeySequence::Delete);
		connect(m_deleteAction, &QAction::triggered, this, &WarRoomMainWindow::deleteSelectedNode);
	}

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
	deleteBtn->setStyleSheet(btnStyle);

	// 工具栏按钮点击 -> 触发对应 action（保持单一触发源，便于统一管理）
	connect(newBtn, &QPushButton::clicked, m_newAction, &QAction::trigger);
	connect(openBtn, &QPushButton::clicked, m_openAction, &QAction::trigger);
	connect(saveBtn, &QPushButton::clicked, m_saveAction, &QAction::trigger);
	connect(undoBtn, &QPushButton::clicked, m_undoAction, &QAction::trigger);
	connect(redoBtn, &QPushButton::clicked, m_redoAction, &QAction::trigger);
	connect(deleteBtn, &QPushButton::clicked, m_deleteAction, &QAction::trigger);

	toolBarLayout->addWidget(newBtn);
	toolBarLayout->addWidget(openBtn);
	toolBarLayout->addWidget(saveBtn);
	toolBarLayout->addWidget(undoBtn);
	toolBarLayout->addWidget(redoBtn);
	toolBarLayout->addWidget(deleteBtn);
	toolBarLayout->addStretch();

	// ---- 文件下拉按钮（包含另存为、导入、导出、退出等功能）----
	QToolButton* fileMenuBtn = new QToolButton(toolBarContainer);
	fileMenuBtn->setText("文件");
	fileMenuBtn->setPopupMode(QToolButton::InstantPopup);
	fileMenuBtn->setStyleSheet(R"(
		QToolButton {
			background: #3A3A3A;
			border: none;
			padding: 4px 10px;
			border-radius: 3px;
			color: #CCCCCC;
			font-size: 12px;
		}
		QToolButton:hover {
			background: #4A4A4A;
		}
		QToolButton::menu-button {
			border: none;
			padding-left: 4px;
		}
	)");

	QMenu* fileMenu = new QMenu(this);

	// 另存为
	if (!m_saveAsAction) {
		m_saveAsAction = new QAction("另存为(&A)...", fileMenu);
		m_saveAsAction->setShortcut(QKeySequence::SaveAs);
		connect(m_saveAsAction, &QAction::triggered, this, &WarRoomMainWindow::onSaveAsAction);
	}
	fileMenu->addAction(m_saveAsAction);

	fileMenu->addSeparator();

	// 导入/导出 JSON
	QAction* importAction = new QAction("导入 JSON(&I)...", fileMenu);
	connect(importAction, &QAction::triggered, this, &WarRoomMainWindow::onImportJson);
	fileMenu->addAction(importAction);

	QAction* exportAction = new QAction("导出 JSON(&E)...", fileMenu);
	connect(exportAction, &QAction::triggered, this, &WarRoomMainWindow::onExportJson);
	fileMenu->addAction(exportAction);

	fileMenu->addSeparator();

	// 退出
	if (!m_exitAction) {
		m_exitAction = new QAction("退出(&X)", fileMenu);
		connect(m_exitAction, &QAction::triggered, this, &QApplication::quit);
	}
	fileMenu->addAction(m_exitAction);

	fileMenuBtn->setMenu(fileMenu);
	toolBarLayout->addWidget(fileMenuBtn);

	// 添加一个简单的视图控制
	QPushButton* resetViewBtn = new QPushButton("重置视图", toolBarContainer);
	resetViewBtn->setStyleSheet(btnStyle);
	connect(resetViewBtn, &QPushButton::clicked, this, &WarRoomMainWindow::onResetView);
	toolBarLayout->addWidget(resetViewBtn);

	// ---- 只读模式切换按钮 ----
	m_readOnlyBtn = new QPushButton("编辑", toolBarContainer);
	m_readOnlyBtn->setStyleSheet(btnStyle);
	connect(m_readOnlyBtn, &QPushButton::clicked, this, &WarRoomMainWindow::onToggleReadOnly);
	toolBarLayout->addWidget(m_readOnlyBtn);

	// ---- 待办侧边栏切换按钮 ----
	m_todoToggleBtn = new QPushButton("待办", toolBarContainer);
	m_todoToggleBtn->setStyleSheet(btnStyle);
	m_todoToggleBtn->setToolTip("显示/隐藏待办侧边栏");
	connect(m_todoToggleBtn, &QPushButton::clicked, this, &WarRoomMainWindow::onToggleTodoSidebar);
	toolBarLayout->addWidget(m_todoToggleBtn);

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

	// 右侧待办侧边栏（默认隐藏）
	m_todoSidebar = new TodoSidebar(m_centralContainer);
	m_todoSidebar->setVisible(false);
	contentLayout->addWidget(m_todoSidebar);

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
		// 首次显示时刷新侧栏（构造函数中的刷新可能因 widget 未显示而无效）
		refreshSidebarTree();
		refreshTodoSidebar();

		// 首次显示时更新 overlay 的画布区域并触发重绘
		if (m_highlightOverlay) {
			m_highlightOverlay->syncGeometry();
			updateCanvasAreaForOverlay();
			m_highlightOverlay->setFocusState(FocusState::CanvasFocus);
		}
	}
}

void WarRoomMainWindow::resizeEvent(QResizeEvent* event)
{
	QMainWindow::resizeEvent(event);

	// 窗口大小变化时更新 overlay
	if (m_highlightOverlay) {
		m_highlightOverlay->syncGeometry();
		updateCanvasAreaForOverlay();
		m_highlightOverlay->update();
	}
}

// ============================================================
// 待办侧边栏
// ============================================================
void WarRoomMainWindow::setupTodoSidebar()
{
	if (!m_todoSidebar) return;
	connect(m_todoSidebar, &TodoSidebar::itemFocused,
		this, &WarRoomMainWindow::onTodoItemFocused);
	connect(m_todoSidebar, &TodoSidebar::itemDoubleClicked,
		this, &WarRoomMainWindow::onTodoItemDoubleClicked);
	connect(m_todoSidebar, &TodoSidebar::itemToggled,
		this, &WarRoomMainWindow::onTodoItemToggled);
}

void WarRoomMainWindow::refreshTodoSidebar()
{
	if (!m_todoSidebar) return;

	std::vector<TodoItemData> items;
	const auto& todoList = m_model.getTodoList();
	items.reserve(todoList.size());

	for (const auto& nodeId : todoList) {
		const warroom::WarNode* node = m_model.getNode(nodeId);
		if (!node) continue;
		if (node->todo_state == warroom::TodoState::None) continue;

		TodoItemData item;
		item.nodeId = nodeId;
		item.title = node->title.empty() ? node->full_text : node->title;
		if (item.title.empty()) item.title = "未命名节点";
		item.done = (node->todo_state == warroom::TodoState::Done);
		item.createdAt = node->todo_created_at;
		items.push_back(item);
	}

	// 按时间排序（最新的在前）
	std::sort(items.begin(), items.end(),
		[](const TodoItemData& a, const TodoItemData& b) {
			return a.createdAt > b.createdAt;
		});

	m_todoSidebar->setTodoData(items);
}

void WarRoomMainWindow::onTodoItemFocused(const std::string& nodeId)
{
	auto it = m_nodeItems.find(QString::fromStdString(nodeId));
	if (it != m_nodeItems.end()) {
		focusNodeAnimated(it.value());
	}
	m_todoSidebar->selectItem(nodeId);
	showNodeHighlight(nodeId, false);  // 右侧栏
}

void WarRoomMainWindow::onTodoItemDoubleClicked(const std::string& nodeId)
{
	// 双击聚焦：平滑居中并在缩放过小时放大到 0.6（与左侧边栏双击逻辑一致）
	auto it = m_nodeItems.find(QString::fromStdString(nodeId));
	if (it != m_nodeItems.end()) {
		auto* item = it.value();
		float currentZoom = m_view->getZoomLevel();
		float targetZoom = (currentZoom < 0.5f) ? 0.6f : currentZoom;

		if (m_cameraAnimator) {
			QRectF itemRect = item->mapToScene(item->boundingRect()).boundingRect();
			m_cameraAnimator->focusOn(itemRect.center(), targetZoom);
		} else {
			m_view->centerOn(item);
			if (currentZoom < 0.5f) {
				warroom::Point2D center = m_view->getViewCenter();
				m_view->setViewCenter(center, 0.6f);
			}
		}

		m_todoSidebar->selectItem(nodeId);
	}
	showNodeHighlight(nodeId, false);  // 右侧栏
}

void WarRoomMainWindow::onTodoItemToggled(const std::string& nodeId, bool done)
{
	m_model.setNodeTodoState(nodeId,
		done ? warroom::TodoState::Done : warroom::TodoState::Pending);
	refreshTodoSidebar();
}

void WarRoomMainWindow::onToggleTodoSidebar()
{
	if (!m_todoSidebar) return;
	bool currentlyVisible = m_todoSidebar->isVisible();
	m_todoSidebar->setVisible(!currentlyVisible);

	// 切换按钮文字
	if (currentlyVisible) {
		m_todoToggleBtn->setText("待办");
	} else {
		m_todoToggleBtn->setText("待办 ✓");
		refreshTodoSidebar();
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
		focusNodeAnimated(it.value());
	}
	m_sidebar->selectNode(nodeId);
	showNodeHighlight(nodeId, true);  // 左侧栏
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
	// 双击聚焦：平滑居中并适当放大
	auto it = m_nodeItems.find(QString::fromStdString(nodeId));
	if (it != m_nodeItems.end()) {
		auto* item = it.value();
		// 双击时如果当前缩放过小，强制目标缩放为 0.6
		float currentZoom = m_view->getZoomLevel();
		float targetZoom = (currentZoom < 0.5f) ? 0.6f : currentZoom;

		// 聚焦到节点中心
		if (m_cameraAnimator) {
			// 直接用 item 的中心点 + 指定缩放
			QRectF itemRect = item->mapToScene(item->boundingRect()).boundingRect();
			m_cameraAnimator->focusOn(itemRect.center(), targetZoom);
		} else {
			m_view->centerOn(item);
			if (currentZoom < 0.5f) {
				warroom::Point2D center = m_view->getViewCenter();
				m_view->setViewCenter(center, 0.6f);
			}
		}

		m_sidebar->selectNode(nodeId);
	}
	showNodeHighlight(nodeId, true);  // 左侧栏
}

// ============================================================
// 相机动画
// ============================================================
void WarRoomMainWindow::focusNodeAnimated(NodeGraphicsItem* item)
{
	if (!item) return;
	if (m_cameraAnimator) {
		m_cameraAnimator->focusOn(item);
	} else if (m_view) {
		m_view->centerOn(item);
	}
}

void WarRoomMainWindow::abortCameraAnimation()
{
	if (m_cameraAnimator && m_cameraAnimator->isAnimating()) {
		m_cameraAnimator->abort();
	}
}

// ============================================================
// 节点高亮提示
// ============================================================
void WarRoomMainWindow::showNodeHighlight(const std::string& nodeId, bool fromLeft)
{
	if (!m_highlightOverlay) return;

	auto it = m_nodeItems.find(QString::fromStdString(nodeId));
	if (it == m_nodeItems.end()) return;
	auto* nodeItem = it.value();

	// 获取侧边栏项在自身坐标系中的起点位置
	QPoint origin;
	if (fromLeft) {
		// 左侧边栏：右边缘
		origin = m_sidebar->getNodeArrowOrigin(nodeId);
	} else {
		// 右侧待办侧边栏：左边缘
		origin = m_todoSidebar->getItemArrowOrigin(nodeId);
	}
	if (origin.x() < 0) return;

	// 将起点映射到 m_centralContainer 坐标系（HighlightOverlay 的坐标系）
	QWidget* sourceWidget = fromLeft ? static_cast<QWidget*>(m_sidebar)
	                                 : static_cast<QWidget*>(m_todoSidebar);
	QPoint containerPos = sourceWidget->mapTo(m_centralContainer, origin);

	// 确保覆盖层在最上层并跟随容器大小
	m_highlightOverlay->syncGeometry();
	m_highlightOverlay->raise();
	m_highlightOverlay->showHighlight(containerPos, nodeItem, m_view, fromLeft);
}
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
	qDebug().nospace().noquote()
		<< "[DESTDBG] onTitleBarClose ENTER this=" << static_cast<void*>(this)
		<< " (WA_DeleteOnClose =" << testAttribute(Qt::WA_DeleteOnClose) << ")";
	if (maybeSave()) {
		qDebug() << "[DESTDBG]   onTitleBarClose: calling close()";
		close();
		qDebug() << "[DESTDBG]   onTitleBarClose: close() returned";
	}
	qDebug() << "[DESTDBG] onTitleBarClose EXIT";
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

	QString docDir = isNewFormatWarroom(path)
		? getArchiveFolderFromWarroom(path)
		: QFileInfo(path).absolutePath();

	warroom::ImageMod::setCurrentDocumentDir(docDir);
	warroom::VideoMod::setCurrentDocumentDir(docDir);

	warroom::WarRoomModel newModel;
	if (newModel.loadFromAuto(path.toStdString())) {
		m_model = std::move(newModel);
		m_currentFilePath = path;
		writeLastOpenFilePath(path);

		m_model.normalizeZValues();

		m_scene->clear();
		rebuildFromModel();

		if (m_view) {
			warroom::Point2D viewPos;
			float zoom;
			m_model.getCameraView(viewPos, zoom);
			m_view->restoreViewState(viewPos, zoom);
		}

		refreshSidebarTree();
		refreshTodoSidebar();
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

	// "自定义颜色..." 按钮（弹出 ColorPickerDialog 让用户自由选色）
	QPushButton* bgCustomBtn = new QPushButton("自定义...", bgColorRow);
	bgCustomBtn->setFixedSize(60, 28);
	bgCustomBtn->setStyleSheet(
		"QPushButton {"
		"	background-color: #2A2A2A;"
		"	color: #CCCCCC;"
		"	border: 2px solid #888888;"
		"	border-radius: 4px;"
		"	font-size: 10px;"
		"}"
		"QPushButton:hover { border-color: #FFFFFF; }"
	);
	bgColorLayout->addWidget(bgCustomBtn);
	bgColorLayout->addStretch();

	QObject::connect(bgCustomBtn, &QPushButton::clicked, [this, dialog, &colorBtns]() {
		ColorPickerDialog picker(dialog);
		picker.setPickerTitle("选择背景颜色");
		picker.setColor(s_canvasBackgroundColor);
		if (picker.exec() == QDialog::Accepted) {
			QColor c = picker.getColor();
			saveCanvasBackgroundColor(c);
			if (m_view) {
				m_view->setBackgroundColor(c);
			}
			// 取消所有预设按钮的选中状态
			for (QPushButton* btn : colorBtns) {
				btn->setChecked(false);
			}
		}
	});

	generalLayout->addWidget(bgColorRow);
	generalLayout->addSpacing(6);

	// ---- 辅助函数：生成"颜色选择"行（label + 预览 + 按钮） ----
	// 返回新创建的 row Widget（已 add 到父 layout 由调用方管理）
	auto makeColorPickerRow = [&](QWidget* parent, const QString& title,
		const QColor& initColor,
		const std::function<void(const QColor&)>& onPicked) -> QWidget*
	{
		auto* row = new QWidget(parent);
		auto* rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		rowLayout->setSpacing(8);

		QLabel* nameLabel = new QLabel(title, row);
		rowLayout->addWidget(nameLabel, 1);

		QLabel* preview = new QLabel(row);
		preview->setFixedSize(60, 28);
		QString ps = QString(
			"background-color: %1;"
			"border: 2px solid #888888;"
			"border-radius: 4px;"
		).arg(initColor.name(QColor::HexArgb));
		preview->setStyleSheet(ps);
		preview->setProperty("currentColor", initColor.name(QColor::HexArgb));
		rowLayout->addWidget(preview);

		QPushButton* pickBtn = new QPushButton("选择颜色...", row);
		pickBtn->setFixedHeight(28);
		rowLayout->addWidget(pickBtn);

		QObject::connect(pickBtn, &QPushButton::clicked, [title, preview, pickBtn, onPicked]() {
			ColorPickerDialog dlg(pickBtn);
			dlg.setPickerTitle(title);
			// 从预览框读出当前颜色作为初值
			QString curStr = preview->property("currentColor").toString();
			QColor cur(curStr);
			if (!cur.isValid()) cur = Qt::white;
			dlg.setColor(cur);
			if (dlg.exec() == QDialog::Accepted) {
				QColor c = dlg.getColor();
				preview->setProperty("currentColor", c.name(QColor::HexArgb));
				QString newStyle = QString(
					"background-color: %1;"
					"border: 2px solid #888888;"
					"border-radius: 4px;"
				).arg(c.name(QColor::HexArgb));
				preview->setStyleSheet(newStyle);
				onPicked(c);
			}
		});

		return row;
	};

	// ---- 节点文本颜色 ----
	QLabel* nodeTextTitle = new QLabel("节点文本颜色", generalPage);
	nodeTextTitle->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold;");
	generalLayout->addWidget(nodeTextTitle);

	QWidget* nodeTextRow = makeColorPickerRow(generalPage, "节点文本颜色", s_nodeTextColor,
		[this](const QColor& c) {
			saveNodeTextColor(c);
			refreshAllNodeTextColors();
		});
	generalLayout->addWidget(nodeTextRow);
	generalLayout->addSpacing(6);

	// ---- 连线颜色 ----
	QLabel* linkColorTitle = new QLabel("连线颜色", generalPage);
	linkColorTitle->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold;");
	generalLayout->addWidget(linkColorTitle);

	QWidget* linkColorRow = makeColorPickerRow(generalPage, "连线颜色", s_linkColor,
		[this](const QColor& c) {
			saveLinkColor(c);
			refreshAllLinkColors();
		});
	generalLayout->addWidget(linkColorRow);
	generalLayout->addSpacing(6);

	// ---- 背景样式设置（点阵/网格/图片） ----
	QLabel* bgStyleTitle = new QLabel("背景样式", generalPage);
	bgStyleTitle->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold;");
	generalLayout->addWidget(bgStyleTitle);

	QWidget* bgStyleRow = new QWidget(generalPage);
	auto* bgStyleLayout = new QHBoxLayout(bgStyleRow);
	bgStyleLayout->setContentsMargins(0, 0, 0, 0);
	bgStyleLayout->setSpacing(8);

	// 图片背景设置区域（默认隐藏，仅在选择"图片"样式时显示）
	QWidget* bgImageRow = new QWidget(generalPage);
	auto* bgImageLayout = new QHBoxLayout(bgImageRow);
	bgImageLayout->setContentsMargins(0, 0, 0, 0);
	bgImageLayout->setSpacing(8);

	QLineEdit* bgImagePathEdit = new QLineEdit(bgImageRow);
	bgImagePathEdit->setReadOnly(true);
	bgImagePathEdit->setFixedHeight(28);
	bgImagePathEdit->setText(s_canvasBackgroundImagePath);
	bgImagePathEdit->setPlaceholderText("未选择图片");
	bgImageLayout->addWidget(bgImagePathEdit, 1);

	QPushButton* bgImageBrowseBtn = new QPushButton("浏览", bgImageRow);
	bgImageBrowseBtn->setFixedHeight(28);
	bgImageLayout->addWidget(bgImageBrowseBtn);

	// 图片模式：平铺 / 拉伸
	QLabel* bgImageModeLabel = new QLabel("模式：", bgImageRow);
	bgImageLayout->addWidget(bgImageModeLabel);

	QPushButton* bgImageTiledBtn = new QPushButton("平铺", bgImageRow);
	bgImageTiledBtn->setFixedSize(50, 28);
	bgImageTiledBtn->setCheckable(true);
	bgImageLayout->addWidget(bgImageTiledBtn);

	QPushButton* bgImageStretchBtn = new QPushButton("拉伸", bgImageRow);
	bgImageStretchBtn->setFixedSize(50, 28);
	bgImageStretchBtn->setCheckable(true);
	bgImageLayout->addWidget(bgImageStretchBtn);

	// 初始化图片模式按钮状态
	if (s_canvasBackgroundImageMode == 0) {
		bgImageTiledBtn->setChecked(true);
	}
	else {
		bgImageStretchBtn->setChecked(true);
	}

	// 图片模式按钮样式
	QString imgModeBtnStyle =
		"QPushButton {"
		"	background-color: #2A2A2A;"
		"	color: #CCCCCC;"
		"	border: 2px solid #555555;"
		"	border-radius: 4px;"
		"	font-size: 11px;"
		"}"
		"QPushButton:checked {"
		"	background-color: #2A5A8A;"
		"	border-color: #FFFFFF;"
		"	color: #FFFFFF;"
		"}";
	bgImageTiledBtn->setStyleSheet(imgModeBtnStyle);
	bgImageStretchBtn->setStyleSheet(imgModeBtnStyle);

	// ---- 背景样式按钮 ----
	struct BgStylePreset {
		const char* name;
		int style; // 0=Dots, 1=Grid, 2=Image
	};
	std::vector<BgStylePreset> stylePresets = {
		{"点阵", 0},
		{"网格", 1},
		{"图片", 2}
	};
	std::vector<QPushButton*> styleBtns;

	// 通用按钮样式
	QString styleBtnBase =
		"QPushButton {"
		"	background-color: #2A2A2A;"
		"	color: #CCCCCC;"
		"	border: 2px solid #555555;"
		"	border-radius: 4px;"
		"	font-size: 11px;"
		"}"
		"QPushButton:checked {"
		"	background-color: #2A5A8A;"
		"	border-color: #FFFFFF;"
		"	color: #FFFFFF;"
		"}"
		"QPushButton:hover {"
		"	border-color: #888888;"
		"}";

	for (size_t i = 0; i < stylePresets.size(); ++i) {
		QPushButton* styleBtn = new QPushButton(stylePresets[i].name, bgStyleRow);
		styleBtn->setFixedSize(60, 28);
		styleBtn->setCheckable(true);
		styleBtn->setStyleSheet(styleBtnBase);

		if (stylePresets[i].style == s_canvasBackgroundStyle) {
			styleBtn->setChecked(true);
		}

		bgStyleLayout->addWidget(styleBtn);
		styleBtns.push_back(styleBtn);

		// 点击：切换样式并保存
		QObject::connect(styleBtn, &QPushButton::clicked, [this, i, &stylePresets, styleBtn, &styleBtns, bgImageRow]() {
			int selectedStyle = stylePresets[i].style;
			saveCanvasBackgroundStyle(selectedStyle);
			if (m_view) {
				m_view->setBackgroundStyle(static_cast<WarRoomView::BackgroundStyle>(selectedStyle));
			}
			// 更新按钮选中状态
			for (QPushButton* btn : styleBtns) {
				btn->setChecked(btn == styleBtn);
			}
			// 仅在"图片"时显示图片选择行
			bgImageRow->setVisible(selectedStyle == 2);
		});
	}
	bgStyleLayout->addStretch();
	generalLayout->addWidget(bgStyleRow);

	// 图片选择行（默认根据当前样式决定显示/隐藏）
	bgImageRow->setVisible(s_canvasBackgroundStyle == 2);
	generalLayout->addWidget(bgImageRow);
	generalLayout->addSpacing(6);

	// ---- 图片背景选择按钮逻辑 ----
	// "浏览" 按钮
	QObject::connect(bgImageBrowseBtn, &QPushButton::clicked, [this, dialog, bgImagePathEdit]() {
		QString filePath = QFileDialog::getOpenFileName(dialog, "选择背景图片", QString(),
			"图片文件 (*.png *.jpg *.jpeg *.bmp *.gif)");
		if (!filePath.isEmpty()) {
			bgImagePathEdit->setText(filePath);
			// 保存并应用
			saveCanvasBackgroundImage(filePath, s_canvasBackgroundImageMode);
			if (m_view) {
				m_view->setBackgroundImage(filePath,
					static_cast<WarRoomView::ImageMode>(s_canvasBackgroundImageMode));
			}
		}
	});

	// 平铺 / 拉伸 按钮互斥切换
	QObject::connect(bgImageTiledBtn, &QPushButton::clicked, [this, bgImageTiledBtn, bgImageStretchBtn, bgImagePathEdit]() {
		s_canvasBackgroundImageMode = 0;
		bgImageTiledBtn->setChecked(true);
		bgImageStretchBtn->setChecked(false);
		QString path = bgImagePathEdit->text();
		if (!path.isEmpty()) {
			saveCanvasBackgroundImage(path, 0);
			if (m_view) {
				m_view->setBackgroundImage(path, WarRoomView::ImageMode::Tiled);
			}
		}
		else {
			// 无图片时只保存模式
			saveCanvasBackgroundImage(s_canvasBackgroundImagePath, 0);
		}
	});

	QObject::connect(bgImageStretchBtn, &QPushButton::clicked, [this, bgImageTiledBtn, bgImageStretchBtn, bgImagePathEdit]() {
		s_canvasBackgroundImageMode = 1;
		bgImageTiledBtn->setChecked(false);
		bgImageStretchBtn->setChecked(true);
		QString path = bgImagePathEdit->text();
		if (!path.isEmpty()) {
			saveCanvasBackgroundImage(path, 1);
			if (m_view) {
				m_view->setBackgroundImage(path, WarRoomView::ImageMode::Stretch);
			}
		}
		else {
			saveCanvasBackgroundImage(s_canvasBackgroundImagePath, 1);
		}
	});

	generalLayout->addSpacing(6);

	// ---- 画布操作模式 ----
	// 通用三选一按钮组样式（用 QButtonGroup 管理互斥）
	auto makeTriButtonGroup = [generalPage](
		const QString& title,
		const std::vector<QString>& labels,
		int currentIdx,
		std::function<void(int)> onSelected) {
			QLabel* sectionTitle = new QLabel(title, generalPage);
			sectionTitle->setStyleSheet("color: #AAAAAA; font-size: 11px; font-weight: bold;");
			generalPage->layout()->addWidget(sectionTitle);

			QWidget* row = new QWidget(generalPage);
			auto* rowLayout = new QHBoxLayout(row);
			rowLayout->setContentsMargins(0, 0, 0, 0);
			rowLayout->setSpacing(8);

			QString btnStyle =
				"QPushButton {"
				"	background-color: #2A2A2A;"
				"	color: #CCCCCC;"
				"	border: 2px solid #555555;"
				"	border-radius: 4px;"
				"	font-size: 11px;"
				"	padding: 4px 12px;"
				"}"
				"QPushButton:checked {"
				"	background-color: #2A5A8A;"
				"	border-color: #FFFFFF;"
				"	color: #FFFFFF;"
				"}"
				"QPushButton:hover {"
				"	border-color: #888888;"
				"}";

			// QButtonGroup 自动管理互斥；父对象为 row，生命周期随 row
			auto* btnGroup = new QButtonGroup(row);
			btnGroup->setExclusive(true);

			for (size_t i = 0; i < labels.size(); ++i) {
				QPushButton* btn = new QPushButton(labels[i], row);
				btn->setCheckable(true);
				btn->setStyleSheet(btnStyle);
				if ((int)i == currentIdx) btn->setChecked(true);
				btnGroup->addButton(btn, (int)i);
				rowLayout->addWidget(btn);

				// 单按钮 clicked：直接回调索引（避免依赖 QButtonGroup::buttonClicked 的重载版本）
				QObject::connect(btn, &QPushButton::clicked, [onSelected, i]() { onSelected((int)i); });
			}

			rowLayout->addStretch();
			generalPage->layout()->addWidget(row);
		};

	// 拖动画布：鼠标中键 / 空格加左键 / 二者皆可
	makeTriButtonGroup("拖动画布", { "鼠标中键", "空格 + 左键", "二者皆可" },
		s_canvasPanMode,
		[this](int idx) {
			saveCanvasPanMode(idx);
			if (m_view) {
				m_view->setPanMode(static_cast<WarRoomView::PanMode>(idx));
			}
		});

	// 移动画布：方向键 / WASD / 二者皆可
	makeTriButtonGroup("移动画布", { "上下左右", "WASD", "二者皆可" },
		s_canvasKeyPanMode,
		[this](int idx) {
			saveCanvasKeyPanMode(idx);
			if (m_view) {
				m_view->setKeyPanMode(static_cast<WarRoomView::KeyPanMode>(idx));
			}
		});

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

			// 设置按钮：仅当模组实现 hasSettings() 返回 true 时启用
			warroom::NodeMod* modPtr = warroom::ModManager::instance().getMod(kv.first);
			bool canSettings = (modPtr && modPtr->hasSettings());
			QPushButton* settingsBtn = new QPushButton("设置", modItem);
			settingsBtn->setFixedSize(50, 24);
			settingsBtn->setEnabled(canSettings);
			if (canSettings) {
				settingsBtn->setStyleSheet(R"(
					QPushButton {
						background-color: #3A5A8A;
						color: #FFFFFF;
						border: none;
						padding: 2px 8px;
						border-radius: 3px;
					}
					QPushButton:hover { background-color: #4A6A9A; }
				)");
				QObject::connect(settingsBtn, &QPushButton::clicked, [modPtr, dialog]() {
					QDialog subDlg(dialog);
					subDlg.setWindowTitle("模组设置");
					// 深色主题，与主设置对话框协调
					subDlg.setStyleSheet(R"(
						QDialog { background-color: #2A2A2A; }
						QLabel { color: #DDDDDD; }
						QComboBox {
							background-color: #3A3A3A;
							color: #FFFFFF;
							border: 1px solid #4A4A4A;
							padding: 2px 6px;
							border-radius: 2px;
						}
						QComboBox QAbstractItemView {
							background-color: #3A3A3A;
							color: #FFFFFF;
							selection-background-color: #4A6A9A;
						}
						QPushButton {
							background-color: #3A5A8A;
							color: #FFFFFF;
							border: none;
							padding: 4px 12px;
							border-radius: 3px;
						}
						QPushButton:hover { background-color: #4A6A9A; }
					)");
					auto* l = new QVBoxLayout(&subDlg);
					l->setContentsMargins(12, 12, 12, 12);
					QWidget* w = modPtr->createSettingsWidget(&subDlg);
					if (w) l->addWidget(w);
					auto* bb = new QDialogButtonBox(
						QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &subDlg);
					l->addWidget(bb);
					QObject::connect(bb, &QDialogButtonBox::accepted, [&]() {
						modPtr->saveSettings(w);
						subDlg.accept();
					});
					QObject::connect(bb, &QDialogButtonBox::rejected, &subDlg, &QDialog::reject);
					subDlg.exec();
				});
			}
			else {
				settingsBtn->setStyleSheet(R"(
					QPushButton {
						background-color: #3A3A3A;
						color: #888888;
						border: none;
						padding: 2px 8px;
						border-radius: 3px;
					}
				)");
			}
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

	// ---- 键位设置页面 ----
	auto* keyPage = new QWidget(dialog);
	auto* keyLayout = new QVBoxLayout(keyPage);
	keyLayout->setContentsMargins(16, 16, 16, 16);
	keyLayout->setSpacing(8);

	auto& keyReg = warroom::KeyBindingRegistry::instance();
	auto categories = keyReg.getAllCategories();
	for (const auto& cat : categories) {
		QLabel* catLabel = new QLabel(QString::fromStdString(cat), keyPage);
		catLabel->setStyleSheet("color: #AAAAAA; font-weight: bold; font-size: 13px; margin-top: 8px;");
		keyLayout->addWidget(catLabel);

		auto bindings = keyReg.getBindingsByCategory(cat);
		for (const auto& binding : bindings) {
			auto* rowWidget = new QWidget(keyPage);
			auto* rowLayout = new QHBoxLayout(rowWidget);
			rowLayout->setContentsMargins(8, 4, 8, 4);
			rowLayout->setSpacing(8);

			QLabel* nameLabel = new QLabel(QString::fromStdString(binding.name), rowWidget);
			nameLabel->setStyleSheet("color: #FFFFFF;");
			nameLabel->setFixedWidth(100);
			rowLayout->addWidget(nameLabel);

			QLabel* keyLabel = new QLabel(QString::fromStdString(binding.currentKey), rowWidget);
			keyLabel->setStyleSheet("color: #88CCFF; font-family: monospace;");
			keyLabel->setFixedWidth(120);
			keyLabel->setAlignment(Qt::AlignCenter);
			rowLayout->addWidget(keyLabel);

			QPushButton* changeBtn = new QPushButton("修改", rowWidget);
			changeBtn->setFixedSize(50, 24);
			changeBtn->setStyleSheet(
				"QPushButton {"
				"	background-color: #444444;"
				"	color: #FFFFFF;"
				"	border: none;"
				"	border-radius: 3px;"
				"	padding: 2px 8px;"
				"}"
				"QPushButton:hover {"
				"	background-color: #555555;"
				"}"
				"QPushButton:pressed {"
				"	background-color: #333333;"
				"}"
			);
			rowLayout->addWidget(changeBtn);

			QPushButton* resetBtn = new QPushButton("默认", rowWidget);
			resetBtn->setFixedSize(50, 24);
			resetBtn->setStyleSheet(
				"QPushButton {"
				"	background-color: #555555;"
				"	color: #CCCCCC;"
				"	border: none;"
				"	border-radius: 3px;"
				"	padding: 2px 8px;"
				"}"
				"QPushButton:hover {"
				"	background-color: #666666;"
				"}"
			);
			rowLayout->addWidget(resetBtn);

			keyLayout->addWidget(rowWidget);

			connect(changeBtn, &QPushButton::clicked, [this, &keyReg, binding, keyLabel]() {
				QDialog keyDialog(this);
				keyDialog.setWindowTitle("设置快捷键");
				keyDialog.setFixedSize(320, 160);
				keyDialog.setStyleSheet(
					"QDialog { background-color: #2D2D2D; }"
					"QLabel { color: #FFFFFF; }"
				);

				auto* dlayout = new QVBoxLayout(&keyDialog);
				dlayout->setContentsMargins(20, 20, 20, 20);

				QLabel* hint = new QLabel("请点击下方输入框并按下快捷键", &keyDialog);
				hint->setAlignment(Qt::AlignCenter);
				dlayout->addWidget(hint);

				QKeySequenceEdit* keyEdit = new QKeySequenceEdit(&keyDialog);
				keyEdit->setStyleSheet(
					"QKeySequenceEdit {"
					"  color: #88CCFF;"
					"  font-family: monospace;"
					"  font-size: 16px;"
					"  background-color: #1E1E1E;"
					"  border: 1px solid #444444;"
					"  border-radius: 4px;"
					"  padding: 8px;"
					"}"
				);
				dlayout->addWidget(keyEdit);

				auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &keyDialog);
				btnBox->setStyleSheet(
					"QPushButton {"
					"  background-color: #444444;"
					"  color: #FFFFFF;"
					"  border: none;"
					"  border-radius: 3px;"
					"  padding: 4px 12px;"
					"}"
					"QPushButton:hover { background-color: #555555; }"
				);
				dlayout->addWidget(btnBox);
				QObject::connect(btnBox, &QDialogButtonBox::accepted, &keyDialog, &QDialog::accept);
				QObject::connect(btnBox, &QDialogButtonBox::rejected, &keyDialog, &QDialog::reject);

				if (keyDialog.exec() == QDialog::Accepted) {
					QString newKey = keyEdit->keySequence().toString(QKeySequence::PortableText);
					if (!newKey.isEmpty()) {
						keyReg.setKey(binding.id, newKey.toStdString());
						keyLabel->setText(newKey);
					}
				}
			});

			connect(resetBtn, &QPushButton::clicked, [&keyReg, binding, keyLabel]() {
				keyReg.resetToDefault(binding.id);
				keyLabel->setText(QString::fromStdString(keyReg.getKey(binding.id)));
			});
		}
	}

	keyLayout->addStretch();

	// 添加页面到 TabWidget
	tabWidget->addTab(generalPage, "通用设置");
	tabWidget->addTab(modPage, "模组设置");
	tabWidget->addTab(keyPage, "键位设置");

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
	QObject::connect(okBtn, &QPushButton::clicked, this, [dialog, this]() {
		saveKeyBindings();
		dialog->accept();
	});

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
// 背景样式配置（点阵/网格）：加载 / 保存 / 获取
// ============================================================
int WarRoomMainWindow::getCanvasBackgroundStyle()
{
	return s_canvasBackgroundStyle;
}

void WarRoomMainWindow::saveCanvasBackgroundStyle(int style)
{
	s_canvasBackgroundStyle = style;
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	settings.setValue("backgroundStyle", style);
	settings.endGroup();
	settings.sync();
}

void WarRoomMainWindow::loadCanvasBackgroundStyle()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) {
		s_canvasBackgroundStyle = 0; // 默认点阵
		return;
	}
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	int style = settings.value("backgroundStyle", 0).toInt();
	settings.endGroup();

	if (style >= 0 && style <= 2) {
		s_canvasBackgroundStyle = style; // 0=Dots, 1=Grid, 2=Image
	}
	else {
		s_canvasBackgroundStyle = 0; // 默认点阵
	}
}

// ============================================================
// 画布拖动 / 移动模式配置：加载 / 保存 / 获取
// ============================================================
int WarRoomMainWindow::getCanvasPanMode() { return s_canvasPanMode; }

void WarRoomMainWindow::saveCanvasPanMode(int mode)
{
	s_canvasPanMode = mode;
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	settings.setValue("panMode", mode);
	settings.endGroup();
	settings.sync();
}

void WarRoomMainWindow::loadCanvasPanMode()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) { s_canvasPanMode = 0; return; }
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	int mode = settings.value("panMode", 0).toInt();
	settings.endGroup();
	s_canvasPanMode = (mode >= 0 && mode <= 2) ? mode : 0;
}

int WarRoomMainWindow::getCanvasKeyPanMode() { return s_canvasKeyPanMode; }

void WarRoomMainWindow::saveCanvasKeyPanMode(int mode)
{
	s_canvasKeyPanMode = mode;
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	settings.setValue("keyPanMode", mode);
	settings.endGroup();
	settings.sync();
}

void WarRoomMainWindow::loadCanvasKeyPanMode()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) { s_canvasKeyPanMode = 0; return; }
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	int mode = settings.value("keyPanMode", 0).toInt();
	settings.endGroup();
	s_canvasKeyPanMode = (mode >= 0 && mode <= 2) ? mode : 0;
}

// ============================================================
// 图片背景配置：加载 / 保存 / 获取
// ============================================================
QString WarRoomMainWindow::getCanvasBackgroundImagePath()
{
	return s_canvasBackgroundImagePath;
}

int WarRoomMainWindow::getCanvasBackgroundImageMode()
{
	return s_canvasBackgroundImageMode;
}

void WarRoomMainWindow::saveCanvasBackgroundImage(const QString& path, int mode)
{
	s_canvasBackgroundImagePath = path;
	s_canvasBackgroundImageMode = mode;
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	settings.setValue("backgroundImagePath", path);
	settings.setValue("backgroundImageMode", mode); // 0=Tiled, 1=Stretch
	settings.endGroup();
	settings.sync();
}

void WarRoomMainWindow::loadCanvasBackgroundImage()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) {
		s_canvasBackgroundImagePath.clear();
		s_canvasBackgroundImageMode = 0;
		return;
	}
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	QString path = settings.value("backgroundImagePath", "").toString();
	int mode = settings.value("backgroundImageMode", 0).toInt();
	settings.endGroup();

	s_canvasBackgroundImagePath = path;
	if (mode == 0 || mode == 1) {
		s_canvasBackgroundImageMode = mode;
	}
	else {
		s_canvasBackgroundImageMode = 0; // 默认平铺
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

// ============================================================
// 节点文本颜色配置：加载 / 保存 / 获取
// ============================================================
QColor WarRoomMainWindow::getNodeTextColor()
{
	return s_nodeTextColor;
}

void WarRoomMainWindow::saveNodeTextColor(const QColor& color)
{
	s_nodeTextColor = color;
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	settings.setValue("nodeTextColor", color.name(QColor::HexArgb));
	settings.endGroup();
	settings.sync();
}

void WarRoomMainWindow::loadNodeTextColor()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) {
		s_nodeTextColor = QColor(240, 240, 240);
		return;
	}
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	QString colorName = settings.value("nodeTextColor", "#F0F0F0").toString();
	settings.endGroup();

	QColor loadedColor(colorName);
	if (loadedColor.isValid()) {
		s_nodeTextColor = loadedColor;
	}
	else {
		s_nodeTextColor = QColor(240, 240, 240);
	}
}

// ============================================================
// 连线颜色配置：加载 / 保存 / 获取
// ============================================================
QColor WarRoomMainWindow::getLinkColor()
{
	return s_linkColor;
}

void WarRoomMainWindow::saveLinkColor(const QColor& color)
{
	s_linkColor = color;
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	settings.setValue("linkColor", color.name(QColor::HexArgb));
	settings.endGroup();
	settings.sync();
}

void WarRoomMainWindow::loadLinkColor()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) {
		s_linkColor = QColor(150, 150, 150);
		return;
	}
	QSettings settings(cfgFile, QSettings::IniFormat);
	settings.beginGroup("Canvas");
	QString colorName = settings.value("linkColor", "#969696").toString();
	settings.endGroup();

	QColor loadedColor(colorName);
	if (loadedColor.isValid()) {
		s_linkColor = loadedColor;
	}
	else {
		s_linkColor = QColor(150, 150, 150);
	}
}

// ============================================================
// 通知所有节点刷新文本颜色
// ============================================================
void WarRoomMainWindow::refreshAllNodeTextColors()
{
	for (QGraphicsItem* item : m_scene->items()) {
		if (auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item)) {
			nodeItem->refreshTextColor(s_nodeTextColor);
		}
	}
}

// ============================================================
// 通知所有连线刷新颜色
// ============================================================
void WarRoomMainWindow::refreshAllLinkColors()
{
	for (QGraphicsItem* item : m_scene->items()) {
		if (auto* linkItem = dynamic_cast<LinkGraphicsItem*>(item)) {
			linkItem->refreshLinkColor(s_linkColor);
		}
	}
}

// ============================================================
// 键位配置：注册 / 加载 / 保存 / 应用
// ============================================================
void WarRoomMainWindow::registerAppKeyBindings()
{
	auto& reg = warroom::KeyBindingRegistry::instance();
	reg.registerBindings({
		warroom::KeyBinding("app.new", "新建", "应用", "Ctrl+N"),
		warroom::KeyBinding("app.open", "打开", "应用", "Ctrl+O"),
		warroom::KeyBinding("app.save", "保存", "应用", "Ctrl+S"),
		warroom::KeyBinding("app.save_as", "另存为", "应用", "Ctrl+Shift+S"),
		warroom::KeyBinding("app.exit", "退出", "应用", "Ctrl+Q"),
		warroom::KeyBinding("app.undo", "撤销", "应用", "Ctrl+Z"),
		warroom::KeyBinding("app.redo", "重做", "应用", "Ctrl+Y"),
		warroom::KeyBinding("app.delete", "删除", "应用", "Delete"),
	});
}

void WarRoomMainWindow::loadKeyBindings()
{
	QString cfgFile = getConfigFilePath();
	if (!QFile::exists(cfgFile)) return;
	QSettings settings(cfgFile, QSettings::IniFormat);
	warroom::KeyBindingRegistry::instance().loadFromSettings(settings);
	applyKeyBindings();
}

void WarRoomMainWindow::saveKeyBindings()
{
	QString cfgFile = getConfigFilePath();
	QSettings settings(cfgFile, QSettings::IniFormat);
	warroom::KeyBindingRegistry::instance().saveToSettings(settings);
	applyKeyBindings();
}

void WarRoomMainWindow::applyKeyBindings()
{
	auto& reg = warroom::KeyBindingRegistry::instance();
	qDebug() << "[KEYDBG] applyKeyBindings called";
	// 注意：qDebug 移到 if 外，便于诊断 action 是否被创建
	qDebug() << "[KEYDBG] action pointers:"
		<< "new=" << m_newAction
		<< "open=" << m_openAction
		<< "save=" << m_saveAction
		<< "saveAs=" << m_saveAsAction
		<< "exit=" << m_exitAction
		<< "undo=" << m_undoAction
		<< "redo=" << m_redoAction
		<< "delete=" << m_deleteAction;
	if (m_newAction) {
		m_newAction->setShortcut(reg.getQKeySequence("app.new"));
		qDebug() << "[KEYDBG] app.new shortcut =" << m_newAction->shortcut();
	}
	if (m_openAction) {
		m_openAction->setShortcut(reg.getQKeySequence("app.open"));
		qDebug() << "[KEYDBG] app.open shortcut =" << m_openAction->shortcut();
	}
	if (m_saveAction) {
		m_saveAction->setShortcut(reg.getQKeySequence("app.save"));
		qDebug() << "[KEYDBG] app.save shortcut =" << m_saveAction->shortcut();
	}
	if (m_saveAsAction) {
		m_saveAsAction->setShortcut(reg.getQKeySequence("app.save_as"));
		qDebug() << "[KEYDBG] app.save_as shortcut =" << m_saveAsAction->shortcut();
	}
	if (m_exitAction) {
		m_exitAction->setShortcut(reg.getQKeySequence("app.exit"));
		qDebug() << "[KEYDBG] app.exit shortcut =" << m_exitAction->shortcut();
	}
	if (m_undoAction) {
		m_undoAction->setShortcut(reg.getQKeySequence("app.undo"));
		qDebug() << "[KEYDBG] app.undo shortcut =" << m_undoAction->shortcut();
	}
	if (m_redoAction) {
		m_redoAction->setShortcut(reg.getQKeySequence("app.redo"));
		qDebug() << "[KEYDBG] app.redo shortcut =" << m_redoAction->shortcut();
	}
	if (m_deleteAction) {
		m_deleteAction->setShortcut(reg.getQKeySequence("app.delete"));
		qDebug() << "[KEYDBG] app.delete shortcut =" << m_deleteAction->shortcut();
	}
	qDebug() << "[KEYDBG] m_view =" << m_view << " | actions on m_view =" << (m_view ? m_view->actions().size() : -1);
}