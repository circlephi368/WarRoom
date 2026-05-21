#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_myQtproject.h"
#include "war_room_model.h"
#include "undo_manager.h"
class QGraphicsScene;
class QGraphicsView;
class NodeGraphicsItem;

class WarRoomMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    WarRoomMainWindow(QWidget* parent = nullptr);
    ~WarRoomMainWindow();

private:
    void setupScene();
    void populateFromModel();

    // 拖拽回写
    void onNodeMoved(const std::string& nodeId, float newX, float newY);
    void onNodeMoveFinished(const std::string& nodeId, float oldX, float oldY, float newX, float newY);  // 新增
    void keyPressEvent(QKeyEvent* event) override;  // 新增，用于 Ctrl+Z / Ctrl+Y

    void syncAllItemsFromModel();

    void refreshLinks();

    warroom::UndoManager m_undoManager;  // 新增
    Ui::myQtprojectClass ui;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;

    warroom::WarRoomModel m_model;   // 提升为成员
};