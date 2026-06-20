// ColorPickerDialog.cpp
#include "ColorPickerDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSlider>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

// ============================================================
// 内部子控件：SV 正方形（饱和度-明度）
// ============================================================
ColorPickerDialog::SaturationValueSquare::SaturationValueSquare(QWidget* parent)
	: QWidget(parent)
{
	// 固定为正方形，不随对话框缩放改变大小
	setFixedSize(260, 260);
	setMouseTracking(true);
	updateImage();
}

void ColorPickerDialog::SaturationValueSquare::setHue(int h)
{
	m_hue = qBound(0, h, 359);
	updateImage();
	update();
}

void ColorPickerDialog::SaturationValueSquare::setSaturationValue(int s, int v)
{
	m_saturation = qBound(0, s, 255);
	m_value = qBound(0, v, 255);
	update();
}

void ColorPickerDialog::SaturationValueSquare::paintEvent(QPaintEvent* /*event*/)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);
	p.drawImage(0, 0, m_image);

	// 绘制当前位置的指示圆圈
	int x = (m_saturation * width()) / 255;
	int y = height() - (m_value * height()) / 255 - 1;
	p.setPen(QPen(Qt::white, 2));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(QPoint(x, y), 7, 7);
	p.setPen(QPen(Qt::black, 1));
	p.drawEllipse(QPoint(x, y), 8, 8);
}

void ColorPickerDialog::SaturationValueSquare::mousePressEvent(QMouseEvent* event)
{
	emitChangeAt(event->pos());
	event->accept();
}

void ColorPickerDialog::SaturationValueSquare::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton) {
		emitChangeAt(event->pos());
		event->accept();
	}
}

void ColorPickerDialog::SaturationValueSquare::resizeEvent(QResizeEvent* /*event*/)
{
	updateImage();
}

void ColorPickerDialog::SaturationValueSquare::updateImage()
{
	if (width() <= 0 || height() <= 0) return;
	m_image = QImage(width(), height(), QImage::Format_RGB32);

	for (int y = 0; y < height(); ++y) {
		int v = 255 - (y * 255) / height();
		for (int x = 0; x < width(); ++x) {
			int s = (x * 255) / width();
			m_image.setPixelColor(x, y, QColor::fromHsv(m_hue, s, v));
		}
	}
}

void ColorPickerDialog::SaturationValueSquare::emitChangeAt(const QPoint& pos)
{
	int x = qBound(0, pos.x(), width() - 1);
	int y = qBound(0, pos.y(), height() - 1);
	m_saturation = (x * 255) / width();
	m_value = 255 - (y * 255) / height();
	update();
	emit svChanged(m_saturation, m_value);
}

// ============================================================
// 内部子控件：色相滑块
// ============================================================
ColorPickerDialog::HueSlider::HueSlider(QWidget* parent)
	: QWidget(parent)
{
	setMinimumSize(24, 180);
	setMouseTracking(true);
	updateImage();
}

void ColorPickerDialog::HueSlider::setHue(int h)
{
	m_hue = qBound(0, h, 359);
	update();
}

void ColorPickerDialog::HueSlider::paintEvent(QPaintEvent* /*event*/)
{
	QPainter p(this);
	p.drawImage(0, 0, m_image);

	// 绘制指示箭头
	int y = (m_hue * height()) / 360;
	p.setBrush(Qt::white);
	p.setPen(QPen(Qt::black, 1));
	QPolygon leftArrow;
	leftArrow << QPoint(0, y - 6) << QPoint(0, y + 6) << QPoint(8, y);
	p.drawPolygon(leftArrow);
	QPolygon rightArrow;
	rightArrow << QPoint(width(), y - 6) << QPoint(width(), y + 6) << QPoint(width() - 8, y);
	p.drawPolygon(rightArrow);
}

