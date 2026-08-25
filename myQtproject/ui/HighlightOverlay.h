// HighlightOverlay.h
//
// 透明覆盖层：
// 1. 侧边栏聚焦时绘制箭头+边框高亮（3秒淡出）
// 2. 持续显示"操作焦点指示器"（黄色直角三角形）
//
// 焦点指示器说明：
//   - 黄色填充等腰直角三角形，直角顶点指向当前焦点对象
//   - 节点焦点：三角形绘制在节点左上角外部/内部（根据裁剪情况）
//   - 画布焦点：三角形绘制在画布可视区域左上角
//   - 外部焦点：三角形绘制在屏幕左上角（带外部焦点标记样式）
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
#include <climits>

enum class FocusState {
	NoFocus,        // 焦点在画布之外（侧边栏等）
	CanvasFocus,    // 焦点在画布上（无节点选中）
	NodeFocus       // 焦点在某个节点上
};

class HighlightOverlay : public QWidget
{
	Q_OBJECT

public:
	explicit HighlightOverlay(QWidget* parent)
		: QWidget(parent)
	{
		setAttribute(Qt::WA_TransparentForMouseEvents, true);
		setAttribute(Qt::WA_NoSystemBackground, true);
		setAttribute(Qt::WA_TranslucentBackground, true);

		if (parent) {
			setGeometry(parent->rect());
		}

		m_timer.setInterval(16);
		m_timer.setTimerType(Qt::PreciseTimer);
		connect(&m_timer, &QTimer::timeout, this, [this]() {
			if (m_hlActive && m_elapsed.elapsed() >= m_durationMs) {
				m_hlActive = false;
				m_timer.stop();
			}
			update();
		});

		m_focusTimer.setInterval(50);
		m_focusTimer.setTimerType(Qt::PreciseTimer);
		connect(&m_focusTimer, &QTimer::timeout, this, &HighlightOverlay::updateFocusDisplay);
		m_focusTimer.start();
	}

	void syncGeometry() {
		if (auto* p = parentWidget()) {
			setGeometry(p->rect());
		}
	}

	// ---- 侧边栏高亮（3秒淡出）----
	void showHighlight(const QPoint& fromPos, NodeGraphicsItem* nodeItem,
					   WarRoomView* view, bool fromLeft)
	{
		m_fromPos = fromPos;
		m_hlNodeItem = nodeItem;
		m_view = view;
		m_fromLeft = fromLeft;
		m_hlActive = true;
		m_elapsed.start();
		m_timer.start();
		update();
	}

	bool isHighlightActive() const { return m_hlActive; }

	void setView(WarRoomView* view) {
		m_view = view;
	}

	// ---- 焦点指示器 ----
	void setFocusState(FocusState state, NodeGraphicsItem* nodeItem = nullptr)
	{
		m_focusState = state;
		m_focusNodeItem = nodeItem;
		update();
	}

	FocusState focusState() const { return m_focusState; }

	void setCanvasArea(const QRect& rect) {
		m_canvasArea = rect;
		update();
	}
	void setExternalFocusName(const QString& name) { m_externalFocusName = name; update(); }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		// 不使用 Antialiasing，确保三角形为直角无圆角

		// 1. 绘制侧边栏高亮（如果激活）
		if (m_hlActive && m_hlNodeItem && m_view) {
			qreal opacity = computeHighlightOpacity();
			if (opacity > 0.0) {
				drawHighlight(&p, opacity);
			}
		}

		// 2. 绘制焦点指示器
		drawFocusIndicator(&p);
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
	}

