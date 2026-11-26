/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/intro_look.h"

#include "ayu/ayu_settings.h"
#include "base/timer.h"
#include "core/file_utilities.h"
#include "hoursgram/customization/video_background.h"
#include "hoursgram/music/music_common.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "ui/countryinput.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/show.h"
#include "ui/widgets/fields/masked_input_field.h"
#include "ui/effects/animation_value.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_font.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_hoursgram_design.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QLinearGradient>
#include <QtWidgets/QApplication>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>

#include <cmath>
#include <deque>
#include <map>

namespace HoursGram::Customization {
namespace {

using Background = AyuSettings::IntroBackground;
using HoursGram::Music::Tr;

// The glow is soft, so it is drawn this many times smaller and scaled up:
// that keeps a full window of gradients cheap enough to move.
constexpr auto kGlowScale = 6;

// The preview draws the screen of this size, scaled down to fit.
constexpr auto kPreviewWidth = 1000;
constexpr auto kPreviewHeight = 640;

// What HorusGram adds to the look, kept next to the video or picture of
// the login screen.
// Stickers of the user's own over the login screen: TGS, Lottie or a
// picture, placed where the constructor put them.
struct Sticker {
	QString file; // A file of the folder.
	float64 x = 50.; // Center, in percent of the screen.
	float64 y = 50.;
	int size = 120;
	int rotation = 0;
	int opacity = 100;
	bool loop = true;
};

struct Extra {
	int cardRadius = -1; // By the style.
	int cardOpacity = 100;
	bool round = false;
	QString buttonColor; // RRGGBB, or by the theme.
	QString logo; // A file of the folder.
	// A logo for a step of its own: "phone", "code", "password", "signup",
	// "qr" or "email". A step without one shows the general logo.
	std::map<QString, QString> stepLogos;
	int logoSize = 120;
	std::vector<Sticker> stickers;
};

[[nodiscard]] QString IntroFolder() {
	return cWorkingDir() + u"tdata/hoursgram_intro/"_q;
}

[[nodiscard]] Extra LoadExtra() {
	auto result = Extra();
	auto file = QFile(IntroFolder() + u"look.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	result.cardRadius = std::clamp(
		root.value(u"cardRadius"_q).toInt(-1),
		-1,
		60);
	result.cardOpacity = std::clamp(
		root.value(u"cardOpacity"_q).toInt(100),
		0,
		100);
	result.round = root.value(u"round"_q).toBool(false);
	result.buttonColor = root.value(u"buttonColor"_q).toString();
	result.logo = QFileInfo(root.value(u"logo"_q).toString()).fileName();
	if (!result.logo.isEmpty()
		&& !QFile::exists(IntroFolder() + result.logo)) {
		result.logo = QString();
	}
	const auto steps = root.value(u"stepLogos"_q).toObject();
	for (auto i = steps.begin(); i != steps.end(); ++i) {
		const auto file = QFileInfo(i.value().toString()).fileName();
		if (!file.isEmpty() && QFile::exists(IntroFolder() + file)) {
			result.stepLogos.emplace(i.key(), file);
		}
	}
	result.logoSize = std::clamp(
		root.value(u"logoSize"_q).toInt(120),
		40,
		240);
	for (const auto &value : root.value(u"stickers"_q).toArray()) {
		const auto object = value.toObject();
		auto sticker = Sticker{
			.file = QFileInfo(object.value(u"file"_q).toString()).fileName(),
			.x = std::clamp(object.value(u"x"_q).toDouble(50.), 0., 100.),
			.y = std::clamp(object.value(u"y"_q).toDouble(50.), 0., 100.),
			.size = std::clamp(object.value(u"size"_q).toInt(120), 16, 400),
			.rotation = object.value(u"rotation"_q).toInt(0) % 360,
			.opacity = std::clamp(object.value(u"opacity"_q).toInt(100), 0, 100),
			.loop = object.value(u"loop"_q).toBool(true),
		};
		if (!sticker.file.isEmpty()
			&& QFile::exists(IntroFolder() + sticker.file)) {
			result.stickers.push_back(std::move(sticker));
		}
	}
	return result;
}

[[nodiscard]] Extra &Look() {
	static auto result = LoadExtra();
	return result;
}

[[nodiscard]] rpl::event_stream<> &Changes() {
	static auto result = rpl::event_stream<>();
	return result;
}

void SaveExtra() {
	const auto &look = Look();
	QDir().mkpath(IntroFolder());
	auto root = QJsonObject();
	root.insert(u"cardRadius"_q, look.cardRadius);
	root.insert(u"cardOpacity"_q, look.cardOpacity);
	root.insert(u"round"_q, look.round);
	root.insert(u"buttonColor"_q, look.buttonColor);
	root.insert(u"logo"_q, look.logo);
	auto steps = QJsonObject();
	for (const auto &[step, file] : look.stepLogos) {
		steps.insert(step, file);
	}
	root.insert(u"stepLogos"_q, steps);
	root.insert(u"logoSize"_q, look.logoSize);
	auto stickers = QJsonArray();
	for (const auto &sticker : look.stickers) {
		auto object = QJsonObject();
		object.insert(u"file"_q, sticker.file);
		object.insert(u"x"_q, sticker.x);
		object.insert(u"y"_q, sticker.y);
		object.insert(u"size"_q, sticker.size);
		object.insert(u"rotation"_q, sticker.rotation);
		object.insert(u"opacity"_q, sticker.opacity);
		object.insert(u"loop"_q, sticker.loop);
		stickers.append(object);
	}
	root.insert(u"stickers"_q, stickers);
	auto file = QFile(IntroFolder() + u"look.json"_q);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	}
	Changes().fire({});
}

// A logo or a sticker: an animation of Telegram (TGS or Lottie) or a
// picture, drawn at one size.
struct LogoState {
	QString path;
	int size = 0;
	std::unique_ptr<Lottie::Icon> icon;
	QImage image;
	bool played = false;
};

[[nodiscard]] LogoState &Logo() {
	static auto result = LogoState();
	return result;
}

[[nodiscard]] std::vector<LogoState> &StickerStates() {
	static auto result = std::vector<LogoState>();
	return result;
}

void PaintAnimation(
		QPainter &p,
		LogoState &state,
		const QString &path,
		QRect area,
		bool loop) {
	const auto size = area.width();
	if (state.path != path || state.size != size) {
		state = LogoState();
		state.path = path;
		state.size = size;
		const auto lower = path.toLower();
		if (lower.endsWith(u".tgs"_q) || lower.endsWith(u".json"_q)) {
			state.icon = std::make_unique<Lottie::Icon>(Lottie::IconDescriptor{
				.path = path,
				.sizeOverride = QSize(size, size),
			});
		} else {
			state.image = Images::Read({ .path = path }).image;
		}
	}
	if (state.icon) {
		if (!state.icon->valid()) {
			return;
		}
		const auto frames = state.icon->framesCount();
		if (frames > 1
			&& !state.icon->animating()
			&& (loop || !state.played)) {
			state.played = true;
			state.icon->animate([] { Changes().fire({}); }, 0, frames - 1);
		}
		state.icon->paintInCenter(p, area);
	} else if (!state.image.isNull()) {
		const auto scaled = state.image.size().scaled(
			area.size(),
			Qt::KeepAspectRatio);
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(
			QRect(
				area.x() + (area.width() - scaled.width()) / 2,
				area.y() + (area.height() - scaled.height()) / 2,
				scaled.width(),
				scaled.height()),
			state.image);
	}
}

[[nodiscard]] QString &CurrentStep() {
	static auto result = QString();
	return result;
}

[[nodiscard]] const QStringList &StepKeys() {
	static const auto result = QStringList{
		u"phone"_q, u"code"_q, u"password"_q, u"signup"_q, u"qr"_q,
		u"email"_q,
	};
	return result;
}

// The logo of the step shown now, or the general one.
[[nodiscard]] QString LogoFile() {
	const auto &look = Look();
	const auto i = look.stepLogos.find(CurrentStep());
	return (i != end(look.stepLogos)) ? i->second : look.logo;
}

[[nodiscard]] bool HasAnyLogo() {
	const auto &look = Look();
	return !look.logo.isEmpty() || !look.stepLogos.empty();
}

void PaintLogo(QPainter &p, QRect area) {
	auto &logo = Logo();
	const auto file = LogoFile();
	if (file.isEmpty()) {
		if (!logo.path.isEmpty()) {
			logo = LogoState();
		}
		return;
	}
	PaintAnimation(p, logo, IntroFolder() + file, area, true);
}

void PaintStickers(QPainter &p, QSize outer) {
	const auto &look = Look();
	auto &states = StickerStates();
	states.resize(look.stickers.size());
	for (auto i = 0, count = int(look.stickers.size()); i != count; ++i) {
		const auto &sticker = look.stickers[i];
		const auto size = style::ConvertScale(sticker.size);
		const auto center = QPointF(
			outer.width() * sticker.x / 100.,
			outer.height() * sticker.y / 100.);
		p.save();
		p.translate(center);
		if (sticker.rotation) {
			p.rotate(sticker.rotation);
		}
		p.setOpacity(sticker.opacity / 100.);
		PaintAnimation(
			p,
			states[i],
			IntroFolder() + sticker.file,
			QRect(-size / 2, -size / 2, size, size),
			sticker.loop);
		p.restore();
	}
}

// Only files of these kinds are written from a file of the constructor.
[[nodiscard]] QString SafeSuffix(const QString &name, bool media) {
	const auto suffix = QFileInfo(name).suffix().toLower();
	static const auto pictures = QStringList{
		u"tgs"_q, u"json"_q, u"png"_q, u"jpg"_q, u"jpeg"_q, u"webp"_q,
		u"gif"_q, u"bmp"_q,
	};
	static const auto videos = QStringList{
		u"mp4"_q, u"mov"_q, u"mkv"_q, u"webm"_q, u"avi"_q, u"m4v"_q,
		u"gif"_q, u"jpg"_q, u"jpeg"_q, u"png"_q, u"webp"_q, u"bmp"_q,
	};
	return (media ? videos : pictures).contains(suffix) ? suffix : QString();
}

[[nodiscard]] QByteArray DecodeData(const QJsonObject &data) {
	return QByteArray::fromBase64(
		data.value(u"base64"_q).toString().toLatin1());
}

[[nodiscard]] bool WriteFile(const QString &path, const QByteArray &bytes) {
	auto file = QFile(path);
	return !bytes.isEmpty()
		&& file.open(QIODevice::WriteOnly)
		&& (file.write(bytes) == bytes.size());
}

// The logo is copied into the folder, as logo.<extension>.
void ChooseLogo(not_null<QWidget*> parent, Fn<void(QString)> toast) {
	const auto filter = Tr("Animations and pictures", "Анимации и картинки")
		+ u" (*.tgs *.json *.png *.jpg *.jpeg *.webp *.gif);;"_q
		+ FileDialog::AllFilesFilter();
	FileDialog::GetOpenPath(
		parent.get(),
		Tr("Logo of the login screen", "Логотип экрана входа"),
		filter,
		crl::guard(parent.get(), [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			const auto source = result.paths.front();
			const auto suffix = QFileInfo(source).suffix().toLower();
			const auto name = u"logo."_q
				+ (suffix.isEmpty() ? u"png"_q : suffix);
			const auto folder = IntroFolder();
			QDir().mkpath(folder);
			for (const auto &old : QDir(folder).entryList(
					{ u"logo.*"_q },
					QDir::Files)) {
				QFile::remove(folder + old);
			}
			Logo() = LogoState();
			if (!QFile::copy(source, folder + name)) {
				if (toast) {
					toast(Tr(
						"Could not copy the file.",
						"Не удалось скопировать файл."));
				}
				return;
			}
			Look().logo = name;
			SaveExtra();
		}));
}

void RemoveGeneralLogo() {
	auto &look = Look();
	if (!look.logo.isEmpty()) {
		QFile::remove(IntroFolder() + look.logo);
	}
	look.logo = QString();
	Logo() = LogoState();
}

void RemoveStepLogos() {
	const auto folder = IntroFolder();
	for (const auto &old : QDir(folder).entryList(
			{ u"logo_*"_q },
			QDir::Files)) {
		QFile::remove(folder + old);
	}
	Look().stepLogos.clear();
	Logo() = LogoState();
}

void RemoveLogo() {
	RemoveGeneralLogo();
	RemoveStepLogos();
	SaveExtra();
}

[[nodiscard]] QString StripHex(QString value) {
	value = value.trimmed();
	if (value.startsWith('#')) {
		value = value.mid(1);
	}
	return (value.size() == 6 && QColor(u"#"_q + value).isValid())
		? value.toUpper()
		: QString();
}

// Applies what the HTML constructor of HorusGram saved (horusgram-look.json):
// the look of the login screen with its files inside. Returns false when
// it is not a file of the constructor; `needFiles` tells whether the files
// of the background or of the logo were not inside.
bool ApplyConstructor(const QJsonObject &root, bool *needFiles) {
	const auto login = root.value(u"login"_q).toObject();
	if (login.isEmpty()) {
		return false;
	}
	auto &settings = AyuSettings::getInstance();
	const auto type = login.value(u"bgType"_q).toString();
	if (type == u"plain"_q) {
		settings.setIntroBackground(Background::Plain);
	} else if (type == u"gradient"_q) {
		settings.setIntroBackground(Background::Gradient);
	} else if (type == u"glow"_q) {
		settings.setIntroBackground(Background::Glow);
	}
	if (type != u"file"_q && HasIntroMedia()) {
		RemoveIntroMedia();
	}
	settings.setIntroColor1(
		StripHex(login.value(u"c1"_q).toString()));
	settings.setIntroColor2(
		StripHex(login.value(u"c2"_q).toString()));
	SetIntroMediaBlur(login.value(u"blur"_q).toInt(10));
	SetIntroMediaDim(login.value(u"dim"_q).toInt(0));
	if (login.contains(u"title"_q)) {
		settings.setIntroTitle(
			login.value(u"title"_q).toString().trimmed().left(48));
	}
	settings.setIntroCard(login.value(u"card"_q).toBool(true));

	auto &look = Look();
	look.cardRadius = std::clamp(
		login.value(u"cardRadius"_q).toInt(22),
		0,
		60);
	look.cardOpacity = std::clamp(
		login.value(u"cardAlpha"_q).toInt(100),
		0,
		100);
	look.round = (login.value(u"fieldRadius"_q).toInt(0) > 8)
		|| (login.value(u"btnRadius"_q).toInt(0) > 8);
	look.buttonColor = StripHex(
		login.value(u"btnColor"_q).toString());
	look.logoSize = std::clamp(
		login.value(u"logoSize"_q).toInt(120),
		40,
		240);

	// The files themselves, when the constructor put them inside.
	const auto folder = IntroFolder();
	QDir().mkpath(folder);
	const auto media = login.value(u"fileData"_q).toObject();
	if (type == u"file"_q && !media.isEmpty()) {
		const auto suffix = SafeSuffix(
			media.value(u"name"_q).toString(),
			true);
		if (!suffix.isEmpty()) {
			SetIntroMedia(suffix, DecodeData(media));
		}
	}
	const auto logo = login.value(u"logoData"_q).toObject();
	if (!logo.isEmpty()) {
		const auto suffix = SafeSuffix(
			logo.value(u"name"_q).toString(),
			false);
		const auto name = u"logo."_q + suffix;
		if (!suffix.isEmpty()) {
			for (const auto &old : QDir(folder).entryList(
					{ u"logo.*"_q },
					QDir::Files)) {
				QFile::remove(folder + old);
			}
			Logo() = LogoState();
			look.logo = WriteFile(folder + name, DecodeData(logo))
				? name
				: QString();
		}
	} else if (login.value(u"logo"_q).toString().isEmpty()) {
		// The constructor shows its own plane: no logo here.
		RemoveGeneralLogo();
	}
	if (login.contains(u"logos"_q)) {
		RemoveStepLogos();
		const auto logos = login.value(u"logos"_q).toObject();
		for (const auto &step : StepKeys()) {
			const auto data = logos.value(step).toObject();
			if (data.isEmpty()) {
				continue;
			}
			const auto suffix = SafeSuffix(
				data.value(u"name"_q).toString(),
				false);
			if (suffix.isEmpty()) {
				continue;
			}
			const auto name = u"logo_"_q + step + u"."_q + suffix;
			if (WriteFile(folder + name, DecodeData(data))) {
				look.stepLogos.emplace(step, name);
			}
		}
	}
	if (root.contains(u"stickers"_q)) {
		for (const auto &old : QDir(folder).entryList(
				{ u"sticker_*"_q },
				QDir::Files)) {
			QFile::remove(folder + old);
		}
		StickerStates().clear();
		look.stickers.clear();
		auto index = 0;
		for (const auto &value : root.value(u"stickers"_q).toArray()) {
			const auto object = value.toObject();
			const auto data = object.value(u"data"_q).toObject();
			if (object.value(u"page"_q).toString() != u"login"_q
				|| data.isEmpty()) {
				continue;
			}
			const auto suffix = SafeSuffix(
				data.value(u"name"_q).toString(),
				false);
			if (suffix.isEmpty()) {
				continue;
			}
			const auto name = u"sticker_%1."_q.arg(index++) + suffix;
			if (!WriteFile(folder + name, DecodeData(data))) {
				continue;
			}
			look.stickers.push_back({
				.file = name,
				.x = std::clamp(object.value(u"x"_q).toDouble(50.), 0., 100.),
				.y = std::clamp(object.value(u"y"_q).toDouble(50.), 0., 100.),
				.size = std::clamp(object.value(u"size"_q).toInt(120), 16, 400),
				.rotation = object.value(u"rotation"_q).toInt(0) % 360,
				.opacity = std::clamp(object.value(u"opacity"_q).toInt(100), 0, 100),
				.loop = object.value(u"loop"_q).toBool(true),
			});
		}
	}
	SaveExtra();

	if (needFiles) {
		const auto needMedia = (type == u"file"_q) && !HasIntroMedia();
		const auto needLogo = !login.value(u"logo"_q).toString().isEmpty()
			&& look.logo.isEmpty();
		*needFiles = needMedia || needLogo;
	}
	return true;
}

// The look HorusGram comes with, the same file of the constructor.
[[nodiscard]] QJsonObject DefaultLook() {
	auto file = QFile(u":/gui/hoursgram-intro-look.json"_q);
	return file.open(QIODevice::ReadOnly)
		? QJsonDocument::fromJson(file.readAll()).object()
		: QJsonObject();
}

// Takes a file of the constructor picked by the user.
void ImportConstructor(
		not_null<QWidget*> parent,
		Fn<void(QString)> toast) {
	FileDialog::GetOpenPath(
		parent.get(),
		Tr("Settings from the constructor", "Настройки из конструктора"),
		u"JSON (*.json);;"_q + FileDialog::AllFilesFilter(),
		crl::guard(parent.get(), [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			auto file = QFile(result.paths.front());
			// The files come inside, a video can make it large.
			constexpr auto kMaxSize = 400 * 1024 * 1024;
			if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxSize) {
				if (toast) {
					toast(Tr(
						"The file is too large.",
						"Файл слишком большой."));
				}
				return;
			}
			const auto root = QJsonDocument::fromJson(
				file.readAll()).object();
			auto needFiles = false;
			if (!ApplyConstructor(root, &needFiles)) {
				if (toast) {
					toast(Tr(
						"This is not a file of the constructor.",
						"Это не файл конструктора."));
				}
				return;
			}
			if (toast) {
				toast(needFiles
					? Tr(
						"Done. Now pick the background and the logo files.",
						"Готово. Осталось выбрать файлы фона и логотипа.")
					: Tr("Done.", "Готово."));
			}
		}));
}

