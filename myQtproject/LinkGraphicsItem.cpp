#include "LinkGraphicsItem.h"
#include "war_room_model.h"
LinkGraphicsItem::LinkGraphicsItem(const std::string& linkId,
    const warroom::WarRoomModel& model,
    QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_linkId(linkId)
    , m_model(model)
{
    setFlags(ItemIsSelectable);
    setZValue(-1);  // 连线在节点下方
    updatePositions();
}

QRectF LinkGraphicsItem::boundingRect() const
{
    // 包围盒略大于起止点形成的矩形
    QRectF rect(m_startPoint, m_endPoint);
    return rect.normalized().adjusted(-50, -50, 50, 50);
}

void LinkGraphicsItem::updatePositions()
{
    prepareGeometryChange();

    const warroom::WarLink* link = m_model.getLink(m_linkId);
    if (!link) return;

    warroom::Point2D start = link->start_anchor->resolvePosition(m_model);
    warroom::Point2D end = link->end_anchor->resolvePosition(m_model);

    m_startPoint = QPointF(start.x, start.y);
    m_endPoint = QPointF(end.x, end.y);
}

void LinkGraphicsItem::paint(QPainter* painter,
    const QStyleOptionGraphicsItem*,
    QWidget*)
{
    if (m_startPoint == m_endPoint) return;

    const warroom::WarLink* link = m_model.getLink(m_linkId);
    if (!link) return;

    painter->setRenderHint(QPainter::Antialiasing);

    // 颜色
    QColor color = colorForType("");
    if (!link->color.empty())
        color = QColor(QString::fromStdString(link->color));

    // 线型
    QPen pen(color, 2.0);
    switch (link->type) {
    case warroom::LinkType::Inspiration:
        pen.setStyle(Qt::DashLine);
        break;
    case warroom::LinkType::Contradiction:
        pen.setWidth(3);
        break;
    case warroom::LinkType::Negation:
        pen.setColor(QColor("#e74c3c"));
        pen.setWidth(2);
        break;
    default:
        break;
    }

    if (isSelected()) {
        pen.setColor(QColor(100, 180, 255));
        pen.setWidth(3);
    }

    painter->setPen(pen);

    // 计算控制点（贝塞尔曲线）
    QPointF delta = m_endPoint - m_startPoint;
    double dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
    double offset = std::min(dist * 0.4, 120.0);

    QPointF ctrl1(m_startPoint.x() + offset, m_startPoint.y());
    QPointF ctrl2(m_endPoint.x() - offset, m_endPoint.y());

    // 路径
    QPainterPath path;
    path.moveTo(m_startPoint);
    path.cubicTo(ctrl1, ctrl2, m_endPoint);
    painter->drawPath(path);

    // 箭头
    QPointF tip = m_endPoint;
    // 在曲线上取离终点很近的一点作为箭尾方向
    double t = 0.98;
    double mt = 1.0 - t;
    QPointF nearTip(
        mt * mt * mt * m_startPoint.x() + 3 * mt * mt * t * ctrl1.x() + 3 * mt * t * t * ctrl2.x() + t * t * t * m_endPoint.x(),
        mt * mt * mt * m_startPoint.y() + 3 * mt * mt * t * ctrl1.y() + 3 * mt * t * t * ctrl2.y() + t * t * t * m_endPoint.y()
    );

    QPointF p1, p2;
    arrowHead(tip, nearTip, p1, p2);

    painter->setBrush(color);
    painter->setPen(Qt::NoPen);
    painter->drawPolygon(QPolygonF() << tip << p1 << p2);

    // 标签
    if (!link->label.empty()) {
        QPointF mid = path.pointAtPercent(0.5);
        painter->setPen(QPen(QColor("#333333")));
        painter->setFont(QFont("Microsoft YaHei", 9));
        painter->drawText(mid + QPointF(5, -5),
            QString::fromStdString(link->label));
    }
}

QColor LinkGraphicsItem::colorForType(const std::string&) {
    return QColor("#aaaaaa");
}

void LinkGraphicsItem::arrowHead(const QPointF& tip, const QPointF& from,
    QPointF& p1, QPointF& p2)
{
    double angle = std::atan2(tip.y() - from.y(), tip.x() - from.x());
    double arrowLen = 10.0;
    double arrowAngle = 0.45;  // ~26度

    p1 = QPointF(
        tip.x() - arrowLen * std::cos(angle - arrowAngle),
        tip.y() - arrowLen * std::sin(angle - arrowAngle)
    );
    p2 = QPointF(
        tip.x() - arrowLen * std::cos(angle + arrowAngle),
        tip.y() - arrowLen * std::sin(angle + arrowAngle)
    );
}