/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/chats/hidden_chats.h"

#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/openssl_help.h"
#include "base/random.h"
#include "base/unique_qptr.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/notify/data_notify_settings.h"
#include "data/notify/data_peer_notify_settings.h"
#include "history/history.h"
#include "hoursgram/music/music_common.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace HoursGram::Chats {
namespace {

using HoursGram::Music::Tr;

constexpr auto kIterations = 200000;
constexpr auto kSaltSize = 32;
constexpr auto kMinPassword = 4;

using Peers = base::flat_set<uint64>;

struct State {
	bool loaded = false;
	QByteArray salt;
	QByteArray hash;
	// By the user id of the account.
	base::flat_map<uint64, Peers> locked;
	base::flat_map<uint64, Peers> temporary;
};

[[nodiscard]] State &Get() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString Path() {
	return cWorkingDir() + u"tdata/hoursgram_hidden.json"_q;
}

void Load() {
	auto &state = Get();
	if (state.loaded) {
		return;
	}
	state.loaded = true;
	auto file = QFile(Path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	state.salt = QByteArray::fromHex(root.value(u"salt"_q).toString().toLatin1());
	state.hash = QByteArray::fromHex(root.value(u"hash"_q).toString().toLatin1());
	const auto chats = root.value(u"chats"_q).toObject();
	for (auto i = chats.begin(); i != chats.end(); ++i) {
		auto &peers = state.locked[i.key().toULongLong()];
		for (const auto &value : i.value().toArray()) {
			peers.emplace(value.toString().toULongLong());
		}
	}
}

void Save() {
	const auto &state = Get();
	auto chats = QJsonObject();
	for (const auto &[account, peers] : state.locked) {
		auto list = QJsonArray();
		for (const auto peer : peers) {
			list.append(QString::number(peer));
		}
		if (!list.isEmpty()) {
			chats.insert(QString::number(account), list);
		}
	}
	auto root = QJsonObject();
	root.insert(u"salt"_q, QString::fromLatin1(state.salt.toHex()));
	root.insert(u"hash"_q, QString::fromLatin1(state.hash.toHex()));
	root.insert(u"chats"_q, chats);
	auto file = QFile(Path());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	}
}

[[nodiscard]] uint64 AccountOf(not_null<const PeerData*> peer) {
	return peer->session().userId().bare;
}

[[nodiscard]] QByteArray HashOf(const QString &password, const QByteArray &salt) {
	const auto utf = password.toUtf8();
	const auto result = openssl::Pbkdf2Sha512(
		bytes::make_span(utf),
		bytes::make_span(salt),
		kIterations);
	return QByteArray(
		reinterpret_cast<const char*>(result.data()),
		int(result.size()));
}

[[nodiscard]] bool HasPassword() {
	Load();
	return !Get().hash.isEmpty();
}

void SetPassword(const QString &password) {
	Load();
	auto &state = Get();
	state.salt = QByteArray(kSaltSize, Qt::Uninitialized);
	base::RandomFill(state.salt.data(), state.salt.size());
	state.hash = HashOf(password, state.salt);
	Save();
}

[[nodiscard]] bool CheckPassword(const QString &password) {
	Load();
	const auto &state = Get();
	return !state.hash.isEmpty() && (HashOf(password, state.salt) == state.hash);
}

void Refresh(not_null<PeerData*> peer) {
	if (const auto history = peer->owner().historyLoaded(peer)) {
		history->updateChatListExistence();
	}
}

[[nodiscard]] bool IsLocked(not_null<const PeerData*> peer) {
	Load();
	const auto &locked = Get().locked;
	const auto i = locked.find(AccountOf(peer));
	return (i != end(locked)) && i->second.contains(peer->id.value);
}

[[nodiscard]] bool IsTemporary(not_null<const PeerData*> peer) {
	const auto &temporary = Get().temporary;
	const auto i = temporary.find(AccountOf(peer));
	return (i != end(temporary)) && i->second.contains(peer->id.value);
}

void SetLocked(not_null<PeerData*> peer, bool locked) {
	Load();
	auto &peers = Get().locked[AccountOf(peer)];
	if (locked) {
		peers.emplace(peer->id.value);
	} else {
		peers.remove(peer->id.value);
	}
	Save();
	Refresh(peer);
}

void SetTemporary(not_null<PeerData*> peer, bool hidden) {
	auto &peers = Get().temporary[AccountOf(peer)];
	if (hidden) {
		peers.emplace(peer->id.value);
	} else {
		peers.remove(peer->id.value);
	}
	Refresh(peer);
}

[[nodiscard]] std::vector<not_null<PeerData*>> PeersOf(
		not_null<Main::Session*> session,
		const base::flat_map<uint64, Peers> &all) {
	auto result = std::vector<not_null<PeerData*>>();
	const auto i = all.find(session->userId().bare);
	if (i == end(all)) {
		return result;
	}
	for (const auto value : i->second) {
		if (const auto peer = session->data().peerLoaded(PeerId(value))) {
			result.push_back(peer);
		}
	}
	return result;
}

// Asks for the password: twice when there is none yet.
void AskPassword(
		not_null<Window::SessionController*> controller,
		Fn<void()> done,
		bool forceCreate = false) {
	const auto create = forceCreate || !HasPassword();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(create
			? Tr("New password", "Новый пароль")
			: Tr("Protected folder", "Защищённая папка")));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(create
				? Tr(
					"The password opens the protected folder. It is kept on "
					"this computer only and can not be recovered.",
					"Пароль открывает защищённую папку. Он хранится только "
					"на этом компьютере, восстановить его нельзя.")
				: Tr(
					"Enter the password of the folder.",
					"Введите пароль папки.")),
			st::boxLabel));
		// A password field is not an RpWidget, so it sits in one.
		const auto addField = [&](const QString &placeholder) {
			const auto wrap = box->addRow(object_ptr<Ui::RpWidget>(box));
			const auto field = Ui::CreateChild<Ui::PasswordInput>(
				wrap,
				st::defaultInputField,
				rpl::single(placeholder));
			field->show();
			wrap->resize(wrap->width(), field->height());
			wrap->widthValue() | rpl::on_next([=](int width) {
				field->resize(width, field->height());
				field->move(0, 0);
			}, wrap->lifetime());
			return field;
		};
		const auto first = addField(Tr("Password", "Пароль"));
		const auto second = create
			? addField(Tr("Repeat the password", "Повторите пароль"))
			: nullptr;
		box->setFocusCallback([=] {
			first->setFocusFast();
		});
		const auto submit = [=] {
			const auto value = first->getLastText();
			if (create) {
				if (value.size() < kMinPassword) {
					first->showError();
					return;
				} else if (second->getLastText() != value) {
					second->showError();
					return;
				}
				SetPassword(value);
			} else if (!CheckPassword(value)) {
				first->showError();
				return;
			}
			box->closeBox();
			done();
		};
		QObject::connect(first, &Ui::MaskedInputField::submitted, submit);
		if (second) {
			QObject::connect(second, &Ui::MaskedInputField::submitted, submit);
		}
		box->addButton(rpl::single(Tr("Open", "Открыть")), submit);
		box->addButton(rpl::single(Tr("Cancel", "Отмена")), [=] {
			box->closeBox();
		});
	}));
}