[[nodiscard]] QColor Parse(const QString &hex, QColor fallback) {
	if (hex.isEmpty()) {
		return fallback;
	}
	const auto color = QColor(u"#"_q + hex);
	return color.isValid() ? color : fallback;
}

[[nodiscard]] QColor AutoFirstColor() {
	return anim::color(st::windowBg, st::windowBgActive, 0.45);
}

[[nodiscard]] QColor AutoSecondColor() {
	return anim::color(st::windowBg, st::windowBgActive, 0.06);
}

[[nodiscard]] QColor FirstColor() {
	return Parse(AyuSettings::getInstance().introColor1(), AutoFirstColor());
}

[[nodiscard]] QColor SecondColor() {
	return Parse(AyuSettings::getInstance().introColor2(), AutoSecondColor());
}

void PaintGradient(QPainter &p, QRect rect) {
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., FirstColor());
	gradient.setColorAt(1., SecondColor());
	p.fillRect(rect, gradient);
}

void PaintGlow(QPainter &p, QRect rect, crl::time now) {
	const auto first = FirstColor();
	const auto second = SecondColor();
	const auto small = QSize(
		std::max(rect.width() / kGlowScale, 1),
		std::max(rect.height() / kGlowScale, 1));
	auto image = QImage(small, QImage::Format_ARGB32_Premultiplied);
	image.fill(second);
	{
		auto q = QPainter(&image);
		auto hq = PainterHighQualityEnabler(q);
		const auto t = now / 1000.;
		const auto w = float64(small.width());
		const auto h = float64(small.height());
		const auto size = std::max(w, h);
		const auto blob = [&](QPointF center, float64 radius, QColor color) {
			auto gradient = QRadialGradient(center, radius);
			auto transparent = color;
			transparent.setAlpha(0);
			gradient.setColorAt(0., color);
			gradient.setColorAt(1., transparent);
			q.fillRect(image.rect(), gradient);
		};
		// Three soft lights drift slowly along their own paths, so the
		// picture never repeats in an obvious way.
		auto main = first;
		main.setAlpha(235);
		blob(
			{
				w * (0.25 + 0.12 * std::sin(t * 0.21)),
				h * (0.30 + 0.15 * std::cos(t * 0.17)),
			},
			size * 0.62,
			main);
		// A very light accent would glare at full strength.
		auto accent = st::windowBgActive->c;
		accent.setAlpha(accent.lightness() > 200 ? 56 : 120);
		blob(
			{
				w * (0.78 + 0.10 * std::cos(t * 0.13)),
				h * (0.72 + 0.12 * std::sin(t * 0.19)),
			},
			size * 0.55,
			accent);
		auto mixed = anim::color(first, accent, 0.5);
		mixed.setAlpha(170);
		blob(
			{
				w * (0.58 + 0.20 * std::sin(t * 0.09 + 1.)),
				h * (0.12 + 0.10 * std::cos(t * 0.11 + 2.)),
			},
			size * 0.45,
			mixed);
	}
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(rect, image);
	p.restore();
}

