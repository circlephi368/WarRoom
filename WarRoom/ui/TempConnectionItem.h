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
	void setSnapPoint(const QPointF& snap, int fromEdge = 0, int toEdge = 0);

	QRectF boundingRect() const override;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
	QPointF m_start;
	QPointF m_end;
	QPointF m_snapPoint;
	bool m_hasSnap = false;
	int m_startEdge = 0;   // 起点边缘方向
	int m_endEdge = 0;     // 终点边缘方向
};