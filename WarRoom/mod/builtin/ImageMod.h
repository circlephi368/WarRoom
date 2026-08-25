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
#include "ui/NodeGraphicsItem.h"  // 需要 NodeGraphicsItem 完整定义以进行向上转型

#include <QMovie>
#include <QGraphicsItem>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QDir>
#include <QMimeData>
#include <QAction>
#include <QMenu>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSettings>
#include <QCoreApplication>

namespace warroom {

	class ImageMod : public NodeMod {
	public:
		// ---------- 存档目录支持 ----------
		// mod_data/builtin.image 目录路径（每个 ImageMod 实例共享同一目录）
		static QString& modDataSubdir() {
			static QString dir = "mod_data/builtin.image";
			return dir;
		}

		// ---------- 模组级单例的"当前文档目录" ----------
		// 由主窗口在打开/保存文档时调用，便于把图片路径转换为相对路径
		static QString& currentDocumentDir() {
			static QString dir;
			return dir;
		}
		static void setCurrentDocumentDir(const QString& dir) {
			currentDocumentDir() = dir;
		}

		// ---------- 模组设置：默认展示模式 ----------
		// 复用主程序的 config/settings.ini，用独立 group 存放
		static QString settingsFilePath() {
			return QCoreApplication::applicationDirPath()
				+ QLatin1String("/config/settings.ini");
		}
		// 默认展示模式（持久化）。true=原图，false=缩略图
		static bool defaultShowOriginal() {
			QSettings s(settingsFilePath(), QSettings::IniFormat);
			s.beginGroup("builtin.image");
			return s.value("defaultShowOriginal", false).toBool();
		}
		static void setDefaultShowOriginal(bool v) {
			QSettings s(settingsFilePath(), QSettings::IniFormat);
			s.beginGroup("builtin.image");
			s.setValue("defaultShowOriginal", v);
			s.endGroup();
			s.sync();
		}

		// ---------- 节点私有数据 ----------
		struct PrivateData {
			// 序列化用，原样保存（可能是相对路径，也可能是绝对路径）
			QString storedPath;
			// 运行时缓存
			QPixmap pixmap;
			QString resolvedAbsPath;
			// 显示模式：true=原图（不压缩），false=缩略图（压缩到节点尺寸）
			bool showOriginal = false;  // 默认缩略图模式
			// GIF 播放（仅 .gif 文件使用）
			QMovie* movie = nullptr;
			QGraphicsItem* graphicsItem = nullptr;  // 用于触发重绘

