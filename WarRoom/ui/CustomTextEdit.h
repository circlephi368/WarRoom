// CustomTextEdit.h
#pragma once
#include <QTextEdit>
#include <QPointer>

class CustomTextEdit : public QTextEdit {
	Q_OBJECT

public:
	explicit CustomTextEdit(QWidget* parent = nullptr);
	~CustomTextEdit() = default;

	void setTransparentMode(bool transparent);
	void setCustomScrollbar(bool enabled);

	// 在编辑器被销毁前调用，阻止后续事件处理
	void prepareForDestruction();

signals:
	// 右键菜单请求：转发给 NodeGraphicsItem 处理
	// globalPos 为右键的全局坐标
	void customContextMenuRequested(const QPoint& globalPos);

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	void updateScrollbarVisibility();

	bool m_transparentMode = true;
	bool m_customScrollbar = true;
	bool m_isValid = true;  // 标记编辑器是否仍然有效
};