// A list of chats: a click opens the chat or puts it back into the list.
void AddPeerRows(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		std::vector<not_null<PeerData*>> peers,
		QString backText,
		Fn<void(not_null<PeerData*>)> back) {
	const auto menu = box->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	for (const auto &peer : peers) {
		const auto button = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				rpl::single(peer->name()),
				st::defaultSettingsButton),
			style::margins());
		button->setClickedCallback([=] {
			*menu = base::make_unique_q<Ui::PopupMenu>(
				box,
				st::popupMenuWithIcons);
			(*menu)->addAction(Tr("Open", "Открыть"), [=] {
				box->closeBox();
				controller->showPeerHistory(peer);
			}, &st::menuIconShowInChat);
			(*menu)->addAction(backText, [=] {
				back(peer);
				button->hide();
				box->verticalLayout()->resizeToWidth(box->width());
			}, &st::menuIconArchiveOpen);
			(*menu)->popup(QCursor::pos());
		});
	}
}

void ShowLocked(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Tr("Protected folder", "Защищённая папка")));
		const auto peers = PeersOf(session, Get().locked);
		if (peers.empty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(Tr(
					"No chats here. Add one with \"Protected folder\" in "
					"the menu of a chat in the chat list.",
					"Здесь пока нет чатов. Добавьте чат пунктом «В защищённую "
					"папку» в его меню в списке чатов.")),
				st::boxLabel));
		}
		AddPeerRows(
			box,
			controller,
			peers,
			Tr("Back to the chat list", "Вернуть в список чатов"),
			[=](not_null<PeerData*> peer) { SetLocked(peer, false); });
		box->addButton(rpl::single(Tr("Close", "Закрыть")), [=] {
			box->closeBox();
		});
		box->addLeftButton(rpl::single(Tr("New password", "Сменить пароль")), [=] {
			box->closeBox();
			// The old password was asked a moment ago to get here.
			AskPassword(controller, [=] {
				controller->showToast(Tr(
					"The password is changed.",
					"Пароль изменён."));
			}, true);
		});
	}));
}

