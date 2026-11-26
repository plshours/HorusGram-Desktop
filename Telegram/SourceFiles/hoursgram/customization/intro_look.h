/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace style {
struct FlatLabel;
struct InputField;
struct LinkButton;
struct RoundButton;
} // namespace style

namespace HoursGram::Customization {

// The look of the login screen, as set in Customization: the background
// (plain like Telegram's, a gradient, or a slow glow), a card the form sits
// on, and a title above the card.

// Whether anything is drawn besides the plain Telegram background.
[[nodiscard]] bool IntroDecorated();

// The glow moves, so whoever paints it has to repaint now and then.
[[nodiscard]] bool IntroAnimated();

// The card around a form whose content starts at `contentTop` and ends at
// `contentTop + cardBottom`: the card adapts cleanly to each step's height
// and stays vertically centered in the window.
[[nodiscard]] QRect IntroCardRect(
	QSize outer,
	int contentTop,
	int cardBottom);

// Paints the background, the card and the title for a screen of `outer`
// size. `now` moves the glow.
void PaintIntro(
	QPainter &p,
	QSize outer,
	int contentTop,
	int cardBottom,
	crl::time now);

// Everything the login screen look has, with a live preview: the box goes
// to Customization and to the settings of the login screen.
void IntroLookBox(not_null<Ui::GenericBox*> box);

// What the user added in HorusGram over the look above: a video or a
// picture behind the form, a logo of their own above it, the card's
// rounding and transparency, round fields and a round button of a color
// of their choice. Kept in tdata/hoursgram_intro.

// Fires whenever the screen has to be painted again: a setting changed,
// a new frame of the video or of the logo is there.
[[nodiscard]] rpl::producer<> IntroRepaints();

// The style of a field of the login screen: the usual one or the round,
// rendered in the HorusGram login font (Montserrat).
[[nodiscard]] const style::InputField &IntroField(
	const style::InputField &classic,
	const style::InputField &round);

// The style of the main button and the color the user picked for it.
[[nodiscard]] const style::RoundButton &IntroNextButton();
[[nodiscard]] std::optional<QColor> IntroNextButtonColor();

// Login screen text and link styles rendered in Montserrat.
[[nodiscard]] const style::FlatLabel &IntroTitleStyle();
[[nodiscard]] const style::FlatLabel &IntroDescriptionStyle();
[[nodiscard]] const style::FlatLabel &IntroErrorStyle(bool centered);
[[nodiscard]] const style::FlatLabel &IntroPasswordHintStyle();
[[nodiscard]] const style::FlatLabel &IntroQrTitleStyle();
[[nodiscard]] const style::FlatLabel &IntroQrStepStyle();
[[nodiscard]] const style::FlatLabel &IntroQrStepNumberStyle();
[[nodiscard]] const style::FlatLabel &IntroFooterStyle();
[[nodiscard]] const style::LinkButton &IntroLinkStyle();

// Round fields paint nothing under themselves: the step paints a plate
// under each of its fields.
void PaintIntroFields(QPainter &p, not_null<QWidget*> step);

// Vertical `contentTop` that centers the whole login card (header + form
// of `cardBottom` height) in a window of `height`.
[[nodiscard]] int IntroContentTop(int height, int cardBottom);

// The step of the phone number goes without its own title when the card
// shows the title of the user's choice.
[[nodiscard]] bool IntroHidesPhoneTitle();

// The step shown now: "phone", "code", "password", "signup", "qr" or
// "email". Each may have a logo of its own.
void SetIntroStep(const QString &step);

// The first time, the look of the login screen is the one HorusGram comes
// with (Resources/hoursgram-intro-look.json, a file of the constructor).
void EnsureDefaultIntroLook();

} // namespace HoursGram::Customization
