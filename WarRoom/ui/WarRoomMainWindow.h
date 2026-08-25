#pragma once

// 标准库
#include <string>

// Qt 头文件
#include <qevent.h>
#include <qmainwindow.h>
#include <qmimedata.h>
#include <qpoint.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qwidget.h>
#include <qfont.h>
#include <qcolor.h>
#include <QResizeEvent>

// 项目核心
#include "core/command/undo_manager.h"
#include "core/warroom/war_node.h"
#include "core/warroom/war_room_model.h"
#include "core/warroom/warroom_types.h"

// 项目 UI
#include "ui_myQtproject.h"
#include "warroomview.h"
#include "HighlightOverlay.h"
#include "ui/CustomTitleBar.h"
#include "ui/CustomSidebar.h"
#include "ui/TodoSidebar.h"
#include "ui/WindowHelper.h"

// 前向声明
class QGraphicsScene;
class QGraphicsView;
class QPushButton;
class NodeGraphicsItem;
class CameraAnimator;
class LinkGraphicsItem;

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
	void onLinkLabelEditRequested(const warroom::Uuid& linkId);
	void onLinkColorChangeRequested(const warroom::Uuid& linkId, const QString& newColor);
	void setupLinkItemConnections(LinkGraphicsItem* linkItem);
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

	// 背景样式（0=Dots, 1=Grid, 2=Image）
	static int getCanvasBackgroundStyle();
	static void saveCanvasBackgroundStyle(int style);
	static void loadCanvasBackgroundStyle();
	static int s_canvasBackgroundStyle;

	// 图片背景配置
	static QString getCanvasBackgroundImagePath();
	static int getCanvasBackgroundImageMode();     // 0=Tiled, 1=Stretch
	static void saveCanvasBackgroundImage(const QString& path, int mode);
	static void loadCanvasBackgroundImage();
	static QString s_canvasBackgroundImagePath;
	static int s_canvasBackgroundImageMode;

	// ---- 节点文本颜色配置 ----
	static QColor getNodeTextColor();
	static void saveNodeTextColor(const QColor& color);
	static void loadNodeTextColor();
	static QColor s_nodeTextColor;

	// ---- 连线颜色配置 ----
	static QColor getLinkColor();
	static void saveLinkColor(const QColor& color);
	static void loadLinkColor();
	static QColor s_linkColor;

	// ---- 画布拖动 / 移动模式配置 ----
	// panMode: 0=中键, 1=空格+左键, 2=二者皆可
	// keyPanMode: 0=方向键, 1=WASD, 2=二者皆可
	static int getCanvasPanMode();
	static void saveCanvasPanMode(int mode);
	static void loadCanvasPanMode();
	static int s_canvasPanMode;
	static int getCanvasKeyPanMode();
	static void saveCanvasKeyPanMode(int mode);
	static void loadCanvasKeyPanMode();
	static int s_canvasKeyPanMode;

	// ---- 键位配置 ----
	void registerAppKeyBindings();
	void loadKeyBindings();
	void saveKeyBindings();
	void applyKeyBindings();

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

	void onToggleReadOnly();

	// 视图操作
	void onResetView();         // 重置视图

	// ---- 节点操作
	void onNodeSelectedForZBoost(const std::string& nodeId);  // 提升节点 Z 值

	// ---- 焦点指示器更新 ----
	void updateFocusOnNode(NodeGraphicsItem* item);
	void updateFocusOnCanvas();
	void updateFocusNoFocus(const QString& reason);
	void updateCanvasAreaForOverlay();

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

	// ---- 相机动画 ----
	// 平滑聚焦到指定节点（用于侧栏点击聚焦）
	void focusNodeAnimated(NodeGraphicsItem* item);
	// 用户开始拖动画布时中止相机动画
	void abortCameraAnimation();

	// ---- 节点高亮提示 ----
	// 显示从侧边栏项指向节点的箭头+高亮（fromLeft=true左侧栏, false右侧栏）
	void showNodeHighlight(const std::string& nodeId, bool fromLeft);

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
	// 拖放创建节点：遍历主模组，第一个能处理 mime 数据的模组创建节点
	void addNodeFromDrop(QPointF scenePos, const QMimeData* mimeData);
	void editNode(const std::string& nodeId);

	// ---- 颜色刷新 ----
	void refreshAllNodeTextColors();
	void refreshAllLinkColors();

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
	CameraAnimator* m_cameraAnimator = nullptr;  // 相机动画器
	HighlightOverlay* m_highlightOverlay = nullptr;  // 高亮覆盖层

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
	QPushButton* m_readOnlyBtn = nullptr; // 只读模式切换按钮
	TodoSidebar* m_todoSidebar = nullptr;   // 右侧待办侧边栏
	QPushButton* m_todoToggleBtn = nullptr; // 待办侧边栏切换按钮

	// ---- 主程序快捷键 Action（用于键位设置实时更新） ----
	QAction* m_newAction = nullptr;
	QAction* m_openAction = nullptr;
	QAction* m_saveAction = nullptr;
	QAction* m_saveAsAction = nullptr;
	QAction* m_exitAction = nullptr;
	QAction* m_undoAction = nullptr;
	QAction* m_redoAction = nullptr;
	QAction* m_deleteAction = nullptr;

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

	// 待办侧边栏
	void setupTodoSidebar();
	void refreshTodoSidebar();
	void onTodoItemFocused(const std::string& nodeId);
	void onTodoItemDoubleClicked(const std::string& nodeId);
	void onTodoItemToggled(const std::string& nodeId, bool done);
	void onToggleTodoSidebar();

	void showEvent(QShowEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
};