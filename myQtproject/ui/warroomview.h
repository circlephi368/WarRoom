// warroomview.h
#pragma once

#include <QGraphicsView>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QPainter>
#include <QPaintEvent>
#include <QColor>
#include <QPixmap>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QPolygonF>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QTimer>
#include <QApplication>
#include <QDebug>
#include <cmath>
#include "core/warroom/war_room_model.h"
#include "mod/ModManager.h"
#include "CameraAnimator.h"

class WarRoomView : public QGraphicsView
{
	Q_OBJECT

public:
	// 背景样式枚举
	enum class BackgroundStyle {
		Dots = 0,   // 点阵（默认）
		Grid = 1,   // 网格线
		Image = 2   // 图片背景（平铺或拉伸）
	};

	// 图片背景模式
	enum class ImageMode {
		Tiled = 0,    // 平铺（保持原尺寸，重复填满）
		Stretch = 1   // 拉伸铺满整个可见画布
	};

	// 画布拖动模式（鼠标）
	enum class PanMode {
		MiddleButton = 0,    // 鼠标中键
		SpaceLeftButton = 1, // 空格 + 左键
		Both = 2             // 二者皆可
	};

	// 画布移动模式（键盘）
	enum class KeyPanMode {
		ArrowKeys = 0,    // 上下左右
		WASD = 1,         // WASD
		Both = 2          // 二者皆可
	};

	explicit WarRoomView(QGraphicsScene* scene, QWidget* parent = nullptr)
		: QGraphicsView(scene, parent)
	{
		setRenderHint(QPainter::Antialiasing);
		setDragMode(QGraphicsView::RubberBandDrag);   // 左键框选
		setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

		// FullViewportUpdate：强制每帧完整重绘整个视口，避免拖拽拖影和局部更新白边
		setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

		setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
		setResizeAnchor(QGraphicsView::AnchorUnderMouse);
		setCursor(Qt::ArrowCursor);

		// 关键：QGraphicsView 继承自 QFrame，默认有 StyledPanel+Sunken 边框
		// 必须显式移除，否则视口外缘会绘制 1-2px 的浅色/白色边框
		setFrameShape(QFrame::NoFrame);
		setFrameShadow(QFrame::Plain);
		setLineWidth(0);
		setMidLineWidth(0);

		// 默认深色画布背景
		m_backgroundColor = QColor(30, 30, 30);
		setBackgroundBrush(m_backgroundColor);

		// 用样式表设置 viewport 本身的背景色和无边框，覆盖系统默认的绘制
		// 同时设置 QGraphicsView 为透明，让 viewport 的绘制接管
		setStyleSheet(
			"QGraphicsView {"
			"   background: transparent;"
			"   border: none;"
			"   padding: 0px;"
			"}"
			"QGraphicsView > QWidget {"
			"   background-color: #1E1E1E;"
			"   border: none;"
			"   padding: 0px;"
			"}"
		);

		// 禁止 viewport 自动填充背景（由 drawBackground + 样式表接管）
		viewport()->setAutoFillBackground(false);
		viewport()->setAttribute(Qt::WA_NoSystemBackground, true);

		// ---- 键盘平移定时器 ----
		// 持续按下方向键/WASD 时，每 16ms (~60fps) 更新动画器的目标位置
		m_keyPanTimer.setInterval(16);
		connect(&m_keyPanTimer, &QTimer::timeout, this, [this]() {
			if (!m_animator) return;

			const int step = 40;  // 每次移动像素数（视口坐标），越大越快
			int dx = 0, dy = 0;
			if (m_keyPanMask & KeyPanUp)    dy -= step;
			if (m_keyPanMask & KeyPanDown)  dy += step;
			if (m_keyPanMask & KeyPanLeft)  dx -= step;
			if (m_keyPanMask & KeyPanRight) dx += step;

			if (dx == 0 && dy == 0) return;

			// 转换为场景坐标增量（当前缩放下）
			qreal sceneDx = dx / m_currentScale;
			qreal sceneDy = dy / m_currentScale;

			// 注入动画器，由弹簧平滑追踪目标
			m_animator->nudgeTarget(sceneDx, sceneDy);
		});
	}