void PaintCard(QPainter &p, QRect card) {
	auto hq = PainterHighQualityEnabler(p);
	const auto &look = Look();
	const auto radius = float64((look.cardRadius >= 0)
		? style::ConvertScale(look.cardRadius)
		: st::introLookCardRadius);
	const auto opacity = look.cardOpacity / 100.;
	if (opacity <= 0.) {
		return;
	}

	// A soft shadow under the card, a little lower than the card itself.
	const auto shadow = st::introLookCardShadow;
	const auto steps = 6;
	p.setPen(Qt::NoPen);
	for (auto i = steps; i > 0; --i) {
		const auto grow = shadow * i / steps;
		auto color = st::windowShadowFg->c;
		color.setAlphaF(0.045 * opacity);
		p.setBrush(color);
		p.drawRoundedRect(
			QRectF(card).marginsAdded(
				{ grow * 1., grow * 1., grow * 1., grow * 1. }
			).translated(0, shadow / 4.),
			radius + grow,
			radius + grow);
	}

	auto bg = st::windowBg->c;
	bg.setAlphaF(bg.alphaF() * opacity);
	auto line = st::windowBgOver->c;
	line.setAlphaF(line.alphaF() * opacity);
	p.setBrush(bg);
	p.setPen(QPen(line, st::lineWidth));
	const auto half = st::lineWidth / 2.;
	p.drawRoundedRect(
		QRectF(card).marginsRemoved({ half, half, half, half }),
		radius,
		radius);
}

