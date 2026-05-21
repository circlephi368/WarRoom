#include "LinkGraphicsItem.h"
#include "war_room_model.h"

#include <cmath>

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------
LinkGraphicsItem::LinkGraphicsItem(const warroom::Uuid& linkId,
    const warroom::WarRoomModel& model,
    QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_linkId(linkId)
    , m_model(model)
{
    setFlags(ItemIsSelectable);
    setZValue(-1);
    updatePositions();
}

// ---------------------------------------------------------------------------
// boundingRect
// ---------------------------------------------------------------------------
QRectF LinkGraphicsItem::boundingRect() const
{
    QRectF rect(m_startPoint, m_endPoint);

    // 把路径点也纳入包围盒
    for (const auto& wp : m_wayPoints) {
        rect = rect.united(QRectF(wp, QSizeF(1, 1)));
    }

    rect = rect.normalized();
    return rect.adjusted(-60, -60, 60, 60);
}

// ---------------------------------------------------------------------------
// 刷新端点
// ---------------------------------------------------------------------------
void LinkGraphicsItem::updatePositions()
{
    prepareGeometryChange();

    const warroom::WarLink* link = m_model.getLink(m_linkId);
    if (!link) {
        m_startPoint = {};
        m_endPoint = {};
        m_wayPoints.clear();
        return;
    }

    // 解析起点
    warroom::Point2D s = link->start_anchor->resolvePosition(m_model);
    m_startPoint = QPointF(s.x, s.y);

    // 解析路径点
    m_wayPoints.clear();
    for (const auto& anchor : link->waypoints) {
        warroom::Point2D p = anchor->resolvePosition(m_model);
        m_wayPoints.append(QPointF(p.x, p.y));
    }

    // 解析终点
    warroom::Point2D e = link->end_anchor->resolvePosition(m_model);
    m_endPoint = QPointF(e.x, e.y);
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
void LinkGraphicsItem::paint(QPainter* painter,
    const QStyleOptionGraphicsItem*,
    QWidget*)
{
    if (m_startPoint == m_endPoint && m_wayPoints.isEmpty())
        return;

    const warroom::WarLink* link = m_model.getLink(m_linkId);
    if (!link)
        return;

    painter->setRenderHint(QPainter::Antialiasing);

    // ----- 颜色 -----
    QColor baseColor = link->color.empty()
        ? defaultColorForType(link->type)
        : QColor(QString::fromStdString(link->color));

    // ----- 画笔 -----
    QPen pen(baseColor, 2.0);
    switch (link->type) {
    case warroom::LinkType::Inspiration:
        pen.setStyle(Qt::DashLine);
        break;
    case warroom::LinkType::Contradiction:
        pen.setWidth(3);
        break;
    default:
        break;
    }

    if (isSelected()) {
        pen.setColor(highlightColor());
        pen.setWidth(3);
    }

    painter->setPen(pen);

    // ----- 构建路径 -----
    QPainterPath path;
    buildPath(path);

    painter->drawPath(path);

    // ----- 箭头 -----
    if (path.elementCount() >= 2) {
        // 沿路径取靠近终点的一点作为箭尾方向
        double t = (m_wayPoints.isEmpty()) ? 0.95 : 0.98;
        QPointF nearTip = path.pointAtPercent(t);
        QPointF tip = path.currentPosition();  // 路径终点

        QPointF p1, p2;
        arrowHeadPoints(tip, nearTip, p1, p2);

        painter->setBrush(pen.color());
        painter->setPen(Qt::NoPen);
        painter->drawPolygon(QPolygonF() << tip << p1 << p2);
    }

    // ----- 标签 -----
    if (!link->label.empty()) {
        QPointF mid = path.pointAtPercent(0.5);
        painter->setPen(QColor("#333333"));
        painter->setFont(QFont("Microsoft YaHei", 9));
        painter->drawText(mid + QPointF(5, -5),
            QString::fromStdString(link->label));
    }
}

// ---------------------------------------------------------------------------
// 构建路径：有路径点时沿路径点走折线，无路径点时画贝塞尔
// ---------------------------------------------------------------------------
void LinkGraphicsItem::buildPath(QPainterPath& path) const
{
    if (m_wayPoints.isEmpty()) {
        // 无路径点 → 贝塞尔曲线
        BezierControl ctrl = computeControlPoints(m_startPoint, m_endPoint);
        path.moveTo(m_startPoint);
        path.cubicTo(ctrl.ctrl1, ctrl.ctrl2, m_endPoint);
    }
    else {
        // 有路径点 → 逐段直线（平滑升级可改此处）
        path.moveTo(m_startPoint);
        for (const auto& wp : m_wayPoints) {
            path.lineTo(wp);
        }
        path.lineTo(m_endPoint);
    }
}

// ---------------------------------------------------------------------------
// 类型 → 默认颜色
// ---------------------------------------------------------------------------
QColor LinkGraphicsItem::defaultColorForType(warroom::LinkType type)
{
    switch (type) {
    case warroom::LinkType::Dependency:     return QColor("#3498db");
    case warroom::LinkType::Contradiction:  return QColor("#e67e22");
    case warroom::LinkType::Transformation: return QColor("#2ecc71");
    case warroom::LinkType::Inspiration:    return QColor("#9b59b6");
    case warroom::LinkType::Negation:       return QColor("#e74c3c");
    case warroom::LinkType::UsingMethod:    return QColor("#1abc9c");
    }
    return QColor("#aaaaaa");
}

// ---------------------------------------------------------------------------
// 贝塞尔控制点
// ---------------------------------------------------------------------------
LinkGraphicsItem::BezierControl
LinkGraphicsItem::computeControlPoints(QPointF from, QPointF to)
{
    QPointF delta = to - from;
    double dist = std::hypot(delta.x(), delta.y());
    double offset = std::min(dist * 0.4, 120.0);

    return { QPointF(from.x() + offset, from.y()),
             QPointF(to.x() - offset, to.y()) };
}

// ---------------------------------------------------------------------------
// 箭头三角形
// ---------------------------------------------------------------------------
void LinkGraphicsItem::arrowHeadPoints(const QPointF& tip,
    const QPointF& from,
    QPointF& p1,
    QPointF& p2)
{
    double angle = std::atan2(tip.y() - from.y(), tip.x() - from.x());
    constexpr double len = 10.0;
    constexpr double spread = 0.45;

    p1 = QPointF(tip.x() - len * std::cos(angle - spread),
        tip.y() - len * std::sin(angle - spread));
    p2 = QPointF(tip.x() - len * std::cos(angle + spread),
        tip.y() - len * std::sin(angle + spread));
}