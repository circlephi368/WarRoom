// NodeGraphicsItem.h
#pragma once
#include <QGraphicsItem>
#include <QPainter>
#include <QPointer>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsProxyWidget>
#include <QStyleOption>
#include <QStyleOptionGraphicsItem>
#include <QPixmap>
#include <QRectF>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextDocument>
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
	NodeGraphicsItem(const std::string& nodeId, warroom::WarRoomModel* model, QGraphicsItem* parent = nullptr);
	~NodeGraphicsItem();
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

	// 刷新字体（字体配置变更时调用）
	void refreshFont(const QFont& font);

	// 刷新文本颜色（节点文本颜色配置变更时调用）
	void refreshTextColor(const QColor& color);

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

	// 获取编辑器中当前选中的文本（仅编辑模式下有效，无选中返回空串）
	QString getSelectedText() const;

	// 直接将传入字符串设为节点标题（由外部菜单动作调用）
	void setTitleFromString(const std::string& newTitle);

	// 强制失效缓存并重绘（用于缩放稳定后）
	void forceRefreshCache() { invalidateCache(); update(); }

	// 编辑器内右键菜单（由 CustomTextEdit::contextMenuEvent 转发触发）
	void showEditorContextMenu(const QPoint& globalPos);

	/**
	 * @brief 在节点被删除前调用，使节点进入"安全状态"
	 * 会隐藏所有锚点、保存并退出编辑模式、断开信号连接
	 */
	void prepareForRemoval();

	/**
	 * @brief 检查节点是否即将被删除（用于判断是否还能进行操作）
	 */
	bool isPendingRemoval() const { return m_pendingRemoval; }


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
	void editFinished(const std::string& nodeId);
	// 节点标题被修改（由右键菜单"选词设标题"/"重命名标题"触发）
	void titleChanged(const std::string& nodeId);
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

	// 拖放支持（拖到已有节点上 → 转发给模组 canAcceptDrop/onDrop）
	void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;
	void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;
	void dropEvent(QGraphicsSceneDragDropEvent* event) override;
	
	//void focusOutEvent(QFocusEvent* event) override;  // 可选：失焦保存

	void keyPressEvent(QKeyEvent* event) override;

private:
	// 辅助方法：获取节点数据
	const warroom::WarNode* getNode() const;
	warroom::WarNode* getNodeMutable();

	std::string m_nodeId;
	warroom::WarRoomModel* m_model;  // 模型指针（外部所有权，析构时可能已失效）

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

	// ---- 文字渲染缓存（按当前缩放等级缓存 pixmap，避免放大模糊） ----
	mutable QPixmap m_cachedPixmap;
	mutable qreal m_cachedZoom = 0.0;   // 缓存时的缩放等级（相对于 scene）
	mutable qreal m_cachedDPR = 0.0;    // 缓存时的 devicePixelRatio
	mutable int m_cacheVersion = -1;   // 缓存版本号（文本/尺寸变化时递增）
	int m_currentVersion = 0;           // 当前版本号（每次内容变化 +1）

	void rebuildCachePixmap(qreal zoom, qreal dpr);
	void invalidateCache();
	bool cacheIsValid(qreal zoom, qreal dpr) const;

	bool m_pendingRemoval = false;  // 标记是否正在被删除

	// 右键菜单显示期间设为 true，用于抑制 FocusOut 触发的退出编辑
	bool m_inContextMenu = false;

	// 浏览器嵌入相关
	QGraphicsProxyWidget* m_browserProxy = nullptr;
	void createBrowserWidget();
	void destroyBrowserWidget();
	void updateBrowserGeometry();
	bool isInBrowseMode() const;

	// 字体等配置（可从模型或全局读取）
	QFont m_editorFont{ "Microsoft YaHei", 10 };
	int m_textPadding = 8;                // 文本内边距
	QColor m_textColor{ 240, 240, 240 };  // 节点文本颜色（默认浅灰）
};