void EnsureMontserratFont() {
	static auto done = false;
	if (done) {
		return;
	}
	done = true;
	const auto db = QFontDatabase();
	if (!db.families().contains(u"Montserrat"_q)) {
		for (const auto path : {
			u":/gui/fonts/Montserrat-Regular.ttf"_q,
			u":/gui/fonts/Montserrat-Italic.ttf"_q,
			u":/gui/fonts/Montserrat-SemiBold.ttf"_q,
			u":/gui/fonts/Montserrat-SemiBoldItalic.ttf"_q,
		}) {
			QFontDatabase::addApplicationFont(path);
		}
	}
	// Keep explicit fallbacks so Qt never stalls scanning all system fonts
	// on Windows for characters absent from Montserrat.
	QFont::insertSubstitutions(u"Montserrat"_q, {
		u"Segoe UI"_q,
		u"Segoe UI Emoji"_q,
		u"Segoe UI Symbol"_q,
		u"Arial"_q,
	});
}

[[nodiscard]] style::font IntroMontserratFont(const style::font &base) {
	EnsureMontserratFont();
	const auto size = base->f.pixelSize();
	const auto flags = base->flags();
	using Flags = std::decay_t<decltype(flags)>;
	struct Entry {
		int size = 0;
		Flags flags{};
		std::unique_ptr<style::owned_font> font;
	};
	static auto cache = std::vector<Entry>();
	for (const auto &entry : cache) {
		if (entry.size == size && entry.flags == flags) {
			return entry.font->font();
		}
	}
	auto owned = std::make_unique<style::owned_font>(
		u"Montserrat"_q,
		flags,
		size);
	const auto result = owned->font();
	cache.push_back({ size, flags, std::move(owned) });
	return result;
}