	// [DESTDBG] 析构函数：记录 view 的销毁，及其当前关联的 scene 指针
	// 用于观察 view 与 scene 的相对销毁顺序
	~WarRoomView() {
		qDebug().nospace().noquote()
			<< "[DESTDBG] >>> ~WarRoomView ENTER this=" << static_cast<void*>(this)
			<< " scene=" << static_cast<void*>(this->scene())
			<< " | (基类 QGraphicsView 析构将处理 scene 关联)";
		qDebug().nospace().noquote()
			<< "[DESTDBG] <<< ~WarRoomView EXIT this=" << static_cast<void*>(this);
	}

	// ---- 画布拖动 / 移动模式配置 ----
	void setPanMode(PanMode m) { m_panMode = m; }
	PanMode getPanMode() const { return m_panMode; }
	void setKeyPanMode(KeyPanMode m) { m_keyPanMode = m; }
	KeyPanMode getKeyPanMode() const { return m_keyPanMode; }

	// ---- 编辑状态标志（由 WarRoomMainWindow 设置）----
	// 当节点处于编辑模式时，键盘平移应被禁用，避免影响文本输入
	void setIsEditing(bool editing) { m_isEditing = editing; }
	bool isEditing() const { return m_isEditing; }

	// ---- 注入动画器（用于平滑平移和缩放）----
	void setAnimator(CameraAnimator* animator) { m_animator = animator; }

	// 设置背景颜色
	void setBackgroundColor(const QColor& color) {
		m_backgroundColor = color;
		setBackgroundBrush(color);

		// 同步更新样式表中 viewport 的背景色，确保边缘与画布一致
		QString style = QString(
			"QGraphicsView {"
			"   background: transparent;"
			"   border: none;"
			"   padding: 0px;"
			"}"
			"QGraphicsView > QWidget {"
			"   background-color: %1;"
			"   border: none;"
			"   padding: 0px;"
			"}"
		).arg(color.name());
		setStyleSheet(style);

		// 颜色改变，重建装饰 tile
		m_cachedTile = QPixmap();
		update();
	}

	QColor getBackgroundColor() const { return m_backgroundColor; }

	// 设置背景样式（点阵 / 网格 / 图片）
	void setBackgroundStyle(BackgroundStyle style) {
		m_backgroundStyle = style;
		m_cachedTile = QPixmap();   // 样式变化，重建 tile
		update();
	}

	BackgroundStyle getBackgroundStyle() const { return m_backgroundStyle; }

	// 设置图片背景（仅在 style=Image 时生效）
	void setBackgroundImage(const QString& imagePath, ImageMode mode = ImageMode::Tiled) {
		m_imagePath = imagePath;
		m_imageMode = mode;
		m_cachedTile = QPixmap();
		if (QFile::exists(imagePath)) {
			m_imagePixmap = QPixmap(imagePath);
		} else {
			m_imagePixmap = QPixmap();
		}
		update();
	}

	QString getBackgroundImagePath() const { return m_imagePath; }
	ImageMode getBackgroundImageMode() const { return m_imageMode; }

	// 检查图片背景是否有效加载
	bool hasValidImage() const { return !m_imagePixmap.isNull(); }

	// 绘制画布背景
	// 说明：使用 tile 平铺方式替代逐点绘制，解决两大问题：
	//   1. 大画布时范围问题：tile 不受 sceneRect 限制，可见区域都会被填满
	//   2. 性能问题：tile 方案比逐点 drawPoint 快 10-100 倍
	void drawBackground(QPainter* painter, const QRectF& rect) override
	{
		Q_UNUSED(rect);

		// 关键：获取当前可见的完整场景矩形（不受传入 rect 限制）
		// mapToScene(viewport()->rect()) 是视口四个角映射到场景坐标的矩形
		QPolygonF visiblePoly = mapToScene(viewport()->rect());
		QRectF visibleRect = visiblePoly.boundingRect();

		// 扩展一个网格外边缘，避免边缘闪烁
		const int margin = 200;
		QRectF drawArea = visibleRect.adjusted(-margin, -margin, margin, margin);

		// 1) 纯色背景
		painter->fillRect(drawArea, m_backgroundColor);

		// 2) 根据当前缩放决定是否叠加装饰
		qreal scale = transform().m11();
		if (scale < 0.15) {
			// 缩放过小时不画装饰（避免过密的视觉混乱）
			return;
		}

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, false);