void ColorPickerDialog::HueSlider::mousePressEvent(QMouseEvent* event)
{
	m_hue = qBound(0, (event->y() * 360) / height(), 359);
	update();
	emit hueChanged(m_hue);
	event->accept();
}

void ColorPickerDialog::HueSlider::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton) {
		m_hue = qBound(0, (event->y() * 360) / height(), 359);
		update();
		emit hueChanged(m_hue);
		event->accept();
	}
}

void ColorPickerDialog::HueSlider::resizeEvent(QResizeEvent* /*event*/)
{
	updateImage();
}

void ColorPickerDialog::HueSlider::updateImage()
{
	if (width() <= 0 || height() <= 0) return;
	m_image = QImage(width(), height(), QImage::Format_RGB32);

	for (int y = 0; y < height(); ++y) {
		int h = (y * 360) / height();
		QColor c = QColor::fromHsv(h, 255, 255);
		for (int x = 0; x < width(); ++x) {
			m_image.setPixelColor(x, y, c);
		}
	}
}

// ============================================================
// 内部子控件：Alpha 滑块
// ============================================================
ColorPickerDialog::AlphaSlider::AlphaSlider(QWidget* parent)
	: QWidget(parent)
{
	setMinimumSize(24, 180);
	setMouseTracking(true);
	updateImage();
}

void ColorPickerDialog::AlphaSlider::setColor(const QColor& baseColor)
{
	m_baseColor = baseColor;
	updateImage();
	update();
}

void ColorPickerDialog::AlphaSlider::setAlpha(int a)
{
	m_alpha = qBound(0, a, 255);
	update();
}

void ColorPickerDialog::AlphaSlider::paintEvent(QPaintEvent* /*event*/)
{
	QPainter p(this);
	// 先画棋盘格背景表示透明
	int cellSize = 6;
	for (int y = 0; y < height(); y += cellSize) {
		for (int x = 0; x < width(); x += cellSize) {
			bool dark = ((x / cellSize + y / cellSize) % 2 == 0);
			p.fillRect(x, y, cellSize, cellSize,
				dark ? QColor(220, 220, 220) : QColor(255, 255, 255));
		}
	}
	// 再画半透明色叠加
	p.drawImage(0, 0, m_image);

	// 绘制指示箭头
	int y = height() - (m_alpha * height()) / 255;
	p.setBrush(Qt::white);
	p.setPen(QPen(Qt::black, 1));
	QPolygon leftArrow;
	leftArrow << QPoint(0, y - 6) << QPoint(0, y + 6) << QPoint(8, y);
	p.drawPolygon(leftArrow);
	QPolygon rightArrow;
	rightArrow << QPoint(width(), y - 6) << QPoint(width(), y + 6) << QPoint(width() - 8, y);
	p.drawPolygon(rightArrow);
}

void ColorPickerDialog::AlphaSlider::mousePressEvent(QMouseEvent* event)
{
	m_alpha = qBound(0, 255 - (event->y() * 255) / height(), 255);
	update();
	emit alphaChanged(m_alpha);
	event->accept();
}

void ColorPickerDialog::AlphaSlider::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton) {
		m_alpha = qBound(0, 255 - (event->y() * 255) / height(), 255);
		update();
		emit alphaChanged(m_alpha);
		event->accept();
	}
}

void ColorPickerDialog::AlphaSlider::resizeEvent(QResizeEvent* /*event*/)
{
	updateImage();
}

void ColorPickerDialog::AlphaSlider::updateImage()
{
	if (width() <= 0 || height() <= 0) return;
	m_image = QImage(width(), height(), QImage::Format_ARGB32);

	for (int y = 0; y < height(); ++y) {
		int a = 255 - (y * 255) / height();
		QColor c = m_baseColor;
		c.setAlpha(a);
		for (int x = 0; x < width(); ++x) {
			m_image.setPixelColor(x, y, c);
		}
	}
}

