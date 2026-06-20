// mod/builtin/VideoMod.h
//
// 内置主模组：视频节点。
//
// 行为：
//   - 节点绘制视频首帧缩略图（若可提取），否则显示视频图标+文件名占位；
//   - 在节点的 primary_mod_data 里存 { "path": "相对/绝对路径" }；
//   - 双击节点弹出文件对话框更换视频；
//   - 支持拖放本地视频文件到节点上；
//   - 视频若加载不到则降级为浅灰底 + "视频缺失"占位，不影响整体运行。
//
// 路径策略：
//   - 优先按"模型当前文档路径"为基准解析相对路径；
//   - 若用户选择的视频不在文档目录下，保留绝对路径。
//
#pragma once

#include "mod/NodeMod.h"
#include "core/warroom/war_node.h"
#include "ui/NodeGraphicsItem.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QDebug>
#include <QPixmap>
#include <QPainter>
#include <QCoreApplication>
#include <QString>
#include <QDir>
#include <QMimeData>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QImage>
#include <QUrl>
#include <QElapsedTimer>
#include <QEventLoop>

namespace warroom {

	class VideoMod : public NodeMod {
	public:
		// ---------- 存档目录支持 ----------
		// mod_data/builtin.video 目录路径
		static QString& modDataSubdir() {
			static QString dir = "mod_data/builtin.video";
			return dir;
		}

		// ---------- 模组级单例的"当前文档目录" ----------
		static QString& currentDocumentDir() {
			static QString dir;
			return dir;
		}
		static void setCurrentDocumentDir(const QString& dir) {
			currentDocumentDir() = dir;
		}

		// ---------- 节点私有数据 ----------
		struct PrivateData {
			QString storedPath;
			QPixmap thumbnail;
			QString resolvedAbsPath;
			QString fileName;
			bool loadingFailed = false;

			// 视频播放
			QMediaPlayer* player = nullptr;
			QVideoSink* videoSink = nullptr;
			QPixmap currentFrame;
			bool isPlaying = false;
			NodeGraphicsItem* graphicsItem = nullptr;

			~PrivateData() {
				// [DESTDBG] 析构入口，记录 this、player、videoSink 地址
				// 若同一 this 出现两次 -> PrivateData 被双重析构
				qDebug().nospace().noquote()
					<< "[DESTDBG] >>> ~VideoMod::PrivateData ENTER this=" << static_cast<void*>(this)
					<< " player=" << static_cast<void*>(player)
					<< " videoSink=" << static_cast<void*>(videoSink)
					<< " graphicsItem=" << static_cast<void*>(graphicsItem)
					<< " isPlaying=" << isPlaying
					<< " storedPath=" << storedPath;
				if (player) {
					qDebug() << "[DESTDBG]   ~VideoMod::PrivateData: stopping & deleting player";
					player->stop();
					delete player;
					player = nullptr;
				}
				// videoSink 由 QObject 父子机制自动清理（创建时 parent = player）
				videoSink = nullptr;
				qDebug().nospace().noquote()
					<< "[DESTDBG] <<< ~VideoMod::PrivateData EXIT this=" << static_cast<void*>(this);
			}
		};

		// ---------- 基础信息 ----------
		ModInfo getInfo() const override {
			return {
				"builtin.video",          // id
				"Video",                  // name
				"0.1.0",                  // version
				"warroom",                // author
				"Display a video thumbnail inside a node.",
				""                        // icon
			};
		}
		bool isPrimary() const override { return true; }

		// ---------- 生命周期 ----------
		void* onCreateNode(WarNode* /*node*/, ::NodeGraphicsItem* item) override {
			auto* d = new PrivateData();
			d->graphicsItem = item;
			return d;
		}
		void onDestroyNode(void* modData) override {
			delete static_cast<PrivateData*>(modData);
		}
		void onNodeLoaded(WarNode* /*node*/, void* modData) override {
			auto* d = static_cast<PrivateData*>(modData);
			reloadThumbnail(d);
		}

