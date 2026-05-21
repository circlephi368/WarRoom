#pragma once
#include "war_room_model.h"
#include <QGraphicsItem>
#include <QPainter>
#include <QPen>
#include <cmath>

//class WarRoomModel;
//using namespace warroom;
class LinkGraphicsItem : public QGraphicsObject
{
    Q_OBJECT

public:
    LinkGraphicsItem(const std::string& linkId,
        const warroom::WarRoomModel& model,
        QGraphicsItem* parent = nullptr);

    // 包围盒：覆盖整条曲线的可能范围
    QRectF boundingRect() const override;

    // 绘制
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override;

    // 刷新端点位置（从模型重新解析锚点坐标）
    void updatePositions();

    const std::string& linkId() const { return m_linkId; }

private:
    std::string m_linkId;
    const warroom::WarRoomModel& m_model;

    // 缓存的端点画布坐标
    QPointF m_startPoint;
    QPointF m_endPoint;

    // 连线类型 → 颜色
    static QColor colorForType(const std::string& type);

    // 计算箭头三角形的三个点
    static void arrowHead(const QPointF& tip, const QPointF& from,
        QPointF& p1, QPointF& p2);
};