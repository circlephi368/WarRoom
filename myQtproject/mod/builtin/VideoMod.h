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

#include <QFileDialog>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
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
            // 序列化用，原样保存（可能是相对路径，也可能是绝对路径）
            QString storedPath;
            // 运行时缓存
            QPixmap thumbnail;
            QString resolvedAbsPath;
            QString fileName;
            bool loadingFailed = false;
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
        void* onCreateNode(WarNode* /*node*/, ::NodeGraphicsItem* /*item*/) override {
            return new PrivateData();
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

            if (!d || d->loadingFailed || d->thumbnail.isNull()) {
                // 绘制占位符
                drawPlaceholder(p, rect, d);
                return true;
            }

            // 等比缩放居中绘制缩略图
            QSize target = rect.size().toSize();
            QPixmap scaled = d->thumbnail.scaled(
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

            // 绘制播放按钮图标（三角）在中心
            drawPlayButton(p, rect);

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

        void onDrop(const QMimeData* mimeData, WarNode* node, void* modData) override {
            if (!mimeData || !mimeData->hasUrls()) return;
            QList<QUrl> urls = mimeData->urls();
            if (urls.isEmpty()) return;
            QString file = urls.first().toLocalFile();
            if (file.isEmpty() || !isVideoFile(file)) return;
            setVideoPath(node, static_cast<PrivateData*>(modData), file);
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
            d->resolvedAbsPath = resolveStored(d->storedPath);
            d->fileName = QFileInfo(d->resolvedAbsPath).fileName();
            d->loadingFailed = false;

            if (d->resolvedAbsPath.isEmpty()) {
                d->thumbnail = QPixmap();
                return;
            }

            // 尝试提取视频首帧作为缩略图
            QImage frame = extractFirstFrame(d->resolvedAbsPath);
            if (!frame.isNull()) {
                d->thumbnail = QPixmap::fromImage(frame);
            }
            else {
                d->thumbnail = QPixmap();
                d->loadingFailed = true;
            }
        }

        // 使用 QMediaPlayer 提取视频首帧
        static QImage extractFirstFrame(const QString& videoPath) {
            QMediaPlayer player;
            QVideoSink* videoSink = new QVideoSink(&player);
            player.setVideoSink(videoSink);
            player.setSource(QUrl::fromLocalFile(videoPath));

            QImage capturedFrame;
            bool frameReceived = false;

            QObject::connect(videoSink, &QVideoSink::videoFrameChanged,
                [&capturedFrame, &frameReceived](const QVideoFrame& frame) {
                    if (!frameReceived && frame.isValid()) {
                        capturedFrame = frame.toImage();
                        frameReceived = true;
                    }
                });

            // 开始播放并立即暂停以获取首帧
            player.play();

            // 等待帧捕获（最多 3 秒）
            QElapsedTimer timer;
            timer.start();
            while (!frameReceived && timer.elapsed() < 3000) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
                // 如果已经播放超过 100ms，暂停以定格
                if (player.position() > 100) {
                    player.pause();
                }
            }

            player.stop();
            delete videoSink;

            return capturedFrame;
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