			~PrivateData() {
				// [DESTDBG] 析构入口，记录 this、movie 地址、storedPath
				// 若同一 this 出现两次 -> PrivateData 被双重析构
				qDebug().nospace().noquote()
					<< "[DESTDBG] >>> ~ImageMod::PrivateData ENTER this=" << static_cast<void*>(this)
					<< " movie=" << static_cast<void*>(movie)
					<< " graphicsItem=" << static_cast<void*>(graphicsItem)
					<< " storedPath=" << storedPath;
				if (movie) {
					qDebug() << "[DESTDBG]   ~ImageMod::PrivateData: stopping & deleting movie";
					movie->stop();
					delete movie;
					movie = nullptr;
				}
				qDebug().nospace().noquote()
					<< "[DESTDBG] <<< ~ImageMod::PrivateData EXIT this=" << static_cast<void*>(this);
			}
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
		void* onCreateNode(WarNode* /*node*/, ::NodeGraphicsItem* item) override {
			auto* d = new PrivateData();
			d->graphicsItem = item;  // 保存以便 QMovie::frameChanged 时触发重绘
			// 新建节点按模组默认展示模式初始化（用户可通过设置页面配置）
			d->showOriginal = defaultShowOriginal();
			return d;
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
			j["showOriginal"] = d->showOriginal;  // 保存显示模式
			return j;
		}
		void deserialize(void* modData, const nlohmann::json& data) override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!d) return;
			if (data.contains("path") && data["path"].is_string()) {
				d->storedPath = QString::fromStdString(data["path"].get<std::string>());
			}
			// 读取显示模式（默认 false = 缩略图）
			if (data.contains("showOriginal") && data["showOriginal"].is_boolean()) {
				d->showOriginal = data["showOriginal"].get<bool>();
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

			// 取当前要绘制的 pixmap：GIF 播放中时取 movie 当前帧，否则用缓存的 pixmap
			QPixmap curPix = (d && d->movie && d->movie->state() == QMovie::Running)
				? d->movie->currentPixmap() : (d ? d->pixmap : QPixmap());

			if (curPix.isNull()) {
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

			// 根据显示模式决定绘制方式
			if (d->showOriginal) {
				// 原图模式：等比缩放以完整显示在节点内（不放大，只缩小），
				// 超出的部分会被裁剪，保持裁剪区域为节点矩形
				p->save();
				QPainterPath clipPath;
				clipPath.addRoundedRect(rect, 6, 6);
				p->setClipPath(clipPath);

				// 计算缩放后的大小（只缩小不放大）
				int imgW = curPix.width();
				int imgH = curPix.height();
				float scaleX = static_cast<float>(imgW) / rect.width();
				float scaleY = static_cast<float>(imgH) / rect.height();
				float scale = qMax(scaleX, scaleY);  // 取较大值确保完整显示
				int drawW = qRound(imgW / scale);
				int drawH = qRound(imgH / scale);

				QRectF dst(
					rect.x() + (rect.width() - drawW) / 2.0,
					rect.y() + (rect.height() - drawH) / 2.0,
					drawW, drawH
				);
				p->drawPixmap(dst.toRect(), curPix);
				p->restore();

			}
			else {
				// 缩略图模式：等比缩放居中绘制
				QSize target = rect.size().toSize();
				QPixmap scaled = curPix.scaled(
					target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
				QRectF dst(
					rect.x() + (rect.width() - scaled.width()) / 2.0,
					rect.y() + (rect.height() - scaled.height()) / 2.0,
					scaled.width(), scaled.height()
				);
				p->drawPixmap(dst.toRect(), scaled);
			}
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

		void setArchiveBaseDir(const QString& /*archiveDir*/,
			const QString& /*modDataSubdir*/) override {
			// 模组使用 currentDocumentDir() 解析路径，这里无需特殊处理
		}

		// 节点绑定的"外部图片文件"绝对路径（用于存档目录复制）
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

		// ---------- 拖放到空白处创建新图片节点 ----------
		bool canCreateNodeFromDrop(const QMimeData* mimeData) const override {
			if (!mimeData || !mimeData->hasUrls()) return false;
			QString file = mimeData->urls().first().toLocalFile();
			if (file.isEmpty()) return false;
			static const QStringList imgExts = { "png","jpg","jpeg","bmp","gif","webp" };
			return imgExts.contains(QFileInfo(file).suffix().toLower());
		}
		void onDropToNewNode(const QMimeData* mimeData, WarNode* node, void* modData) override {
			auto* d = static_cast<PrivateData*>(modData);
			if (!mimeData || !mimeData->hasUrls()) return;
			QString file = mimeData->urls().first().toLocalFile();
			if (file.isEmpty()) return;
			setImagePath(node, d, file);
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
				d->resolvedAbsPath.clear();
				reloadPixmap(d);  // 同时清理 movie 与 pixmap
				if (ctx.requestNodeRefresh) {
					ctx.requestNodeRefresh(ctx.nodeId);
				}
			});
			imageMenu->addAction(clearImageAction);

			// 显示模式子菜单（原图/缩略图）
			imageMenu->addSeparator();
			QMenu* displayMenu = imageMenu->addMenu("显示模式");

			QAction* thumbnailAction = new QAction("缩略图（完整填充）", displayMenu);
			thumbnailAction->setCheckable(true);
			thumbnailAction->setChecked(!d->showOriginal);  // 当前是缩略图模式则选中
			QObject::connect(thumbnailAction, &QAction::triggered, [this, &ctx, d]() {
				d->showOriginal = false;
				if (ctx.requestNodeRefresh) {
					ctx.requestNodeRefresh(ctx.nodeId);
				}
			});
			displayMenu->addAction(thumbnailAction);

			QAction* originalAction = new QAction("原图（等比缩放）", displayMenu);
			originalAction->setCheckable(true);
			originalAction->setChecked(d->showOriginal);  // 当前是原图模式则选中
			QObject::connect(originalAction, &QAction::triggered, [this, &ctx, d]() {
				d->showOriginal = true;
				if (ctx.requestNodeRefresh) {
					ctx.requestNodeRefresh(ctx.nodeId);
				}
			});
			displayMenu->addAction(originalAction);

			// 显示图片路径（只读信息项）
			if (!d->storedPath.isEmpty()) {
				imageMenu->addSeparator();
				QAction* pathInfo = new QAction(
					QString("路径: %1").arg(d->storedPath), imageMenu);
				pathInfo->setEnabled(false);
				imageMenu->addAction(pathInfo);

				// 显示图片尺寸信息
				if (!d->pixmap.isNull()) {
					QAction* sizeInfo = new QAction(
						QString("尺寸: %1×%2").arg(d->pixmap.width()).arg(d->pixmap.height()),
						imageMenu);
					sizeInfo->setEnabled(false);
					imageMenu->addAction(sizeInfo);
				}
			}

			return true; // 已添加分隔线和菜单项
		}

		// ---------- 模组设置界面 ----------
		bool hasSettings() const override { return true; }

		QWidget* createSettingsWidget(QWidget* parent) override {
			QWidget* w = new QWidget(parent);
			auto* form = new QFormLayout(w);
			form->setContentsMargins(10, 10, 10, 10);
			form->setSpacing(8);

			auto* combo = new QComboBox(w);
			combo->addItem(QObject::tr("缩略图（完整填充）"), false);  // index 0
			combo->addItem(QObject::tr("原图（等比缩放）"), true);     // index 1
			combo->setCurrentIndex(defaultShowOriginal() ? 1 : 0);
			// 便于 saveSettings 反向查找
			combo->setObjectName("defaultShowModeCombo");
			form->addRow(QObject::tr("默认展示模式："), combo);

			// 说明文字
			auto* hint = new QLabel(
				QObject::tr("放置新图片节点时使用的默认显示模式。\n"
					"已存在的节点不受影响，可在右键菜单中单独修改。"), w);
			hint->setWordWrap(true);
			hint->setStyleSheet("color: #888888; font-size: 11px;");
			form->addRow(QString(), hint);

			return w;
		}

		void saveSettings(QWidget* widget) override {
			if (!widget) return;
			auto* combo = widget->findChild<QComboBox*>("defaultShowModeCombo");
			if (!combo) return;
			bool v = combo->currentData().toBool();
			setDefaultShowOriginal(v);
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
			// 清理旧的 QMovie
			if (d->movie) {
				d->movie->stop();
				delete d->movie;
				d->movie = nullptr;
			}
			d->resolvedAbsPath = resolveStored(d->storedPath);
			if (d->resolvedAbsPath.isEmpty()) {
				d->pixmap = QPixmap();
				return;
			}
			// GIF：用 QMovie 播放动画
			if (d->resolvedAbsPath.toLower().endsWith(".gif")) {
				d->movie = new QMovie(d->resolvedAbsPath);
				if (d->movie->isValid()) {
					QObject::connect(d->movie, &QMovie::frameChanged, [d]() {
						if (d->graphicsItem) d->graphicsItem->update();
					});
					d->movie->start();
					d->pixmap = d->movie->currentPixmap();
					return;
				}
				// gif 无效，当作普通图片
				delete d->movie;
				d->movie = nullptr;
			}
			// 普通图片
			d->pixmap.load(d->resolvedAbsPath);
		}
	};

} // namespace warroom
