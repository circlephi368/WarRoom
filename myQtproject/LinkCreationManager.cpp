// LinkCreationManager.cpp
#include "LinkCreationManager.h"
#include "ConnectionAnchor.h"
#include "TempConnectionItem.h"
#include "NodeGraphicsItem.h"
#include "WarRoomMainWindow.h"
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QDebug>

LinkCreationManager& LinkCreationManager::instance()
{
    static LinkCreationManager mgr;
    return mgr;
}

void LinkCreationManager::setScene(QGraphicsScene* scene)
{
    if (m_scene) {
        m_scene->removeEventFilter(this);
    }

    m_scene = scene;

    if (m_scene) {
        m_scene->installEventFilter(this);
        qDebug() << "Event filter installed on scene";
    }
}

bool LinkCreationManager::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_isConnecting || !m_scene || watched != m_scene) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::GraphicsSceneMouseMove) {
        auto* mouseEvent = static_cast<QGraphicsSceneMouseEvent*>(event);
        updateTempConnection(mouseEvent->scenePos());
        return true;  // 拦截事件
    }

    if (event->type() == QEvent::GraphicsSceneMouseRelease) {
        auto* mouseEvent = static_cast<QGraphicsSceneMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            endConnection(mouseEvent->scenePos());
            return true;
        }
    }

    return QObject::eventFilter(watched, event);
}

void LinkCreationManager::startConnection(ConnectionAnchor* anchor, const QPointF& scenePos)
{
    qDebug() << "=== startConnection ===";

    if (!m_scene) {
        qDebug() << "ERROR: m_scene is null!";
        return;
    }

    m_startAnchor = anchor;
    m_isConnecting = true;

    // 显示所有节点的锚点
    showAllAnchors(true);

    // 隐藏起始锚点（避免吸附到自己）
    m_startAnchor->hide();

    if (m_tempItem) {
        m_scene->removeItem(m_tempItem.get());
        m_tempItem.reset();
    }

    m_tempItem = std::make_unique<TempConnectionItem>(scenePos);
    m_tempItem->setZValue(9999);
    m_scene->addItem(m_tempItem.get());

    updateTempConnection(scenePos);

    qDebug() << "  Connection started, showing all anchors";
}

void LinkCreationManager::updateTempConnection(const QPointF& scenePos)
{
    if (!m_tempItem) {
        qDebug() << "updateTempConnection: m_tempItem is null!";
        return;
    }

    // 寻找可吸附的锚点
    ConnectionAnchor* snapAnchor = findSnapAnchor(scenePos, 25.0f);

    if (snapAnchor && snapAnchor != m_startAnchor) {
        QPointF snapPos = snapAnchor->scenePos();
        m_tempItem->setSnapPoint(snapPos);
        m_snapAnchor = snapAnchor;
        // 可选：改变光标样式表示可吸附
        m_scene->views().first()->setCursor(Qt::PointingHandCursor);
    }
    else {
        m_tempItem->setEndPoint(scenePos);
        m_snapAnchor = nullptr;
        m_scene->views().first()->setCursor(Qt::CrossCursor);
    }
}
void LinkCreationManager::showAllAnchors(bool show)
{
    if (!m_scene) return;

    // 遍历场景中的所有节点，显示/隐藏它们的锚点
    QList<QGraphicsItem*> items = m_scene->items();
    for (QGraphicsItem* item : items) {
        if (auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item)) {
            for (auto* anchor : nodeItem->anchors()) {
                if (show) {
                    anchor->show();
                    // 拖拽时高亮其他节点的锚点
                    anchor->setBrush(QColor(100, 150, 255, 200));
                }
                else {
                    anchor->hide();
                    anchor->setBrush(QColor(100, 150, 255, 180));  // 恢复原色
                }
            }
        }
    }
}

void LinkCreationManager::endConnection(const QPointF& scenePos)
{
    qDebug() << "=== endConnection ===";

    if (!m_tempItem || !m_startAnchor || !m_mainWindow) {
        cleanup();
        return;
    }

    // 检查是否有效目标
    ConnectionAnchor* targetAnchor = findSnapAnchor(scenePos, 25.0f);
    if (targetAnchor && targetAnchor != m_startAnchor) {
        NodeGraphicsItem* fromNode = m_startAnchor->parentNode();
        NodeGraphicsItem* toNode = targetAnchor->parentNode();

        if (fromNode && toNode) {
            qDebug() << "  Creating link from" << fromNode->nodeId().c_str()
                << "to" << toNode->nodeId().c_str();
            m_mainWindow->createLinkBetweenNodes(
                fromNode->nodeId(),
                toNode->nodeId()
            );
        }
    }

    cleanup();
}

void LinkCreationManager::cancelConnection()
{
    qDebug() << "cancelConnection";
    cleanup();
}

void LinkCreationManager::cleanup()
{
    // 恢复所有锚点的显示状态
    showAllAnchors(false);

    if (m_tempItem && m_scene) {
        m_scene->removeItem(m_tempItem.get());
    }
    m_tempItem.reset();
    m_startAnchor = nullptr;
    m_snapAnchor = nullptr;
    m_isConnecting = false;

    if (m_scene && !m_scene->views().isEmpty()) {
        m_scene->views().first()->setCursor(Qt::ArrowCursor);
    }
}

ConnectionAnchor* LinkCreationManager::findSnapAnchor(const QPointF& scenePos, float radius)
{
    if (!m_scene) return nullptr;

    QRectF searchRect(scenePos.x() - radius, scenePos.y() - radius,
        radius * 2, radius * 2);

    QList<QGraphicsItem*> items = m_scene->items(searchRect);

    for (QGraphicsItem* item : items) {
        if (auto* anchor = dynamic_cast<ConnectionAnchor*>(item)) {
            // 跳过正在拖拽的起始锚点
            if (anchor == m_startAnchor) continue;

            QPointF anchorPos = anchor->scenePos();
            float dist = static_cast<float>(std::hypot(anchorPos.x() - scenePos.x(),
                anchorPos.y() - scenePos.y()));
            if (dist <= radius) {
                return anchor;
            }
        }
    }
    return nullptr;
}