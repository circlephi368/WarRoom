// CameraAnimator.cpp
//
// 临界阻尼弹簧模型实现细节：
//
// 状态方程（每个轴独立）：
//   x'' = -k * (x - target) - c * x'
//   其中 k 为刚度，c 为阻尼
//   临界阻尼条件：c = 2 * sqrt(k)，此时无振荡且最快收敛
//
// 半隐式欧拉积分（稳定且简单）：
//   vel += acc * dt
//   pos += vel * dt
//
// 缩放在对数空间做差：log(target) - log(current)
// 这样视觉上缩放速度是均匀的，不会出现"越近越慢"

#include "CameraAnimator.h"
#include "warroomview.h"
#include "NodeGraphicsItem.h"
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <cmath>
#include <qDebug>

CameraAnimator::CameraAnimator(WarRoomView* view, QObject* parent)
	: QObject(parent), m_view(view)
{
	m_timer.setInterval(16); // ~60 FPS
	m_timer.setTimerType(Qt::PreciseTimer);
	connect(&m_timer, &QTimer::timeout, this, &CameraAnimator::tick);
}

CameraAnimator::~CameraAnimator()
{
	if (m_active && m_view) {
		// 析构时恢复 view 的交互状态
		m_view->setInteractive(true);
	}
}

void CameraAnimator::focusOn(QGraphicsItem* item)
{
	if (!m_view || !item) return;

	// 计算目标位置（item 的中心点在场景坐标）
	QRectF itemRect = item->mapToScene(item->boundingRect()).boundingRect();
	QPointF target = itemRect.center();

	// 计算合适的缩放（节点过大时缩小到完全可见）
	float fitZoom = computeFitZoom(item);
	float targetZoom = (fitZoom > 0) ? fitZoom : m_view->getZoomLevel();

	focusOn(target, targetZoom);
}

void CameraAnimator::focusOn(const QPointF& targetScenePos, float targetZoom)
{
	if (!m_view) return;

	// 限制目标缩放范围（与 view 的 wheelEvent 保持一致）
	targetZoom = std::clamp(static_cast<qreal>(targetZoom), 0.1, 5.0);

	// 读取当前真实状态
	QPointF currentPos = m_view->mapToScene(m_view->viewport()->rect().center());
	qreal currentZoom = m_view->getZoomLevel();

	// ---- 快速路径：已经足够接近目标，跳过动画 ----
	QPointF posDiff = currentPos - targetScenePos;
	qreal posErr = std::hypot(posDiff.x(), posDiff.y());
	qreal zoomErr = std::abs(currentZoom - targetZoom);
	qreal velMag = std::hypot(m_vel.x(), m_vel.y());

	constexpr qreal kPosSkipThreshold = 0.5;      // 同 reachedTarget 阈值
	constexpr qreal kZoomSkipThreshold = 0.001;     // 同 reachedTarget 阈值
	constexpr qreal kVelSkipThreshold = 1.0;           // 同 reachedTarget 阈值

	if (posErr < kPosSkipThreshold
		&& zoomErr < kZoomSkipThreshold
		&& velMag < kVelSkipThreshold
		&& std::abs(m_zoomVel) < kVelSkipThreshold) {
		qDebug() << "[CAMDBG] animation skipped (already centered). posErr ="
				 << posErr << "zoomErr =" << zoomErr
				 << "velMag =" << velMag << "zoomVel =" << m_zoomVel;
		return;
	}

	// 初始化当前状态
	m_pos = currentPos;
	m_zoom = currentZoom;

	// 重置为自动导航参数
	m_usingInputParams = false;

	// 设置目标
	m_targetPos = targetScenePos;
	m_targetZoom = targetZoom;

	// 启动动画
	if (!m_active) {
		m_active = true;
		m_view->setInteractive(false); // 禁用画布交互（拖动节点等）
		emit animationStarted();
		qDebug() << "[CAMDBG] animation started: target =" << m_targetPos
				 << "zoom =" << m_targetZoom
				 << "posErr =" << posErr << "zoomErr =" << zoomErr;
	}

	m_clock.start();
	m_timer.start();
}

void CameraAnimator::abort()
{
	if (!m_active) return;

	m_timer.stop();
	m_active = false;
	m_usingInputParams = false;

	// 归零速度
	m_vel = QPointF(0, 0);
	m_zoomVel = 0.0;

	if (m_view) {
		m_view->setInteractive(true);
	}

	qDebug() << "[CAMDBG] animation aborted";
	emit animationFinished();
}

