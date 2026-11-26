/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/customization/video_background.h"

#include "base/timer.h"
#include "core/file_utilities.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "hoursgram/music/music_common.h"
#include "mainwindow.h"
#include "ui/image/image_prepare.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtGui/QTransform>

namespace HoursGram::Customization {
namespace {

using namespace FFmpeg;
using HoursGram::Music::Tr;

// Frames are decoded this wide: the picture is blurred anyway, and a small
// one is cheap to decode, to blur and to scale up.
constexpr auto kFrameWidth = 480;
constexpr auto kMinFrameDuration = crl::time(33);
constexpr auto kMaxFrameDuration = crl::time(66);
// Nothing is decoded while no chat paints the background.
constexpr auto kIdleAfter = crl::time(1500);
constexpr auto kDefaultBlur = 14;
constexpr auto kDefaultDim = 45;

struct InputDeleter {
	void operator()(AVFormatContext *value) {
		if (value) {
			avformat_close_input(&value);
		}
	}
};
using InputPointer = std::unique_ptr<AVFormatContext, InputDeleter>;

// Reads the video over and over, one small blurred frame at a time. Only
// one frame is asked for at a time, so it is used by one thread at a time.
class Decoder final {
public:
	explicit Decoder(QString path) : _path(std::move(path)) {
	}

	[[nodiscard]] bool open();
	[[nodiscard]] QImage next(int blur);
	[[nodiscard]] crl::time frameDuration() const {
		return _frameDuration;
	}
	// A picture: decoded once, then only drawn.
	[[nodiscard]] bool still() const {
		return _still;
	}

private:
	[[nodiscard]] bool readFrame();
	[[nodiscard]] QImage convert(int blur);

