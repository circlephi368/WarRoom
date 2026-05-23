// warroomview.h
#pragma once

#include <QGraphicsView>
#include <QWheelEvent>
#include "war_room_model.h"

class WarRoomView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit WarRoomView(QGraphicsScene* scene, QWidget* parent = nullptr)
        : QGraphicsView(scene, parent)
    {
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
        setTransformationAnchor(QGraphicsView::NoAnchor);
        setResizeAnchor(QGraphicsView::NoAnchor);
    }
    // 获取当前视图状态（用于保存）
    warroom::Point2D getViewCenter() const {
        QPointF center = mapToScene(viewport()->rect().center());
        return { static_cast<float>(center.x()), static_cast<float>(center.y()) };
    }

    float getZoomLevel() const { return m_currentScale; }

    // 恢复视图状态
    void setViewCenter(const warroom::Point2D& center, float zoom) {
        m_currentScale = zoom;
        QPointF target(center.x, center.y);
        QPointF viewCenter = mapToScene(viewport()->rect().center());
        QPointF delta = target - viewCenter;

        resetTransform();
        scale(zoom, zoom);
        translate(delta.x(), delta.y());
    }

    // 保存/恢复的便捷方法
    void saveViewState(warroom::Point2D& pos, float& zoom) {
        pos = getViewCenter();
        zoom = getZoomLevel();
    }

    void restoreViewState(const warroom::Point2D& pos, float zoom) {
        setViewCenter(pos, zoom);
    }
protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const double scaleFactor = 1.15;
        double factor = (event->angleDelta().y() > 0) ? scaleFactor : (1.0 / scaleFactor);

        double newScale = m_currentScale * factor;
        if (newScale < 0.1 || newScale > 5.0)
            return;

        m_currentScale = newScale;

        QPointF scenePos = mapToScene(event->position().toPoint());
        scale(factor, factor);
        QPointF delta = mapToScene(event->position().toPoint()) - scenePos;
        translate(delta.x(), delta.y());
    }

private:
    double m_currentScale = 1.0;
};