[[nodiscard]] style::FlatLabel WithMontserrat(style::FlatLabel base) {
	base.style.font = IntroMontserratFont(base.style.font);
	return base;
}

[[nodiscard]] style::InputField WithMontserrat(style::InputField base) {
	base.style.font = IntroMontserratFont(base.style.font);
	base.placeholderFont = IntroMontserratFont(base.placeholderFont);
	return base;
}

[[nodiscard]] style::RoundButton WithMontserrat(style::RoundButton base) {
	base.style.font = IntroMontserratFont(base.style.font);
	return base;
}

// With the card on, the logo and the title are inside it, above the form:
// the card adapts cleanly to the step height and stays centered vertically.
struct Header {
	int logo = 0;
	int title = 0;
	int height = 0;
};

constexpr auto kCardTopMargin = 20;
constexpr auto kCardBottomMargin = 40;

[[nodiscard]] bool HeaderInCard() {
	return AyuSettings::getInstance().introCard();
}

[[nodiscard]] Header ComputeHeader(int logo) {
	const auto skip = st::introLookLogoSkip;
	auto result = Header();
	const auto title = AyuSettings::getInstance().introTitle().trimmed();
	result.title = title.isEmpty()
		? 0
		: IntroMontserratFont(st::introLookCardTitleFont)->height;
	result.logo = logo;
	if (!result.logo && !result.title) {
		return result;
	}
	result.height = 2 * skip
		+ result.logo
		+ ((result.logo && result.title) ? skip : 0)
		+ result.title
		+ skip;
	return result;
}

[[nodiscard]] int WantedLogo() {
	return HasAnyLogo() ? style::ConvertScale(Look().logoSize) : 0;
}

// Computes the card header for a window of `windowHeight` and a step form of
// `cardBottom` height, shrinking the logo only when needed to fit vertically.
[[nodiscard]] Header CardHeaderForHeight(int windowHeight, int cardBottom) {
	const auto without = ComputeHeader(0);
	const auto topMargin = style::ConvertScale(kCardTopMargin);
	const auto bottomMargin = style::ConvertScale(kCardBottomMargin);
	const auto room = windowHeight
		- topMargin
		- bottomMargin
		- cardBottom
		- without.height
		- (without.title ? st::introLookLogoSkip : st::introLookLogoSkip);
	auto logo = std::min(WantedLogo(), room);
	if (logo < st::introLookLogoMin) {
		logo = 0;
	}
	return ComputeHeader(logo);
}

[[nodiscard]] QColor TitleColor() {
	if (HasIntroMedia()) {
		return QColor(255, 255, 255);
	} else if (AyuSettings::getInstance().introBackground() == Background::Plain) {
		return st::introTitleFg->c;
	}
	const auto middle = anim::color(FirstColor(), SecondColor(), 0.5);
	return (middle.lightness() < 150)
		? QColor(255, 255, 255)
		: QColor(0x1F, 0x1F, 0x24);
}

// Returns the top of the title, or -1 when there is none.
int PaintTitle(QPainter &p, QRect card, const QString &title) {
	if (title.isEmpty()) {
		return -1;
	}
	const auto font = IntroMontserratFont(st::introLookTitleFont);
	const auto top = card.y() - st::introLookTitleSkip - font->height;
	if (top < st::introLookTitleSkip / 2) {
		// No room above the form on a small window.
		return -1;
	}
	p.setFont(font);
	p.setPen(TitleColor());
	const auto text = font->elided(title, card.width());
	p.drawText(
		QRect(card.x(), top, card.width(), font->height),
		Qt::AlignCenter,
		text);
	return top;
}

// What the form looks like in the preview: a title, two fields and the
// button, drawn as plain shapes.
void PaintPreviewForm(QPainter &p, QRect card) {
	auto hq = PainterHighQualityEnabler(p);
	const auto left = card.x() + card.width() / 2 - 150;
	auto top = card.y() + st::introLookCardTop + 24;
	p.setPen(Qt::NoPen);

	p.setBrush(st::introTitleFg);
	p.drawRoundedRect(QRect(left + 50, top, 200, 18), 9, 9);
	top += 40;
	p.setBrush(st::introDescriptionFg);
	p.drawRoundedRect(QRect(left + 20, top, 260, 10), 5, 5);
	top += 20;
	p.drawRoundedRect(QRect(left + 60, top, 180, 10), 5, 5);
	top += 56;

	for (auto i = 0; i != 2; ++i) {
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(QRect(left, top, 300, 4), 2, 2);
		top += 56;
	}
	top += 24;
	p.setBrush(st::activeButtonBg);
	p.drawRoundedRect(QRect(left, top, 300, 42), 8, 8);
}

void PaintPreview(QPainter &p, QSize size, crl::time now) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = float64(st::introLookPreviewRadius);
	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(QPointF(), QSizeF(size)), radius, radius);
	p.setClipPath(clip);

	// The screen of the virtual size is drawn centered and scaled to fill.
	const auto virtualSize = QSize(kPreviewWidth, kPreviewHeight);
	const auto scale = std::max(
		size.width() / float64(virtualSize.width()),
		size.height() / float64(virtualSize.height()));
	p.translate(
		(size.width() - virtualSize.width() * scale) / 2.,
		(size.height() - virtualSize.height() * scale) / 2.);
	p.scale(scale, scale);

	const auto cardBottom = st::introLookCardBottom;
	const auto contentTop = IntroContentTop(virtualSize.height(), cardBottom);
	PaintIntro(p, virtualSize, contentTop, cardBottom, now);
	// The form starts where the step would put it, under the header.
	auto form = IntroCardRect(virtualSize, contentTop, cardBottom);
	form.moveTop(contentTop - st::introLookCardTop);
	PaintPreviewForm(p, form);
}

