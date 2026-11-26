/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class PeerData;

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace HoursGram::Chats {

// Chats taken out of the chat list. Some only until the client starts
// again; others are kept in a folder behind a password, which is stored
// on this computer as a salted PBKDF2 hash. Only the list hides them:
// their messages, notifications and the search of Telegram are as before.
[[nodiscard]] bool IsHidden(not_null<const History*> history);

// "Hide until restart" and "Protected folder" in the menu of a chat in the
// chat list.
void AddHideActions(
	not_null<PeerData*> peer,
	not_null<Window::SessionController*> controller,
	const Ui::Menu::MenuCallback &addAction);

// The chats hidden until restart, and the protected folder after its
// password.
void ShowHiddenChats(not_null<Window::SessionController*> controller);

// "/mute", "/mute 2h" and "/unmute" sent in a private chat change its
// notifications instead of being sent. True when the text was a command.
[[nodiscard]] bool HandleChatCommand(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const QString &text);

} // namespace HoursGram::Chats
