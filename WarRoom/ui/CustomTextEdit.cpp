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
}

void CustomTextEdit::focusOutEvent(QFocusEvent* event) {
	if (!m_isValid) {
		event->ignore();
		return;
	}
	QTextEdit::focusOutEvent(event);
	m_transparentMode = true;
}

void CustomTextEdit::contextMenuEvent(QContextMenuEvent* event) {
	if (!m_isValid) {
		event->ignore();
		return;
	}
	// 不调用基类 contextMenuEvent，避免弹出 QTextEdit 默认菜单
	// 直接发出信号，由 NodeGraphicsItem 构建自定义菜单
	emit customContextMenuRequested(event->globalPos());
	event->accept();
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