// ============================================================
// 颜色选择器对话框
// ============================================================
ColorPickerDialog::ColorPickerDialog(QWidget* parent)
	: QDialog(parent)
{
	setupUI();
	setColor(Qt::white);
	m_oldColor = m_currentColor;
}

void ColorPickerDialog::setPickerTitle(const QString& title)
{
	setWindowTitle(title);
	if (m_titleLabel) m_titleLabel->setText(title);
}

void ColorPickerDialog::setColor(const QColor& color)
{
	m_initialColor = color;
	m_oldColor = color;
	hsvFromColor(color, m_hue, m_saturation, m_value, m_alpha);
	syncFromColor();
	syncToColor();
}

void ColorPickerDialog::setupUI()
{
	setWindowTitle("颜色选择器");
	setModal(true);
	resize(560, 380);
	setStyleSheet(R"(
		QDialog { background-color: #2A2A2A; color: #DDDDDD; }
		QLabel { color: #DDDDDD; }
		QLineEdit {
			background-color: #1E1E1E; color: #DDDDDD;
			border: 1px solid #555; padding: 4px; border-radius: 3px;
		}
		QSlider::groove:vertical {
			background: #444; width: 4px; border-radius: 2px;
		}
		QSlider::handle:vertical {
			background: #888; height: 14px; margin: 0 -6px;
			border-radius: 3px;
		}
		QPushButton {
			background-color: #3A3A3A; color: #DDDDDD;
			border: 1px solid #555; padding: 6px 16px; border-radius: 3px;
		}
		QPushButton:hover { background-color: #4A4A4A; }
		QPushButton:pressed { background-color: #2A5A8A; }
	)");

	auto* rootLayout = new QHBoxLayout(this);
	rootLayout->setContentsMargins(12, 12, 12, 12);
	rootLayout->setSpacing(12);

	// 左侧：SV 正方形
	auto* leftContainer = new QWidget(this);
	auto* leftLayout = new QVBoxLayout(leftContainer);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->setSpacing(4);

	m_titleLabel = new QLabel("颜色选择器", leftContainer);
	m_titleLabel->setStyleSheet("color: #FFFFFF; font-size: 13px; font-weight: bold;");
	leftLayout->addWidget(m_titleLabel);

	m_svSquare = new SaturationValueSquare(leftContainer);
	leftLayout->addWidget(m_svSquare);

	// 左侧容器：固定尺寸（不参与水平拉伸）
	rootLayout->addWidget(leftContainer, 0);

	// 中间：色相、饱和度、明度、Alpha 滑块
	auto* middleContainer = new QWidget(this);
	auto* middleLayout = new QVBoxLayout(middleContainer);
	middleLayout->setContentsMargins(0, 0, 0, 0);
	middleLayout->setSpacing(6);

	auto* hueLabel = new QLabel("色相 (H):", middleContainer);
	middleLayout->addWidget(hueLabel);
	m_hueSlider = new HueSlider(middleContainer);
	middleLayout->addWidget(m_hueSlider, 1);

	auto* satLabel = new QLabel("饱和度 (S):", middleContainer);
	middleLayout->addWidget(satLabel);
	m_saturationSlider = new QSlider(Qt::Horizontal, middleContainer);
	m_saturationSlider->setRange(0, 255);
	m_saturationSlider->setValue(0);
	middleLayout->addWidget(m_saturationSlider);

	auto* valLabel = new QLabel("明度 (V):", middleContainer);
	middleLayout->addWidget(valLabel);
	m_valueSlider = new QSlider(Qt::Horizontal, middleContainer);
	m_valueSlider->setRange(0, 255);
	m_valueSlider->setValue(255);
	middleLayout->addWidget(m_valueSlider);

	auto* alphaLabel = new QLabel("透明度 (A):", middleContainer);
	middleLayout->addWidget(alphaLabel);
	m_alphaSliderBar = new QSlider(Qt::Horizontal, middleContainer);
	m_alphaSliderBar->setRange(0, 255);
	m_alphaSliderBar->setValue(255);
	middleLayout->addWidget(m_alphaSliderBar);

	middleLayout->addStretch();
	rootLayout->addWidget(middleContainer, 1);

	// 右侧：Alpha 渐变条 + 颜色预览 + 十六进制 + 按钮
	auto* rightContainer = new QWidget(this);
	auto* rightLayout = new QVBoxLayout(rightContainer);
	rightLayout->setContentsMargins(0, 0, 0, 0);
	rightLayout->setSpacing(6);

	auto* alphaVisLabel = new QLabel("Alpha 渐变:", rightContainer);
	rightLayout->addWidget(alphaVisLabel);
	m_alphaSlider = new AlphaSlider(rightContainer);
	rightLayout->addWidget(m_alphaSlider, 1);

	auto* prevLabel = new QLabel("预览:", rightContainer);
	rightLayout->addWidget(prevLabel);
	m_colorPreview = new QLabel(rightContainer);
	m_colorPreview->setMinimumHeight(50);
	m_colorPreview->setStyleSheet(
		"background-color: #FFFFFF;"
		"border: 1px solid #888; border-radius: 3px;");
	rightLayout->addWidget(m_colorPreview);

	auto* hexLabel = new QLabel("HEX:", rightContainer);
	rightLayout->addWidget(hexLabel);
	m_hexEdit = new QLineEdit(rightContainer);
	m_hexEdit->setPlaceholderText("#RRGGBB 或 #AARRGGBB");
	QRegularExpressionValidator* hexValidator = new QRegularExpressionValidator(
		QRegularExpression("^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$"), m_hexEdit);
	m_hexEdit->setValidator(hexValidator);
	rightLayout->addWidget(m_hexEdit);

	rightLayout->addStretch();

	auto* btnRow = new QWidget(rightContainer);
	auto* btnLayout = new QHBoxLayout(btnRow);
	btnLayout->setContentsMargins(0, 0, 0, 0);
	btnLayout->setSpacing(6);

	QPushButton* resetBtn = new QPushButton("重置", btnRow);
	QPushButton* okBtn = new QPushButton("确定", btnRow);
	QPushButton* cancelBtn = new QPushButton("取消", btnRow);

	btnLayout->addWidget(resetBtn);
	btnLayout->addStretch();
	btnLayout->addWidget(okBtn);
	btnLayout->addWidget(cancelBtn);
	rightLayout->addWidget(btnRow);

	rootLayout->addWidget(rightContainer, 1);

	// 信号连接
	// SV 正方形：同时更新 saturation 和 value（两个参数都要用，否则垂直拖动无效）
	connect(m_svSquare, &SaturationValueSquare::svChanged, this,
		[this](int s, int v) {
			if (m_syncing) return;
			m_saturation = qBound(0, s, 255);
			m_value = qBound(0, v, 255);
			syncToColor();
			syncFromColor();
		});
	connect(m_hueSlider, &HueSlider::hueChanged,
		this, &ColorPickerDialog::onHueChanged);
	connect(m_alphaSlider, &AlphaSlider::alphaChanged,
		this, &ColorPickerDialog::onAlphaChanged);
	connect(m_saturationSlider, &QSlider::valueChanged,
		this, &ColorPickerDialog::onSaturationChanged);
	connect(m_valueSlider, &QSlider::valueChanged,
		this, &ColorPickerDialog::onValueChanged);
	connect(m_alphaSliderBar, &QSlider::valueChanged,
		this, &ColorPickerDialog::onAlphaChanged);
	connect(m_hexEdit, &QLineEdit::editingFinished,
		this, &ColorPickerDialog::onHexChanged);

	connect(resetBtn, &QPushButton::clicked, this, &ColorPickerDialog::onResetClicked);
	connect(okBtn, &QPushButton::clicked, this, &ColorPickerDialog::onConfirmClicked);
	connect(cancelBtn, &QPushButton::clicked, this, &ColorPickerDialog::onCancelClicked);
}

void ColorPickerDialog::onHueChanged(int value)
{
	if (m_syncing) return;
	m_hue = qBound(0, value, 359);
	m_svSquare->setHue(m_hue);
	m_alphaSlider->setColor(hsvToColor(m_hue, m_saturation, m_value, 255));
	syncToColor();
	syncFromColor();
}

void ColorPickerDialog::onSaturationChanged(int value)
{
	if (m_syncing) return;
	m_saturation = qBound(0, value, 255);
	m_svSquare->setSaturationValue(m_saturation, m_value);
	syncToColor();
	syncFromColor();
}

void ColorPickerDialog::onValueChanged(int value)
{
	if (m_syncing) return;
	m_value = qBound(0, value, 255);
	m_svSquare->setSaturationValue(m_saturation, m_value);
	syncToColor();
	syncFromColor();
}

void ColorPickerDialog::onAlphaChanged(int value)
{
	if (m_syncing) return;
	m_alpha = qBound(0, value, 255);
	syncToColor();
	syncFromColor();
}

void ColorPickerDialog::onHexChanged()
{
	if (m_syncing) return;
	QString text = m_hexEdit->text().trimmed();
	if (text.isEmpty()) return;
	QColor c(text);
	if (!c.isValid()) return;
	hsvFromColor(c, m_hue, m_saturation, m_value, m_alpha);
	m_svSquare->setHue(m_hue);
	m_svSquare->setSaturationValue(m_saturation, m_value);
	m_alphaSlider->setColor(hsvToColor(m_hue, m_saturation, m_value, 255));
	syncFromColor();
	syncToColor();
}

void ColorPickerDialog::onResetClicked()
{
	setColor(m_initialColor);
}

void ColorPickerDialog::onConfirmClicked()
{
	accept();
}

void ColorPickerDialog::onCancelClicked()
{
	m_currentColor = m_oldColor;
	reject();
}

void ColorPickerDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	if (m_svSquare) m_svSquare->updateImage();
	if (m_hueSlider) m_hueSlider->updateImage();
	if (m_alphaSlider) m_alphaSlider->updateImage();
}

void ColorPickerDialog::syncFromColor()
{
	m_syncing = true;
	m_hueSlider->setHue(m_hue);
	m_svSquare->setHue(m_hue);
	m_svSquare->setSaturationValue(m_saturation, m_value);
	m_saturationSlider->setValue(m_saturation);
	m_valueSlider->setValue(m_value);
	m_alphaSliderBar->setValue(m_alpha);
	m_alphaSlider->setAlpha(m_alpha);
	m_alphaSlider->setColor(hsvToColor(m_hue, m_saturation, m_value, 255));
	m_syncing = false;
}

void ColorPickerDialog::syncToColor()
{
	m_currentColor = hsvToColor(m_hue, m_saturation, m_value, m_alpha);

	QString style = QString("background-color: %1; border: 1px solid #888; border-radius: 3px;")
		.arg(m_currentColor.name(QColor::HexArgb));
	m_colorPreview->setStyleSheet(style);

	m_hexEdit->blockSignals(true);
	m_hexEdit->setText(m_currentColor.name(QColor::HexArgb).toUpper());
	m_hexEdit->blockSignals(false);

	emit colorChanged(m_currentColor);
}

QColor ColorPickerDialog::hsvToColor(int h, int s, int v, int a) const
{
	QColor c = QColor::fromHsv(qBound(0, h, 359), qBound(0, s, 255), qBound(0, v, 255));
	c.setAlpha(qBound(0, a, 255));
	return c;
}

void ColorPickerDialog::hsvFromColor(const QColor& c, int& h, int& s, int& v, int& a) const
{
	h = c.hue();
	if (h < 0) h = 0;
	s = c.saturation();
	v = c.value();
	a = c.alpha();
}