		if (m_backgroundStyle == BackgroundStyle::Image) {
			// === 图片背景 ===
			if (m_imagePixmap.isNull()) {
				painter->restore();
				return;
			}

			if (m_imageMode == ImageMode::Tiled) {
				// 平铺：以 tile 尺寸对齐到原点，保证滚动时图片连续
				int tw = qMax(1, m_imagePixmap.width());
				int th = qMax(1, m_imagePixmap.height());
				int startX = static_cast<int>(std::floor(drawArea.left() / tw)) * tw;
				int startY = static_cast<int>(std::floor(drawArea.top() / th)) * th;
				int endX = static_cast<int>(drawArea.right());
				int endY = static_cast<int>(drawArea.bottom());
				for (int y = startY; y < endY; y += th) {
					for (int x = startX; x < endX; x += tw) {
						painter->drawPixmap(QPointF(x, y), m_imagePixmap);
					}
				}
			} else {
				// 拉伸：铺满整个可见场景区域
				painter->drawPixmap(drawArea, m_imagePixmap,
					QRectF(0, 0, m_imagePixmap.width(), m_imagePixmap.height()));
			}
		} else {
			// === 点阵 / 网格：用缓存的 tile 平铺 ===
			if (m_cachedTile.isNull()) {
				buildBackgroundTile();
			}
			if (m_cachedTile.isNull()) {
				painter->restore();
				return;
			}

			// 对齐到 tile 边界（与场景坐标 (0,0) 对齐，确保滚动时图案保持连续）
			int tw = m_cachedTile.width();
			int th = m_cachedTile.height();
			int startX = static_cast<int>(std::floor(drawArea.left() / tw)) * tw;
			int startY = static_cast<int>(std::floor(drawArea.top() / th)) * th;
			int endX = static_cast<int>(std::ceil(drawArea.right()));
			int endY = static_cast<int>(std::ceil(drawArea.bottom()));

			for (int y = startY; y < endY; y += th) {
				for (int x = startX; x < endX; x += tw) {
					painter->drawPixmap(QPointF(x, y), m_cachedTile);
				}
			}
		}

		painter->restore();
	}

	warroom::Point2D getViewCenter() const {
		QPointF center = mapToScene(viewport()->rect().center());
		return { static_cast<float>(center.x()), static_cast<float>(center.y()) };
	}

	float getZoomLevel() const { return m_currentScale; }

	void setViewCenter(const warroom::Point2D& center, float zoom) {
		m_currentScale = zoom;
		resetTransform();
		scale(zoom, zoom);
		centerOn(center.x, center.y);
	}

	void saveViewState(warroom::Point2D& pos, float& zoom) {
		pos = getViewCenter();
		zoom = getZoomLevel();
	}

	void restoreViewState(const warroom::Point2D& pos, float zoom) {
		setViewCenter(pos, zoom);
	}

signals:
	// 拖放文件到空白处创建新节点（scenePos 为鼠标在场景中的坐标）
	// mimeData 指针仅在槽函数同步执行期间有效，槽函数内需立即处理
	void dropToCreateNode(QPointF scenePos, const QMimeData* mimeData);
	// 用户开始拖动画布（中键或空格+左键），用于中止相机动画
	void userPanStarted();
	// 视图失去焦点
	void viewFocusLost();

