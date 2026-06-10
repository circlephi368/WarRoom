#pragma once

// 标准库
#include <string>

// Qt 头文件
#include <qevent.h>
#include <qmainwindow.h>
#include <qpoint.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qwidget.h>
#include <qfont.h>
#include <qcolor.h>

// 项目核心
#include "core/command/undo_manager.h"
#include "core/warroom/war_node.h"
#include "core/warroom/war_room_model.h"
#include "core/warroom/warroom_types.h"

// 项目 UI
#include "ui_myQtproject.h"
#include "warroomview.h"
#include "ui/CustomTitleBar.h"
#include "ui/CustomSidebar.h"
#include "ui/WindowHelper.h"

// 前向声明
class QGraphicsScene;
class QGraphicsView;
class QPushButton;
class NodeGraphicsItem;

// 节点上下文信息（用于删除撤销）
struct NodeContext {
    warroom::Uuid nodeId;
    warroom::WarNode savedNode;
    warroom::Uuid parentId;
    int index;
};

class WarRoomMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 构造与析构
    WarRoomMainWindow(QWidget* parent = nullptr);
    ~WarRoomMainWindow();

    // 连线操作
    void deleteLink(const warroom::Uuid& linkId);
    void createLinkBetweenNodes(const std::string& fromId, int fromEdge,
        const std::string& toId, int toEdge);
    // 拖拽锚点到空白处：创建新节点并自动连线
    void createNodeAndLink(const std::string& fromId, int fromEdge, QPointF scenePos);

    // ---- 字体配置（公开，供 NodeGraphicsItem/LinkGraphicsItem 调用）----
    static QFont getNodeFont();
    void saveNodeFont(const QFont& font);
    static void loadNodeFont();

    // 刷新所有节点字体的接口（字体变更后调用）
    void refreshAllNodeFonts();

    // ---- 背景颜色配置（公开，供 WarRoomView 调用）----
    static QColor getCanvasBackgroundColor();
    static void saveCanvasBackgroundColor(const QColor& color);
    static void loadCanvasBackgroundColor();
    static QColor s_canvasBackgroundColor;

private slots:
    // 文件操作
    void onNewAction();         // 新建
    void onSaveAction();        // 保存
    void onSaveAsAction();      // 另存为
    void onLoadAction();        // 打开

    // 导入导出
    void onExportJson();        // 导出 JSON
    void onImportJson();        // 导入 JSON

    // 编辑操作
    void onUndo();              // 撤销
    void onRedo();              // 重做

    // 视图操作
    void onResetView();         // 重置视图

    // 节点操作
    void onNodeSelectedForZBoost(const std::string& nodeId);  // 提升节点 Z 值

    // 帮助
    void onAbout();             // 关于

