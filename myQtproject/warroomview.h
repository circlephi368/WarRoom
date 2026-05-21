// warroomview.h
#pragma once

#include <QGraphicsView>
#include <QWheelEvent>

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