protected:
	void wheelEvent(QWheelEvent* event) override
	{
		if (event->modifiers() & Qt::ControlModifier) {
			// Ctrl + 滚轮：缩放（通过动画器平滑过渡）
			const double scaleFactor = 1.50;  // 每次滚轮缩放 25%，速度更快
			double factor = (event->angleDelta().y() > 0) ? scaleFactor : (1.0 / scaleFactor);
			double newScale = m_currentScale * factor;
			if (newScale < 0.1 || newScale > 5.0) return;
			newScale = std::clamp(newScale, 0.1, 5.0);

			// 获取鼠标在场景中的位置作为缩放锚点
			QPointF mouseScenePos = mapToScene(event->position().toPoint());

			if (m_animator) {
				// 注入动画器，平滑缩放（不触发中断，因为这是动画的一部分）
				m_animator->setZoomTargetAt(mouseScenePos, newScale);
			} else {
				// 无动画器时回退到直接缩放
				m_currentScale = newScale;
				scale(factor, factor);
				emit userPanStarted();
			}
		} else {
			// 普通滚轮：滚动视图（不缩放）
			emit userPanStarted();  // 用户主动滚动，中止相机动画
			QGraphicsView::wheelEvent(event);
		}
	}

	void mousePressEvent(QMouseEvent* event) override
	{
		// 鼠标中键拖动：仅当 panMode 启用中键时
		if (event->button() == Qt::MiddleButton &&
			(m_panMode == PanMode::MiddleButton || m_panMode == PanMode::Both)) {
			m_middleButtonPressed = true;
			m_lastPanPos = event->pos();
			setCursor(Qt::ClosedHandCursor);
			emit userPanStarted();  // 通知中止相机动画
			event->accept();
			return;
		}
		// 空格 + 左键拖动：仅当 panMode 启用空格+左键且空格当前按下时
		if (event->button() == Qt::LeftButton && m_spacePressed &&
			(m_panMode == PanMode::SpaceLeftButton || m_panMode == PanMode::Both)) {
			m_spacePanActive = true;
			m_lastPanPos = event->pos();
			setCursor(Qt::ClosedHandCursor);
			emit userPanStarted();  // 通知中止相机动画
			event->accept();
			return;
		}
		QGraphicsView::mousePressEvent(event);
	}

	void mouseMoveEvent(QMouseEvent* event) override
	{
		if (m_middleButtonPressed || m_spacePanActive) {
			QPoint delta = event->pos() - m_lastPanPos;
			if (!delta.isNull()) {
				horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
				verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
				m_lastPanPos = event->pos();
			}
			event->accept();
			return;
		}
		QGraphicsView::mouseMoveEvent(event);
	}

	void mouseReleaseEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::MiddleButton && m_middleButtonPressed) {
			m_middleButtonPressed = false;
			// 空格仍按下时保持 OpenHand 光标，否则恢复
			setCursor(m_spacePressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
			event->accept();
			return;
		}
		if (event->button() == Qt::LeftButton && m_spacePanActive) {
			m_spacePanActive = false;
			setCursor(m_spacePressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
			event->accept();
			return;
		}
		QGraphicsView::mouseReleaseEvent(event);
	}

	// ========== 键盘平移（空格 / 方向键 / WASD）==========
	void keyPressEvent(QKeyEvent* event) override
	{
		// 节点处于编辑模式时，全部交给基类，避免影响文本输入
		if (m_isEditing) {
			QGraphicsView::keyPressEvent(event);
			return;
		}

		// 空格：进入"待拖动"状态（光标变为 OpenHand）
		if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
			if (m_panMode == PanMode::SpaceLeftButton || m_panMode == PanMode::Both) {
				m_spacePressed = true;
				if (!m_spacePanActive && !m_middleButtonPressed) {
					setCursor(Qt::OpenHandCursor);
				}
				event->accept();
				return;
			}
		}

		// 方向键 / WASD：更新 m_keyPanMask 并启动定时器
		if (m_keyPanMode == KeyPanMode::ArrowKeys || m_keyPanMode == KeyPanMode::Both) {
			int prev = m_keyPanMask;
			switch (event->key()) {
				case Qt::Key_Up:    m_keyPanMask |= KeyPanUp;    break;
				case Qt::Key_Down:  m_keyPanMask |= KeyPanDown;  break;
				case Qt::Key_Left:  m_keyPanMask |= KeyPanLeft;  break;
				case Qt::Key_Right: m_keyPanMask |= KeyPanRight; break;
				default: break;
			}
			if (m_keyPanMask != prev) {
				if (m_keyPanMask != 0 && !m_keyPanTimer.isActive()) m_keyPanTimer.start();
				event->accept();
				return;
			}
		}
		if (m_keyPanMode == KeyPanMode::WASD || m_keyPanMode == KeyPanMode::Both) {
			int prev = m_keyPanMask;
			switch (event->key()) {
				case Qt::Key_W: m_keyPanMask |= KeyPanUp;    break;
				case Qt::Key_S: m_keyPanMask |= KeyPanDown;  break;
				case Qt::Key_A: m_keyPanMask |= KeyPanLeft;  break;
				case Qt::Key_D: m_keyPanMask |= KeyPanRight; break;
				default: break;
			}
			if (m_keyPanMask != prev) {
				if (m_keyPanMask != 0 && !m_keyPanTimer.isActive()) m_keyPanTimer.start();
				event->accept();
				return;
			}
		}

		QGraphicsView::keyPressEvent(event);
	}

	void keyReleaseEvent(QKeyEvent* event) override
	{
		if (m_isEditing) {
			QGraphicsView::keyReleaseEvent(event);
			return;
		}

		if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
			m_spacePressed = false;
			if (!m_spacePanActive && !m_middleButtonPressed) {
				setCursor(Qt::ArrowCursor);
			}
			event->accept();
			return;
		}

		int prev = m_keyPanMask;
		switch (event->key()) {
			case Qt::Key_Up:
			case Qt::Key_W: m_keyPanMask &= ~KeyPanUp;    break;
			case Qt::Key_Down:
			case Qt::Key_S: m_keyPanMask &= ~KeyPanDown;  break;
			case Qt::Key_Left:
			case Qt::Key_A: m_keyPanMask &= ~KeyPanLeft;  break;
			case Qt::Key_Right:
			case Qt::Key_D: m_keyPanMask &= ~KeyPanRight; break;
			default: break;
		}
		if (m_keyPanMask != prev) {
			if (m_keyPanMask == 0 && m_keyPanTimer.isActive()) m_keyPanTimer.stop();
			event->accept();
			return;
		}

		QGraphicsView::keyReleaseEvent(event);
	}

	// 失去焦点时清理键盘状态（防止按住键点击其他控件导致 keyReleaseEvent 丢失）
	void focusOutEvent(QFocusEvent* event) override
	{
		m_keyPanMask = 0;
		if (m_keyPanTimer.isActive()) m_keyPanTimer.stop();
		m_spacePressed = false;
		m_spacePanActive = false;

		if (!m_middleButtonPressed) {
			setCursor(Qt::ArrowCursor);
		}

		emit viewFocusLost();
		QGraphicsView::focusOutEvent(event);
	}

	// ========== 拖放支持（空白处创建节点）==========
	// 遍历所有主模组，任一 canCreateNodeFromDrop 返回 true 就接受拖放
	void dragEnterEvent(QDragEnterEvent* event) override
	{
		if (event->source() == this) {
			event->ignore();
			return;
		}
		const QMimeData* mime = event->mimeData();
		if (!mime) { event->ignore(); return; }
		auto& mm = warroom::ModManager::instance();
		for (const std::string& modId : mm.getPrimaryMods()) {
			if (warroom::NodeMod* mod = mm.getMod(modId)) {
				if (mod->canCreateNodeFromDrop(mime)) {
					event->acceptProposedAction();
					return;
				}
			}
		}
		event->ignore();
	}

	void dragMoveEvent(QDragMoveEvent* event) override
	{
		if (event->source() == this) {
			event->ignore();
			return;
		}
		const QMimeData* mime = event->mimeData();
		if (!mime) { event->ignore(); return; }
		auto& mm = warroom::ModManager::instance();
		for (const std::string& modId : mm.getPrimaryMods()) {
			if (warroom::NodeMod* mod = mm.getMod(modId)) {
				if (mod->canCreateNodeFromDrop(mime)) {
					event->acceptProposedAction();
					return;
				}
			}
		}
		event->ignore();
	}

	void dropEvent(QDropEvent* event) override
	{
		if (event->source() == this) {
			event->ignore();
			return;
		}
		const QMimeData* mime = event->mimeData();
		if (!mime) { event->ignore(); return; }

		// 找到第一个能处理的主模组
		auto& mm = warroom::ModManager::instance();
		for (const std::string& modId : mm.getPrimaryMods()) {
			if (warroom::NodeMod* mod = mm.getMod(modId)) {
				if (mod->canCreateNodeFromDrop(mime)) {
					// 转换为场景坐标，发信号让 MainWindow 创建节点
					QPointF scenePos = mapToScene(event->pos());
					emit dropToCreateNode(scenePos, mime);
					event->acceptProposedAction();
					return;
				}
			}
		}
		event->ignore();
	}

