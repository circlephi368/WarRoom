// warroomview.h
#pragma once

#include <QGraphicsView>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include "core/warroom/war_room_model.h"

class WarRoomView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit WarRoomView(QGraphicsScene* scene, QWidget* parent = nullptr)
        : QGraphicsView(scene, parent)
    {
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::RubberBandDrag);   // 左键框选
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorUnderMouse);
        setCursor(Qt::ArrowCursor);
    }

    warroom::Point2D getViewCenter() const {
        QPointF center = mapToScene(viewport()->rect().center());
        return { static_cast<float>(center.x()), static_cast<float>(center.y()) };
    }

    float getZoomLevel() const { return m_currentScale; }

    void setViewCenter(const warroom::Point2D& center, float zoom) {
        m_currentScale = zoom;
        resetTransform();
        scale(zoom, zoom);
        centerOn(center.x, center.y);
    }

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
        if (event->modifiers() & Qt::ControlModifier) {
            // Ctrl + 滚轮：缩放
            const double scaleFactor = 1.15;
            double factor = (event->angleDelta().y() > 0) ? scaleFactor : (1.0 / scaleFactor);
            double newScale = m_currentScale * factor;
            if (newScale < 0.1 || newScale > 5.0) return;
            m_currentScale = newScale;
            scale(factor, factor);
        }
        else {
            // 普通滚轮：滚动视图（不缩放）
            QGraphicsView::wheelEvent(event);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::MiddleButton) {
            m_middleButtonPressed = true;
            m_lastPanPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;  // 不传递给基类，避免干扰
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_middleButtonPressed) {
            QPoint delta = event->pos() - m_lastPanPos;
            if (!delta.isNull()) {
                horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
                verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
                m_lastPanPos = event->pos();
            }
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::MiddleButton && m_middleButtonPressed) {
            m_middleButtonPressed = false;
            setCursor(Qt::ArrowCursor);
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

private:
    double m_currentScale = 1.0;
    bool m_middleButtonPressed = false;
    QPoint m_lastPanPos;
};