	const QString _path;
	InputPointer _input;
	int _index = -1;
	CodecPointer _codec;
	FramePointer _frame;
	SwscalePointer _scale;
	crl::time _frameDuration = 40;
	int _rotation = 0;
	int _emptyLoops = 0;
	bool _still = false;

};

bool Decoder::open() {
	auto raw = (AVFormatContext*)nullptr;
	const auto name = QFile::encodeName(_path);
	if (avformat_open_input(&raw, name.constData(), nullptr, nullptr) < 0) {
		return false;
	}
	_input = InputPointer(raw);
	if (avformat_find_stream_info(raw, nullptr) < 0) {
		return false;
	}
	_index = av_find_best_stream(raw, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (_index < 0) {
		return false;
	}
	const auto stream = raw->streams[_index];
	_codec = MakeCodecPointer({ .stream = stream });
	if (!_codec) {
		return false;
	}
	_rotation = ReadRotationFromMetadata(stream);
	const auto format = QString::fromLatin1(raw->iformat->name);
	_still = format.contains(u"image2"_q)
		|| format.endsWith(u"_pipe"_q)
		|| (stream->nb_frames == 1);
	const auto rate = stream->avg_frame_rate;
	if (rate.num > 0 && rate.den > 0) {
		_frameDuration = std::clamp(
			crl::time(1000) * rate.den / rate.num,
			kMinFrameDuration,
			kMaxFrameDuration);
	}
	_frame = MakeFramePointer();
	return _frame != nullptr;
}

bool Decoder::readFrame() {
	auto packet = av_packet_alloc();
	const auto guard = gsl::finally([&] { av_packet_free(&packet); });
	while (true) {
		const auto received = avcodec_receive_frame(_codec.get(), _frame.get());
		if (received >= 0) {
			_emptyLoops = 0;
			return true;
		} else if (received == AVERROR_EOF) {
			// The end: from the start again.
			if (++_emptyLoops > 2) {
				return false;
			}
			avcodec_flush_buffers(_codec.get());
			av_seek_frame(_input.get(), _index, 0, AVSEEK_FLAG_BACKWARD);
			continue;
		} else if (received != AVERROR(EAGAIN)) {
			return false;
		}
		const auto read = av_read_frame(_input.get(), packet);
		if (read < 0) {
			avcodec_send_packet(_codec.get(), nullptr);
			continue;
		}
		if (packet->stream_index == _index) {
			avcodec_send_packet(_codec.get(), packet);
		}
		av_packet_unref(packet);
	}
}

QImage Decoder::convert(int blur) {
	const auto width = _frame->width;
	const auto height = _frame->height;
	if (width <= 0 || height <= 0) {
		return QImage();
	}
	const auto target = QSize(
		kFrameWidth,
		std::max(1, int(int64(kFrameWidth) * height / width)));
	_scale = MakeSwscalePointer(_frame.get(), target, &_scale);
	if (!_scale) {
		return QImage();
	}
	auto image = QImage(target, QImage::Format_ARGB32_Premultiplied);
	uint8_t *dst[4] = { image.bits(), nullptr, nullptr, nullptr };
	int dstStride[4] = { int(image.bytesPerLine()), 0, 0, 0 };
	sws_scale(
		_scale.get(),
		_frame->data,
		_frame->linesize,
		0,
		height,
		dst,
		dstStride);
	if (_rotation) {
		image = image.transformed(QTransform().rotate(_rotation));
	}
	// The blur is given in pixels of the screen; this picture is a few
	// times smaller than the window it covers.
	const auto radius = std::clamp(blur / 3, 0, 20);
	return radius
		? Images::BlurLargeImage(std::move(image), radius)
		: image;
}

QImage Decoder::next(int blur) {
	if (!readFrame()) {
		return QImage();
	}
	auto result = convert(blur);
	av_frame_unref(_frame.get());
	return result;
}

class Player final {
public:
	// The one behind the chats.
	static Player &Instance() {
		static auto result = Player(
			u"hoursgram_background/"_q,
			kDefaultBlur,
			kDefaultDim);
		return result;
	}
	// The one behind the login screen.
	static Player &Intro() {
		static auto result = Player(u"hoursgram_intro/"_q, 10, 0);
		return result;
	}

	[[nodiscard]] bool paint(QPainter &p, QSize fill, QRect clip);
	[[nodiscard]] rpl::producer<> repaints() const {
		return _repaints.events();
	}
	[[nodiscard]] rpl::producer<bool> hasValue() const {
		return _has.value();
	}
	[[nodiscard]] bool has() const {
		return _has.current();
	}

	void choose(not_null<QWidget*> parent, Fn<void(QString)> toast);
	// Puts a file in place: `write` writes it to the path given.
	[[nodiscard]] bool install(
		QString suffix,
		Fn<bool(const QString &path)> write);
	void remove();

	[[nodiscard]] int blur() const {
		return _blur;
	}
	[[nodiscard]] int dim() const {
		return _dim;
	}
	void setBlur(int value);
	void setDim(int value);

private:
	Player(QString folder, int blur, int dim);

	void load();
	void save() const;
	void start();
	void stop();
	void tick();
	[[nodiscard]] QString folder() const;

	const QString _folder;
	QString _video;
	int _blur = 0;
	int _dim = 0;
	rpl::variable<bool> _has = false;

	std::shared_ptr<Decoder> _decoder;
	QImage _frame;
	base::Timer _timer;
	uint64 _generation = 0;
	bool _decoding = false;
	crl::time _lastPaint = 0;
	rpl::event_stream<> _repaints;

};

Player::Player(QString folder, int blur, int dim)
: _folder(std::move(folder))
, _blur(blur)
, _dim(dim)
, _timer([=] { tick(); }) {
	load();
	start();
}

QString Player::folder() const {
	return cWorkingDir() + u"tdata/"_q + _folder;
}

void Player::load() {
	auto file = QFile(folder() + u"settings.json"_q);
	if (file.open(QIODevice::ReadOnly)) {
		const auto root = QJsonDocument::fromJson(file.readAll()).object();
		_video = root.value(u"video"_q).toString();
		_blur = std::clamp(root.value(u"blur"_q).toInt(_blur), 0, 40);
		_dim = std::clamp(root.value(u"dim"_q).toInt(_dim), 0, 85);
	}
	// Only a file of this folder, whatever the settings say.
	_video = QFileInfo(_video).fileName();
	if (!_video.isEmpty() && !QFile::exists(folder() + _video)) {
		_video = QString();
	}
	_has = !_video.isEmpty();
}

void Player::save() const {
	QDir().mkpath(folder());
	auto root = QJsonObject();
	root.insert(u"video"_q, _video);
	root.insert(u"blur"_q, _blur);
	root.insert(u"dim"_q, _dim);
	auto file = QFile(folder() + u"settings.json"_q);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	}
}

void Player::start() {
	stop();
	if (_video.isEmpty()) {
		return;
	}
	auto decoder = std::make_shared<Decoder>(folder() + _video);
	if (!decoder->open()) {
		return;
	}
	_decoder = std::move(decoder);
	if (!_decoder->still()) {
		_timer.callEach(_decoder->frameDuration());
	}
	tick();
}

void Player::stop() {
	++_generation;
	_timer.cancel();
	_decoder = nullptr;
	_decoding = false;
	_frame = QImage();
}

void Player::tick() {
	if (!_decoder || _decoding) {
		return;
	}
	// Not while the client is in the background or no chat is shown, but
	// always the first frame, so there is something to paint at once.
	const auto idle = !_frame.isNull()
		&& ((QGuiApplication::applicationState() != Qt::ApplicationActive)
			|| (crl::now() - _lastPaint > kIdleAfter));
	if (idle) {
		return;
	}
	if (_decoder->still() && !_frame.isNull()) {
		return;
	}
	_decoding = true;
	const auto decoder = _decoder;
	const auto generation = _generation;
	const auto blur = _blur;
	// The players live as long as the application, so `this` stays.
	const auto that = this;
	crl::async([=] {
		auto frame = decoder->next(blur);
		crl::on_main([=, frame = std::move(frame)] {
			auto &player = *that;
			if (player._generation != generation) {
				return;
			}
			player._decoding = false;
			if (frame.isNull()) {
				if (player._frame.isNull()) {
					player.stop();
				}
				return;
			}
			player._frame = frame;
			player._repaints.fire({});
		});
	});
}

bool Player::paint(QPainter &p, QSize fill, QRect clip) {
	if (!_decoder && _frame.isNull()) {
		return false;
	}
	_lastPaint = crl::now();
	if (_frame.isNull() || fill.isEmpty()) {
		p.fillRect(clip, QColor(0, 0, 0));
		return true;
	}
	// Cover the whole area, cutting what is over on two sides.
	const auto size = _frame.size().scaled(fill, Qt::KeepAspectRatioByExpanding);
	const auto target = QRect(
		QPoint((fill.width() - size.width()) / 2, (fill.height() - size.height()) / 2),
		size);
	p.save();
	p.setClipRect(clip);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(target, _frame);
	if (_dim > 0) {
		p.fillRect(clip, QColor(0, 0, 0, _dim * 255 / 100));
	}
	p.restore();
	return true;
}

void Player::choose(not_null<QWidget*> parent, Fn<void(QString)> toast) {
	const auto filter = Tr("Videos and pictures", "Видео и картинки")
		+ u" (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.gif"
			" *.jpg *.jpeg *.png *.webp *.bmp);;"_q
		+ FileDialog::AllFilesFilter();
	FileDialog::GetOpenPath(
		parent.get(),
		Tr("Video or picture for the background", "Видео или картинка для фона"),
		filter,
		crl::guard(parent.get(), [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			const auto source = result.paths.front();
			const auto done = install(
				QFileInfo(source).suffix(),
				[=](const QString &path) {
					return QFile::copy(source, path);
				});
			if (!done && toast) {
				toast(Tr(
					"This file could not be opened.",
					"Этот файл не получилось открыть."));
			}
		}));
}

bool Player::install(QString suffix, Fn<bool(const QString &path)> write) {
	suffix = suffix.toLower();
	static const auto allowed = QStringList{
		u"mp4"_q, u"mov"_q, u"mkv"_q, u"webm"_q, u"avi"_q, u"m4v"_q,
		u"gif"_q, u"jpg"_q, u"jpeg"_q, u"png"_q, u"webp"_q, u"bmp"_q,
	};
	if (!allowed.contains(suffix)) {
		suffix = u"mp4"_q;
	}
	const auto name = u"video."_q + suffix;
	const auto folder = this->folder();
	QDir().mkpath(folder);
	stop();
	// The old file goes first, whatever its extension was.
	for (const auto &old : QDir(folder).entryList(
			{ u"video.*"_q },
			QDir::Files)) {
		QFile::remove(folder + old);
	}
	if (!write(folder + name)) {
		_video = QString();
		_has = false;
		save();
		_repaints.fire({});
		return false;
	}
	_video = name;
	_has = true;
	save();
	start();
	if (!_decoder) {
		remove();
		return false;
	}
	_repaints.fire({});
	return true;
}

void Player::remove() {
	stop();
	if (!_video.isEmpty()) {
		QFile::remove(folder() + _video);
	}
	_video = QString();
	_has = false;
	save();
	_repaints.fire({});
}

void Player::setBlur(int value) {
	_blur = std::clamp(value, 0, 40);
	save();
	// A picture is blurred once, when it is read.
	if (_decoder && _decoder->still()) {
		start();
	}
}

void Player::setDim(int value) {
	_dim = std::clamp(value, 0, 85);
	save();
	_repaints.fire({});
}

} // namespace

bool PaintVideoBackground(QPainter &p, QSize fill, QRect clip) {
	return Player::Instance().paint(p, fill, clip);
}

rpl::producer<> VideoBackgroundRepaints() {
	return Player::Instance().repaints();
}

bool HasVideoBackground() {
	return Player::Instance().has();
}

rpl::producer<bool> HasVideoBackgroundValue() {
	return Player::Instance().hasValue();
}

void ChooseVideoBackground(not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller);
	Player::Instance().choose(controller->widget().get(), [=](QString text) {
		if (const auto strong = weak.get()) {
			strong->showToast(text);
		}
	});
}

