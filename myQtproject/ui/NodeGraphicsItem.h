// NodeGraphicsItem.h
#pragma once
#include <QGraphicsItem>
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsProxyWidget>
#include <QTextEdit>
#include <string>
#include "core/warroom/war_room_model.h"
#include "ConnectionAnchor.h"


enum class EditMode {
    Preview,      // 静态渲染模式
    Editing       // 编辑器模式
};

class NodeGraphicsItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // 构造函数声明
    NodeGraphicsItem(const std::string& nodeId, warroom::WarRoomModel& model, QGraphicsItem* parent = nullptr);

    // 包围盒
    QRectF boundingRect() const override;
    // 获取自定义包围盒（包含子树）
    QRectF getCustomBoundingRect() const { return m_customBoundingRect; }

    // 更新自身的自定义包围盒（递归计算子树）
    void updateCustomBoundingRect();

    // 全量重算所有包围盒（静态方法，方便触发）
    static void rebuildAllBoundingRects(QHash<QString, NodeGraphicsItem*>& nodeItems);
    
    // 绘制
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override;

    // 获取四个边缘锚点的位置（相对于节点）
    QPointF getAnchorPos(int edge) const;

    // 创建锚点
    void createAnchors();
    // 更新锚点位置
    void updateAnchorsPosition();
    // 获取锚点列表
    QList<ConnectionAnchor*> anchors() const { return m_anchors; }

    // 刷新显示（从模型重新读取数据）
    void refresh();

    // 更新 Z 顺序
    void updateAbsoluteZ(int absolute_z);

    // 获取节点ID
    const std::string& nodeId() const { return m_nodeId; }

    // 获取节点尺寸（直接从模型获取）
    float getWidth() const;
    float getHeight() const;

    // 八个调整方向
    enum ResizeHandle {
        Handle_None = -1,
        Handle_TopLeft,
        Handle_Top,
        Handle_TopRight,
        Handle_Right,
        Handle_BottomRight,
        Handle_Bottom,
        Handle_BottomLeft,
        Handle_Left
    };

    // 获取当前悬停的手柄（用于光标样式）
    ResizeHandle handleAt(const QPointF& scenePos) const;

    // 设置调整大小模式
    void setResizingEnabled(bool enabled) { m_resizingEnabled = enabled; }

    // 设置节点大小（会修改模型）
    void setNodeSize(float width, float height);

    void setEditMode(EditMode mode);
    EditMode editMode() const { return m_editMode; }

    // 由外部调用：强制保存并退出编辑（例如点击其他节点时）
    void saveAndExitEditMode();

signals:
    void positionChanged(const std::string& nodeId, float newX, float newY);
    void moveFinished(const std::string& nodeId, float oldX, float oldY, float newX, float newY);
    //节点大小改变信号
    void sizeChanged(const std::string& nodeId, float newWidth, float newHeight);
    //大小调整完成信号（用于撤销命令）
    void resizeFinished(const std::string& nodeId,
        float oldWidth, float oldHeight,
        float newWidth, float newHeight);
    void selectedForZBoost(const std::string& nodeId);  //选中时提升 Z 值
    //编辑请求信号
    void editRequested(const std::string& nodeId);
protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;  // 更新光标
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    
    //void focusOutEvent(QFocusEvent* event) override;  // 可选：失焦保存
private:
    // 辅助方法：获取节点数据
    const warroom::WarNode* getNode() const;
    warroom::WarNode* getNodeMutable();

    std::string m_nodeId;
    warroom::WarRoomModel& m_model;  // 模型引用

    QList<ConnectionAnchor*> m_anchors;

    QRectF m_customBoundingRect;  // 包含自身+所有子孙的包围盒
    // 拖拽相关
    float m_dragStartX = 0.0f;
    float m_dragStartY = 0.0f;

    // 调整大小相关成员
    bool m_resizing = false;
    ResizeHandle m_activeHandle = Handle_None;
    QPointF m_resizeStartLocalPos;           // 场景坐标
    float m_resizeStartWidth = 0.0f;
    float m_resizeStartHeight = 0.0f;
    bool m_resizingEnabled = true;      // 是否允许调整大小

    static constexpr int HANDLE_SIZE = 12;      // 手柄大小（像素）
    static constexpr int HANDLE_HIT_TOLERANCE = 10;  // 命中容差

    void createInlineEditor();
    void destroyEditor();
    void saveContentToModel();           // 将编辑器内容写回模型
    void refreshPreviewDocument();       // 从模型更新 QTextDocument
    void updateEditorGeometry();         // 编辑器位置/大小同步
    void initializePreviewDocument();
    EditMode m_editMode = EditMode::Preview;
    QGraphicsProxyWidget* m_editorProxy = nullptr;
    QTextEdit* m_textEdit = nullptr;
    QTextDocument m_previewDocument;      // 预览用文档（与编辑器共用配置）

    // 字体等配置（可从模型或全局读取）
    QFont m_editorFont{ "Microsoft YaHei", 10 };
    int m_textPadding = 8;                // 文本内边距
};