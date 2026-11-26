/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace Window {
class SessionController;
} // namespace Window

namespace HoursGram::Chats {

// Picks a video file and sends it to the chat as a round video message:
// the middle square of the picture, 400 by 400, at most the first minute,
// with its sound. The file is converted on this computer.
void SendRoundFromFile(
	not_null<Window::SessionController*> controller,
	not_null<History*> history);

} // namespace HoursGram::Chats
