// mod/builtin/WebMod.h
#pragma once

#include "mod/NodeMod.h"
#include "core/warroom/war_node.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QString>
#include <QDir>
#include <QUrl>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QInputDialog>
#include <QRegularExpression>
#include <QFontMetrics>
#include <QBuffer>
#include <QPointer>
#include <QCoreApplication>

// 解决 Windows Socket API 冲突
#ifdef Q_OS_WIN
#undef connect
#endif

namespace warroom {

    class NodeGraphicsItem;

    class WebMod : public NodeMod {
    public:
        // ---------- 全局 QNetworkAccessManager ----------
        static QNetworkAccessManager& networkManager() {
            static QNetworkAccessManager mgr;
            return mgr;
        }

        // ---------- 节点私有数据 ----------
        struct PrivateData {
            QString url;
            QString displayUrl;
            QString title;
            QString domain;
            QString description;
            QPixmap thumbnail;
            bool fetchInProgress = false;
            bool fetchFailed = false;
            bool hasSetUrl = false;
            QPointer<QObject> repaintTarget;
        };

        // ---------- 基础信息 ----------
        ModInfo getInfo() const override {
            return {
                "builtin.web",
                "Web",
                "0.1.0",
                "warroom",
                "Display a web page card with OpenGraph metadata.",
                ""
            };
        }
        bool isPrimary() const override { return true; }

        // ---------- 生命周期 ----------
        void* onCreateNode(WarNode* /*node*/, NodeGraphicsItem* item) override {
            auto* d = new PrivateData();
            d->repaintTarget = item;
            return d;
        }
        void onDestroyNode(void* modData) override {
            delete static_cast<PrivateData*>(modData);
        }
        void onNodeLoaded(WarNode* /*node*/, void* modData) override {
            auto* d = static_cast<PrivateData*>(modData);
            if (d && d->hasSetUrl && !d->url.isEmpty() && !d->fetchInProgress) {
                d->fetchInProgress = true;
                startFetch(d);
            }
        }

        // ---------- 序列化 ----------
        nlohmann::json serialize(void* modData) const override {
            auto* d = static_cast<PrivateData*>(modData);
            if (!d) return {};
            nlohmann::json j;
            j["url"] = d->url.toStdString();
            return j;
        }
        void deserialize(void* modData, const nlohmann::json& data) override {
            auto* d = static_cast<PrivateData*>(modData);
            if (!d) return;
            if (data.contains("url") && data["url"].is_string()) {
                QString urlStr = QString::fromStdString(data["url"].get<std::string>());
                setUrlData(d, urlStr);
            }
        }

