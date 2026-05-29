#include "LinkGraphicsItem.h"
#include "core/warroom/war_room_model.h"
#include "WarRoomMainWindow.h"
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QGraphicsScene>
#include <qgraphicsview.h>
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
    return m_hitArea.boundingRect();
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

    // 更新命中区域
    updateHitArea();
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

// shape() 返回精确的命中路径
QPainterPath LinkGraphicsItem::shape() const
{
    return m_hitArea;
}

// ---------------------------------------------------------------------------
// 构建路径：有路径点时沿路径点走折线，无路径点时画贝塞尔
// ---------------------------------------------------------------------------
void LinkGraphicsItem::buildPath(QPainterPath& path) const
{
    if (m_wayPoints.isEmpty()) {
        // 查询出入 edge
        const warroom::WarLink* link = m_model.getLink(m_linkId);
        int fromEdge = -1;
        int toEdge = -1;
        if (link) {
            if (auto* na = dynamic_cast<const warroom::NodeAnchor*>(link->start_anchor.get()))
                fromEdge = na->edge;
            if (auto* na = dynamic_cast<const warroom::NodeAnchor*>(link->end_anchor.get()))
                toEdge = na->edge;
        }

        BezierControl ctrl = computeControlPoints(m_startPoint, fromEdge,
            m_endPoint, toEdge);
        path.moveTo(m_startPoint);
        path.cubicTo(ctrl.ctrl1, ctrl.ctrl2, m_endPoint);
    }
    else {
        // 有路径点，暂时保持原逻辑
        path.moveTo(m_startPoint);
        for (const auto& wp : m_wayPoints) {
            path.lineTo(wp);
        }
        path.lineTo(m_endPoint);
    }
}

QPointF LinkGraphicsItem::edgeDirection(int edge)
{
    switch (edge) {
    case 0: return QPointF(1.0, 0.0);   // 右
    case 1: return QPointF(0.0, 1.0);   // 下
    case 2: return QPointF(-1.0, 0.0);  // 左
    case 3: return QPointF(0.0, -1.0);  // 上
    default: return QPointF(1.0, 0.0);  // 默认右
    }
}

void LinkGraphicsItem::updateHitArea()
{
    QPainterPath path;
    buildPath(path);

    QPainterPathStroker stroker;
    stroker.setWidth(10.0);   // 线宽 2 + 两侧各 4px 容差，总共约 10px 的命中宽度
    m_hitArea = stroker.createStroke(path);
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
LinkGraphicsItem::computeControlPoints(QPointF from, int fromEdge,
    QPointF to, int toEdge)
{
    QPointF delta = to - from;
    double dist = std::hypot(delta.x(), delta.y());
    double offset = std::min(dist * 0.4, 120.0);

    QPointF fromDir = edgeDirection(fromEdge);
    QPointF toDir = edgeDirection(toEdge);

    // 如果某端 edge 未指定，回退到连线方向的水平投影
    if (fromDir.isNull()) {
        fromDir = QPointF(delta.x() > 0 ? 1.0 : -1.0, 0.0);
    }
    if (toDir.isNull()) {
        toDir = QPointF(delta.x() > 0 ? -1.0 : 1.0, 0.0);
    }

    QPointF ctrl1 = from + fromDir * offset;
    QPointF ctrl2 = to + toDir * offset;   // 注意：toDir 指向从节点中心向外，所以这里是加，不是减
    return { ctrl1, ctrl2 };
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
void LinkGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // 清除其他选中，选中当前连线
        if (scene()) {
            for (auto* item : scene()->selectedItems()) {
                item->setSelected(false);
            }
        }
        setSelected(true);
        event->accept();
    }
    QGraphicsObject::mousePressEvent(event);
}

void LinkGraphicsItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    QMenu menu;
    QAction* deleteAction = menu.addAction("删除连线");

    QAction* selectedAction = menu.exec(event->screenPos());

    if (selectedAction == deleteAction) {
        // 通知 MainWindow 删除连线
        if (auto* mainWindow = qobject_cast<WarRoomMainWindow*>(scene()->views().first()->parent())) {
            mainWindow->deleteLink(m_linkId);
        }
    }
}