private:
    // ---- 场景初始化 ----
    void setupScene();          // 初始化场景和视图
    void populateFromModel();   // 从模型填充场景（含测试数据）
    void rebuildFromModel();    // 从模型重建场景（不含测试数据）
    void clearScene();          // 清空场景

    // ---- UI 组件设置 ----
    void setupMenuBar();        // 设置菜单栏
    void setupToolBar();        // 设置工具栏
    void setupSceneConnections(); // 设置场景信号连接

    // ---- 文件检查 ----
    bool maybeSave();           // 检查是否需要保存

    // ---- 节点信号连接 ----
    void connectNodeSignals(NodeGraphicsItem* item);

    // ---- 拖拽回写 ----
    void onNodeMoved(const std::string& nodeId, float newX, float newY);
    void onNodeMoveFinished(const std::string& nodeId,
        float oldX, float oldY, float newX, float newY);

    // ---- 尺寸变更回写 ----
    void onNodeSizeChanged(const std::string& nodeId, float newWidth, float newHeight);
    void onNodeResizeFinished(const std::string& nodeId,
        float oldWidth, float oldHeight,
        float newWidth, float newHeight);

    // ---- 键盘事件 ----
    void keyPressEvent(QKeyEvent* event) override;  // Ctrl+Z / Ctrl+Y

    // ---- 视图同步 ----
    void syncAllItemsFromModel();   // 全量同步图形项与模型
    void refreshLinks();            // 刷新所有连线位置
    void refreshAllLinksZValue();   // 刷新所有连线的 Z 值

    // ---- 节点查找 ----
    std::string findTopmostNodeAtPoint(QPointF scenePos, const std::string& excludeId);

    // ---- 父子关系 ----
    void reparentNode(const std::string& nodeId, const std::string& newParentId);

    // ---- 子树位置更新 ----
    void updateSubtreePositionRecursive(const std::string& nodeId);

    // ---- Z 值更新 ----
    void updateSubtreeZValues(const std::string& nodeId);  // 递归更新子树 Z 值

    // ---- 包围盒 ----
    void rebuildAllBoundingRects();  // 全量刷新自定义包围盒

    // ---- 命令执行 ----
    void executeCommand(std::unique_ptr<warroom::Command> cmd);

    // ---- 节点操作辅助 ----
    NodeContext captureNodeContext(const warroom::Uuid& nodeId);
    void deleteSelectedNode();
    void addNodeAtPosition(QPointF scenePos);
    void addNodeAtPosition(QPointF scenePos, const warroom::Uuid& parentId);
    // 多模态节点入口：在指定位置创建一个绑定了 modId 主模组的节点
    void addNodeWithMod(QPointF scenePos,
        const warroom::Uuid& parentId, const std::string& modId);
    void editNode(const std::string& nodeId);

    // ---- 右键菜单 ----
    void contextMenuEvent(QContextMenuEvent* event) override;

    // ---- 主窗口ui ----
    void setupCustomUi();              // 构建自绘 UI 框架
    void setupTitleBar();              // 设置标题栏信号连接
    void setupSidebar();               // 设置侧边栏信号连接
    void buildSidebarData(std::vector<TreeNodeData>& outNodes) const;
    
    
    // ---- 成员变量 ----
    Ui::myQtprojectClass ui;
    QGraphicsScene* m_scene = nullptr;
    WarRoomView* m_view = nullptr;

    warroom::UndoManager m_undoManager;
    warroom::WarRoomModel m_model;

    QHash<QString, NodeGraphicsItem*> m_nodeItems;  // 节点 ID -> 图形项映射

    QString m_currentFilePath;                      // 当前文件路径
    std::string m_currentEditingNodeId;             // 当前编辑中的节点 ID

    // ---- 自绘 UI 组件 ----
    QWidget* m_centralContainer = nullptr;   // 中央容器
    CustomTitleBar* m_titleBar = nullptr;    // 自绘标题栏
    CustomSidebar* m_sidebar = nullptr;      // 左侧边栏
    QWidget* m_canvasArea = nullptr;         // 画布区域（容纳 WarRoomView）
    QWidget* m_settingsCorner = nullptr;     // 左下角设置按钮容器
    QPushButton* m_settingsButton = nullptr; // 左下角设置按钮
    QPushButton* m_sidebarCollapseBtn = nullptr; // 侧边栏折叠按钮

    // ---- 配置管理 ----
    // 返回配置目录（独立于可执行文件，用于存储基础信息）
    static QString getConfigDir();
    static QString getConfigFilePath();

    // 读写上次打开的文件路径
    static QString readLastOpenFilePath();
    static void writeLastOpenFilePath(const QString& path);

    // 从文件路径加载
    void loadFromFilePath(const QString& path);

    // 设置按钮槽
    void onSettingsClicked();

    // 窗口拖拽状态
    bool m_windowDragging = false;
    QPoint m_dragGlobalStart;

    // 全局字体配置（静态，所有节点共享）
    static QFont s_nodeFont;

protected:
    // 事件过滤器（处理场景点击以退出编辑）
    bool eventFilter(QObject* watched, QEvent* event) override;

    // 窗口拖拽和缩放（无边框窗口必需）
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

    // 标题栏信号处理
    void onTitleBarMinimize();
    void onTitleBarMaximize();
    void onTitleBarClose();

    // 侧边栏信号处理
    void onSidebarNodeFocused(const std::string& nodeId);
    void onSidebarNodeDoubleClicked(const std::string& nodeId);
    void onToggleSidebarCollapse();  // 切换侧边栏折叠/展开

    // 侧边栏数据刷新
    void refreshSidebarTree();

    void showEvent(QShowEvent* event) override;
};