private:
	// ---- 侧边栏高亮相关 ----
	qreal computeHighlightOpacity() const
	{
		if (!m_hlActive) return 0.0;
		qint64 elapsed = m_elapsed.elapsed();
		if (elapsed >= m_durationMs) return 0.0;
		return 1.0 - static_cast<qreal>(elapsed) / static_cast<qreal>(m_durationMs);
	}

	void drawHighlight(QPainter* p, qreal opacity)
	{
		if (!m_view || !m_hlNodeItem) return;

		QRectF sceneRect = m_hlNodeItem->mapToScene(m_hlNodeItem->boundingRect()).boundingRect();
		QPoint tl = m_view->viewport()->mapTo(parentWidget(), m_view->mapFromScene(sceneRect.topLeft()));
		QPoint br = m_view->viewport()->mapTo(parentWidget(), m_view->mapFromScene(sceneRect.bottomRight()));
		QRect nodeRect = QRect(tl, br).normalized();

		QColor hlColor(100, 180, 255);
		hlColor.setAlphaF(opacity);
		p->setPen(QPen(hlColor, 3));
		p->setBrush(Qt::NoBrush);
		p->drawRoundedRect(nodeRect.adjusted(-2, -2, 2, 2), 8, 8);

		QPoint target = nearestCorner(m_fromPos, nodeRect);
		drawArrow(p, m_fromPos, target, opacity);
	}

	static QPoint nearestCorner(const QPoint& from, const QRect& rect)
	{
		QPoint corners[4] = {
			rect.topLeft(), rect.topRight(),
			rect.bottomLeft(), rect.bottomRight()
		};
		QPoint best = corners[0];
		qreal bestDist = (std::numeric_limits<qreal>::max)();
		for (int i = 0; i < 4; ++i) {
			qreal d = QPoint::dotProduct(corners[i] - from, corners[i] - from);
			if (d < bestDist) { bestDist = d; best = corners[i]; }
		}
		return best;
	}

	void drawArrow(QPainter* p, const QPoint& from, const QPoint& to, qreal opacity)
	{
		QColor arrowColor(255, 200, 100);
		arrowColor.setAlphaF(opacity);
		QPen pen(arrowColor, 2);
		p->setPen(pen);
		p->setBrush(arrowColor);
		p->drawLine(from, to);

		constexpr qreal arrowSize = 10.0;
		constexpr qreal kPi = 3.14159265358979323846;
		QLineF line(from, to);
		qreal angle = std::atan2(line.dy(), line.dx());

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

	// ---- 焦点指示器 ----
	void drawFocusIndicator(QPainter* p)
	{
		
		if (m_focusState == FocusState::NoFocus) {
			drawNoFocusIndicator(p);
		} else if (m_focusState == FocusState::CanvasFocus) {
			drawCanvasFocusIndicator(p);
		} else if (m_focusState == FocusState::NodeFocus && m_focusNodeItem) {
			drawNodeFocusIndicator(p);
		} else {
			;
		}
	}

	// 无焦点（外部焦点）：在屏幕左上角绘制三角形 + 外部焦点标记
	void drawNoFocusIndicator(QPainter* p)
	{
		QPoint screenTopLeft = rect().topLeft() + QPoint(8, 8);
		drawTriangleAt(p, screenTopLeft, false);
	}

	// 画布焦点：在画布可视区域左上角绘制
	void drawCanvasFocusIndicator(QPainter* p)
	{
		QPoint canvasTopLeft = m_canvasArea.topLeft() + QPoint(8, 8);
		drawTriangleAt(p, canvasTopLeft, false);
	}

		// 节点焦点：在节点左上角绘制，根据裁剪情况决定内/外/边缘
	void drawNodeFocusIndicator(QPainter* p)
	{
		if (!m_focusNodeItem || !m_view) return;

		// 计算节点在 overlay 坐标系中的矩形
		QRectF sceneRect = m_focusNodeItem->mapToScene(m_focusNodeItem->boundingRect()).boundingRect();
		QPoint tl = m_view->viewport()->mapTo(parentWidget(), m_view->mapFromScene(sceneRect.topLeft()));
		QRect nodeRect(tl, QSize(static_cast<int>(m_focusNodeItem->getWidth()),
								 static_cast<int>(m_focusNodeItem->getHeight())));

		QPoint corner = nodeRect.topLeft();
		// 使用画布可视区域作为裁剪范围
		QRect visibleRect = m_canvasArea;

		if (!visibleRect.isValid() || visibleRect.isEmpty()) {
			visibleRect = rect();
		}

		// 检查三角形绘制在外部是否会被裁剪
		QRect triangleOutside = computeTriangleRect(corner, false);
		bool outsideClipped = !visibleRect.contains(triangleOutside);

		if (outsideClipped) {
			// 外部会被裁剪，尝试内部绘制
			QRect triangleInside = computeTriangleRect(corner, true);
			bool insideClipped = !visibleRect.contains(triangleInside);

			if (insideClipped) {
				// 角点附近完全不可见，检查节点的边
				constexpr int kTriSize = 16;

				// 检查节点的上边和左边（与角点相连的两条边）
				bool topEdgeVisible = nodeRect.top() <= visibleRect.bottom() && nodeRect.top() >= visibleRect.top();
				bool leftEdgeVisible = nodeRect.left() <= visibleRect.right() && nodeRect.left() >= visibleRect.left();

				if (!topEdgeVisible && !leftEdgeVisible) {
					// 两条边都不可见，使用画布左上角
					QPoint canvasTopLeft = m_canvasArea.topLeft() + QPoint(8, 8);
					drawTriangleAt(p, canvasTopLeft, false);
				} else {
					// 至少有一条边可见，将三角形紧贴可见的边
					QPoint drawPos;
					bool drawInside = true;

					if (topEdgeVisible && leftEdgeVisible) {
						// 两条边都可见，选择更靠近角点的那条边
						int topDist = qAbs(corner.y() - visibleRect.top());
						int leftDist = qAbs(corner.x() - visibleRect.left());

						if (topDist < leftDist) {
							// 上边更靠近
							int x = qBound(visibleRect.left(), corner.x(), visibleRect.right() - kTriSize);
							int y = visibleRect.top();
							drawPos = QPoint(x, y);
						} else {
							// 左边更靠近
							int x = visibleRect.left();
							int y = qBound(visibleRect.top(), corner.y(), visibleRect.bottom() - kTriSize);
							drawPos = QPoint(x, y);
						}
					} else if (topEdgeVisible) {
						// 只有上边可见
						int x = qBound(visibleRect.left(), corner.x(), visibleRect.right() - kTriSize);
						int y = visibleRect.top();
						drawPos = QPoint(x, y);
					} else {
						// 只有左边可见
						int x = visibleRect.left();
						int y = qBound(visibleRect.top(), corner.y(), visibleRect.bottom() - kTriSize);
						drawPos = QPoint(x, y);
					}
					drawTriangleAt(p, drawPos, drawInside);
				}
			} else {
				// 内部绘制没问题
				drawTriangleAt(p, corner, true);
			}
		} else {
			// 外部绘制没问题
			drawTriangleAt(p, corner, false);
		}
	}

	// 计算三角形的包围盒（用于裁剪检测）
	// atCorner: 三角形直角顶点位置
	// inside: true=三角形在矩形内部（翻转），false=在外部
	QRect computeTriangleRect(const QPoint& atCorner, bool inside) const
	{
		constexpr int kTriSize = 16;
		if (inside) {
			// 三角形在内部，直角顶点在左上角，三角形向右下方延伸
			return QRect(atCorner, QSize(kTriSize, kTriSize));
		} else {
			// 三角形在外部，直角顶点在左上角，三角形向左上方延伸
			return QRect(atCorner - QPoint(kTriSize, kTriSize), QSize(kTriSize, kTriSize));
		}
	}

	// 在指定位置绘制黄色直角三角形
	// atCorner: 直角顶点位置
	// inside: true=三角形在目标矩形内部，false=在外部
	void drawTriangleAt(QPainter* p, const QPoint& atCorner, bool inside)
	{
		constexpr int kTriSize = 16;

		// 50% 透明度的黄色
		QColor triColor(255, 200, 0, 128);
		p->setPen(Qt::NoPen);  // 无描边
		p->setBrush(triColor);

		QPainterPath path;
		if (inside) {
			// 三角形在内部：直角顶点在左上角，两边沿上边和左边延伸
			path.moveTo(atCorner);                              // 直角顶点
			path.lineTo(atCorner + QPoint(kTriSize, 0));       // 右顶点
			path.lineTo(atCorner + QPoint(0, kTriSize));       // 下顶点
		} else {
			// 三角形在外部：直角顶点在左上角，两边沿上边和左边延伸（向外）
			path.moveTo(atCorner);                              // 直角顶点
			path.lineTo(atCorner + QPoint(-kTriSize, 0));     // 左顶点
			path.lineTo(atCorner + QPoint(0, -kTriSize));     // 上顶点
		}
		path.closeSubpath();
		p->drawPath(path);

		// 如果是外部焦点，绘制额外的标记（虚线框）
		if (m_focusState == FocusState::NoFocus) {
			p->setPen(QPen(QColor(255, 200, 0, 128), 1, Qt::DashLine));
			p->setBrush(Qt::NoBrush);
			QPoint corner = atCorner + QPoint(kTriSize + 4, kTriSize + 4);
			p->drawRect(QRect(corner, QSize(80, 24)));
			if (!m_externalFocusName.isEmpty()) {
				p->setPen(QPen(QColor(255, 200, 0), 1));
				p->drawText(QRect(corner, QSize(80, 24)), Qt::AlignCenter, m_externalFocusName);
			}
		}
	}

	// 定时更新焦点显示（跟踪节点位置变化）
	void updateFocusDisplay()
	{
		if (m_focusState == FocusState::NodeFocus && m_focusNodeItem) {
			update();
		}
	}

	// ---- 成员变量 ----
	// 侧边栏高亮
	QPoint m_fromPos;
	QPointer<NodeGraphicsItem> m_hlNodeItem;
	QPointer<WarRoomView> m_view;
	bool m_fromLeft = true;
	bool m_hlActive = false;

	// 焦点指示器
	FocusState m_focusState = FocusState::NoFocus;
	QPointer<NodeGraphicsItem> m_focusNodeItem;
	QRect m_canvasArea;
	QString m_externalFocusName;

	// 定时器
	QTimer m_timer;
	QTimer m_focusTimer;
	QElapsedTimer m_elapsed;
	static constexpr qint64 m_durationMs = 3000;
};
