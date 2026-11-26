/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QPainter;
class QWidget;

namespace Window {
class SessionController;
} // namespace Window

namespace HoursGram::Customization {

// A video the user picked plays behind the chats instead of the background
// of the theme: without sound, over and over, blurred and darkened so that
// the messages stay readable. The file is copied into tdata and decoded on
// this computer, a small picture at a time; nothing leaves it.

// Paints the current frame over `fill` and returns true, or returns false
// when there is no video and the usual background is to be painted.
bool PaintVideoBackground(QPainter &p, QSize fill, QRect clip);

// Fires when a new frame is there to be painted.
[[nodiscard]] rpl::producer<> VideoBackgroundRepaints();

[[nodiscard]] bool HasVideoBackground();
[[nodiscard]] rpl::producer<bool> HasVideoBackgroundValue();

// Asks for a video file and puts it behind the chats.
void ChooseVideoBackground(not_null<Window::SessionController*> controller);
void RemoveVideoBackground();

// Blur in pixels of the screen, 0 to 40, and darkening in percent, 0 to 85.
[[nodiscard]] int VideoBackgroundBlur();
[[nodiscard]] int VideoBackgroundDim();
void SetVideoBackgroundBlur(int value);
void SetVideoBackgroundDim(int value);

// The same for the login screen: a video or a picture of the user's own
// behind the form, kept in tdata/hoursgram_intro.
bool PaintIntroMedia(QPainter &p, QSize fill, QRect clip);
[[nodiscard]] rpl::producer<> IntroMediaRepaints();
[[nodiscard]] bool HasIntroMedia();
[[nodiscard]] rpl::producer<bool> HasIntroMediaValue();
void ChooseIntroMedia(not_null<QWidget*> parent, Fn<void(QString)> toast);
void RemoveIntroMedia();
// Puts the given file (a video or a picture) behind the login form.
bool SetIntroMedia(const QString &suffix, const QByteArray &bytes);
[[nodiscard]] int IntroMediaBlur();
[[nodiscard]] int IntroMediaDim();
void SetIntroMediaBlur(int value);
void SetIntroMediaDim(int value);

} // namespace HoursGram::Customization
