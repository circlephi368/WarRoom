// HighlightOverlay.h
//
// 透明覆盖层：在侧边栏点击聚焦节点时，绘制从侧边栏项指向目标节点的
// 直线箭头 + 节点边框高亮，3 秒内淡出。
//
// 设计要点：
//   - 覆盖在 m_centralContainer 之上，鼠标事件穿透（WA_TransparentForMouseEvents）
//   - 箭头终点随镜头移动实时更新（每帧从 nodeItem 的场景坐标重新映射到视口）
//   - 节点高亮为圆角矩形边框，围绕节点在视口中的包围盒
//   - 淡出使用 QElapsedTimer 计时，60fps QTimer 驱动重绘

#pragma once

#include "warroomview.h"
#include "NodeGraphicsItem.h"


#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsItem>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <cmath>
#include <limits>


class HighlightOverlay : public QWidget
{
	Q_OBJECT

public:
	explicit HighlightOverlay(QWidget* parent)
		: QWidget(parent)
	{
		// 鼠标事件穿透
		setAttribute(Qt::WA_TransparentForMouseEvents, true);
		setAttribute(Qt::WA_NoSystemBackground, true);
		setAttribute(Qt::WA_TranslucentBackground, true);

		// 覆盖整个父 widget
		if (parent) {
			setGeometry(parent->rect());
		}

		m_timer.setInterval(16); // ~60 FPS
		m_timer.setTimerType(Qt::PreciseTimer);
		connect(&m_timer, &QTimer::timeout, this, [this]() {
			if (m_elapsed.elapsed() >= m_durationMs) {
				m_timer.stop();
				m_active = false;
			}
			update();
		});
	}

	// 设置几何形状跟随父 widget
	void syncGeometry() {
		if (auto* p = parentWidget()) {
			setGeometry(p->rect());
		}
	}

	// 触发高亮显示
	// fromPos: 箭头起点（在 parentWidget 坐标系中）
	// nodeItem: 目标节点
	// view: 画布视图（用于坐标转换）
	// fromLeft: true=起点在左侧边栏（箭头朝右），false=起点在右侧边栏（箭头朝左）
	void showHighlight(const QPoint& fromPos, NodeGraphicsItem* nodeItem,
					   WarRoomView* view, bool fromLeft)
	{
		m_fromPos = fromPos;
		m_nodeItem = nodeItem;
		m_view = view;
		m_fromLeft = fromLeft;
		m_active = true;
		m_elapsed.start();
		m_timer.start();
		update();
	}

	// 是否正在显示高亮
	bool isActive() const { return m_active; }

protected:
	void paintEvent(QPaintEvent*) override
	{
		if (!m_active || !m_nodeItem || !m_view) return;

		qreal opacity = computeOpacity();
		if (opacity <= 0.0) return;

		// ---- 计算节点在视口中的矩形 ----
		// 场景坐标 → viewport 坐标 → parentWidget(centralContainer) 坐标
		// mapTo 会自动穿越 widget 层级（viewport → view → canvasArea → centralContainer）
		QRectF sceneRect = m_nodeItem->mapToScene(m_nodeItem->boundingRect()).boundingRect();
		QPoint tl = m_view->viewport()->mapTo(parentWidget(), m_view->mapFromScene(sceneRect.topLeft()));
		QPoint br = m_view->viewport()->mapTo(parentWidget(), m_view->mapFromScene(sceneRect.bottomRight()));
		QRect nodeRect = QRect(tl, br).normalized();

		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);

		// ---- 绘制节点高亮边框 ----
		QColor hlColor(100, 180, 255);
		hlColor.setAlphaF(opacity);
		p.setPen(QPen(hlColor, 3));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(nodeRect.adjusted(-2, -2, 2, 2), 8, 8);

		// ---- 计算箭头终点（节点矩形最近的角） ----
		QPoint target = nearestCorner(m_fromPos, nodeRect);

		// ---- 绘制箭头 ----
		drawArrow(&p, m_fromPos, target, opacity);
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
	}

private:
	qreal computeOpacity() const
	{
		if (!m_active) return 0.0;
		qint64 elapsed = m_elapsed.elapsed();
		if (elapsed >= m_durationMs) return 0.0;
		// 线性淡出
		return 1.0 - static_cast<qreal>(elapsed) / static_cast<qreal>(m_durationMs);
	}

	// 找到矩形距离 from 最近的角点
	static QPoint nearestCorner(const QPoint& from, const QRect& rect)
	{
		QPoint corners[4] = {
			rect.topLeft(),
			rect.topRight(),
			rect.bottomLeft(),
			rect.bottomRight()
		};
		QPoint best = corners[0];
		qreal bestDist = (std::numeric_limits<qreal>::max)();
		for (int i = 0; i < 4; ++i) {
			qreal d = QPoint::dotProduct(corners[i] - from, corners[i] - from);
			if (d < bestDist) {
				bestDist = d;
				best = corners[i];
			}
		}
		return best;
	}

	// 绘制直线箭头（含箭头头部）
	void drawArrow(QPainter* p, const QPoint& from, const QPoint& to, qreal opacity)
	{
		QColor arrowColor(255, 200, 100);
		arrowColor.setAlphaF(opacity);

		QPen pen(arrowColor, 2);
		p->setPen(pen);
		p->setBrush(arrowColor);

		// 主线
		p->drawLine(from, to);

		// 箭头头部
		constexpr qreal arrowSize = 10.0;
		constexpr qreal kPi = 3.14159265358979323846;
		QLineF line(from, to);
		qreal angle = std::atan2(line.dy(), line.dx());

		// 箭头两翼
		QPointF wing1(to.x() - arrowSize * std::cos(angle - kPi / 6.0),
					  to.y() - arrowSize * std::sin(angle - kPi / 6.0));
		QPointF wing2(to.x() - arrowSize * std::cos(angle + kPi / 6.0),
					  to.y() - arrowSize * std::sin(angle + kPi / 6.0));

		QPainterPath head;
		head.moveTo(to);
		head.lineTo(wing1);
		head.lineTo(wing2);
		head.closeSubpath();
		p->drawPath(head);
	}

	QPoint m_fromPos;
	QPointer<NodeGraphicsItem> m_nodeItem;
	QPointer<WarRoomView> m_view;
	bool m_fromLeft = true;
	bool m_active = false;

	QTimer m_timer;
	QElapsedTimer m_elapsed;
	static constexpr qint64 m_durationMs = 3000; // 3秒
};
