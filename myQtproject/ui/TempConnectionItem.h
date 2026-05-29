// TempConnectionItem.h
#pragma once

#include <QGraphicsObject>
#include <QPainter>
#include <QPointF>

class TempConnectionItem : public QGraphicsObject
{
    Q_OBJECT

public:
    TempConnectionItem(const QPointF& start, QGraphicsItem* parent = nullptr);

    void setEndPoint(const QPointF& end);
    void setSnapPoint(const QPointF& snap);  // 吸附目标点（可选）

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    QPointF m_start;
    QPointF m_end;
    QPointF m_snapPoint;  // 吸附点，为null时表示无吸附
    bool m_hasSnap = false;
};