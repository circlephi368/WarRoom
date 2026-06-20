// mod/NodeMod.h
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>
#include <QPainter>
#include <QWidget>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>

#include "mod/KeyBinding.h"

// NodeGraphicsItem 位于全局命名空间，需要在 warroom 命名空间外做前向声明
class NodeGraphicsItem;

namespace warroom {

	class WarNode;
	class WarRoomModel;
	class WarLink;

	// 模组元信息
	struct ModInfo {
		std::string id;
		std::string name;
		std::string version;
		std::string author;
		std::string description;
		std::string icon;  // 图标路径或名称
	};

	// 模组优先级
	enum class ModPriority {
		Lowest = 0,
		Low = 1,
		Normal = 2,
		High = 3,
		Highest = 4
	};

	// 模组渲染上下文
	struct ModRenderContext {
		QPainter* painter;
		QRectF rect;
		const WarNode* node;
		float zoomLevel;
		bool isSelected;
		bool isHovered;
	};

	// 模组右键菜单上下文
	struct ModMenuContext {
		QMenu* menu;                    // 主菜单（由主窗口创建）
		QWidget* parent;                // 父窗口（用于创建子菜单等）
		WarNode* node;                  // 右键点击的节点
		void* modData;                  // 模组私有数据
		std::string nodeId;             // 节点ID
		// 模组修改数据后可调用此回调请求刷新节点显示
		std::function<void(const std::string& nodeId)> requestNodeRefresh;
	};

	// 模组交互结果
	enum class ModInteractionResult {
		Ignored,     // 未处理，继续传递
		Handled,     // 已处理，停止传递
		Consumed     // 已消费，停止传递并阻止默认行为
	};

	// 节点模组基类
	class NodeMod {
	public:
		virtual ~NodeMod() = default;

		// ========== 基础信息 ==========
		virtual ModInfo getInfo() const = 0;
		virtual bool isPrimary() const { return true; }
		virtual ModPriority getPriority() const { return ModPriority::Normal; }

		// ========== 生命周期 ==========
		virtual void* onCreateNode(WarNode* node, ::NodeGraphicsItem* item) { return nullptr; }
		virtual void onDestroyNode(void* modData) {}
		virtual void onNodeLoaded(WarNode* node, void* modData) {}
		virtual void onNodeSaved(WarNode* node, void* modData) {}

		// ========== 序列化 ==========
		virtual nlohmann::json serialize(void* modData) const { return {}; }
		virtual void deserialize(void* modData, const nlohmann::json& data) {}

		// ========== 渲染 ==========
		// 返回值：true=已绘制主要部分，false=需要继续绘制
		virtual bool onPaint(const ModRenderContext& ctx, void* modData) { return false; }

		// 获取模组推荐的节点大小
		virtual QSizeF getPreferredSize(const WarNode* node, void* modData) const {
			return QSizeF(160, 60);
		}

		// 获取模组需要的最小大小
		virtual QSizeF getMinimumSize(const WarNode* node, void* modData) const {
			return QSizeF(60, 40);
		}

		// ========== 右键菜单 ==========
		// 允许模组向右键菜单添加自己的项
		// 返回值：true=添加了分隔线（后续菜单项应与默认项分开），false=没有添加分隔线
		virtual bool onContextMenu(const ModMenuContext& ctx, void* modData) {
			Q_UNUSED(ctx);
			Q_UNUSED(modData);
			return false;
		}

		// 节点尚未启用该辅助模组时，也能在右键菜单里显示入口
		// 用于"启用 XX 模组"之类的操作
		// 返回值：true=添加了菜单项，false=不显示
		virtual bool onContextMenuForNode(const ModMenuContext& ctx) {
			Q_UNUSED(ctx);
			return false;
		}

		// 节点创建时，辅助模组可决定是否自动附加到该节点
		// 返回 true 表示自动附加
		virtual bool shouldAutoAttach(const WarNode* node) const {
			Q_UNUSED(node);
			return false;
		}

		// ========== 模组设置界面 ==========
		// 是否支持自定义设置（默认 false）
		// 返回 true 则"模组设置"页面会显示"设置"按钮
		virtual bool hasSettings() const { return false; }

		// 创建设置界面的 widget（parent 由调用方管理生命周期）
		// 返回的 widget 中模组可放置自己的配置控件
		virtual QWidget* createSettingsWidget(QWidget* parent) {
			Q_UNUSED(parent);
			return nullptr;
		}

		// 保存设置（从 widget 中读取用户配置并应用到模组）
		// widget 就是 createSettingsWidget 返回的那个
		virtual void saveSettings(QWidget* widget) {
			Q_UNUSED(widget);
		}

		// ========== 键位设置 ==========
		// 返回模组的键位绑定列表（主程序会统一在"键位设置"页面展示）
		// 默认返回空列表
		virtual std::vector<KeyBinding> getKeyBindings() const {
			return {};
		}

		// ========== 交互（责任链） ==========
		virtual ModInteractionResult onMousePress(QGraphicsSceneMouseEvent* event,
			const WarNode* node, void* modData) {
			return ModInteractionResult::Ignored;
		}

		virtual ModInteractionResult onMouseMove(QGraphicsSceneMouseEvent* event,
			const WarNode* node, void* modData) {
			return ModInteractionResult::Ignored;
		}