		// ---------- 序列化 ----------
		nlohmann::json serialize(void* modData) const override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return {};
			nlohmann::json j;
			j["path"] = d->storedPath.toStdString();
			return j;
		}
		void deserialize(void* modData, const nlohmann::json& data) override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return;
			if (data.contains("path") && data["path"].is_string()) {
				d->storedPath = QString::fromStdString(data["path"].get<std::string>());
			}
		}

		// ---------- 渲染 ----------
		bool onPaint(const ModRenderContext& ctx, void* modData) override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!ctx.painter) return false;

			QPainter* p = ctx.painter;
			QRectF rect = ctx.rect;

			// 背景：深色卡片，适合视频内容
			p->setBrush(QColor(35, 35, 35));
			p->setPen(QPen(QColor(80, 80, 80), 1));
			p->drawRoundedRect(rect, 6, 6);

			if (!d || d->loadingFailed || (d->thumbnail.isNull() && d->currentFrame.isNull())) {
				// 绘制占位符
				drawPlaceholder(p, rect, d);
				return true;
			}

			// 播放时显示当前帧，否则显示缩略图
			QPixmap drawPix;
			if (d->isPlaying && !d->currentFrame.isNull()) {
				drawPix = d->currentFrame;
			}
			else {
				drawPix = d->thumbnail;
			}

			// 等比缩放居中绘制
			QSize target = rect.size().toSize();
			QPixmap scaled = drawPix.scaled(
				target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			QRectF dst(
				rect.x() + (rect.width() - scaled.width()) / 2.0,
				rect.y() + (rect.height() - scaled.height()) / 2.0,
				scaled.width(), scaled.height()
			);
			p->drawPixmap(dst.toRect(), scaled);

			// 在底部叠加半透明的文件名条
			if (!d->fileName.isEmpty()) {
				QRectF textBg(rect.x(), rect.bottom() - 24, rect.width(), 24);
				p->fillRect(textBg, QColor(0, 0, 0, 160));
				p->setPen(QColor(220, 220, 220));
				QFont f = p->font();
				f.setPointSize(8);
				p->setFont(f);
				QString elided = QFontMetrics(f).elidedText(
					d->fileName, Qt::ElideMiddle, static_cast<int>(rect.width() - 12));
				p->drawText(textBg, Qt::AlignCenter, elided);
			}

			// 未播放时绘制播放按钮图标
			if (!d->isPlaying) {
				drawPlayButton(p, rect);
			}

			return true;
		}

		QSizeF getPreferredSize(const WarNode* /*node*/, void* modData) const override {
			auto* d = static_cast<PrivateData*>(modData);
			if (d && !d->thumbnail.isNull()) {
				// 按缩略图尺寸建议（限制在 160~640 之间）
				int w = qBound(160, d->thumbnail.width(), 640);
				int h = qBound(120, d->thumbnail.height(), 480);
				return QSizeF(w, h);
			}
			return QSizeF(240, 180);
		}

		// ---------- 交互：点击播放/暂停 ----------
		ModInteractionResult onMousePress(QGraphicsSceneMouseEvent* event,
			const WarNode* /*node*/, void* modData) override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!d || !d->player || d->loadingFailed) return ModInteractionResult::Ignored;

			QPointF center = event->pos();
			QPointF buttonCenter(d->graphicsItem->boundingRect().center().x(),
				d->graphicsItem->boundingRect().center().y() - 10);

			// 判断是否点击在播放按钮区域（圆形区域，半径 18）
			qreal dist = QLineF(center, buttonCenter).length();
			if (dist <= 18) {
				if (d->isPlaying) {
					d->player->pause();
					d->isPlaying = false;
				}
				else {
					d->player->play();
					d->isPlaying = true;
				}
				return ModInteractionResult::Consumed;
			}
			return ModInteractionResult::Ignored;
		}

		// ---------- 交互：双击换视频 ----------
		ModInteractionResult onMouseDoubleClick(QGraphicsSceneMouseEvent* event,
			const WarNode* node, void* modData) override
		{
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return ModInteractionResult::Ignored;

			QString picked = QFileDialog::getOpenFileName(
				nullptr,
				QObject::tr("Choose a video"),
				d->resolvedAbsPath.isEmpty() ? QString() : QFileInfo(d->resolvedAbsPath).absolutePath(),
				QObject::tr("Videos (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v)")
			);
			if (picked.isEmpty()) {
				return ModInteractionResult::Consumed;
			}
			setVideoPath(const_cast<WarNode*>(node), d, picked);
			return ModInteractionResult::Consumed;
		}

		// ---------- 拖放接收视频 ----------
		bool canAcceptDrop(const QMimeData* mimeData,
			const WarNode* /*node*/, void* /*modData*/) const override {
			if (!mimeData || !mimeData->hasUrls()) return false;
			QList<QUrl> urls = mimeData->urls();
			if (urls.isEmpty()) return false;
			QString file = urls.first().toLocalFile();
			return isVideoFile(file);
		}

		void setArchiveBaseDir(const QString& /*archiveDir*/,
			const QString& /*modDataSubdir*/) override {
			// 模组使用 currentDocumentDir() 解析路径
		}

		// 节点绑定的"外部视频文件"绝对路径
		QStringList collectExternalFiles(const WarNode* node, void* modData) const override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return {};
			// 优先使用运行时已解析的绝对路径（加载时已缓存），
			// 避免保存时 currentDocumentDir() 被修改导致解析错误
			if (!d->resolvedAbsPath.isEmpty()) {
				QFileInfo fi(d->resolvedAbsPath);
				if (fi.exists()) return { d->resolvedAbsPath };
			}
			// 回退：重新解析 storedPath
			QString abs = resolveStored(d->storedPath);
			if (abs.isEmpty()) return {};
			QFileInfo fi(abs);
			if (!fi.exists() || !fi.isAbsolute()) return {};
			return { abs };
		}

		void onDrop(const QMimeData* mimeData, WarNode* node, void* modData) override {
			if (!mimeData || !mimeData->hasUrls()) return;
			QList<QUrl> urls = mimeData->urls();
			if (urls.isEmpty()) return;
			QString file = urls.first().toLocalFile();
			if (file.isEmpty() || !isVideoFile(file)) return;
			setVideoPath(node, static_cast<PrivateData*>(modData), file);
		}

		// ---------- 拖放到空白处创建新视频节点 ----------
		bool canCreateNodeFromDrop(const QMimeData* mimeData) const override {
			if (!mimeData || !mimeData->hasUrls()) return false;
			QString file = mimeData->urls().first().toLocalFile();
			if (file.isEmpty()) return false;
			return isVideoFile(file);
		}
		void onDropToNewNode(const QMimeData* mimeData, WarNode* node, void* modData) override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!mimeData || !mimeData->hasUrls()) return;
			QString file = mimeData->urls().first().toLocalFile();
			if (file.isEmpty() || !isVideoFile(file)) return;
			setVideoPath(node, d, file);
		}

	private:
		// ---------- 辅助方法 ----------
		static bool isVideoFile(const QString& filePath) {
			static const QStringList videoExts = {
				"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "mpeg", "mpg", "3gp"
			};
			QString ext = QFileInfo(filePath).suffix().toLower();
			return videoExts.contains(ext);
		}

		static QString toStoredPath(const QString& absPath) {
			const QString docDir = currentDocumentDir();
			if (docDir.isEmpty()) return absPath;
			QDir base(docDir);
			QString rel = base.relativeFilePath(absPath);
			if (rel.startsWith("..") || rel.contains(":/")) return absPath;
			return rel;
		}

		static QString resolveStored(const QString& stored) {
			if (stored.isEmpty()) return {};
			QFileInfo fi(stored);
			if (fi.isAbsolute()) return fi.absoluteFilePath();
			const QString docDir = currentDocumentDir();
			if (docDir.isEmpty()) return stored;
			return QDir(docDir).absoluteFilePath(stored);
		}

		void setVideoPath(WarNode* /*node*/, PrivateData* d, const QString& absPath) {
			if (!d) return;
			d->storedPath = toStoredPath(absPath);
			reloadThumbnail(d);
		}

		void reloadThumbnail(PrivateData* d) {
			if (!d) return;

			// 清理旧的播放器（videoSink 由 QObject 父子机制自动清理）
			if (d->player) {
				d->player->stop();
				delete d->player;
				d->player = nullptr;
			}
			d->videoSink = nullptr;
			d->currentFrame = QPixmap();
			d->isPlaying = false;

			d->resolvedAbsPath = resolveStored(d->storedPath);
			d->fileName = QFileInfo(d->resolvedAbsPath).fileName();
			d->loadingFailed = false;

			if (d->resolvedAbsPath.isEmpty()) {
				d->thumbnail = QPixmap();
				return;
			}

			// 创建播放器和 sink
			d->player = new QMediaPlayer();
			d->videoSink = new QVideoSink(d->player);
			d->player->setVideoSink(d->videoSink);
			d->player->setSource(QUrl::fromLocalFile(d->resolvedAbsPath));

			// 连接帧更新信号 —— 用 graphicsItem 作为 context，确保在主线程执行
			QObject::connect(d->videoSink, &QVideoSink::videoFrameChanged,
				d->graphicsItem,
				[d](const QVideoFrame& frame) {
					if (frame.isValid()) {
						d->currentFrame = QPixmap::fromImage(frame.toImage());
						if (d->graphicsItem) {
							d->graphicsItem->update();
						}
					}
				});

			// 连接播放状态变化（播放结束时重置）
			QObject::connect(d->player, &QMediaPlayer::mediaStatusChanged,
				d->graphicsItem,
				[d](QMediaPlayer::MediaStatus status) {
					if (status == QMediaPlayer::EndOfMedia) {
						d->isPlaying = false;
						d->player->setPosition(0);
						if (d->graphicsItem) {
							d->graphicsItem->update();
						}
					}
				});

			// 用播放器本身提取首帧（play 后立即 pause，获取第一帧）
			bool gotFrame = false;
			QMetaObject::Connection conn;
			conn = QObject::connect(d->videoSink, &QVideoSink::videoFrameChanged,
				d->graphicsItem,
				[d, &gotFrame, &conn](const QVideoFrame& frame) {
					if (!gotFrame && frame.isValid()) {
						gotFrame = true;
						d->thumbnail = QPixmap::fromImage(frame.toImage());
						d->currentFrame = d->thumbnail;
						d->player->pause();
						QObject::disconnect(conn);
					}
				});

			d->player->play();

			// 等待首帧（最多 3 秒）
			QElapsedTimer timer;
			timer.start();
			while (!gotFrame && timer.elapsed() < 3000) {
				QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
			}

			if (!gotFrame) {
				QObject::disconnect(conn);
				d->player->stop();
				d->thumbnail = QPixmap();
				d->loadingFailed = true;
			}
		}

		void drawPlaceholder(QPainter* p, const QRectF& rect, PrivateData* d) {
			// 绘制视频图标（简化的摄像机形状）
			p->setPen(QColor(120, 120, 120));
			p->setBrush(Qt::NoBrush);

			// 摄像机机身
			QRectF body(rect.center().x() - 24, rect.center().y() - 16, 48, 32);
			p->drawRoundedRect(body, 4, 4);
			// 镜头
			p->drawEllipse(rect.center(), 10, 10);
			// 顶部取景器
			QRectF viewfinder(rect.center().x() - 8, rect.center().y() - 24, 16, 8);
			p->drawRect(viewfinder);

			// 提示文字
			QFont f = p->font();
			f.setPointSize(9);
			p->setFont(f);
			QString hint = (d && !d->storedPath.isEmpty())
				? QObject::tr("Video missing:\n%1").arg(d->storedPath)
				: QObject::tr("Double-click to choose a video\n(or drag && drop)");
			p->drawText(rect.adjusted(0, 40, 0, 0), Qt::AlignCenter | Qt::TextWordWrap, hint);
		}

		void drawPlayButton(QPainter* p, const QRectF& rect) {
			// 绘制半透明圆形背景 + 白色三角播放图标
			QPointF center(rect.center().x(), rect.center().y() - 10);
			qreal radius = 18;

			p->setBrush(QColor(0, 0, 0, 120));
			p->setPen(Qt::NoPen);
			p->drawEllipse(center, radius, radius);

			// 白色三角
			p->setBrush(QColor(255, 255, 255, 220));
			QPointF triangle[3] = {
				QPointF(center.x() - 6, center.y() - 8),
				QPointF(center.x() - 6, center.y() + 8),
				QPointF(center.x() + 8, center.y())
			};
			p->drawPolygon(triangle, 3);
		}
	};

} // namespace warroom
