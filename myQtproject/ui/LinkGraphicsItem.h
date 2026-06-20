// LinkGraphicsItem.h
#pragma once
#include <QMenu>
#include <QGraphicsObject>
#include <QGraphicsSceneContextMenuEvent>
#include <QPainter>
#include <QPen>

#include "core/warroom/warroom_types.h"
#include "core/warroom/war_link.h"

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
	 * @param model  模型指针（外部所有权，析构时可能已失效）
	 * @param parent 父图形项
	 */
	LinkGraphicsItem(const warroom::Uuid& linkId,
		warroom::WarRoomModel* model,
		QGraphicsItem* parent = nullptr);

	~LinkGraphicsItem();

	// ---- QGraphicsItem 接口 ----
	QRectF boundingRect() const override;
	void paint(QPainter* painter,
		const QStyleOptionGraphicsItem* option,
		QWidget* widget) override;
	QPainterPath shape() const override;

	// ---- 自有接口 ----
	const warroom::Uuid& linkId() const { return m_linkId; }

	// 当模型数据变更后调用，重新解析锚点并刷新几何
	void updatePositions();

	// 根据相连节点的 Z 值更新自身的 Z 值
	void updateZValueFromNodes();

	// 刷新连线默认颜色（全局连线颜色配置变更时调用）
	void refreshLinkColor(const QColor& color);

signals:
	// 请求删除此连线（由外部如 WarRoomMainWindow 响应）
	void deletionRequested(const warroom::Uuid& linkId);

private:
	// ---- 颜色映射 ----
	static QColor defaultColorForType(warroom::LinkType type);
	static QColor highlightColor() { return QColor(100, 180, 255); }
	
	// ---- 路径构建 ----
	QVector<QPointF> m_wayPoints;
	void buildPath(QPainterPath& path) const;

	// 计算出入连接线方向
	// 根据 edge 返回单位方向向量（从节点中心指向边缘外）
	// edge: 0=右(+x), 1=下(+y), 2=左(-x), 3=上(-y), 其他返回0右
	static QPointF edgeDirection(int edge);
	
	QPainterPath m_hitArea;  // 用于命中检测的路径轮廓
	void updateHitArea();
	// ---- 贝塞尔控制点计算（便于独立替换） ----
	struct BezierControl {
		QPointF ctrl1;
		QPointF ctrl2;
	};
	static BezierControl computeControlPoints(
		QPointF from, int fromEdge,
		QPointF to, int toEdge);

	// ---- 箭头 ----
	static void arrowHeadPoints(const QPointF& tip,
		const QPointF& fromDir,
		QPointF& p1,
		QPointF& p2);

	// ---- 数据成员 ----
	warroom::Uuid m_linkId;
	warroom::WarRoomModel* m_model;  // 外部所有权指针，析构时不删除

	QPointF m_startPoint;
	QPointF m_endPoint;

	// 全局连线默认颜色缓存（由 refreshLinkColor 设置）
	QColor m_globalLinkColor{ 150, 150, 150 };
};