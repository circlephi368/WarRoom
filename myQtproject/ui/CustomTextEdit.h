#pragma once
#include <QTextEdit>

class CustomTextEdit : public QTextEdit {
    Q_OBJECT

public:
    explicit CustomTextEdit(QWidget* parent = nullptr);

    void setTransparentMode(bool transparent);
    void setCustomScrollbar(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void updateScrollbarVisibility();

    bool m_transparentMode = true;
    bool m_customScrollbar = true;
};