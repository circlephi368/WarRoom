// mod/builtin/AnnotationMod.h
//
// 内置辅助模组：节点标注。
//
// 行为：
//   - 作为辅助模组叠加在主模组（如 ImageMod）之上；
//   - Ctrl+左键点击节点添加标注点；
//   - 拖拽标注点可移动位置；
//   - 双击标注点编辑标签；
//   - 右键菜单：删除标注、切换显示/隐藏、选择标记类型/颜色。
//
// 坐标策略：
//   标注坐标用 0~1 的相对值（相对于节点宽高），节点缩放时标注位置不变。
//
#pragma once

#include "mod/NodeMod.h"
#include "core/warroom/war_node.h"

#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QUuid>
#include <QDebug>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>
#include <QLineEdit>
#include <QString>
#include <QStringList>
#include <vector>
#include <cmath>

namespace warroom {

	class AnnotationMod : public NodeMod {
	public:
		// ---------- 标注类型 ----------
		enum class MarkType {
			Circle = 0,
			Rect = 1,
			Cross = 2,
			Arrow = 3
		};

		// ---------- 单个标注 ----------
		struct Annotation {
			QString id;
			float x = 0.5f;       // 0~1，相对节点宽度
			float y = 0.5f;       // 0~1，相对节点高度
			QString label;
			QColor color = QColor(255, 0, 0, 200);  // 默认半透明红
			MarkType type = MarkType::Circle;
		};

		// ---------- 节点私有数据 ----------
		struct PrivateData {
			std::vector<Annotation> annotations;
			bool visible = true;
			MarkType defaultType = MarkType::Circle;     // 新建标注的默认类型
			QColor defaultColor = QColor(255, 0, 0, 200); // 新建标注的默认颜色
			int hoveredIdx = -1;    // 鼠标悬停的标注索引
			int draggingIdx = -1;   // 正在拖拽的标注索引
			// 缓存节点尺寸（由 onPaint 更新，供鼠标事件做坐标转换）
			float cachedNodeWidth = 0;
			float cachedNodeHeight = 0;

			~PrivateData() {
				// [DESTDBG] 析构入口，记录 this 与标注数量
				qDebug().nospace().noquote()
					<< "[DESTDBG] >>> ~AnnotationMod::PrivateData ENTER this=" << static_cast<void*>(this)
					<< " annotations=" << annotations.size()
					<< " visible=" << visible;
				qDebug().nospace().noquote()
					<< "[DESTDBG] <<< ~AnnotationMod::PrivateData EXIT this=" << static_cast<void*>(this);
			}
		};

		// ---------- 基础信息 ----------
		ModInfo getInfo() const override {
			return {
				"builtin.annotation",   // id
				"Annotation",           // name
				"0.1.0",                // version
				"warroom",              // author
				"Add markers/annotations on a node.",
				""                      // icon
			};
		}
		bool isPrimary() const override { return false; }  // 辅助模组

		// ---------- 生命周期 ----------
		void* onCreateNode(WarNode* /*node*/, ::NodeGraphicsItem* /*item*/) override {
			return new PrivateData();
		}
		void onDestroyNode(void* modData) override {
			delete static_cast<PrivateData*>(modData);
		}

		// ---------- 序列化 ----------
		nlohmann::json serialize(void* modData) const override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return {};
			nlohmann::json j;
			j["visible"] = d->visible;
			j["defaultType"] = static_cast<int>(d->defaultType);
			j["defaultColor"] = d->defaultColor.name(QColor::HexArgb).toStdString();