[[nodiscard]] QString MuteText(int seconds) {
	if (!seconds) {
		return Tr("Notifications of this chat are off.", "Уведомления этого чата выключены.");
	}
	const auto hours = seconds / 3600;
	const auto minutes = seconds / 60;
	const auto value = (hours >= 24 && !(hours % 24))
		? (QString::number(hours / 24) + Tr(" d", " д"))
		: (hours && !(seconds % 3600))
		? (QString::number(hours) + Tr(" h", " ч"))
		: (QString::number(minutes) + Tr(" min", " мин"));
	return Tr(
		"Notifications of this chat are off for %1.",
		"Уведомления этого чата выключены на %1.").arg(value);
}

} // namespace

bool IsHidden(not_null<const History*> history) {
	const auto peer = history->peer;
	return IsTemporary(peer) || IsLocked(peer);
}

void AddHideActions(
		not_null<PeerData*> peer,
		not_null<Window::SessionController*> controller,
		const Ui::Menu::MenuCallback &addAction) {
	if (peer->isSelf()) {
		return;
	}
	const auto weak = base::make_weak(controller);
	addAction(Tr("Hide until restart", "Скрыть до перезапуска"), [=] {
		SetTemporary(peer, true);
		if (const auto strong = weak.get()) {
			strong->showToast(Tr(
				"The chat is hidden until the app starts again. It is in "
				"\"Hidden chats\" of the main menu.",
				"Чат скрыт до перезапуска. Он в «Скрытых чатах» главного меню."));
		}
	}, &st::menuIconArchive);
	addAction(Tr("Protected folder", "В защищённую папку"), [=] {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto lock = [=] {
			SetLocked(peer, true);
			if (const auto strong = weak.get()) {
				strong->showToast(Tr(
					"The chat is in the protected folder.",
					"Чат в защищённой папке."));
			}
		};
		if (HasPassword()) {
			lock();
		} else {
			AskPassword(strong, lock);
		}
	}, &st::menuIconLock);
}

void ShowHiddenChats(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Tr("Hidden chats", "Скрытые чаты")));
		const auto peers = PeersOf(session, Get().temporary);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(peers.empty()
				? Tr(
					"No chats are hidden until restart.",
					"Нет чатов, скрытых до перезапуска.")
				: Tr(
					"Hidden until the app starts again:",
					"Скрыты до перезапуска:")),
			st::boxLabel));
		AddPeerRows(
			box,
			controller,
			peers,
			Tr("Back to the chat list", "Вернуть в список чатов"),
			[=](not_null<PeerData*> peer) { SetTemporary(peer, false); });
		box->addButton(rpl::single(Tr("Close", "Закрыть")), [=] {
			box->closeBox();
		});
		box->addLeftButton(
			rpl::single(Tr("Protected folder", "Защищённая папка")),
			[=] {
				box->closeBox();
				AskPassword(controller, [=] { ShowLocked(controller); });
			});
	}));
}

bool HandleChatCommand(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const QString &text) {
	if (!peer->isUser() || peer->isSelf()) {
		return false;
	}
	static const auto command = QRegularExpression(
		u"^\\s*/(mute|unmute)(?:\\s+(\\d{1,4})\\s*(m|min|h|d|м|мин|ч|д)?)?\\s*$"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto match = command.match(text);
	if (!match.hasMatch()) {
		return false;
	}
	auto &settings = peer->owner().notifySettings();
	if (match.captured(1).toLower() == u"unmute"_q) {
		settings.update(peer, Data::MuteValue{ .unmute = true });
		controller->showToast(Tr(
			"Notifications of this chat are on.",
			"Уведомления этого чата включены."));
		return true;
	}
	const auto count = match.captured(2).toInt();
	const auto unit = match.captured(3).toLower();
	const auto seconds = !count
		? 0
		: (unit == u"d"_q || unit == u"д"_q)
		? count * 86400
		: (unit == u"m"_q || unit == u"min"_q || unit == u"м"_q || unit == u"мин"_q)
		? count * 60
		: count * 3600;
	if (seconds) {
		settings.update(peer, Data::MuteValue{ .period = seconds });
	} else {
		settings.update(peer, Data::MuteValue{ .forever = true });
	}
	controller->showToast(MuteText(seconds));
	return true;
}

} // namespace HoursGram::Chats