        // ---------- 渲染 ----------
        bool onPaint(const ModRenderContext& ctx, void* modData) override {
            auto* d = static_cast<PrivateData*>(modData);
            if (!ctx.painter) return false;

            QPainter* p = ctx.painter;
            QRectF rect = ctx.rect;

            p->setBrush(QColor(252, 252, 252));
            p->setPen(QPen(QColor(200, 200, 200), 1));
            p->drawRoundedRect(rect, 6, 6);

            if (!d || !d->hasSetUrl || d->url.isEmpty()) {
                drawEmptyState(p, rect, d);
                return true;
            }

            qreal thumbHeight = 0;
            if (!d->thumbnail.isNull()) {
                thumbHeight = qMin(rect.height() * 0.55, 120.0);
                QRectF thumbRect(rect.x() + 2, rect.y() + 2,
                    rect.width() - 4, thumbHeight);

                QPixmap scaled = d->thumbnail.scaled(
                    thumbRect.size().toSize(),
                    Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);
                p->drawPixmap(thumbRect.toRect(), scaled,
                    QRectF(0, 0, scaled.width(), scaled.height()));

                p->setPen(QPen(QColor(220, 220, 220), 1));
                p->drawLine(QPointF(rect.x() + 2, thumbRect.bottom()),
                    QPointF(rect.right() - 2, thumbRect.bottom()));
            }
            else if (d->fetchInProgress) {
                QRectF loadingRect(rect.x() + 2, rect.y() + 2,
                    rect.width() - 4, 60);
                p->setPen(QColor(180, 180, 180));
                QFont lf = p->font();
                lf.setPointSize(9);
                p->setFont(lf);
                p->drawText(loadingRect, Qt::AlignCenter,
                    QObject::tr("Fetching page info..."));
                thumbHeight = 64;
            }

            qreal textTop = rect.y() + 4 + thumbHeight;
            qreal textAreaHeight = rect.height() - thumbHeight - 8;

            if (!d->title.isEmpty()) {
                QRectF titleRect(rect.x() + 8, textTop,
                    rect.width() - 16, textAreaHeight * 0.5);
                p->setPen(QColor(30, 30, 30));
                QFont tf = p->font();
                tf.setPointSize(10);
                tf.setBold(true);
                p->setFont(tf);
                QString elidedTitle = QFontMetrics(tf).elidedText(
                    d->title, Qt::ElideRight, static_cast<int>(titleRect.width()));
                p->drawText(titleRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, elidedTitle);

                if (!d->description.isEmpty() && textAreaHeight > 64) {
                    QRectF descRect(rect.x() + 8, textTop + 26,
                        rect.width() - 16, textAreaHeight - 50);
                    p->setPen(QColor(100, 100, 100));
                    QFont df = p->font();
                    df.setPointSize(8);
                    df.setBold(false);
                    p->setFont(df);
                    QString elidedDesc = QFontMetrics(df).elidedText(
                        d->description, Qt::ElideRight, static_cast<int>(descRect.width() * 2.5));
                    p->drawText(descRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, elidedDesc);
                }
            }
            else {
                QRectF urlRect(rect.x() + 8, textTop,
                    rect.width() - 16, textAreaHeight);
                p->setPen(QColor(80, 80, 80));
                QFont uf = p->font();
                uf.setPointSize(8);
                p->setFont(uf);
                QString elidedUrl = QFontMetrics(uf).elidedText(
                    d->displayUrl.isEmpty() ? d->url : d->displayUrl,
                    Qt::ElideRight, static_cast<int>(urlRect.width()));
                p->drawText(urlRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, elidedUrl);
            }

            QRectF domainBg(rect.x(), rect.bottom() - 18, rect.width(), 18);
            p->fillRect(domainBg, QColor(245, 245, 245));
            p->setPen(QPen(QColor(200, 200, 200), 1));
            p->drawLine(QPointF(rect.x(), domainBg.top()),
                QPointF(rect.right(), domainBg.top()));
            if (!d->domain.isEmpty()) {
                p->setPen(QColor(80, 120, 200));
                QFont df = p->font();
                df.setPointSize(7);
                p->setFont(df);
                QString elidedDomain = QFontMetrics(df).elidedText(
                    d->domain, Qt::ElideRight, static_cast<int>(rect.width() - 12));
                p->drawText(domainBg.adjusted(6, 0, -6, 0),
                    Qt::AlignLeft | Qt::AlignVCenter, elidedDomain);
            }

            if (d->fetchFailed && !d->fetchInProgress) {
                p->setPen(QColor(200, 80, 80));
                QFont ef = p->font();
                ef.setPointSize(7);
                p->setFont(ef);
                p->drawText(rect.adjusted(4, 4, -4, -4),
                    Qt::AlignTop | Qt::AlignRight, QObject::tr("Load failed"));
            }

            return true;
        }

        QSizeF getPreferredSize(const WarNode* /*node*/, void* modData) const override {
            auto* d = static_cast<PrivateData*>(modData);
            if (d && d->hasSetUrl) {
                if (!d->thumbnail.isNull()) {
                    int w = qBound(180, d->thumbnail.width(), 400);
                    int h = qBound(140, d->thumbnail.height() + 80, 320);
                    return QSizeF(w, h);
                }
                return QSizeF(240, 140);
            }
            return QSizeF(200, 120);
        }

