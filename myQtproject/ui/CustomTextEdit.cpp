#include "CustomTextEdit.h"
#include <QScrollBar>
#include <QAbstractTextDocumentLayout>

CustomTextEdit::CustomTextEdit(QWidget* parent) : QTextEdit(parent) {
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);

    // 确保文档背景透明
    document()->setDocumentMargin(0);

    setCustomScrollbar(true);
}

void CustomTextEdit::setTransparentMode(bool transparent) {
    m_transparentMode = transparent;
    // 透明模式 确保完全无背景
    if (m_transparentMode) {
        setStyleSheet("QTextEdit { background: transparent; }");
    }
    update();
}

void CustomTextEdit::setCustomScrollbar(bool enabled) {
    m_customScrollbar = enabled;
    if (enabled) {
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
}

void CustomTextEdit::paintEvent(QPaintEvent* event) {
    if (m_transparentMode) {
        // 完全透明模式：只让 QTextEdit 绘制文本，不绘制任何背景
        // 直接调用基类，但需要确保 viewport 背景不绘制
        QTextEdit::paintEvent(event);
    }
    else {
        // 编辑模式：可以保持原有行为或简单调用基类
        QTextEdit::paintEvent(event);
    }
}

void CustomTextEdit::resizeEvent(QResizeEvent* event) {
    QTextEdit::resizeEvent(event);
    updateScrollbarVisibility();
}

void CustomTextEdit::focusInEvent(QFocusEvent* event) {
    QTextEdit::focusInEvent(event);
    m_transparentMode = false;
    setStyleSheet("QTextEdit { background: transparent; }");
}

void CustomTextEdit::focusOutEvent(QFocusEvent* event) {
    QTextEdit::focusOutEvent(event);
    m_transparentMode = true;
    setStyleSheet("QTextEdit { background: transparent; }");
}

void CustomTextEdit::updateScrollbarVisibility() {
    if (!m_customScrollbar) return;

    QTextDocument* doc = document();
    if (!doc) return;

    QSizeF docSize = doc->documentLayout()->documentSize();
    QRect viewRect = viewport()->rect();

    verticalScrollBar()->setVisible(docSize.height() > viewRect.height());
    horizontalScrollBar()->setVisible(docSize.width() > viewRect.width());
}