void RemoveVideoBackground() {
	Player::Instance().remove();
}

int VideoBackgroundBlur() {
	return Player::Instance().blur();
}

int VideoBackgroundDim() {
	return Player::Instance().dim();
}

void SetVideoBackgroundBlur(int value) {
	Player::Instance().setBlur(value);
}

void SetVideoBackgroundDim(int value) {
	Player::Instance().setDim(value);
}

bool PaintIntroMedia(QPainter &p, QSize fill, QRect clip) {
	return Player::Intro().paint(p, fill, clip);
}

rpl::producer<> IntroMediaRepaints() {
	return Player::Intro().repaints();
}

bool HasIntroMedia() {
	return Player::Intro().has();
}

rpl::producer<bool> HasIntroMediaValue() {
	return Player::Intro().hasValue();
}

void ChooseIntroMedia(not_null<QWidget*> parent, Fn<void(QString)> toast) {
	Player::Intro().choose(parent, std::move(toast));
}

void RemoveIntroMedia() {
	Player::Intro().remove();
}

bool SetIntroMedia(const QString &suffix, const QByteArray &bytes) {
	return Player::Intro().install(suffix, [&](const QString &path) {
		auto file = QFile(path);
		return file.open(QIODevice::WriteOnly)
			&& (file.write(bytes) == bytes.size());
	});
}

int IntroMediaBlur() {
	return Player::Intro().blur();
}

int IntroMediaDim() {
	return Player::Intro().dim();
}

void SetIntroMediaBlur(int value) {
	Player::Intro().setBlur(value);
}

void SetIntroMediaDim(int value) {
	Player::Intro().setDim(value);
}

} // namespace HoursGram::Customization
