#pragma once

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QStyle>

class CustomTitleBar : public QWidget
{
    Q_OBJECT

public:
    enum Button {
        None = 0x00,
        Minimize = 0x01,
        Maximize = 0x02,
        Close = 0x04,
        All = Minimize | Maximize | Close
    };
    Q_DECLARE_FLAGS(Buttons, Button)

        explicit CustomTitleBar(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(36);
        setCursor(Qt::ArrowCursor);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 0, 0, 0);
        layout->setSpacing(0);

        // 窗口标题（左对齐）
        m_titleLabel = new QLabel("WarRoom", this);
        m_titleLabel->setStyleSheet("color: #CCCCCC; font-size: 12px;");
        layout->addWidget(m_titleLabel);
        layout->addStretch();

        // 最小化按钮
        m_minimizeBtn = createTitleButton("—");
        m_minimizeBtn->setToolTip("最小化");
        layout->addWidget(m_minimizeBtn);

        // 最大化/还原按钮
        m_maximizeBtn = createTitleButton("□");
        m_maximizeBtn->setToolTip("最大化");
        layout->addWidget(m_maximizeBtn);

        // 关闭按钮
        m_closeBtn = createTitleButton("✕");
        m_closeBtn->setToolTip("关闭");
        m_closeBtn->setObjectName("closeButton");
        layout->addWidget(m_closeBtn);

        // 连接信号
        connect(m_minimizeBtn, &QPushButton::clicked, this, &CustomTitleBar::minimizeClicked);
        connect(m_maximizeBtn, &QPushButton::clicked, this, &CustomTitleBar::maximizeClicked);
        connect(m_closeBtn, &QPushButton::clicked, this, &CustomTitleBar::closeClicked);
    }

    void setButtons(Buttons buttons) {
        m_minimizeBtn->setVisible(buttons & Minimize);
        m_maximizeBtn->setVisible(buttons & Maximize);
        m_closeBtn->setVisible(buttons & Close);
    }

    void setTitle(const QString& title) {
        m_title = title;
        m_titleLabel->setText(title);
        update();
    }

    void setMaximized(bool maximized) {
        m_maximized = maximized;
        m_maximizeBtn->setText(maximized ? "❐" : "□");
        m_maximizeBtn->setToolTip(maximized ? "还原" : "最大化");
    }

signals:
    void minimizeClicked();
    void maximizeClicked();
    void closeClicked();
    void titleBarDoubleClicked();

    // 用于通知主窗口开始/结束窗口移动
    void startWindowDrag(const QPoint& globalPos);
    void windowDrag(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 背景：深灰色，与主窗口风格统一
        p.fillRect(rect(), QColor(30, 30, 30));

        // 底部细线分隔
        p.setPen(QPen(QColor(50, 50, 50), 1));
        p.drawLine(0, height() - 1, width(), height() - 1);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragStartGlobal = event->globalPos();
            emit startWindowDrag(event->globalPos());
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging) {
            emit windowDrag(event->globalPos());
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        m_dragging = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit titleBarDoubleClicked();
        }
    }

private:
    QPushButton* createTitleButton(const QString& text) {
        auto* btn = new QPushButton(text, this);
        btn->setFixedSize(46, 36);
        btn->setFlat(true);
        btn->setCursor(Qt::ArrowCursor);

        // 按钮样式：悬停变色
        btn->setStyleSheet(R"(
            QPushButton {
                background: transparent;
                border: none;
                color: #999999;
                font-size: 14px;
            }
            QPushButton:hover {
                background: #3A3A3A;
                color: #E0E0E0;
            }
            QPushButton#closeButton:hover {
                background: #E81123;
                color: white;
            }
            QPushButton:pressed {
                background: #2A2A2A;
            }
            QPushButton#closeButton:pressed {
                background: #BF0F1D;
            }
        )");
        return btn;
    }

    QLabel* m_titleLabel = nullptr;
    QPushButton* m_minimizeBtn = nullptr;
    QPushButton* m_maximizeBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QString m_title = "WarRoom";
    bool m_maximized = false;
    bool m_dragging = false;
    QPoint m_dragStartGlobal;
};