        // ---------- 交互 ----------
        ModInteractionResult onMouseDoubleClick(QGraphicsSceneMouseEvent* event,
            const WarNode* node, void* modData) override
        {
            Q_UNUSED(event);
            auto* d = static_cast<PrivateData*>(modData);
            if (!d) return ModInteractionResult::Ignored;

            if (!d->hasSetUrl || d->url.isEmpty()) {
                bool ok = false;
                QString inputUrl = QInputDialog::getText(
                    nullptr,
                    QObject::tr("Enter Web URL"),
                    QObject::tr("URL:"),
                    QLineEdit::Normal,
                    QString(),
                    &ok);
                if (!ok || inputUrl.trimmed().isEmpty()) {
                    return ModInteractionResult::Consumed;
                }
                QString normalized = normalizeUrl(inputUrl.trimmed());
                setUrl(const_cast<WarNode*>(node), d, normalized);
                return ModInteractionResult::Consumed;
            }

            QDesktopServices::openUrl(QUrl(d->url));
            return ModInteractionResult::Consumed;
        }

        // ---------- 拖放 ----------
        bool canAcceptDrop(const QMimeData* mimeData,
            const WarNode* /*node*/, void* /*modData*/) const override {
            if (!mimeData) return false;
            if (mimeData->hasUrls()) return true;
            if (mimeData->hasText()) {
                QString text = mimeData->text().trimmed();
                return text.startsWith("http://") || text.startsWith("https://");
            }
            return false;
        }

        void onDrop(const QMimeData* mimeData, WarNode* node, void* modData) override {
            auto* d = static_cast<PrivateData*>(modData);
            if (!d || !mimeData) return;

            QString urlStr;
            if (mimeData->hasUrls()) {
                QList<QUrl> urls = mimeData->urls();
                if (!urls.isEmpty()) {
                    urlStr = urls.first().toString();
                }
            }
            if (urlStr.isEmpty() && mimeData->hasText()) {
                QString text = mimeData->text().trimmed();
                if (text.startsWith("http://") || text.startsWith("https://")) {
                    urlStr = text;
                }
            }
            if (urlStr.isEmpty()) return;

            setUrl(node, d, normalizeUrl(urlStr));
        }

    private:
        // ---------- URL 工具（保持静态）----------
        static QString normalizeUrl(const QString& raw) {
            QString url = raw.trimmed();
            if (!url.startsWith("http://") && !url.startsWith("https://")) {
                url = "https://" + url;
            }
            return url;
        }

        static QString extractDomain(const QString& url) {
            QUrl qurl(url);
            QString host = qurl.host();
            if (host.startsWith("www.")) {
                host = host.mid(4);
            }
            return host;
        }

        static QString stripProtocol(const QString& url) {
            QString u = url;
            if (u.startsWith("https://")) u = u.mid(8);
            else if (u.startsWith("http://")) u = u.mid(7);
            if (u.startsWith("www.")) u = u.mid(4);
            return u;
        }

        void setUrl(WarNode* /*node*/, PrivateData* d, const QString& urlStr) {
            if (!d) return;
            setUrlData(d, urlStr);
            if (!urlStr.isEmpty()) {
                d->fetchInProgress = true;
                startFetch(d);
            }
            triggerRepaint(d);
        }

        void setUrlData(PrivateData* d, const QString& urlStr) {
            d->url = urlStr;
            d->displayUrl = stripProtocol(urlStr);
            d->domain = extractDomain(urlStr);
            d->hasSetUrl = true;
            d->fetchFailed = false;
            d->title.clear();
            d->description.clear();
            d->thumbnail = QPixmap();
        }

