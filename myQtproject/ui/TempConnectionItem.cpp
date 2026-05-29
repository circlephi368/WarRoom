// TempConnectionItem.cpp
#include "TempConnectionItem.h"
#include <cmath>

TempConnectionItem::TempConnectionItem(const QPointF& start, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_start(start)
    , m_end(start)
{
    setZValue(1000);  // 在最上层
    setAcceptHoverEvents(false);
}

void TempConnectionItem::setEndPoint(const QPointF& end)
{
    prepareGeometryChange();
    m_end = end;
    m_hasSnap = false;
    update();
}

void TempConnectionItem::setSnapPoint(const QPointF& snap)
{
    prepareGeometryChange();
    m_snapPoint = snap;
    m_hasSnap = true;
    update();
}

QRectF TempConnectionItem::boundingRect() const
{
    QPointF end = m_hasSnap ? m_snapPoint : m_end;
    QRectF rect(m_start, end);
    return rect.normalized().adjusted(-10, -10, 10, 10);
}

void TempConnectionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    QPointF end = m_hasSnap ? m_snapPoint : m_end;

    painter->setRenderHint(QPainter::Antialiasing);

    // 虚线 + 半透明
    QPen pen(QColor(100, 150, 255, 200), 2);
    pen.setStyle(Qt::DashLine);
    painter->setPen(pen);

    // 贝塞尔曲线（和正式连线风格一致）
    QPointF delta = end - m_start;
    double offset = std::min(std::hypot(delta.x(), delta.y()) * 0.3, 80.0);
    QPointF ctrl1(m_start.x() + offset, m_start.y());
    QPointF ctrl2(end.x() - offset, end.y());

    QPainterPath path;
    path.moveTo(m_start);
    path.cubicTo(ctrl1, ctrl2, end);
    painter->drawPath(path);

    // 如果吸附中，绘制一个小圆形指示
    if (m_hasSnap) {
        painter->setBrush(QColor(100, 150, 255, 100));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(end, 8, 8);
    }
}