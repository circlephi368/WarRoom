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

        // ========== 辅助工具 ==========
        template<typename T>
        T* getModData(void* modData) {
            return reinterpret_cast<T*>(modData);
        }
    };

} // namespace warroom