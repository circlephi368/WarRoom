#pragma once
#include <QMenu>
#include <QGraphicsObject>
#include <QGraphicsSceneContextMenuEvent>
#include <QPainter>
#include <QPen>

#include "warroom_types.h"
#include "war_link.h"

// 前置声明
namespace warroom {
    class WarRoomModel;
}
class WarRoomMainWindow;
/**
 * @brief 连线图形项，负责将 WarLink 数据渲染为贝塞尔曲线。
 *
 * 连线颜色优先使用 link 的自定义颜色，否则根据 LinkType 回落默认色。
 * 控制点计算独立封装，便于后续升级为更智能的路由算法。
 */
class LinkGraphicsItem : public QGraphicsObject
{
    Q_OBJECT

public:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    /**
     * @param linkId 对应 WarLink::id
     * @param model  只读模型引用，用于解析锚点坐标
     * @param parent 父图形项
     */
    LinkGraphicsItem(const warroom::Uuid& linkId,
        const warroom::WarRoomModel& model,
        QGraphicsItem* parent = nullptr);

    // ---- QGraphicsItem 接口 ----
    QRectF boundingRect() const override;
    void paint(QPainter* painter,
        const QStyleOptionGraphicsItem* option,
        QWidget* widget) override;

    // ---- 自有接口 ----
    const warroom::Uuid& linkId() const { return m_linkId; }

    /// 当模型数据变更后调用，重新解析锚点并刷新几何
    void updatePositions();

private:
    // ---- 颜色映射 ----
    static QColor defaultColorForType(warroom::LinkType type);
    static QColor highlightColor() { return QColor(100, 180, 255); }
    
    // ---- 路径构建 ----
    QVector<QPointF> m_wayPoints;
    void buildPath(QPainterPath& path) const;
    
    // ---- 贝塞尔控制点计算（便于独立替换） ----
    struct BezierControl {
        QPointF ctrl1;
        QPointF ctrl2;
    };
    static BezierControl computeControlPoints(QPointF from, QPointF to);

    // ---- 箭头 ----
    static void arrowHeadPoints(const QPointF& tip,
        const QPointF& fromDir,
        QPointF& p1,
        QPointF& p2);

    // ---- 数据成员 ----
    warroom::Uuid m_linkId;
    const warroom::WarRoomModel& m_model;

    QPointF m_startPoint;
    QPointF m_endPoint;
};