			nlohmann::json arr = nlohmann::json::array();
			for (const auto& a : d->annotations) {
				nlohmann::json ja;
				ja["id"] = a.id.toStdString();
				ja["x"] = a.x;
				ja["y"] = a.y;
				ja["label"] = a.label.toStdString();
				ja["color"] = a.color.name(QColor::HexArgb).toStdString();
				ja["type"] = static_cast<int>(a.type);
				arr.push_back(ja);
			}
			j["annotations"] = arr;
			return j;
		}
		void deserialize(void* modData, const nlohmann::json& data) override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return;
			d->annotations.clear();
			if (data.contains("visible") && data["visible"].is_boolean()) {
				d->visible = data["visible"].get<bool>();
			}
			if (data.contains("defaultType") && data["defaultType"].is_number_integer()) {
				d->defaultType = static_cast<MarkType>(data["defaultType"].get<int>());
			}
			if (data.contains("defaultColor") && data["defaultColor"].is_string()) {
				d->defaultColor = QColor(QString::fromStdString(data["defaultColor"].get<std::string>()));
			}
			if (data.contains("annotations") && data["annotations"].is_array()) {
				for (const auto& ja : data["annotations"]) {
					Annotation a;
					a.id = QString::fromStdString(ja.value("id", ""));
					a.x = ja.value("x", 0.5f);
					a.y = ja.value("y", 0.5f);
					a.label = QString::fromStdString(ja.value("label", ""));
					a.color = QColor(QString::fromStdString(ja.value("color", "#c8ff0000")));
					a.type = static_cast<MarkType>(ja.value("type", 0));
					d->annotations.push_back(a);
				}
			}
		}

		// ---------- 渲染 ----------
		bool onPaint(const ModRenderContext& ctx, void* modData) override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!d || !d->visible || d->annotations.empty()) return false;

			QPainter* p = ctx.painter;
			QRectF rect = ctx.rect;

			// 缓存节点尺寸，供鼠标事件做坐标转换
			d->cachedNodeWidth = static_cast<float>(rect.width());
			d->cachedNodeHeight = static_cast<float>(rect.height());

			p->save();
			// 裁剪到节点矩形内，避免标注溢出
			QPainterPath clip;
			clip.addRoundedRect(rect, 6, 6);
			p->setClipPath(clip);

			QFont f = p->font();
			f.setPointSize(9);
			f.setBold(true);
			p->setFont(f);

			for (size_t i = 0; i < d->annotations.size(); ++i) {
				const auto& a = d->annotations[i];
				QPointF center(
					rect.x() + a.x * rect.width(),
					rect.y() + a.y * rect.height()
				);
				drawMark(p, center, a, i == static_cast<size_t>(d->hoveredIdx));
			}

			p->restore();
			return false;  // 不阻止主模组绘制
		}

		// ---------- 交互 ----------
		ModInteractionResult onMousePress(QGraphicsSceneMouseEvent* event,
			const WarNode* /*node*/, void* modData) override
		{
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return ModInteractionResult::Ignored;

			// 只处理 Ctrl+左键（添加标注）或点击已有标注（选中/拖拽）
			QPointF pos = event->pos();

			// 先检查是否点击了已有标注
			int hit = hitTest(d, pos);
			if (hit >= 0) {
				if (event->button() == Qt::LeftButton) {
					d->draggingIdx = hit;
					return ModInteractionResult::Consumed;
				}
				return ModInteractionResult::Ignored;
			}

			// Ctrl+左键：添加新标注
			if (event->button() == Qt::LeftButton &&
				(event->modifiers() & Qt::ControlModifier)) {
				// 需要节点矩形来转换坐标，但 event->pos 已经是节点局部坐标
				// 用 0~1 相对坐标存储，这里需要节点尺寸
				// 由于无法直接获取节点尺寸，用一个临时方案：用 event->scenePos 和节点 rect
				// 实际上 modData 无法拿到 rect，这里先用 pos 占位，在 paint 时再修正
				// 更好的方式：让框架在调用时传入节点 rect
				// 临时方案：用 0.5 0.5 作为初始位置，然后在 onMouseMove 中更新
				// 不行，这样体验差。改为在这里用一个 hack：从 NodeGraphicsItem 拿尺寸
				// 但 modData 里没存 item 指针。
				// 最简方案：在 PrivateData 里缓存节点尺寸，从 onPaint 的 ctx.rect 更新
				Annotation a;
				a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
				a.label = QString("标注 %1").arg(d->annotations.size() + 1);
				a.color = d->defaultColor;
				a.type = d->defaultType;
				// 用缓存的位置信息转换
				if (d->cachedNodeWidth > 0 && d->cachedNodeHeight > 0) {
					a.x = static_cast<float>(pos.x() / d->cachedNodeWidth);
					a.y = static_cast<float>(pos.y() / d->cachedNodeHeight);
					a.x = std::clamp(a.x, 0.0f, 1.0f);
					a.y = std::clamp(a.y, 0.0f, 1.0f);
				} else {
					a.x = 0.5f;
					a.y = 0.5f;
				}
				d->annotations.push_back(a);
				d->hoveredIdx = static_cast<int>(d->annotations.size()) - 1;
				return ModInteractionResult::Consumed;
			}

			return ModInteractionResult::Ignored;
		}

		ModInteractionResult onMouseMove(QGraphicsSceneMouseEvent* event,
			const WarNode* /*node*/, void* modData) override
		{
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return ModInteractionResult::Ignored;

			// 拖拽标注
			if (d->draggingIdx >= 0 && d->draggingIdx < static_cast<int>(d->annotations.size())) {
				QPointF pos = event->pos();
				if (d->cachedNodeWidth > 0 && d->cachedNodeHeight > 0) {
					auto& a = d->annotations[d->draggingIdx];
					a.x = std::clamp(static_cast<float>(pos.x() / d->cachedNodeWidth), 0.0f, 1.0f);
					a.y = std::clamp(static_cast<float>(pos.y() / d->cachedNodeHeight), 0.0f, 1.0f);
				}
				return ModInteractionResult::Consumed;
			}

			// 悬停检测
			int hit = hitTest(d, event->pos());
			if (hit != d->hoveredIdx) {
				d->hoveredIdx = hit;
			}
			return ModInteractionResult::Ignored;
		}

		ModInteractionResult onMouseRelease(QGraphicsSceneMouseEvent* /*event*/,
			const WarNode* /*node*/, void* modData) override
		{
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return ModInteractionResult::Ignored;
			if (d->draggingIdx >= 0) {
				d->draggingIdx = -1;
				return ModInteractionResult::Consumed;
			}
			return ModInteractionResult::Ignored;
		}

		ModInteractionResult onMouseDoubleClick(QGraphicsSceneMouseEvent* event,
			const WarNode* /*node*/, void* modData) override
		{
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return ModInteractionResult::Ignored;

			int hit = hitTest(d, event->pos());
			if (hit < 0) return ModInteractionResult::Ignored;

			// 编辑标签
			bool ok = false;
			QString newLabel = QInputDialog::getText(
				nullptr,
				QObject::tr("编辑标注"),
				QObject::tr("标签:"),
				QLineEdit::Normal,
				d->annotations[hit].label,
				&ok
			);
			if (ok) {
				d->annotations[hit].label = newLabel;
			}
			return ModInteractionResult::Consumed;
		}

		// ---------- 右键菜单 ----------
		bool onContextMenu(const ModMenuContext& ctx, void* modData) override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!d || !ctx.menu) return false;

			ctx.menu->addSeparator();
			QMenu* annoMenu = ctx.menu->addMenu("标注");

			// 显示/隐藏
			QAction* toggleVisible = new QAction(
				d->visible ? "隐藏标注" : "显示标注", annoMenu);
			QObject::connect(toggleVisible, &QAction::triggered, [d, ctx]() {
				d->visible = !d->visible;
				if (ctx.requestNodeRefresh) ctx.requestNodeRefresh(ctx.nodeId);
			});
			annoMenu->addAction(toggleVisible);

			// 默认标记类型
			annoMenu->addSeparator();
			QMenu* typeMenu = annoMenu->addMenu("新建标注类型");
			QStringList typeNames = { "圆形", "矩形", "十字", "箭头" };
			for (int i = 0; i < typeNames.size(); ++i) {
				QAction* act = new QAction(typeNames[i], typeMenu);
				act->setCheckable(true);
				act->setChecked(static_cast<int>(d->defaultType) == i);
				QObject::connect(act, &QAction::triggered, [d, i]() {
					d->defaultType = static_cast<MarkType>(i);
				});
				typeMenu->addAction(act);
			}

			// 默认颜色
			QAction* colorAction = new QAction("默认颜色...", annoMenu);
			QObject::connect(colorAction, &QAction::triggered, [d, ctx]() {
				QColor c = QColorDialog::getColor(d->defaultColor, nullptr,
					QObject::tr("选择标注颜色"), QColorDialog::ShowAlphaChannel);
				if (c.isValid()) d->defaultColor = c;
			});
			annoMenu->addAction(colorAction);

			// 标注列表（删除）
			if (!d->annotations.empty()) {
				annoMenu->addSeparator();
				QMenu* listMenu = annoMenu->addMenu("标注列表");
				for (size_t i = 0; i < d->annotations.size(); ++i) {
					const auto& a = d->annotations[i];
					QString text = a.label.isEmpty()
						? QString("#%1").arg(i + 1) : a.label;
					QAction* item = new QAction(text, listMenu);
					QObject::connect(item, &QAction::triggered, [d, i, ctx]() {
						if (i < d->annotations.size()) {
							d->annotations.erase(d->annotations.begin() + i);
							if (ctx.requestNodeRefresh) ctx.requestNodeRefresh(ctx.nodeId);
						}
					});
					listMenu->addAction(item);
				}

				QAction* clearAll = new QAction("清空所有标注", annoMenu);
				QObject::connect(clearAll, &QAction::triggered, [d, ctx]() {
					d->annotations.clear();
					d->hoveredIdx = -1;
					d->draggingIdx = -1;
					if (ctx.requestNodeRefresh) ctx.requestNodeRefresh(ctx.nodeId);
				});
				annoMenu->addAction(clearAll);
			}

			// 移除标注功能
			annoMenu->addSeparator();
			QAction* removeAction = new QAction("移除标注功能", annoMenu);
			QObject::connect(removeAction, &QAction::triggered, [this, ctx]() {
				if (!ctx.node) return;
				warroom::ModManager::instance().removeAuxiliaryMod(
					ctx.node, "builtin.annotation");
				if (ctx.requestNodeRefresh) ctx.requestNodeRefresh(ctx.nodeId);
			});
			annoMenu->addAction(removeAction);

			return true;
		}

		// 节点尚未启用标注时，在右键菜单里显示"启用标注"入口
		bool onContextMenuForNode(const ModMenuContext& ctx) override {
			if (!ctx.menu || !ctx.node) return false;
			// 只给图片/视频节点显示启用入口（文本节点不需要标注）
			const std::string& pt = ctx.node->primary_mod_type;
			if (pt != "builtin.image" && pt != "builtin.video") return false;

			ctx.menu->addSeparator();
			QAction* enableAction = new QAction("启用标注功能", ctx.menu);
			QObject::connect(enableAction, &QAction::triggered, [this, ctx]() {
				if (!ctx.node) return;
				// AnnotationMod 的 onCreateNode 不依赖 item，传 nullptr 即可
				warroom::ModManager::instance().addAuxiliaryMod(
					const_cast<WarNode*>(ctx.node), nullptr, "builtin.annotation");
				if (ctx.requestNodeRefresh) ctx.requestNodeRefresh(ctx.nodeId);
			});
			ctx.menu->addAction(enableAction);
			return true;
		}

		// 图片/视频节点自动附加标注
		bool shouldAutoAttach(const WarNode* node) const override {
			if (!node) return false;
			return node->primary_mod_type == "builtin.image"
				|| node->primary_mod_type == "builtin.video";
		}

	private:
		// ---------- 绘制单个标注 ----------
		void drawMark(QPainter* p, const QPointF& center, const Annotation& a, bool hovered) const {
			const float r = 10.0f;
			p->setBrush(a.color);
			QPen pen(QColor(255, 255, 255, 220), 1.5);
			if (hovered) pen.setWidthF(2.5);
			p->setPen(pen);

			switch (a.type) {
				case MarkType::Circle: {
					p->drawEllipse(center, r, r);
					break;
				}
				case MarkType::Rect: {
					p->drawRect(QRectF(center.x() - r, center.y() - r, r * 2, r * 2));
					break;
				}
				case MarkType::Cross: {
					p->setBrush(Qt::NoBrush);
					p->drawLine(center.x() - r, center.y(), center.x() + r, center.y());
					p->drawLine(center.x(), center.y() - r, center.x(), center.y() + r);
					// 外圈
					p->setBrush(a.color);
					p->drawEllipse(center, r * 0.4, r * 0.4);
					break;
				}
				case MarkType::Arrow: {
					// 简易向下箭头
					QPainterPath path;
					path.moveTo(center.x(), center.y() + r);
					path.lineTo(center.x() - r * 0.7, center.y() - r * 0.5);
					path.lineTo(center.x() + r * 0.7, center.y() - r * 0.5);
					path.closeSubpath();
					p->drawPath(path);
					break;
				}
			}

			// 标签
			if (!a.label.isEmpty()) {
				p->setPen(QColor(255, 255, 255, 230));
				QFontMetrics fm(p->font());
				int tw = fm.horizontalAdvance(a.label);
				int th = fm.height();
				QRectF labelBg(
					center.x() + r + 2,
					center.y() - th / 2.0,
					tw + 8, th
				);
				p->setBrush(QColor(0, 0, 0, 160));
				p->setPen(Qt::NoPen);
				p->drawRoundedRect(labelBg, 3, 3);
				p->setPen(QColor(255, 255, 255, 230));
				p->drawText(labelBg.adjusted(4, 0, -4, 0),
					Qt::AlignLeft | Qt::AlignVCenter, a.label);
			}
		}

		// ---------- 命中测试 ----------
		// pos 是节点局部坐标
		int hitTest(PrivateData* d, const QPointF& pos) const {
			if (!d) return -1;
			if (d->cachedNodeWidth <= 0 || d->cachedNodeHeight <= 0) return -1;
			for (int i = static_cast<int>(d->annotations.size()) - 1; i >= 0; --i) {
				const auto& a = d->annotations[i];
				QPointF center(
					a.x * d->cachedNodeWidth,
					a.y * d->cachedNodeHeight
				);
				// 用圆形命中区，半径放大一点便于点击
				QPointF diff = pos - center;
				if (diff.x() * diff.x() + diff.y() * diff.y() <= 15.0 * 15.0) {
					return i;
				}
			}
			return -1;
		}
	};

} // namespace warroom