[[nodiscard]] QString ColorText(const QString &hex) {
	return hex.isEmpty()
		? Tr("By theme", "По теме")
		: (u"#"_q + hex);
}

} // namespace

bool IntroDecorated() {
	const auto &settings = AyuSettings::getInstance();
	return (settings.introBackground() != Background::Plain)
		|| settings.introCard()
		|| !settings.introTitle().trimmed().isEmpty()
		|| HasIntroMedia()
		|| HasAnyLogo()
		|| !Look().stickers.empty();
}

bool IntroAnimated() {
	// With animations off the glow stands still and costs nothing.
	return !anim::Disabled()
		&& (AyuSettings::getInstance().introBackground() == Background::Glow);
}

QRect IntroCardRect(QSize outer, int contentTop, int cardBottom) {
	const auto width = std::min(st::introLookCardWidth, outer.width());
	const auto header = HeaderInCard()
		? CardHeaderForHeight(outer.height(), cardBottom)
		: Header();
	const auto top = header.height ? header.height : st::introLookCardTop;
	return QRect(
		(outer.width() - width) / 2,
		contentTop - top,
		width,
		top + cardBottom);
}

bool IntroHidesPhoneTitle() {
	// The title of the card says it already.
	return HeaderInCard()
		&& !AyuSettings::getInstance().introTitle().trimmed().isEmpty();
}

void PaintIntro(
		QPainter &p,
		QSize outer,
		int contentTop,
		int cardBottom,
		crl::time now) {
	const auto &settings = AyuSettings::getInstance();
	const auto rect = QRect(QPoint(), outer);
	if (!PaintIntroMedia(p, outer, rect)) {
		switch (settings.introBackground()) {
		case Background::Plain: p.fillRect(rect, st::windowBg); break;
		case Background::Gradient: PaintGradient(p, rect); break;
		case Background::Glow: PaintGlow(p, rect, now); break;
		}
	}
	const auto card = IntroCardRect(outer, contentTop, cardBottom);
	if (settings.introCard()) {
		PaintCard(p, card);
	}
	if (HeaderInCard()) {
		// The logo and the title at the top of the card.
		const auto header = CardHeaderForHeight(outer.height(), cardBottom);
		auto top = card.y() + 2 * st::introLookLogoSkip;
		if (header.logo) {
			PaintLogo(
				p,
				QRect(
					(outer.width() - header.logo) / 2,
					top,
					header.logo,
					header.logo));
			top += header.logo + st::introLookLogoSkip;
		}
		if (header.title) {
			const auto font = IntroMontserratFont(st::introLookCardTitleFont);
			p.setFont(font);
			p.setPen(st::introTitleFg);
			p.drawText(
				QRect(card.x(), top, card.width(), font->height),
				Qt::AlignCenter,
				font->elided(settings.introTitle().trimmed(), card.width()));
		}
		PaintStickers(p, outer);
		return;
	}
	const auto titleTop = PaintTitle(
		p,
		card,
		settings.introTitle().trimmed());

	// The logo above the title, as large as asked if there is room.
	const auto bottom = ((titleTop >= 0) ? titleTop : card.y())
		- st::introLookLogoSkip;
	const auto available = bottom - st::introLookLogoSkip;
	const auto size = std::min(
		style::ConvertScale(Look().logoSize),
		available);
	if (size >= st::introLookLogoMin) {
		PaintLogo(
			p,
			QRect((outer.width() - size) / 2, bottom - size, size, size));
	}
	PaintStickers(p, outer);
}

rpl::producer<> IntroRepaints() {
	return rpl::merge(
		AyuSettings::getInstance().introLookChanges(),
		Changes().events(),
		IntroMediaRepaints());
}

const style::InputField &IntroField(
		const style::InputField &classic,
		const style::InputField &round) {
	const auto &base = Look().round ? round : classic;
	// Fields keep a reference to their style: a deque does not move its
	// elements when it grows, a vector would and leave them dangling.
	static auto cache = std::deque<
		std::pair<const style::InputField*, style::InputField>>();
	for (const auto &[key, styled] : cache) {
		if (key == &base) {
			return styled;
		}
	}
	cache.emplace_back(&base, WithMontserrat(base));
	return cache.back().second;
}

const style::RoundButton &IntroNextButton() {
	static const auto round = WithMontserrat(st::introRoundNextButton);
	static const auto classic = WithMontserrat(st::introNextButton);
	return Look().round ? round : classic;
}

const style::FlatLabel &IntroTitleStyle() {
	static const auto result = WithMontserrat(st::introTitle);
	return result;
}

const style::FlatLabel &IntroDescriptionStyle() {
	static const auto result = WithMontserrat(st::introDescription);
	return result;
}

const style::FlatLabel &IntroErrorStyle(bool centered) {
	static const auto left = WithMontserrat(st::introError);
	static const auto center = WithMontserrat(st::introErrorCentered);
	return centered ? center : left;
}

const style::FlatLabel &IntroPasswordHintStyle() {
	static const auto result = WithMontserrat(st::introPasswordHint);
	return result;
}

const style::FlatLabel &IntroQrTitleStyle() {
	static const auto result = WithMontserrat(st::introQrTitle);
	return result;
}

const style::FlatLabel &IntroQrStepStyle() {
	static const auto result = WithMontserrat(st::introQrStep);
	return result;
}

const style::FlatLabel &IntroQrStepNumberStyle() {
	static const auto result = WithMontserrat(st::defaultFlatLabel);
	return result;
}

const style::FlatLabel &IntroFooterStyle() {
	static const auto result = WithMontserrat(st::introFooter);
	return result;
}

const style::LinkButton &IntroLinkStyle() {
	static const auto result = [] {
		auto copy = st::introLink;
		copy.font = IntroMontserratFont(copy.font);
		copy.overFont = IntroMontserratFont(copy.overFont);
		return copy;
	}();
	return result;
}

std::optional<QColor> IntroNextButtonColor() {
	const auto hex = Look().buttonColor;
	if (hex.isEmpty()) {
		return std::nullopt;
	}
	const auto color = QColor(u"#"_q + hex);
	return color.isValid() ? std::make_optional(color) : std::nullopt;
}

