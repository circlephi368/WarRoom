// mod/builtin/ImageMod.h
//
// 内置主模组：图片节点。
//
// 行为：
//   - 节点不再绘制默认背景与文本，而是把图片按节点矩形等比缩放铺满；
//   - 在节点的 primary_mod_data 里存一个 { "path": "相对/绝对路径" }；
//   - 双击节点弹出文件对话框换图（若节点尚未设置图片）；
//   - 图片若加载不到则降级为浅灰底 + "图片缺失"占位文字，不影响整体运行。
//
// 路径策略：
//   - 优先按"模型当前文档路径"为基准解析相对路径；
//   - 若用户选择的图片不在文档目录下，会保留绝对路径（也允许）。
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
#include <QAction>
#include <QMenu>

namespace warroom {

    class ImageMod : public NodeMod {
    public:
        // ---------- 模组级单例的"当前文档目录" ----------
        // 由主窗口在打开/保存文档时调用，便于把图片路径转换为相对路径
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
            QPixmap pixmap;
            QString resolvedAbsPath;
        };

        // ---------- 基础信息 ----------
        ModInfo getInfo() const override {
            return {
                "builtin.image",          // id
                "Image",                  // name
                "0.1.0",                  // version
                "warroom",                // author
                "Display an image inside a node.",
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
            // 反序列化已经把 storedPath 填进来了，加载实际像素
            reloadPixmap(static_cast<PrivateData*>(modData));
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
        // 返回 true：完全替代默认绘制
        bool onPaint(const ModRenderContext& ctx, void* modData) override {
            auto* d = static_cast<PrivateData*>(modData);
            if (!ctx.painter) return false;

            QPainter* p = ctx.painter;
            QRectF rect = ctx.rect;

            // 背景：浅色卡片，便于和透明 PNG 区分
            p->setBrush(QColor(245, 245, 245));
            p->setPen(QPen(QColor(180, 180, 180), 1));
            p->drawRoundedRect(rect, 6, 6);

            if (!d || d->pixmap.isNull()) {
                // 占位
                p->setPen(QColor(120, 120, 120));
                QFont f = p->font();
                f.setPointSize(10);
                p->setFont(f);
                QString hint = (d && !d->storedPath.isEmpty())
                    ? QObject::tr("Image missing:\n%1").arg(d->storedPath)
                    : QObject::tr("Double-click to choose an image");
                p->drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, hint);
                return true;
            }

            // 等比缩放居中绘制
            QSize target = rect.size().toSize();
            QPixmap scaled = d->pixmap.scaled(
                target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QRectF dst(
                rect.x() + (rect.width() - scaled.width()) / 2.0,
                rect.y() + (rect.height() - scaled.height()) / 2.0,
                scaled.width(), scaled.height()
            );
            p->drawPixmap(dst.toRect(), scaled);
            return true;
        }

        QSizeF getPreferredSize(const WarNode* /*node*/, void* modData) const override {
            auto* d = static_cast<PrivateData*>(modData);
            if (d && !d->pixmap.isNull()) {
                // 缺省按图片尺寸建议（限制在 100~600 之间）
                int w = qBound(100, d->pixmap.width(), 600);
                int h = qBound(80, d->pixmap.height(), 400);
                return QSizeF(w, h);
            }
            return QSizeF(200, 150);
        }

        // ---------- 交互：双击换图 ----------
        ModInteractionResult onMouseDoubleClick(QGraphicsSceneMouseEvent* event,
            const WarNode* node, void* modData) override
        {
            auto* d = static_cast<PrivateData*>(modData);
            if (!d) return ModInteractionResult::Ignored;

            // 弹文件对话框
            QString picked = QFileDialog::getOpenFileName(
                nullptr,
                QObject::tr("Choose an image"),
                d->resolvedAbsPath.isEmpty() ? QString() : QFileInfo(d->resolvedAbsPath).absolutePath(),
                QObject::tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)")
            );
            if (picked.isEmpty()) {
                // 用户取消：把事件消费掉，避免触发文本编辑
                return ModInteractionResult::Consumed;
            }
            setImagePath(const_cast<WarNode*>(node), d, picked);
            return ModInteractionResult::Consumed;
        }

        // ---------- 拖放接收图片 ----------
        bool canAcceptDrop(const QMimeData* mimeData,
            const WarNode* /*node*/, void* /*modData*/) const override {
            return mimeData && mimeData->hasUrls();
        }
        void onDrop(const QMimeData* mimeData, WarNode* node, void* modData) override {
            if (!mimeData || !mimeData->hasUrls()) return;
            QList<QUrl> urls = mimeData->urls();
            if (urls.isEmpty()) return;
            QString file = urls.first().toLocalFile();
            if (file.isEmpty()) return;
            setImagePath(node, static_cast<PrivateData*>(modData), file);
        }

        // ---------- 右键菜单 ----------
        bool onContextMenu(const ModMenuContext& ctx, void* modData) override {
            auto* d = static_cast<PrivateData*>(modData);
            if (!d || !ctx.menu) return false;

            // 添加分隔线（表示开始模组特定操作）
            ctx.menu->addSeparator();

            // 添加子菜单
            QMenu* imageMenu = ctx.menu->addMenu("图片选项");

            // 更换图片
            QAction* changeImageAction = new QAction("更换图片", imageMenu);
            QObject::connect(changeImageAction, &QAction::triggered, [this, &ctx, d]() {
                QString picked = QFileDialog::getOpenFileName(
                    ctx.parent,
                    QObject::tr("Choose an image"),
                    d->resolvedAbsPath.isEmpty() ? QString() : QFileInfo(d->resolvedAbsPath).absolutePath(),
                    QObject::tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)")
                );
                if (!picked.isEmpty()) {
                    setImagePath(ctx.node, d, picked);
                    if (ctx.requestNodeRefresh) {
                        ctx.requestNodeRefresh(ctx.nodeId);
                    }
                }
            });
            imageMenu->addAction(changeImageAction);

            // 清除图片
            QAction* clearImageAction = new QAction("清除图片", imageMenu);
            QObject::connect(clearImageAction, &QAction::triggered, [this, &ctx, d]() {
                d->storedPath.clear();
                d->pixmap = QPixmap();
                d->resolvedAbsPath.clear();
                if (ctx.requestNodeRefresh) {
                    ctx.requestNodeRefresh(ctx.nodeId);
                }
            });
            imageMenu->addAction(clearImageAction);

            // 显示图片路径（只读信息项）
            if (!d->storedPath.isEmpty()) {
                imageMenu->addSeparator();
                QAction* pathInfo = new QAction(
                    QString("路径: %1").arg(d->storedPath), imageMenu);
                pathInfo->setEnabled(false);
                imageMenu->addAction(pathInfo);
            }

            return true; // 已添加分隔线和菜单项
        }

