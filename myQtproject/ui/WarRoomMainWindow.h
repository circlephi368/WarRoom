#pragma once
#include "core/warroom/war_node.h"
#include "core/warroom/warroom_types.h"
#include <qevent.h>
#include <qmainwindow.h>
#include <qpoint.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qwidget.h>
#include <string>

#include "ui_myQtproject.h"
#include "core/command/undo_manager.h"
#include "core/warroom/war_room_model.h"
#include "warroomview.h"
class QGraphicsScene;
class QGraphicsView;
class NodeGraphicsItem;
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
	WarRoomMainWindow(QWidget* parent = nullptr);
	~WarRoomMainWindow();

	void deleteLink(const warroom::Uuid& linkId);
	void createLinkBetweenNodes(const std::string& fromId, int fromEdge, const std::string& toId, int toEdge);

private slots:
	void onExportJson();
	void onImportJson();
	void onNewAction();      // 新建
	void onSaveAction();     // 保存
	void onSaveAsAction();   // 另存为
	void onLoadAction();     // 打开
	void onUndo();           // 撤销
	void onRedo();           // 重做
	void onResetView();      // 重置视图
	void onAbout();          // 关于
private:

	void setupScene();
	void populateFromModel();
	void setupMenuBar();     // 设置菜单栏
	void clearScene();       // 清空场景
	
	bool maybeSave();        // 检查是否需要保存
	// 拖拽回写
	void onNodeMoved(const std::string& nodeId, float newX, float newY);
	void onNodeMoveFinished(const std::string& nodeId, float oldX, float oldY, float newX, float newY);  // 新增
	void keyPressEvent(QKeyEvent* event) override;  // 新增，用于 Ctrl+Z / Ctrl+Y

	void syncAllItemsFromModel();

	void refreshLinks();

	// 统一的命令执行包装，自动触发视图同步
	void executeCommand(std::unique_ptr<warroom::Command> cmd);

	warroom::UndoManager m_undoManager;
	Ui::myQtprojectClass ui;
	QGraphicsScene* m_scene = nullptr;
	WarRoomView* m_view = nullptr;

	// 节点图形项映射表：Uuid -> NodeGraphicsItem*
	QHash<QString, NodeGraphicsItem*> m_nodeItems;

	warroom::WarRoomModel m_model;   // 提升为成员

	//辅助函数，获取删除节点所需的信息
	NodeContext captureNodeContext(const warroom::Uuid& nodeId);
	void deleteSelectedNode();
	void addNodeAtPosition(QPointF scenePos);
	void editNode(const std::string& nodeId);
	void contextMenuEvent(QContextMenuEvent* event);
	QString m_currentFilePath;

	void rebuildFromModel();
	void setupToolBar();
};