void PaintIntroFields(QPainter &p, not_null<QWidget*> step) {
	if (!Look().round) {
		return;
	}
	const auto focused = QApplication::focusWidget();
	auto hq = PainterHighQualityEnabler(p);
	struct Plate {
		QRect rect;
		bool active = false;
	};
	auto plates = std::vector<Plate>();
	for (const auto child : step->children()) {
		const auto widget = qobject_cast<QWidget*>(child);
		if (!widget || widget->isHidden()) {
			continue;
		} else if (!dynamic_cast<Ui::InputField*>(widget)
			&& !dynamic_cast<Ui::MaskedInputField*>(widget)
			&& !dynamic_cast<CountryInput*>(widget)) {
			continue;
		}
		const auto active = focused
			&& (focused == widget || widget->isAncestorOf(focused));
		const auto rect = widget->geometry();
		// Fields side by side on one row (the code of the country and the
		// number) share one plate.
		const auto joined = ranges::find_if(plates, [&](const Plate &plate) {
			return (plate.rect.top() == rect.top())
				&& (plate.rect.height() == rect.height())
				&& (std::abs(plate.rect.right() - rect.left()) < 24
					|| std::abs(rect.right() - plate.rect.left()) < 24);
		});
		if (joined != plates.end()) {
			joined->rect = joined->rect.united(rect);
			joined->active = joined->active || active;
		} else {
			plates.push_back({ rect, active });
		}
	}
	for (const auto &plate : plates) {
		const auto active = plate.active;
		auto rect = QRectF(plate.rect);
		if (active) {
			const auto border = float64(st::introRoundFieldBorder);
			const auto half = border / 2.;
			rect = rect.marginsRemoved({ half, half, half, half });
			p.setPen(QPen(st::activeLineFg->c, border));
		} else {
			p.setPen(Qt::NoPen);
		}
		p.setBrush(st::windowBgOver);
		const auto radius = std::min(rect.height(), rect.width()) / 2.;
		p.drawRoundedRect(rect, radius, radius);
	}
}

void EnsureDefaultIntroLook() {
	static auto checked = false;
	if (checked) {
		return;
	}
	checked = true;
	if (QFile::exists(IntroFolder() + u"look.json"_q)) {
		return;
	}
	ApplyConstructor(DefaultLook(), nullptr);
}

void SetIntroStep(const QString &step) {
	if (CurrentStep() != step) {
		CurrentStep() = step;
		Changes().fire({});
	}
}

int IntroContentTop(int height, int cardBottom) {
	const auto &settings = AyuSettings::getInstance();
	const auto topMargin = style::ConvertScale(kCardTopMargin);
	const auto bottomMargin = style::ConvertScale(kCardBottomMargin);
	if (HeaderInCard()) {
		const auto header = CardHeaderForHeight(height, cardBottom);
		const auto top = header.height
			? header.height
			: st::introLookCardTop;
		const auto cardHeight = top + cardBottom;
		const auto maxTop = std::max(
			topMargin,
			height - bottomMargin - cardHeight);
		const auto cardTop = std::clamp(
			(height - cardHeight) / 2,
			topMargin,
			maxTop);
		return cardTop + top;
	}
	const auto title = settings.introTitle().trimmed();
	const auto titleHeight = title.isEmpty()
		? 0
		: (IntroMontserratFont(st::introLookTitleFont)->height
			+ st::introLookTitleSkip);
	const auto logoRoom = height
		- topMargin
		- bottomMargin
		- cardBottom
		- titleHeight
		- st::introLookLogoSkip;
	auto logo = std::min(WantedLogo(), logoRoom);
	if (logo < st::introLookLogoMin) {
		logo = 0;
	}
	const auto headerHeight = (logo ? (logo + st::introLookLogoSkip) : 0)
		+ titleHeight
		+ (settings.introCard() ? st::introLookCardTop : 0);
	const auto totalHeight = headerHeight + cardBottom;
	const auto maxTop = std::max(
		topMargin,
		height - bottomMargin - totalHeight);
	const auto blockTop = std::clamp(
		(height - totalHeight) / 2,
		topMargin,
		maxTop);
	return std::max(blockTop + headerHeight, st::introStepTopMin);
}