// ============================================================
// 连续输入接口实现
// ============================================================

void CameraAnimator::nudgeTarget(qreal deltaSceneX, qreal deltaSceneY)
{
	if (!m_view) return;

	m_usingInputParams = true;

	// 读取视图当前真实位置
	QPointF realPos = m_view->mapToScene(m_view->viewport()->rect().center());
	qreal realZoom = m_view->getZoomLevel();

	// 检测视图是否被用户外部修改（用上次应用位置做基准，避免滚动条取整误差）
	if (m_active) {
		qreal posErr = std::hypot(realPos.x() - m_lastAppliedPos.x(), realPos.y() - m_lastAppliedPos.y());
		qreal zoomErr = std::abs(realZoom - m_lastAppliedZoom);
		if (posErr > 3.0 || zoomErr > 0.01) {
			// 外部修改：同步当前位置，重置速度
			m_pos = realPos;
			m_zoom = realZoom;
			m_vel = QPointF(0, 0);
			m_zoomVel = 0.0;
			m_targetPos = realPos;
		}
	}

	// 改变目标（弹簧会平滑追踪）
	m_targetPos += QPointF(deltaSceneX, deltaSceneY);

	// 若动画未启动，则启动
	if (!m_active) {
		m_pos = realPos;
		m_zoom = realZoom;
		m_vel = QPointF(0, 0);
		m_zoomVel = 0.0;

		m_active = true;
		emit animationStarted();
		m_clock.start();
		m_timer.start();
	}
}

void CameraAnimator::setZoomTargetAt(const QPointF& anchorScenePos, float targetZoom)
{
	if (!m_view) return;

	targetZoom = std::clamp(targetZoom, 0.1f, 5.0f);

	m_usingInputParams = true;

	// 读取视图当前真实位置
	QPointF currentCenter = m_view->mapToScene(m_view->viewport()->rect().center());
	qreal currentZoom = m_view->getZoomLevel();

	// 检测视图是否被用户外部修改
	if (m_active) {
		qreal posErr = std::hypot(currentCenter.x() - m_lastAppliedPos.x(), currentCenter.y() - m_lastAppliedPos.y());
		qreal zoomErr = std::abs(currentZoom - m_lastAppliedZoom);
		if (posErr > 3.0 || zoomErr > 0.01) {
			m_pos = currentCenter;
			m_zoom = currentZoom;
			m_vel = QPointF(0, 0);
			m_zoomVel = 0.0;
		}
	}

	// 计算目标位置：围绕锚点保持屏幕位置不变
	qreal ratio = currentZoom / targetZoom;
	m_targetPos = anchorScenePos + (currentCenter - anchorScenePos) * ratio;
	m_targetZoom = targetZoom;

	// 若动画未启动，则从当前状态开始过渡
	if (!m_active) {
		m_pos = currentCenter;
		m_zoom = currentZoom;
		m_vel = QPointF(0, 0);
		m_zoomVel = 0.0;

		m_active = true;
		emit animationStarted();
		m_clock.start();
		m_timer.start();
	}
}

void CameraAnimator::tick()
{
	if (!m_active || !m_view) {
		m_timer.stop();
		return;
	}

	qreal dt = m_clock.restart() / 1000.0;
	// 防止 dt 过大（例如窗口暂停后恢复）导致积分爆炸
	dt = std::min(dt, 1.0 / 30.0);

	stepSpring(dt);
	applyToView();

	if (reachedTarget()) {
		// 最终强制对齐到目标，消除稳态误差
		m_pos = m_targetPos;
		m_zoom = m_targetZoom;
		m_vel = QPointF(0, 0);
		m_zoomVel = 0.0;
		applyToView();

		m_timer.stop();
		m_active = false;

		// 只有自动导航模式才需要恢复交互
		if (!m_usingInputParams && m_view) {
			m_view->setInteractive(true);
		}

		// 缩放稳定后强制重绘视口内所有 NodeGraphicsItem，重建文字缓存
		if (m_view && m_view->scene()) {
			QRectF viewRect = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
			QList<QGraphicsItem*> items = m_view->scene()->items(viewRect);
			for (auto* item : items) {
				if (auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item)) {
					nodeItem->forceRefreshCache();
				} else {
					item->update();
				}
			}
		}

		// 重置模式标志
		m_usingInputParams = false;

		qDebug() << "[CAMDBG] animation finished at" << m_pos << "zoom =" << m_zoom;
		emit animationFinished();
	}
}

