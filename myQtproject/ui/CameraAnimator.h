// CameraAnimator.h
//
// 相机动画器：基于临界阻尼弹簧模型的平滑相机运动。
//
// 设计目标：
//   - 速度/加速度驱动，而非目标插值，天然支持连续切换和中断恢复
//   - 平移和缩放统一调度
//   - 动画期间禁止用户拖动，避免冲突
//   - 节点过大时自动调整缩放使节点完全可见
//
// 使用方式：
//   m_animator = new CameraAnimator(m_view, this);
//   m_animator->focusOn(item);           // 平滑聚焦到节点
//   m_animator->focusOn(scenePos, zoom); // 平滑聚焦到指定位置和缩放
//   m_animator->abort();                 // 用户拖动时中止动画

#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QGraphicsItem>

class WarRoomView;

class CameraAnimator : public QObject
{
	Q_OBJECT

public:
	explicit CameraAnimator(WarRoomView* view, QObject* parent = nullptr);
	~CameraAnimator() override;

	// 聚焦到指定图形项（自动计算合适的缩放，节点过大时缩小到完全可见）
	void focusOn(QGraphicsItem* item);

	// 聚焦到指定场景坐标和缩放等级
	void focusOn(const QPointF& targetScenePos, float targetZoom);

	// 中止动画（用户拖动时调用，归零速度）
	void abort();

	// ---- 连续输入接口：改变目标，弹簧自然平滑过渡 ----

	// 平移目标位置（delta 为场景坐标增量）
	void nudgeTarget(qreal deltaSceneX, qreal deltaSceneY);

	// 缩放目标（围绕锚点保持屏幕位置不变）
	// anchorScenePos: 缩放锚点的场景坐标
	// targetZoom: 目标缩放值
	void setZoomTargetAt(const QPointF& anchorScenePos, float targetZoom);

	// 是否正在动画中
	bool isAnimating() const { return m_active; }

signals:
	// 动画开始时发出（可用于禁用交互）
	void animationStarted();
	// 动画结束时发出（可用于恢复交互）
	void animationFinished();

private slots:
	void tick();

private:
	// 弹簧积分一步
	void stepSpring(qreal dt);

	// 检查是否到达目标（误差和速度都小于阈值）
	bool reachedTarget() const;

	// 应用当前状态到 view
	void applyToView();

	// 计算适合显示 item 的缩放等级
	// 返回 < 0 表示不需要调整缩放
	float computeFitZoom(QGraphicsItem* item) const;

	QPointer<WarRoomView> m_view;

	// ---- 当前相机状态 ----
	QPointF m_pos;          // 当前场景中心点
	qreal   m_zoom = 1.0;   // 当前缩放

	// ---- 速度（场景坐标） ----
	QPointF m_vel;          // 位置速度
	qreal   m_zoomVel = 0.0; // 缩放速度（对数空间）

	// ---- 目标 ----
	QPointF m_targetPos;
	qreal   m_targetZoom = 1.0;

	// ---- 弹簧参数（临界阻尼） ----
	// 临界阻尼条件：c = 2 * sqrt(k)
	// 自动导航参数：柔和舒适
	qreal m_springK = 80.0;    // 弹簧刚度（1/s^2）
	qreal m_damping = 17.888;  // 阻尼系数 = 2*sqrt(80) ≈ 17.888

	// 连续输入参数（WASD/滚轮）：更硬更快的响应
	qreal m_inputSpringK = 200.0;
	qreal m_inputDamping = 28.284;  // 2*sqrt(200) ≈ 28.284
	bool  m_usingInputParams = false;  // 当前是否使用连续输入参数

	// ---- 动画驱动 ----
	QTimer m_timer;
	QElapsedTimer m_clock;
	bool m_active = false;

	// ---- 最近一次应用到视图的实际位置（用于检测外部修改） ----
	QPointF m_lastAppliedPos;
	qreal   m_lastAppliedZoom = 1.0;

	// ---- 到达判定阈值 ----
	static constexpr qreal kPosThreshold = 1.0;     // 场景坐标 0.5 像素
	static constexpr qreal kZoomThreshold = 0.005;  // 缩放比 0.1%
	static constexpr qreal kVelThreshold = 1.0;     // 速度 1 像素/秒
};
