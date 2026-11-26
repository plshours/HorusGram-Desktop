/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/glass_backdrop.h"

#include "ui/abstract_button.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"

#include <QtCore/QPointer>
#include <QtGui/QPainter>
#include <QtGui/QScreen>

namespace HoursGram::Customization {
namespace {

// The picture is blurred at a quarter of its size, which is both much
// faster and softer: 7px there are 28px on the screen.
constexpr auto kDownscale = 4;
constexpr auto kBlurRadius = 7;
constexpr auto kSaturation = 1.35;

// The tint of the viewer over the glass, 42% of a near black.
constexpr auto kTintAlpha = 107;

// The tint without glass under it: the viewer is not see-through then,
// or the desktop shows at its sides.
constexpr auto kSolidAlpha = 235;

[[nodiscard]] QPointer<QWidget> &Backdrop() {
	static auto result = QPointer<QWidget>();
	return result;
}

// A picture of the screen that came out blank, all white or all black,
// is no glass: some systems do not let the screen be read.
[[nodiscard]] bool LooksBlank(const QImage &image) {
	if (image.isNull()) {
		return true;
	}
	const auto small = image.scaled(
		16,
		16,
		Qt::IgnoreAspectRatio,
		Qt::FastTransformation).convertToFormat(QImage::Format_ARGB32);
	auto low = 255, high = 0;
	for (auto y = 0; y != small.height(); ++y) {
		for (auto x = 0; x != small.width(); ++x) {
			const auto gray = qGray(small.pixel(x, y));
			low = std::min(low, gray);
			high = std::max(high, gray);
		}
	}
	return (high - low) < 6;
}

// Blurring takes the colors out of a picture, so they are made a little
// richer first, the way frosted glass looks.
void Saturate(QImage &image) {
	const auto clamp = [](float64 value) {
		return int(std::clamp(value, 0., 255.));
	};
	for (auto y = 0, height = image.height(); y != height; ++y) {
		const auto line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (auto x = 0, width = image.width(); x != width; ++x) {
			const auto color = line[x];
			const auto r = qRed(color);
			const auto g = qGreen(color);
			const auto b = qBlue(color);
			const auto gray = (r * 299 + g * 587 + b * 114) / 1000.;
			line[x] = qRgba(
				clamp(gray + (r - gray) * kSaturation),
				clamp(gray + (g - gray) * kSaturation),
				clamp(gray + (b - gray) * kSaturation),
				qAlpha(color));
		}
	}
}

[[nodiscard]] QImage PrepareGlass(QImage image) {
	const auto size = QSize(
		std::max(image.width() / kDownscale, 1),
		std::max(image.height() / kDownscale, 1));
	auto small = image.scaled(
		size,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation
	).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	small.setDevicePixelRatio(1.);
	Saturate(small);
	return Images::BlurLargeImage(std::move(small), kBlurRadius);
}

} // namespace

void ShowGlassBackdrop(not_null<QWidget*> window) {
	HideGlassBackdrop();
	if (!Ui::HoursGramGraphite()
		|| window->isHidden()
		|| window->isMinimized()
		|| window->size().isEmpty()) {
		return;
	}
	const auto screen = window->screen();
	if (!screen) {
		return;
	}
	// The viewer takes the whole screen, so the glass does too: under the
	// client window alone, the desktop showed at the sides of a photo.
	// What is on the screen is taken, not what the widgets paint: parts
	// drawn on the GPU come out blank when they are asked to paint.
	const auto geometry = screen->geometry();
	auto image = screen->grabWindow(
		0,
		0,
		0,
		geometry.width(),
		geometry.height()).toImage();
	if (LooksBlank(image)) {
		return;
	}
	const auto glass = PrepareGlass(std::move(image));

	// A window of its own under the viewer, never focused and not in the
	// taskbar. It takes the clicks that would reach the chats under it.
	const auto cover = new QWidget(
		nullptr,
		Qt::FramelessWindowHint
			| Qt::Tool
			| Qt::WindowDoesNotAcceptFocus
			| Qt::NoDropShadowWindowHint);
	cover->setAttribute(Qt::WA_ShowWithoutActivating);
	cover->setAttribute(Qt::WA_DeleteOnClose);
	cover->setAttribute(Qt::WA_NoSystemBackground);
	cover->setGeometry(geometry);
	const auto painter = Ui::CreateChild<Ui::AbstractButton>(cover);
	painter->setGeometry(cover->rect());
	painter->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(painter);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(painter->rect(), glass);
	}, painter->lifetime());
	painter->show();
	cover->show();
	Backdrop() = cover;
}

void HideGlassBackdrop() {
	if (const auto cover = Backdrop().data()) {
		delete cover;
	}
	Backdrop() = nullptr;
}

QColor ViewerBackground(QColor themed) {
	if (!Ui::HoursGramGraphite()) {
		return themed;
	}
	return QColor(10, 10, 12, Backdrop() ? kTintAlpha : kSolidAlpha);
}

} // namespace HoursGram::Customization
