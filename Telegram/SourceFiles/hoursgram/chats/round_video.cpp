/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/chats/round_video.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "core/file_utilities.h"
#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "history/history.h"
#include "hoursgram/music/music_common.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"

#include <QtCore/QFile>
#include <QtGui/QTransform>

namespace HoursGram::Chats {
namespace {

using namespace FFmpeg;
using HoursGram::Music::Tr;

constexpr auto kSide = 400;
constexpr auto kAudioFrequency = 48'000;
constexpr auto kAudioBitRate = 64 * 1024;
constexpr auto kVideoBitRate = 1536 * 1024;
constexpr auto kMaxDuration = 60 * crl::time(1000);

struct InputDeleter {
	void operator()(AVFormatContext *value) {
		if (value) {
			avformat_close_input(&value);
		}
	}
};
using InputPointer = std::unique_ptr<AVFormatContext, InputDeleter>;

struct Result {
	QByteArray content;
	crl::time duration = 0;
	QString error;
};

// Decodes the file and encodes it again as Telegram wants a round video:
// H.264 400x400 and mono AAC at 48 kHz in MP4. Runs off the main thread.
class Converter final {
public:
	explicit Converter(QString path) : _path(std::move(path)) {
	}

	[[nodiscard]] Result run();

private:
	[[nodiscard]] bool openInput();
	[[nodiscard]] bool openOutput();
	[[nodiscard]] bool initVideo();
	[[nodiscard]] bool initAudio();
	[[nodiscard]] bool decode(
		const CodecPointer &codec,
		const AVPacket *packet,
		bool video);
	[[nodiscard]] bool videoFrame(not_null<AVFrame*> frame);
	[[nodiscard]] bool audioFrame(not_null<AVFrame*> frame);
	[[nodiscard]] bool pushAudio(int samples);
	[[nodiscard]] bool write(
		AVFrame *frame,
		const CodecPointer &codec,
		AVStream *stream);
	[[nodiscard]] bool finish();
	[[nodiscard]] Result fail(const QString &what);

	const QString _path;

	InputPointer _input;
	int _videoIndex = -1;
	int _audioIndex = -1;
	int _rotation = 0;
	CodecPointer _videoDecoder;
	CodecPointer _audioDecoder;
	FramePointer _decoded;

	WriteBytesWrap _result;
	FormatPointer _output;
	AVStream *_videoStream = nullptr;
	CodecPointer _videoCodec;
	FramePointer _videoFrame;
	SwscalePointer _toImage;
	SwscalePointer _toYuv;
	crl::time _videoStart = -1;
	int64_t _lastVideoPts = -1;
	crl::time _videoDuration = 0;
	bool _videoDone = false;