void CameraAnimator::stepSpring(qreal dt)
{
	// 根据当前模式选择弹簧参数
	qreal k = m_usingInputParams ? m_inputSpringK : m_springK;
	qreal c = m_usingInputParams ? m_inputDamping : m_damping;

	// ---- 平移弹簧（x, y 轴独立） ----
	// acc = -k * (pos - target) - c * vel
	QPointF diff = m_pos - m_targetPos;
	QPointF acc(-k * diff.x() - c * m_vel.x(),
				-k * diff.y() - c * m_vel.y());

	// 半隐式欧拉：先更新速度，再更新位置
	m_vel.setX(m_vel.x() + acc.x() * dt);
	m_vel.setY(m_vel.y() + acc.y() * dt);
	m_pos.setX(m_pos.x() + m_vel.x() * dt);
	m_pos.setY(m_pos.y() + m_vel.y() * dt);

	// ---- 缩放弹簧（对数空间） ----
	// 用对数差作为弹簧位移，使缩放速度在视觉上均匀
	qreal logDiff = std::log(m_zoom) - std::log(m_targetZoom);
	qreal zoomAcc = -k * logDiff - c * m_zoomVel;
	m_zoomVel += zoomAcc * dt;
	m_zoom *= std::exp(m_zoomVel * dt);

	// 防止数值溢出
	m_zoom = std::clamp(m_zoom, 0.01, 100.0);
}

bool CameraAnimator::reachedTarget() const
{
	QPointF posDiff = m_pos - m_targetPos;
	qreal posErr = std::hypot(posDiff.x(), posDiff.y());
	qreal zoomErr = std::abs(m_zoom - m_targetZoom);
	qreal velMag = std::hypot(m_vel.x(), m_vel.y());

	return posErr < kPosThreshold
		&& zoomErr < kZoomThreshold
		&& velMag < kVelThreshold
		&& std::abs(m_zoomVel) < kVelThreshold;
}

void CameraAnimator::applyToView()
{
	if (!m_view) return;

	// 使用 setViewCenter 一次性更新位置和缩放
	warroom::Point2D center{ static_cast<float>(m_pos.x()),
							 static_cast<float>(m_pos.y()) };
	m_view->setViewCenter(center, static_cast<float>(m_zoom));

	// 读回视图实际位置（滚动条取整可能导致误差），作为外部修改检测基准
	m_lastAppliedPos = m_view->mapToScene(m_view->viewport()->rect().center());
	m_lastAppliedZoom = m_view->getZoomLevel();
}

float CameraAnimator::computeFitZoom(QGraphicsItem* item) const
{
	if (!m_view || !item) return -1.0f;

	QRectF itemRect = item->mapToScene(item->boundingRect()).boundingRect();
	if (itemRect.isEmpty()) return -1.0f;

	QRectF viewportRect = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();

	// 留 20% 边距，节点不至于贴着视口边缘
	qreal margin = 0.2;
	qreal availW = viewportRect.width() * (1.0 - margin);
	qreal availH = viewportRect.height() * (1.0 - margin);

	// 计算需要的缩放比（场景坐标下节点尺寸 vs 可用尺寸）
	qreal scaleX = availW / itemRect.width();
	qreal scaleY = availH / itemRect.height();
	qreal fitScale = std::min(scaleX, scaleY);

	// 只有节点"过大"（fitScale < 1）才需要缩小
	// 节点完全在视口内时（fitScale >= 1）保持当前缩放
	if (fitScale >= 1.0) return -1.0f;

	// fitScale 是相对于"当前视口"的缩放比
	// 但 setViewCenter 的 zoom 是相对于 1.0 的绝对缩放
	// 当前视口已经是 m_zoom 缩放后的，所以目标绝对缩放 = 当前缩放 * fitScale
	qreal currentZoom = m_view->getZoomLevel();
	qreal targetZoom = currentZoom * fitScale;

	return static_cast<float>(std::clamp(targetZoom, 0.1, 5.0));
}