        // ---------- 异步抓取 ----------
        void startFetch(PrivateData* d) {
            if (d->url.isEmpty()) return;

            QNetworkRequest request;
            request.setUrl(QUrl(d->url));
            request.setHeader(QNetworkRequest::UserAgentHeader,
                "Mozilla/5.0 (compatible; WarRoomWebMod/1.0)");
            request.setRawHeader("Accept", "text/html,application/xhtml+xml,*/*;q=0.8");
            request.setTransferTimeout(8000);

            QNetworkReply* reply = networkManager().get(request);
            QPointer<QObject> target = d->repaintTarget;

            // 使用 QObject::connect 避免与 Windows socket API 冲突
            QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, d, target]() {
                reply->deleteLater();

                if (!d || target.isNull()) return;

                if (reply->error() != QNetworkReply::NoError) {
                    d->fetchInProgress = false;
                    d->fetchFailed = true;
                    if (d->title.isEmpty()) {
                        d->title = d->domain;
                    }
                    triggerRepaint(d);
                    return;
                }

                QByteArray html = reply->readAll();
                parseAndCache(d, html);

                d->fetchInProgress = false;
                d->fetchFailed = false;
                triggerRepaint(d);
                });
        }

        void parseAndCache(PrivateData* d, const QByteArray& html) {
            QString htmlStr = QString::fromUtf8(html);

            QString ogTitle = extractMetaProperty(htmlStr, "og:title");
            if (!ogTitle.isEmpty()) {
                d->title = ogTitle;
            }
            else {
                QString titleTag = extractTagContent(htmlStr, "title");
                if (!titleTag.isEmpty()) {
                    d->title = titleTag.trimmed();
                }
                else {
                    d->title = d->domain;
                }
            }

            QString ogDesc = extractMetaProperty(htmlStr, "og:description");
            if (!ogDesc.isEmpty()) {
                d->description = ogDesc;
            }
            else {
                QString metaDesc = extractMetaName(htmlStr, "description");
                if (!metaDesc.isEmpty()) {
                    d->description = metaDesc;
                }
            }

            QString ogImage = extractMetaProperty(htmlStr, "og:image");
            if (!ogImage.isEmpty()) {
                QUrl baseUrl(d->url);
                QUrl imageUrl(ogImage);
                if (imageUrl.isRelative()) {
                    imageUrl = baseUrl.resolved(imageUrl);
                }
                fetchThumbnail(d, imageUrl.toString());
            }
            else {
                QString favicon = extractFavicon(htmlStr, d->url);
                if (!favicon.isEmpty()) {
                    fetchThumbnail(d, favicon);
                }
            }
        }

        void fetchThumbnail(PrivateData* d, const QString& imageUrl) {
            QPointer<QObject> target = d->repaintTarget;

            QNetworkRequest request;
            request.setUrl(QUrl(imageUrl));
            request.setTransferTimeout(5000);
            QNetworkReply* reply = networkManager().get(request);

            QObject::connect(reply, &QNetworkReply::finished, this, [reply, d, target, this]() {
                reply->deleteLater();
                if (!d || target.isNull()) return;

                if (reply->error() != QNetworkReply::NoError) return;

                QByteArray data = reply->readAll();
                QPixmap pix;
                if (pix.loadFromData(data)) {
                    if (pix.width() > 400 || pix.height() > 300) {
                        pix = pix.scaled(400, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    }
                    d->thumbnail = pix;
                    triggerRepaint(d);
                }
                });
        }

        // ---------- HTML 解析（保持静态）----------
        static QString extractMetaProperty(const QString& html, const QString& property) {
            QRegularExpression re(
                R"(<meta\s+[^>]*property\s*=\s*["'])" +
                QRegularExpression::escape(property) +
                R"("[\s\S]*?content\s*=\s*["']([^"']*)["'])",
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch m = re.match(html);
            if (m.hasMatch()) {
                return decodeHtmlEntities(m.captured(1).trimmed());
            }

            QRegularExpression re2(
                R"(<meta\s+[^>]*content\s*=\s*["']([^"']*)["'][\s\S]*?property\s*=\s*["'])" +
                QRegularExpression::escape(property) + R"(")",
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch m2 = re2.match(html);
            if (m2.hasMatch()) {
                return decodeHtmlEntities(m2.captured(1).trimmed());
            }
            return {};
        }

        static QString extractMetaName(const QString& html, const QString& name) {
            QRegularExpression re(
                R"(<meta\s+[^>]*name\s*=\s*["'])" +
                QRegularExpression::escape(name) +
                R"("[\s\S]*?content\s*=\s*["']([^"']*)["'])",
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch m = re.match(html);
            if (m.hasMatch()) {
                return decodeHtmlEntities(m.captured(1).trimmed());
            }

            QRegularExpression re2(
                R"(<meta\s+[^>]*content\s*=\s*["']([^"']*)["'][\s\S]*?name\s*=\s*["'])" +
                QRegularExpression::escape(name) + R"(")",
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch m2 = re2.match(html);
            if (m2.hasMatch()) {
                return decodeHtmlEntities(m2.captured(1).trimmed());
            }
            return {};
        }

        static QString extractTagContent(const QString& html, const QString& tag) {
            QRegularExpression re(
                "<" + tag + R"(\s*[^>]*>([\s\S]*?)</)" + tag + ">",
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch m = re.match(html);
            if (m.hasMatch()) {
                return decodeHtmlEntities(m.captured(1).trimmed());
            }
            return {};
        }

        static QString extractFavicon(const QString& html, const QString& baseUrl) {
            QRegularExpression re(
                R"(<link\s+[^>]*rel\s*=\s*["']icon["'][^>]*href\s*=\s*["']([^"']*)["'])",
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch m = re.match(html);
            if (m.hasMatch()) {
                QString href = m.captured(1).trimmed();
                QUrl base(baseUrl);
                QUrl iconUrl(href);
                if (iconUrl.isRelative()) {
                    iconUrl = base.resolved(iconUrl);
                }
                return iconUrl.toString();
            }

            QRegularExpression re2(
                R"(<link\s+[^>]*rel\s*=\s*["']shortcut\s+icon["'][^>]*href\s*=\s*["']([^"']*)["'])",
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch m2 = re2.match(html);
            if (m2.hasMatch()) {
                QString href = m2.captured(1).trimmed();
                QUrl base(baseUrl);
                QUrl iconUrl(href);
                if (iconUrl.isRelative()) {
                    iconUrl = base.resolved(iconUrl);
                }
                return iconUrl.toString();
            }

            QUrl base(baseUrl);
            return base.scheme() + "://" + base.host() + "/favicon.ico";
        }

        static QString decodeHtmlEntities(const QString& text) {
            QString result = text;
            result.replace("&amp;", "&");
            result.replace("&lt;", "<");
            result.replace("&gt;", ">");
            result.replace("&quot;", "\"");
            result.replace("&#39;", "'");
            result.replace("&nbsp;", " ");
            QRegularExpression numEntityRe(R"(&#(\d+);)");
            QRegularExpressionMatchIterator it = numEntityRe.globalMatch(result);
            while (it.hasNext()) {
                QRegularExpressionMatch m = it.next();
                int code = m.captured(1).toInt();
                result.replace(m.captured(0), QChar(code));
            }
            return result;
        }

        // ---------- 重绘触发（非静态）----------
        void triggerRepaint(PrivateData* d) {
            if (d && d->repaintTarget) {
                QMetaObject::invokeMethod(d->repaintTarget, "update", Qt::QueuedConnection);
            }
        }

        // ---------- 绘制空状态 ----------
        void drawEmptyState(QPainter* p, const QRectF& rect, PrivateData* d) {
            p->setPen(QColor(160, 160, 160));
            p->setBrush(Qt::NoBrush);

            QPointF center = rect.center();

            p->drawEllipse(center, 22, 22);
            p->drawLine(QPointF(center.x(), center.y() - 22),
                QPointF(center.x(), center.y() + 22));
            p->drawEllipse(center, 22, 8);

            QFont f = p->font();
            f.setPointSize(9);
            p->setFont(f);
            p->setPen(QColor(120, 120, 120));
            QString hint = (d && d->fetchFailed)
                ? QObject::tr("Load failed.\nDouble-click to retry")
                : QObject::tr("Double-click to enter a URL\n(or drag && drop link)");
            p->drawText(rect.adjusted(4, 30, -4, 0),
                Qt::AlignCenter | Qt::TextWordWrap, hint);
        }
    };

} // namespace warroom