	AVStream *_audioStream = nullptr;
	CodecPointer _audioCodec;
	FramePointer _audioFrame;
	SwresamplePointer _swr;
	std::vector<float> _audioTail;
	int64_t _audioPts = 0;
	bool _audioDone = false;

};

Result Converter::fail(const QString &what) {
	return { .error = what };
}

Result Converter::run() {
	if (!openInput()) {
		return fail(u"open"_q);
	} else if (!openOutput()) {
		return fail(u"encoder"_q);
	}
	_decoded = MakeFramePointer();
	auto packet = av_packet_alloc();
	const auto guard = gsl::finally([&] { av_packet_free(&packet); });
	while (!(_videoDone && (_audioDone || !_audioDecoder))) {
		const auto read = av_read_frame(_input.get(), packet);
		if (read < 0) {
			break;
		}
		auto ok = true;
		if (packet->stream_index == _videoIndex && !_videoDone) {
			ok = decode(_videoDecoder, packet, true);
		} else if (packet->stream_index == _audioIndex && !_audioDone) {
			ok = decode(_audioDecoder, packet, false);
		}
		av_packet_unref(packet);
		if (!ok) {
			return fail(u"decode"_q);
		}
	}
	if (!_videoDone && !decode(_videoDecoder, nullptr, true)) {
		return fail(u"decode"_q);
	}
	if (_audioDecoder && !_audioDone && !decode(_audioDecoder, nullptr, false)) {
		return fail(u"decode"_q);
	}
	if (_lastVideoPts < 0) {
		return fail(u"no frames"_q);
	} else if (!finish()) {
		return fail(u"finish"_q);
	}
	const auto audioDuration = _audioPts * crl::time(1000) / kAudioFrequency;
	return {
		.content = base::take(_result.content),
		.duration = std::max(_videoDuration, audioDuration),
	};
}

bool Converter::openInput() {
	auto raw = (AVFormatContext*)nullptr;
	const auto name = QFile::encodeName(_path);
	if (avformat_open_input(&raw, name.constData(), nullptr, nullptr) < 0) {
		return false;
	}
	_input = InputPointer(raw);
	if (avformat_find_stream_info(raw, nullptr) < 0) {
		return false;
	}
	_videoIndex = av_find_best_stream(
		raw,
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	if (_videoIndex < 0) {
		return false;
	}
	const auto videoStream = raw->streams[_videoIndex];
	_rotation = ReadRotationFromMetadata(videoStream);
	_videoDecoder = MakeCodecPointer({
		.parameters = videoStream->codecpar,
		.timeBase = videoStream->time_base,
	});
	if (!_videoDecoder) {
		return false;
	}
	_audioIndex = av_find_best_stream(
		raw,
		AVMEDIA_TYPE_AUDIO,
		-1,
		_videoIndex,
		nullptr,
		0);
	if (_audioIndex >= 0) {
		_audioDecoder = MakeCodecPointer({
			.parameters = raw->streams[_audioIndex]->codecpar,
			.timeBase = raw->streams[_audioIndex]->time_base,
		});
		if (!_audioDecoder) {
			_audioIndex = -1;
		}
	}
	return true;
}

bool Converter::openOutput() {
	_output = MakeWriteFormatPointer(
		static_cast<void*>(&_result),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"mp4"_q);
	if (!_output || !initVideo() || (_audioDecoder && !initAudio())) {
		return false;
	}
	return !AvErrorWrap(avformat_write_header(_output.get(), nullptr));
}

bool Converter::initVideo() {
	auto codec = avcodec_find_encoder_by_name("libopenh264");
	if (!codec) {
		codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	}
	if (!codec) {
		LogError(u"avcodec_find_encoder"_q, u"H264"_q);
		return false;
	}
	_videoStream = avformat_new_stream(_output.get(), codec);
	_videoCodec = CodecPointer(avcodec_alloc_context3(codec));
	if (!_videoStream || !_videoCodec) {
		return false;
	}
	_videoCodec->codec_id = codec->id;
	_videoCodec->codec_type = AVMEDIA_TYPE_VIDEO;
	_videoCodec->width = kSide;
	_videoCodec->height = kSide;
	_videoCodec->time_base = AVRational{ 1, 1'000'000 };
	_videoCodec->framerate = AVRational{ 0, 1 };
	_videoCodec->pix_fmt = AV_PIX_FMT_YUV420P;
	_videoCodec->bit_rate = kVideoBitRate;
	if (AvErrorWrap(avcodec_open2(_videoCodec.get(), codec, nullptr))) {
		return false;
	} else if (AvErrorWrap(avcodec_parameters_from_context(
			_videoStream->codecpar,
			_videoCodec.get()))) {
		return false;
	}
	_videoFrame = MakeFramePointer();
	if (!_videoFrame) {
		return false;
	}
	_videoFrame->format = _videoCodec->pix_fmt;
	_videoFrame->width = kSide;
	_videoFrame->height = kSide;
	return !AvErrorWrap(av_frame_get_buffer(_videoFrame.get(), 0));
}

bool Converter::initAudio() {
	const auto codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!codec) {
		LogError(u"avcodec_find_encoder"_q, u"AAC"_q);
		return false;
	}
	_audioStream = avformat_new_stream(_output.get(), codec);
	_audioCodec = CodecPointer(avcodec_alloc_context3(codec));
	if (!_audioStream || !_audioCodec) {
		return false;
	}
	_audioCodec->sample_fmt = AV_SAMPLE_FMT_FLTP;
	_audioCodec->bit_rate = kAudioBitRate;
	_audioCodec->sample_rate = kAudioFrequency;
	_audioCodec->ch_layout = AV_CHANNEL_LAYOUT_MONO;
	if (AvErrorWrap(avcodec_open2(_audioCodec.get(), codec, nullptr))) {
		return false;
	} else if (AvErrorWrap(avcodec_parameters_from_context(
			_audioStream->codecpar,
			_audioCodec.get()))) {
		return false;
	}
	_audioFrame = MakeFramePointer();
	if (!_audioFrame) {
		return false;
	}
	_audioFrame->nb_samples = _audioCodec->frame_size;
	_audioFrame->format = _audioCodec->sample_fmt;
	_audioFrame->sample_rate = _audioCodec->sample_rate;
	av_channel_layout_copy(&_audioFrame->ch_layout, &_audioCodec->ch_layout);
	return !AvErrorWrap(av_frame_get_buffer(_audioFrame.get(), 0));
}

bool Converter::decode(
		const CodecPointer &codec,
		const AVPacket *packet,
		bool video) {
	auto error = AvErrorWrap(avcodec_send_packet(codec.get(), packet));
	if (error && error.code() != AVERROR(EAGAIN) && error.code() != AVERROR_EOF) {
		// A broken packet is skipped, as players do.
		return true;
	}
	while (true) {
		error = AvErrorWrap(avcodec_receive_frame(codec.get(), _decoded.get()));
		if (error.code() == AVERROR(EAGAIN) || error.code() == AVERROR_EOF) {
			return true;
		} else if (error) {
			return false;
		}
		const auto ok = video
			? videoFrame(_decoded.get())
			: audioFrame(_decoded.get());
		av_frame_unref(_decoded.get());
		if (!ok) {
			return false;
		}
	}
}

bool Converter::videoFrame(not_null<AVFrame*> frame) {
	if (_videoDone) {
		return true;
	}
	const auto stream = _input->streams[_videoIndex];
	const auto pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
		? frame->best_effort_timestamp
		: frame->pts;
	const auto time = (pts != AV_NOPTS_VALUE)
		? PtsToTime(pts, stream->time_base)
		: (_videoStart < 0 ? 0 : (_videoStart + _videoDuration + 33));
	if (_videoStart < 0) {
		_videoStart = time;
	}
	const auto offset = time - _videoStart;
	if (offset >= kMaxDuration) {
		_videoDone = true;
		return true;
	}

	// First as a picture, turned the way the video is meant to be seen.
	const auto size = QSize(frame->width, frame->height);
	_toImage = MakeSwscalePointer(frame, size, &_toImage);
	if (!_toImage) {
		return false;
	}
	auto image = QImage(size, QImage::Format_ARGB32_Premultiplied);
	uint8_t *dst[4] = { image.bits(), nullptr, nullptr, nullptr };
	int dstStride[4] = { int(image.bytesPerLine()), 0, 0, 0 };
	sws_scale(
		_toImage.get(),
		frame->data,
		frame->linesize,
		0,
		frame->height,
		dst,
		dstStride);
	if (_rotation) {
		image = image.transformed(QTransform().rotate(_rotation));
	}

	// Then its middle square, scaled to the round video.
	const auto width = image.width();
	const auto height = image.height();
	const auto side = std::min(width, height);
	const auto x = (width - side) / 2;
	const auto y = (height - side) / 2;
	_toYuv = MakeSwscalePointer(
		QSize(side, side),
		AV_PIX_FMT_BGRA,
		QSize(kSide, kSide),
		AV_PIX_FMT_YUV420P,
		&_toYuv);
	if (!_toYuv
		|| AvErrorWrap(av_frame_make_writable(_videoFrame.get()))) {
		return false;
	}
	const uint8_t *src[1] = {
		image.constBits() + image.bytesPerLine() * y + x * 4,
	};
	int srcStride[1] = { int(image.bytesPerLine()) };
	sws_scale(
		_toYuv.get(),
		src,
		srcStride,
		0,
		side,
		_videoFrame->data,
		_videoFrame->linesize);

	auto encodedPts = int64_t(offset) * 1000;
	if (encodedPts <= _lastVideoPts) {
		encodedPts = _lastVideoPts + 1;
	}
	_lastVideoPts = encodedPts;
	_videoFrame->pts = encodedPts;
	_videoDuration = std::max(_videoDuration, offset);
	return write(_videoFrame.get(), _videoCodec, _videoStream);
}

bool Converter::audioFrame(not_null<AVFrame*> frame) {
	if (_audioDone || !_audioCodec) {
		return true;
	}
	_swr = MakeSwresamplePointer(
		&frame->ch_layout,
		AVSampleFormat(frame->format),
		frame->sample_rate,
		&_audioCodec->ch_layout,
		AV_SAMPLE_FMT_FLTP,
		kAudioFrequency,
		&_swr);
	if (!_swr) {
		return false;
	}
	const auto capacity = int(av_rescale_rnd(
		swr_get_delay(_swr.get(), frame->sample_rate) + frame->nb_samples,
		kAudioFrequency,
		frame->sample_rate,
		AV_ROUND_UP));
	const auto was = _audioTail.size();
	_audioTail.resize(was + capacity);
	uint8_t *out[1] = {
		reinterpret_cast<uint8_t*>(_audioTail.data() + was),
	};
	const auto converted = swr_convert(
		_swr.get(),
		out,
		capacity,
		const_cast<const uint8_t**>(frame->extended_data),
		frame->nb_samples);
	if (converted < 0) {
		return false;
	}
	_audioTail.resize(was + converted);
	return pushAudio(_audioCodec->frame_size);
}

bool Converter::pushAudio(int samples) {
	const auto limit = kMaxDuration * kAudioFrequency / 1000;
	while (!_audioDone && samples > 0 && int(_audioTail.size()) >= samples) {
		if (AvErrorWrap(av_frame_make_writable(_audioFrame.get()))) {
			return false;
		}
		memcpy(_audioFrame->data[0], _audioTail.data(), samples * sizeof(float));
		_audioFrame->nb_samples = samples;
		_audioFrame->pts = _audioPts;
		_audioPts += samples;
		_audioTail.erase(_audioTail.begin(), _audioTail.begin() + samples);
		if (!write(_audioFrame.get(), _audioCodec, _audioStream)) {
			return false;
		}
		if (_audioPts >= limit) {
			_audioDone = true;
		}
	}
	return true;
}

bool Converter::write(
		AVFrame *frame,
		const CodecPointer &codec,
		AVStream *stream) {
	auto error = AvErrorWrap(avcodec_send_frame(codec.get(), frame));
	if (error) {
		LogError(u"avcodec_send_frame"_q, error);
		return false;
	}
	auto packet = av_packet_alloc();
	const auto guard = gsl::finally([&] { av_packet_free(&packet); });
	while (true) {
		error = AvErrorWrap(avcodec_receive_packet(codec.get(), packet));
		if (error.code() == AVERROR(EAGAIN) || error.code() == AVERROR_EOF) {
			return true;
		} else if (error) {
			LogError(u"avcodec_receive_packet"_q, error);
			return false;
		}
		packet->stream_index = stream->index;
		av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
		error = AvErrorWrap(av_interleaved_write_frame(_output.get(), packet));
		if (error) {
			LogError(u"av_interleaved_write_frame"_q, error);
			return false;
		}
	}
}

bool Converter::finish() {
	if (_audioCodec && !_audioDone && !_audioTail.empty()) {
		// The last piece may be shorter than a frame.
		if (!pushAudio(int(_audioTail.size()))) {
			return false;
		}
	}
	if (!write(nullptr, _videoCodec, _videoStream)) {
		return false;
	} else if (_audioCodec && !write(nullptr, _audioCodec, _audioStream)) {
		return false;
	}
	return !AvErrorWrap(av_write_trailer(_output.get()));
}

} // namespace

void SendRoundFromFile(
		not_null<Window::SessionController*> controller,
		not_null<History*> history) {
	const auto weak = base::make_weak(controller);
	const auto filter = Tr("Video files", "Видео")
		+ u" (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.3gp);;"_q
		+ FileDialog::AllFilesFilter();
	FileDialog::GetOpenPath(
		controller->widget().get(),
		Tr("Video for a round message", "Видео для кружка"),
		filter,
		crl::guard(controller, [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			const auto path = result.paths.front();
			controller->showToast(Tr(
				"Making a round video...",
				"Делаю кружок..."));
			crl::async([=] {
				auto converted = Converter(path).run();
				crl::on_main([=, converted = std::move(converted)] {
					const auto strong = weak.get();
					if (!strong) {
						return;
					} else if (!converted.error.isEmpty()
						|| converted.content.isEmpty()) {
						strong->showToast(Tr(
							"Could not make a round video of this file (%1).",
							"Не удалось сделать кружок из этого файла (%1).")
								.arg(converted.error.isEmpty()
									? u"empty"_q
									: converted.error));
						return;
					}
					history->session().api().sendVoiceMessage(
						converted.content,
						VoiceWaveform(),
						converted.duration,
						true,
						nullptr,
						Api::SendAction(history));
				});
			});
		}));
}

} // namespace HoursGram::Chats
