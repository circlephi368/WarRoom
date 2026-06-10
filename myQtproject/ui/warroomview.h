// warroomview.h
#pragma once

#include <QGraphicsView>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QPainter>
#include <QPaintEvent>
#include <QColor>
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

        // FullViewportUpdate：强制每帧完整重绘整个视口，避免拖拽拖影和局部更新白边
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorUnderMouse);
        setCursor(Qt::ArrowCursor);

        // 关键：QGraphicsView 继承自 QFrame，默认有 StyledPanel+Sunken 边框
        // 必须显式移除，否则视口外缘会绘制 1-2px 的浅色/白色边框
        setFrameShape(QFrame::NoFrame);
        setFrameShadow(QFrame::Plain);
        setLineWidth(0);
        setMidLineWidth(0);

        // 默认深色画布背景
        m_backgroundColor = QColor(30, 30, 30);
        setBackgroundBrush(m_backgroundColor);

        // 用样式表设置 viewport 本身的背景色和无边框，覆盖系统默认的绘制
        // 同时设置 QGraphicsView 为透明，让 viewport 的绘制接管
        setStyleSheet(
            "QGraphicsView {"
            "   background: transparent;"
            "   border: none;"
            "   padding: 0px;"
            "}"
            "QGraphicsView > QWidget {"
            "   background-color: #1E1E1E;"
            "   border: none;"
            "   padding: 0px;"
            "}"
        );

        // 禁止 viewport 自动填充背景（由 drawBackground + 样式表接管）
        viewport()->setAutoFillBackground(false);
        viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
    }

    // 设置背景颜色
    void setBackgroundColor(const QColor& color) {
        m_backgroundColor = color;
        setBackgroundBrush(color);

        // 同步更新样式表中 viewport 的背景色，确保边缘与画布一致
        QString style = QString(
            "QGraphicsView {"
            "   background: transparent;"
            "   border: none;"
            "   padding: 0px;"
            "}"
            "QGraphicsView > QWidget {"
            "   background-color: %1;"
            "   border: none;"
            "   padding: 0px;"
            "}"
        ).arg(color.name());
        setStyleSheet(style);

        update();
    }

    // 获取当前背景颜色
    QColor getBackgroundColor() const {
        return m_backgroundColor;
    }

    // 绘制深色画布背景 + 网格点
    void drawBackground(QPainter* painter, const QRectF& rect) override
    {
        // 1) 纯色深色背景（使用动态背景颜色）
        painter->fillRect(rect, m_backgroundColor);

        // 2) 稀疏网格点（浅色），随视图缩放自动变化密度
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);

        const int smallStep = 40;     // 小网格点间距（场景坐标）
        const int bigStep = 200;      // 大网格点间距（场景坐标）

        QPen smallPen(QColor(60, 60, 60, 180), 1);
        QPen bigPen(QColor(85, 85, 85, 220), 1);

        // 计算当前缩放系数，避免点过密
        qreal scale = transform().m11();
        if (scale < 0.25) {
            painter->restore();
            return; // 缩放过小则不画点
        }

        // 扩展绘制区域避免边界闪烁
        qreal margin = bigStep;
        QRectF area = rect.adjusted(-margin, -margin, margin, margin);

        int startX = static_cast<int>(std::floor(area.left() / smallStep)) * smallStep;
        int startY = static_cast<int>(std::floor(area.top() / smallStep)) * smallStep;
        int endX = static_cast<int>(std::ceil(area.right() / smallStep));
        int endY = static_cast<int>(std::ceil(area.bottom() / smallStep));

        painter->setPen(smallPen);
        for (int x = startX; x <= endX; x += smallStep) {
            for (int y = startY; y <= endY; y += smallStep) {
                if ((x % bigStep == 0) && (y % bigStep == 0)) continue;
                painter->drawPoint(QPoint(x, y));
            }
        }

        painter->setPen(bigPen);
        startX = static_cast<int>(std::floor(area.left() / bigStep)) * bigStep;
        startY = static_cast<int>(std::floor(area.top() / bigStep)) * bigStep;
        endX = static_cast<int>(std::ceil(area.right() / bigStep));
        endY = static_cast<int>(std::ceil(area.bottom() / bigStep));
        for (int x = startX; x <= endX; x += bigStep) {
            for (int y = startY; y <= endY; y += bigStep) {
                painter->drawPoint(QPoint(x, y));
            }
        }

        painter->restore();
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
    QColor m_backgroundColor;  // 动态背景颜色
};