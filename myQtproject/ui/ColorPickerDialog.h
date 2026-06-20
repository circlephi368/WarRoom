// ColorPickerDialog.h
#pragma once

#include <QDialog>
#include <QColor>
#include <QPointF>
#include <QImage>

class QWidget;
class QSlider;
class QSpinBox;
class QLineEdit;
class QLabel;
class QPaintEvent;
class QMouseEvent;
class QResizeEvent;
class QShowEvent;

// 颜色选择器：左侧色域选择区域（HS色相-饱和度），右侧三个滑块（HSV + Alpha）
class ColorPickerDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ColorPickerDialog(QWidget* parent = nullptr);

	// 设置/获取当前颜色
	void setColor(const QColor& color);
	QColor getColor() const { return m_currentColor; }

	// 设置对话框标题
	void setPickerTitle(const QString& title);

signals:
	// 颜色变化时发出（仅用于实时预览，不写入配置）
	void colorChanged(const QColor& color);

private slots:
	void onHueChanged(int value);
	void onSaturationChanged(int value);
	void onValueChanged(int value);
	void onAlphaChanged(int value);
	void onHexChanged();
	void onResetClicked();
	void onConfirmClicked();
	void onCancelClicked();

protected:
	void showEvent(QShowEvent* event) override;

private:
	// 色域选择区域（左侧正方形）
	class SaturationValueSquare;
	// 色相滑块（竖直条）
	class HueSlider;
	// Alpha 滑块（竖直条）
	class AlphaSlider;

	void setupUI();
	void syncFromColor();
	void syncToColor();
	void updateSquareImage();
	void updateHueSliderImage();
	void updateAlphaSliderImage();
	QColor hsvToColor(int h, int s, int v, int a = 255) const;
	void hsvFromColor(const QColor& c, int& h, int& s, int& v, int& a) const;

	// 颜色状态（使用 HSV + Alpha）
	int m_hue = 0;         // 0-359
	int m_saturation = 0;  // 0-255
	int m_value = 255;     // 0-255
	int m_alpha = 255;     // 0-255

	QColor m_initialColor;
	QColor m_currentColor;

	// 控件
	SaturationValueSquare* m_svSquare = nullptr;
	HueSlider* m_hueSlider = nullptr;
	class AlphaSlider* m_alphaSlider = nullptr;
	QSlider* m_saturationSlider = nullptr;
	QSlider* m_valueSlider = nullptr;
	QSlider* m_alphaSliderBar = nullptr;
	QLineEdit* m_hexEdit = nullptr;
	QLabel* m_colorPreview = nullptr;
	QLabel* m_titleLabel = nullptr;

	QColor m_oldColor;  // 取消时恢复

	bool m_syncing = false;  // 防止信号循环
};

// 饱和度-明度正方形（HSV 的 S/V 选择区域）
class ColorPickerDialog::SaturationValueSquare : public QWidget
{
	Q_OBJECT
public:
	explicit SaturationValueSquare(QWidget* parent = nullptr);
	void setHue(int h);
	void setSaturationValue(int s, int v);
	QSize sizeHint() const override { return QSize(260, 260); }

	void updateImage();

signals:
	void svChanged(int saturation, int value);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void emitChangeAt(const QPoint& pos);

	int m_hue = 0;
	int m_saturation = 255;
	int m_value = 255;
	QImage m_image;
};

// 色相滑块（垂直条）
class ColorPickerDialog::HueSlider : public QWidget
{
	Q_OBJECT
public:
	explicit HueSlider(QWidget* parent = nullptr);
	void setHue(int h);
	QSize sizeHint() const override { return QSize(30, 260); }

	void updateImage();

signals:
	void hueChanged(int hue);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	
	int m_hue = 0;
	QImage m_image;
};

// Alpha 滑块（垂直条）
class ColorPickerDialog::AlphaSlider : public QWidget
{
	Q_OBJECT
public:
	explicit AlphaSlider(QWidget* parent = nullptr);
	void setColor(const QColor& baseColor);
	void setAlpha(int a);
	QSize sizeHint() const override { return QSize(30, 260); }

	void updateImage();

signals:
	void alphaChanged(int alpha);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	int m_alpha = 255;
	QColor m_baseColor;
	QImage m_image;
};
