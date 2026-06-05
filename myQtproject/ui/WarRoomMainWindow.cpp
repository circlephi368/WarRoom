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

// ============================================================================
// 构造与析构
// ============================================================================

WarRoomMainWindow::WarRoomMainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);
    setupMenuBar();
    setupScene();
    setWindowTitle("War Room");
    populateFromModel();
}

WarRoomMainWindow::~WarRoomMainWindow() {}

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
        auto* linkItem = new LinkGraphicsItem(newLinkId, m_model);
        m_scene->addItem(linkItem);
    }
}

void WarRoomMainWindow::deleteLink(const warroom::Uuid& linkId)
{
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

// ============================================================================
// 私有槽 - 文件操作
// ============================================================================

void WarRoomMainWindow::onNewAction()
{
    if (!maybeSave()) return;

    warroom::WarRoomModel newModel;
    m_model = std::move(newModel);
    m_currentFilePath.clear();

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

    if (m_model.saveToFile(path.toStdString())) {
        m_currentFilePath = path;
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

    if (m_model.saveToFile(fullPath)) {
        m_currentFilePath = QString::fromStdString(fullPath);
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

    warroom::WarRoomModel newModel;
    if (newModel.loadFromFile(path.toStdString())) {
        m_model = std::move(newModel);
        m_currentFilePath = path;

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
    m_scene->setSceneRect(-5000, -5000, 10000, 10000);
    m_view = new WarRoomView(m_scene, this);
    setCentralWidget(m_view);

    LinkCreationManager::instance().setMainWindow(this);
    LinkCreationManager::instance().setScene(m_scene);

    setupSceneConnections();
}

void WarRoomMainWindow::populateFromModel()
{
    using warroom::WarNode;
    using warroom::NodeKind;
    using warroom::Uuid;
    using warroom::WarLink;
    using warroom::LinkType;

    // ---- 创建测试数据 ----
    WarNode group = WarNode::makeGroup("主攻方向", 0, -100);
    group.color = "#80e74c3c";
    Uuid groupId = m_model.addNode(std::move(group), m_model.getDocumentRootId());

    WarNode leaf1 = WarNode::makeLeaf("数据库查询优化", -200, 50);
    leaf1.tags = { "进行中" };
    leaf1.color = "#80e74c3c";
    leaf1.full_text = "test full_text";
    m_model.addNode(std::move(leaf1), groupId);

    WarNode leaf2 = WarNode::makeLeaf("缓存策略调整", 50, 50);
    leaf2.tags = { "未探索" };
    m_model.addNode(std::move(leaf2), groupId);

    WarNode leaf3 = WarNode::makeLeaf("索引重建方案", 300, 50);
    leaf3.tags = { "失败" };
    leaf3.width = 300;
    m_model.addNode(std::move(leaf3), groupId);

    WarNode standalone = WarNode::makeLeaf("网络延迟排查", 500, -100);
    m_model.addNode(std::move(standalone), m_model.getDocumentRootId());

    WarNode standalone2 = WarNode::makeLeaf("网络延迟排查2", 600, -200);
    m_model.addNode(std::move(standalone2), m_model.getDocumentRootId());

    // ---- 查找已创建节点的 ID ----
    Uuid dbNode, cacheNode, indexNode, netNode;
    for (const auto& [id, node] : m_model.getAllNodes()) {
        if (node.title == "数据库查询优化") dbNode = id;
        else if (node.title == "缓存策略调整") cacheNode = id;
        else if (node.title == "索引重建方案") indexNode = id;
        else if (node.title == "网络延迟排查") netNode = id;
    }

    // ---- 创建测试连线 ----
    if (!dbNode.empty() && !cacheNode.empty()) {
        auto link = warroom::WarLink::makeNodeToNode(dbNode, 0, cacheNode, 0,
            warroom::LinkType::Dependency);
        link.label = "依赖";
        link.color = "#f39c12";
        m_model.addLink(std::move(link));
    }
    if (!dbNode.empty() && !indexNode.empty()) {
        auto link = warroom::WarLink::makeNodeToNode(dbNode, 1, indexNode, 2,
            warroom::LinkType::Transformation);
        link.label = "转化为";
        link.color = "#3498db";
        m_model.addLink(std::move(link));
    }
    if (!dbNode.empty() && !netNode.empty()) {
        auto link = warroom::WarLink::makeNodeToNode(dbNode, 2, netNode, 3,
            warroom::LinkType::Inspiration);
        link.label = "启发";
        link.color = "#9b59b6";
        m_model.addLink(std::move(link));
    }

    // ---- 从模型读取连线，创建 LinkGraphicsItem ----
    for (const auto& [linkId, link] : m_model.getAllLinks()) {
        auto* linkItem = new LinkGraphicsItem(linkId, m_model);
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
        auto* linkItem = new LinkGraphicsItem(linkId, m_model);
        m_scene->addItem(linkItem);
    }

    refreshAllLinksZValue();
}

void WarRoomMainWindow::clearScene()
{
    m_nodeItems.clear();
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

NodeContext WarRoomMainWindow::captureNodeContext(const warroom::Uuid& nodeId)
{
    NodeContext ctx;
    ctx.nodeId = nodeId;

    const warroom::WarNode* node = m_model.getNode(nodeId);
    if (node) {
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
    }
    return ctx;
}

void WarRoomMainWindow::deleteSelectedNode()
{
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    std::vector<std::string> nodeIdsToDelete;

    for (QGraphicsItem* item : selected) {
        auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item);
        if (!nodeItem) continue;

        std::string nodeId = nodeItem->nodeId();
        if (nodeId == m_model.getDocumentRootId()) continue;

        nodeIdsToDelete.push_back(nodeId);
    }

    if (nodeIdsToDelete.empty()) return;

    for (const auto& nodeId : nodeIdsToDelete) {
        NodeContext ctx = captureNodeContext(nodeId);

        auto cmd = std::make_unique<warroom::DeleteNodeCommand>(
            ctx.nodeId, ctx.savedNode, ctx.parentId, ctx.index);
        executeCommand(std::move(cmd));
    }
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

        menu.addSeparator();
        warroom::Uuid parentId = nodeItem->nodeId();
        menu.addAction("添加子节点", [this, scenePos, parentId]() {
            addNodeAtPosition(scenePos, parentId);
            });
    }
    else {
        menu.addAction("添加节点", this, [this, scenePos]() {
            addNodeAtPosition(scenePos);
            });
    }

    menu.exec(event->globalPos());
}

// ============================================================================
// 保护方法 - 事件过滤器
// ============================================================================

bool WarRoomMainWindow::eventFilter(QObject* watched, QEvent* event)
{
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