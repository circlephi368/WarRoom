// TempConnectionItem.cpp
#include "TempConnectionItem.h"
#include <cmath>

// 辅助函数：根据边缘方向获取单位向量（与 LinkGraphicsItem 保持一致）
static QPointF edgeDirection(int edge)
{
	switch (edge) {
	case 0: return QPointF(1.0, 0.0);   // 右
	case 1: return QPointF(0.0, 1.0);   // 下
	case 2: return QPointF(-1.0, 0.0);  // 左
	case 3: return QPointF(0.0, -1.0);  // 上
	default: return QPointF(1.0, 0.0);
	}
}

// 贝塞尔控制点计算（与 LinkGraphicsItem::computeControlPoints 逻辑一致）
static void computeBezierControls(
	const QPointF& from, int fromEdge,
	const QPointF& to, int toEdge,
	QPointF& ctrl1, QPointF& ctrl2)
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

	ctrl1 = from + fromDir * offset;
	ctrl2 = to + toDir * offset;
}

TempConnectionItem::TempConnectionItem(const QPointF& start, QGraphicsItem* parent)
	: QGraphicsObject(parent)
	, m_start(start)
	, m_end(start)
	, m_startEdge(0)    // 默认右边缘
	, m_endEdge(0)      // 默认右边缘
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

void TempConnectionItem::setSnapPoint(const QPointF& snap, int fromEdge, int toEdge)
{
	prepareGeometryChange();
	m_snapPoint = snap;
	m_hasSnap = true;
	m_startEdge = fromEdge;
	m_endEdge = toEdge;
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

	// 计算贝塞尔控制点（与正式连线逻辑一致）
	QPointF ctrl1, ctrl2;
	int fromEdge = m_startEdge;
	int toEdge = m_endEdge;

	// 如果没有吸附目标，根据起点和终点的相对位置推断合理的边缘方向
	if (!m_hasSnap) {
		QPointF delta = end - m_start;
		// 推断起点边缘：根据鼠标拖拽方向
		if (std::abs(delta.x()) > std::abs(delta.y())) {
			fromEdge = delta.x() > 0 ? 0 : 2;  // 右 or 左
		}
		else {
			fromEdge = delta.y() > 0 ? 1 : 3;  // 下 or 上
		}
		// 终点边缘：反向推断（鼠标进入方向）
		if (std::abs(delta.x()) > std::abs(delta.y())) {
			toEdge = delta.x() > 0 ? 2 : 0;    // 从右侧来则终点在左，从左来则终点在右
		}
		else {
			toEdge = delta.y() > 0 ? 3 : 1;    // 从下方来则终点在上，从上来则终点在下
		}
	}

	computeBezierControls(m_start, fromEdge, end, toEdge, ctrl1, ctrl2);

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