private:
	// 构建点阵/网格 tile（200x200，与 bigStep 同尺寸）
	// tile 是"以场景坐标 (0,0) 为原点的一个网格单元"，平铺时以 tile 尺寸对齐
	void buildBackgroundTile() {
		const int tileSize = 200;  // 与 bigStep 一致
		const int smallStep = 40;

		m_cachedTile = QPixmap(tileSize, tileSize);
		m_cachedTile.fill(m_backgroundColor);

		QPainter p(&m_cachedTile);
		p.setRenderHint(QPainter::Antialiasing, false);

		if (m_backgroundStyle == BackgroundStyle::Grid) {
			// ---- 网格线 tile ----
			// 细线（每 40px）
			QPen thinPen(QColor(60, 60, 60, 180), 1);
			p.setPen(thinPen);
			for (int i = smallStep; i < tileSize; i += smallStep) {
				if (i != 0 && i != tileSize && i % tileSize != 0) {
					p.drawLine(i, 0, i, tileSize);
					p.drawLine(0, i, tileSize, i);
				}
			}
			// 粗线（tile 边界 = 200px，作为大网格线）
			QPen thickPen(QColor(85, 85, 85, 220), 1);
			p.setPen(thickPen);
			// 画 tile 的上边和左边（相邻 tile 衔接，就形成完整网格）
			p.drawLine(0, 0, tileSize, 0);
			p.drawLine(0, 0, 0, tileSize);

			// 在右下角画一条稍粗的线（确保 tile 边界视觉一致）
			p.drawLine(tileSize - 1, 0, tileSize - 1, tileSize);
			p.drawLine(0, tileSize - 1, tileSize, tileSize - 1);
		} else {
			// ---- 点阵 tile（默认） ----
			QPen smallPen(QColor(60, 60, 60, 180), 1);
			QPen bigPen(QColor(85, 85, 85, 220), 1);

			// 小格点（每 40px）
			p.setPen(smallPen);
			for (int x = 0; x < tileSize; x += smallStep) {
				for (int y = 0; y < tileSize; y += smallStep) {
					if ((x == 0 && y == 0) || (x == tileSize && y == 0) ||
						(x == 0 && y == tileSize) || (x == tileSize && y == tileSize))
						continue;  // 四个角留给"大格点"
					if (x % tileSize == 0 && y % tileSize == 0) continue;
					p.drawPoint(x, y);
				}
			}
			// 大格点（每 200px，即 tile 的四角）
			// 左上角 (0,0) 作为全局锚点；右上、左下、右下也要画，确保平铺时每 200px 都有大格点
			p.setPen(bigPen);
			p.drawPoint(0, 0);
			// 其他三个角由相邻 tile 的 (0,0) 覆盖，但为了保险还是画上：
			p.drawPoint(tileSize - 1, 0);
			p.drawPoint(0, tileSize - 1);
			p.drawPoint(tileSize - 1, tileSize - 1);
		}

		p.end();
	}

	double m_currentScale = 1.0;
	bool m_middleButtonPressed = false;
	QPoint m_lastPanPos;
	QColor m_backgroundColor;
	BackgroundStyle m_backgroundStyle = BackgroundStyle::Dots;
	mutable QPixmap m_cachedTile;    // 缓存的装饰 tile（200x200）

	// 图片背景
	QString m_imagePath;
	ImageMode m_imageMode = ImageMode::Tiled;
	QPixmap m_imagePixmap;

	// ---- 画布拖动 / 移动相关 ----
	PanMode m_panMode = PanMode::MiddleButton;
	KeyPanMode m_keyPanMode = KeyPanMode::ArrowKeys;

	bool m_spacePressed = false;       // 空格键当前是否按下
	bool m_spacePanActive = false;     // 空格+左键拖动进行中

	// 键盘平移方向位掩码
	enum KeyPanBits {
		KeyPanUp    = 1 << 0,
		KeyPanDown  = 1 << 1,
		KeyPanLeft  = 1 << 2,
		KeyPanRight = 1 << 3
	};
	int m_keyPanMask = 0;              // 当前按下的方向位组合
	QTimer m_keyPanTimer;              // 持续平移定时器

	bool m_isEditing = false;          // 是否有节点处于编辑模式（由 WarRoomMainWindow 设置）

	CameraAnimator* m_animator = nullptr;  // 相机动画器（用于平滑平移和缩放）
};