		virtual ModInteractionResult onMouseRelease(QGraphicsSceneMouseEvent* event,
			const WarNode* node, void* modData) {
			return ModInteractionResult::Ignored;
		}

		virtual ModInteractionResult onMouseDoubleClick(QGraphicsSceneMouseEvent* event,
			const WarNode* node, void* modData) {
			return ModInteractionResult::Ignored;
		}

		virtual ModInteractionResult onHoverEnter(QGraphicsSceneHoverEvent* event,
			const WarNode* node, void* modData) {
			return ModInteractionResult::Ignored;
		}

		virtual ModInteractionResult onHoverLeave(QGraphicsSceneHoverEvent* event,
			const WarNode* node, void* modData) {
			return ModInteractionResult::Ignored;
		}

		virtual ModInteractionResult onHoverMove(QGraphicsSceneHoverEvent* event,
			const WarNode* node, void* modData) {
			return ModInteractionResult::Ignored;
		}

		// ========== 键盘事件 ==========
		virtual ModInteractionResult onKeyPress(QKeyEvent* event,
			const WarNode* node, void* modData) {
			return ModInteractionResult::Ignored;
		}

		virtual ModInteractionResult onKeyRelease(QKeyEvent* event,
			const WarNode* node, void* modData) {
			return ModInteractionResult::Ignored;
		}

		// ========== 编辑器 ==========
		virtual QWidget* createEditorWidget(WarNode* node, void* modData, QWidget* parent) {
			return nullptr;
		}

		virtual void updateEditorWidget(QWidget* editor, WarNode* node, void* modData) {}
		virtual void saveFromEditorWidget(QWidget* editor, WarNode* node, void* modData) {}

		// ========== 嵌入 Widget 支持 ==========
		// 返回该模组是否支持在节点内嵌入 QWidget（如 QWebEngineView）
		virtual bool hasEmbeddedWidget() const { return false; }

		// 创建并返回要嵌入的 QWidget（如浏览器视图）
		// parent: QGraphicsProxyWidget，可作为子 widget 的父级
		virtual QWidget* createEmbeddedWidget(WarNode* node, void* modData, QWidget* parent) {
			Q_UNUSED(node);
			Q_UNUSED(modData);
			Q_UNUSED(parent);
			return nullptr;
		}

		// 嵌入 widget 的几何信息需要更新时调用
		virtual void updateEmbeddedWidgetGeometry(QWidget* widget, const QRectF& sceneRect, double zoomLevel) {
			Q_UNUSED(widget);
			Q_UNUSED(sceneRect);
			Q_UNUSED(zoomLevel);
		}

		// 销毁嵌入的 widget
		virtual void destroyEmbeddedWidget(QWidget* widget, void* modData) {
			Q_UNUSED(widget);
			Q_UNUSED(modData);
		}

		// 当前是否处于"嵌入 widget 显示模式"
		// 用于控制 paint() 行为：嵌入模式下跳过 onPaint() 绘制
		virtual bool isEmbeddedWidgetActive(void* modData) const {
			Q_UNUSED(modData);
			return false;
		}

		// ========== 连线相关 ==========
		virtual bool canConnectTo(const WarNode* from, const WarNode* to, void* modData) const {
			return true;
		}

		virtual void onConnectionCreated(const WarLink& link, void* modData) {}
		virtual void onConnectionRemoved(const WarLink& link, void* modData) {}

		// ========== 拖放 ==========
		virtual bool canAcceptDrop(const QMimeData* mimeData, const WarNode* node, void* modData) const {
			return false;
		}

		virtual void onDrop(const QMimeData* mimeData, WarNode* node, void* modData) {}

		// ========== 拖放到空白处创建新节点 ==========
		// 判断该模组能否为这种拖放数据创建新节点（只看 mime 数据，不依赖已有节点）
		// 返回 true 表示"我能处理这种文件，请用我作为主模组创建节点"
		virtual bool canCreateNodeFromDrop(const QMimeData* mimeData) const {
			return false;
		}

		// 新节点创建后调用，让模组填充自己的数据
		// 节点已创建好（primary_mod_type 已设为本模组 id），
		// modData 已通过 onCreateNode 创建，模组在此设置路径等
		virtual void onDropToNewNode(const QMimeData* mimeData, WarNode* node, void* modData) {}

		// ========== 存档目录支持（仅"主模组"需要实现） ==========
		// 返回此节点上需要被打包进存档目录的所有"外部文件"的本地绝对路径。
		// 文件会被复制到：<存档目录>/mod_data/<modId>/<nodeId>_<basename>
		// 主模组需要在 serialize() 之前通过这个钩子让框架完成文件拷贝。
		virtual QStringList collectExternalFiles(const WarNode* /*node*/, void* /*modData*/) const {
			return {};
		}

		// 加载时：把存档目录的 mod_data/<modId>/ 路径告诉模组，便于解析相对路径。
		// 框架在反序列化完每个节点后立即调用。
		virtual void setArchiveBaseDir(const QString& /*archiveDir*/,
			const QString& /*modDataSubdir*/) {}

		// ========== 辅助工具 ==========
		template<typename T>
		T* getModData(void* modData) {
			return reinterpret_cast<T*>(modData);
		}
	};

} // namespace warroom