    private:
        // 把用户选择的绝对路径转成"相对于当前文档目录"的形式（如能）
        static QString toStoredPath(const QString& absPath) {
            const QString docDir = currentDocumentDir();
            if (docDir.isEmpty()) return absPath;
            QDir base(docDir);
            QString rel = base.relativeFilePath(absPath);
            // 跨盘符等情况 relativeFilePath 会返回带很多 ".." 的路径，
            // 此时仍然存绝对路径更稳妥
            if (rel.startsWith("..") || rel.contains(":/")) return absPath;
            return rel;
        }

        // 把存储路径解析成绝对路径
        static QString resolveStored(const QString& stored) {
            if (stored.isEmpty()) return {};
            QFileInfo fi(stored);
            if (fi.isAbsolute()) return fi.absoluteFilePath();
            const QString docDir = currentDocumentDir();
            if (docDir.isEmpty()) return stored;
            return QDir(docDir).absoluteFilePath(stored);
        }

        void setImagePath(WarNode* /*node*/, PrivateData* d, const QString& absPath) {
            if (!d) return;
            d->storedPath = toStoredPath(absPath);
            reloadPixmap(d);
        }

        void reloadPixmap(PrivateData* d) {
            if (!d) return;
            d->resolvedAbsPath = resolveStored(d->storedPath);
            if (d->resolvedAbsPath.isEmpty()) {
                d->pixmap = QPixmap();
                return;
            }
            d->pixmap.load(d->resolvedAbsPath);
        }
    };

} // namespace warroom
