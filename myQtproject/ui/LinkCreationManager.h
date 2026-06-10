// LinkCreationManager.h
#pragma once

#include <QObject>
#include <QPointF>
#include <memory>

class ConnectionAnchor;
class TempConnectionItem;
class QGraphicsScene;
class WarRoomMainWindow;

class LinkCreationManager : public QObject
{
    Q_OBJECT

public:
    static LinkCreationManager& instance();

    void setMainWindow(WarRoomMainWindow* mainWindow) { m_mainWindow = mainWindow; }
    void setScene(QGraphicsScene* scene);

    // 由 ConnectionAnchor 调用
    void startConnection(ConnectionAnchor* anchor, const QPointF& scenePos);
    void endConnection(const QPointF& scenePos);

    // 由场景事件过滤器调用
    void updateTempConnection(const QPointF& scenePos);

    void cancelConnection();

    void cleanup();

    void shutdown() {
        cleanup();
        m_scene = nullptr;
        m_mainWindow = nullptr;
    }

    ConnectionAnchor* findSnapAnchor(const QPointF& scenePos, float radius);

    bool isConnecting() const { return m_isConnecting; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    LinkCreationManager() = default;
    ~LinkCreationManager();
    void showAllAnchors(bool show);
    ConnectionAnchor* m_startAnchor = nullptr;
    ConnectionAnchor* m_snapAnchor = nullptr;
    std::unique_ptr<TempConnectionItem> m_tempItem;
    QGraphicsScene* m_scene = nullptr;
    WarRoomMainWindow* m_mainWindow = nullptr;
    bool m_isConnecting = false;
};