void IntroLookBox(not_null<Ui::GenericBox*> box) {
	EnsureDefaultIntroLook();
	auto &settings = AyuSettings::getInstance();
	const auto container = box->verticalLayout();

	box->setTitle(rpl::single(Tr("Login screen", "Экран входа")));
	box->setWidth(st::introLookBoxWidth);

	// The live preview.
	const auto preview = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::boxRowPadding + style::margins(0, st::boxLittleSkip, 0, 0));
	preview->resize(
		st::introLookBoxWidth - st::boxRowPadding.left() - st::boxRowPadding.right(),
		st::introLookPreviewHeight);
	preview->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(preview);
		PaintPreview(p, preview->size(), crl::now());
	}, preview->lifetime());
	rpl::merge(
		IntroRepaints(),
		style::PaletteChanged()
	) | rpl::on_next([=] {
		preview->update();
	}, preview->lifetime());
	const auto timer = preview->lifetime().make_state<base::Timer>([=] {
		if (IntroAnimated()) {
			preview->update();
		}
	});
	timer->callEach(33);

	// The background.
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("Background", "Фон")));
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		int(settings.introBackground()));
	const auto addRadio = [&](Background value, QString text) {
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				int(value),
				text,
				st::defaultBoxCheckbox),
			st::boxRowPadding + style::margins(0, 0, 0, st::boxLittleSkip));
	};
	addRadio(
		Background::Plain,
		Tr("Plain, like Telegram", "Обычный, как в Telegram"));
	addRadio(Background::Gradient, Tr("Gradient", "Градиент"));
	addRadio(
		Background::Glow,
		Tr("Glow that slowly moves", "Сияние, которое медленно движется"));
	group->setChangedCallback([](int value) {
		AyuSettings::getInstance().setIntroBackground(Background(value));
	});

	// The colors of the gradient and the glow.
	const auto addColor = [&](
			QString title,
			Fn<QString()> current,
			Fn<QColor()> fallback,
			Fn<void(QString)> save) {
		const auto button = container->add(object_ptr<Ui::SettingsButton>(
			container,
			rpl::single(title),
			st::settingsButtonNoIcon));
		const auto value = Ui::CreateChild<Ui::RpWidget>(button);
		value->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto swatch = st::introLookTitleSkip;
		const auto font = st::settingsButtonNoIcon.style.font;
		rpl::merge(
			button->sizeValue() | rpl::to_empty,
			settings.introLookChanges()
		) | rpl::on_next([=] {
			const auto size = button->size();
			const auto text = ColorText(current());
			const auto width = font->width(text) + swatch + swatch / 2;
			value->setGeometry(
				size.width() - st::settingsButtonNoIcon.padding.right() - width,
				0,
				width,
				size.height());
			value->update();
		}, value->lifetime());
		value->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(value);
			auto hq = PainterHighQualityEnabler(p);
			const auto top = (value->height() - swatch) / 2;
			p.setPen(QPen(st::windowBgOver->c, st::lineWidth));
			p.setBrush(Parse(current(), fallback()));
			p.drawEllipse(QRect(0, top, swatch, swatch));
			p.setFont(font);
			p.setPen(st::windowSubTextFg);
			p.drawText(
				QRect(swatch + swatch / 2, 0, value->width(), value->height()),
				Qt::AlignLeft | Qt::AlignVCenter,
				ColorText(current()));
		}, value->lifetime());
		button->setClickedCallback([=] {
			const auto start = Parse(current(), fallback());
			box->uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> inner) {
				const auto editor = inner->addRow(object_ptr<ColorEditor>(
					inner,
					ColorEditor::Mode::HSL,
					start));
				const auto done = [=] {
					save(editor->color().name(QColor::HexRgb).mid(1).toUpper());
					inner->closeBox();
				};
				editor->submitRequests(
				) | rpl::on_next(done, editor->lifetime());
				inner->setFocusCallback([=] {
					editor->setInnerFocus();
				});
				inner->addButton(tr::lng_settings_save(), done);
				inner->addButton(tr::lng_cancel(), [=] {
					inner->closeBox();
				});
				inner->setWidth(editor->width());
			}));
		});
	};
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("Colors", "Цвета")));
	addColor(
		Tr("Main color", "Основной цвет"),
		[] { return AyuSettings::getInstance().introColor1(); },
		AutoFirstColor,
		[](QString color) {
			AyuSettings::getInstance().setIntroColor1(color);
		});
	addColor(
		Tr("Second color", "Второй цвет"),
		[] { return AyuSettings::getInstance().introColor2(); },
		AutoSecondColor,
		[](QString color) {
			AyuSettings::getInstance().setIntroColor2(color);
		});
	container->add(object_ptr<Ui::SettingsButton>(
		container,
		rpl::single(Tr("Colors by theme", "Цвета по теме")),
		st::settingsButtonNoIcon
	))->setClickedCallback([] {
		auto &settings = AyuSettings::getInstance();
		settings.setIntroColor1(QString());
		settings.setIntroColor2(QString());
	});

	// The card and the title.
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("Form", "Форма")));
	const auto card = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			Tr(
				"Put the form on a card",
				"Форма на карточке с тенью"),
			settings.introCard(),
			st::defaultBoxCheckbox),
		st::boxRowPadding + style::margins(0, 0, 0, st::boxLittleSkip));
	card->checkedChanges() | rpl::on_next([](bool checked) {
		AyuSettings::getInstance().setIntroCard(checked);
	}, card->lifetime());

	const auto title = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(Tr("Title above the form", "Надпись над формой")),
		settings.introTitle()));
	title->setMaxLength(48);
	title->changes() | rpl::on_next([=] {
		AyuSettings::getInstance().setIntroTitle(
			title->getLastText().trimmed());
	}, title->lifetime());
	Ui::AddSkip(container);

	// Files of the user's own and the shape of the fields.
	const auto toast = [=](QString text) {
		box->uiShow()->showToast(text);
	};
	const auto addButton = [&](QString text, Fn<void()> callback) {
		container->add(object_ptr<Ui::SettingsButton>(
			container,
			rpl::single(text),
			st::settingsButtonNoIcon
		))->setClickedCallback(std::move(callback));
	};
	Ui::AddSubsectionTitle(
		container,
		rpl::single(Tr("Your own", "Своё")));
	addButton(Tr(
		"Video or picture behind the form",
		"Видео или картинка на фоне"), [=] {
		ChooseIntroMedia(box, toast);
	});
	addButton(Tr("Remove the video or picture", "Убрать видео или картинку"), [=] {
		RemoveIntroMedia();
	});
	addButton(Tr(
		"Logo: TGS, Lottie or a picture",
		"Логотип: TGS, Lottie или картинка"), [=] {
		ChooseLogo(box, toast);
	});
	addButton(Tr("Remove the logo", "Убрать логотип"), [=] {
		RemoveLogo();
	});
	addButton(Tr(
		"Load settings from the constructor (.json)",
		"Загрузить настройки из конструктора (.json)"), [=] {
		ImportConstructor(box, toast);
	});
	addButton(Tr(
		"The look of HorusGram",
		"Вид HorusGram по умолчанию"), [=] {
		if (ApplyConstructor(DefaultLook(), nullptr)) {
			toast(Tr("Done.", "Готово."));
		}
	});
	const auto round = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			Tr(
				"Round fields and button",
				"Круглые поля и кнопка"),
			Look().round,
			st::defaultBoxCheckbox),
		st::boxRowPadding + style::margins(0, st::boxLittleSkip, 0, st::boxLittleSkip));
	round->checkedChanges() | rpl::on_next([](bool checked) {
		if (Look().round != checked) {
			Look().round = checked;
			SaveExtra();
		}
	}, round->lifetime());
	Changes().events() | rpl::on_next([=] {
		if (round->checked() != Look().round) {
			round->setChecked(Look().round);
		}
	}, round->lifetime());
	Ui::AddSkip(container);

	box->addButton(tr::lng_box_ok(), [=] {
		box->closeBox();
	});
	box->addLeftButton(
		rpl::single(Tr("Reset", "Сбросить")),
		[=] {
			auto &settings = AyuSettings::getInstance();
			settings.resetIntroLook();
			group->setValue(int(settings.introBackground()));
			card->setChecked(settings.introCard());
			title->setText(settings.introTitle());
			RemoveLogo();
			RemoveIntroMedia();
			const auto folder = IntroFolder();
			for (const auto &old : QDir(folder).entryList(
					{ u"sticker_*"_q },
					QDir::Files)) {
				QFile::remove(folder + old);
			}
			StickerStates().clear();
			Look() = Extra();
			SaveExtra();
		});
}

} // namespace HoursGram::Customization
