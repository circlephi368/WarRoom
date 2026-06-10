// CustomTextEdit.cpp
#include "CustomTextEdit.h"
#include <QScrollBar>
#include <QAbstractTextDocumentLayout>

CustomTextEdit::CustomTextEdit(QWidget* parent) : QTextEdit(parent) {
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);

    //document()->setDocumentMargin(0);
    setCustomScrollbar(true);
}

void CustomTextEdit::prepareForDestruction()
{
    m_isValid = false;
    // 清除所有待处理的事件
    setEnabled(false);
    // 不主动删除，让 Qt 处理
}

void CustomTextEdit::setTransparentMode(bool transparent) {
    if (!m_isValid) return;
    m_transparentMode = transparent;
    if (m_transparentMode) {
        setStyleSheet("QTextEdit { background: transparent; }");
    }
    update();
}

void CustomTextEdit::setCustomScrollbar(bool enabled) {
    if (!m_isValid) return;
    m_customScrollbar = enabled;
    if (enabled) {
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
}

void CustomTextEdit::paintEvent(QPaintEvent* event) {
    if (!m_isValid) return;
    QTextEdit::paintEvent(event);
}

void CustomTextEdit::resizeEvent(QResizeEvent* event) {
    if (!m_isValid) return;
    QTextEdit::resizeEvent(event);
    updateScrollbarVisibility();
}

void CustomTextEdit::focusInEvent(QFocusEvent* event) {
    if (!m_isValid) {
        event->ignore();
        return;
    }
    QTextEdit::focusInEvent(event);
    m_transparentMode = false;
    setStyleSheet("QTextEdit { background: transparent; }");
}

void CustomTextEdit::focusOutEvent(QFocusEvent* event) {
    if (!m_isValid) {
        event->ignore();
        return;
    }
    QTextEdit::focusOutEvent(event);
    m_transparentMode = true;
    setStyleSheet("QTextEdit { background: transparent; }");
}

void CustomTextEdit::updateScrollbarVisibility() {
    if (!m_isValid || !m_customScrollbar) return;

    QTextDocument* doc = document();
    if (!doc) return;

    QSizeF docSize = doc->documentLayout()->documentSize();
    QRect viewRect = viewport()->rect();

    verticalScrollBar()->setVisible(docSize.height() > viewRect.height());
    horizontalScrollBar()->setVisible(docSize.width() > viewRect.width());
}