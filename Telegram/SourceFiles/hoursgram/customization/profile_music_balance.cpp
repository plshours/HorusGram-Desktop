// This is the source code of HorusGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
#include "hoursgram/customization/profile_music_balance.h"

#include "ayu/ayu_settings.h"
#include "ui/controls/ton_common.h"
#include "main/main_session.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "data/data_user_wallet_card.h"
#include "data/data_changes.h"
#include "ayu/ui/components/saved_music.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_balance_pill.h"
#include "info/profile/info_profile_music_button.h"
#include "info/saved/info_saved_music_widget.h"
#include "lang/lang_keys.h"
#include "lang_auto.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/text/format_song_document_name.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "wallet/wallet_card_view.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/palette.h"

namespace HoursGram::Customization {
namespace {

[[nodiscard]] Info::Profile::MusicButtonData MusicData(
		not_null<DocumentData*> document,
		not_null<HistoryItem*> item) {
	const auto name = Ui::Text::FormatSongNameFor(document);
	return {
		.name = name,
		.title = name.composedName().title,
		.performer = name.composedName().performer,
		.msgId = item->fullId(),
		.mediaView = document->createMediaView(),
	};
}

[[nodiscard]] DocumentData *ItemDocument(HistoryItem *item) {
	const auto media = item ? item->media() : nullptr;
	return media ? media->document() : nullptr;
}

[[nodiscard]] int CardHeight() {
	return st::infoMusicButtonPadding.top()
		+ st::infoMusicButtonCover
		+ st::infoMusicButtonPadding.bottom();
}

// The second part of the row: the wallet balance drawn exactly like the
// track card, a round TON mark where the cover is, the amount and the word
// "Balance" in the two lines and the arrow on the right. The balance pill of
// Telegram stays inside it, invisible, only as the place the wallet card
// opens from.
class BalanceCard final : public Ui::AbstractButton {
public:
	BalanceCard(QWidget *parent, not_null<UserData*> user);

	[[nodiscard]] not_null<Info::Profile::BalancePill*> pill() const;
	[[nodiscard]] int naturalCardWidth(int maxWidth);
	[[nodiscard]] rpl::producer<bool> hasBalanceValue() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void setBalance(int64 balance);
	[[nodiscard]] int textLeft() const;

	const not_null<Info::Profile::BalancePill*> _pill;
	rpl::variable<bool> _hasBalance = false;
	QString _amount;
	QString _label;
	QImage _icon;

};

BalanceCard::BalanceCard(QWidget *parent, not_null<UserData*> user)
: AbstractButton(parent)
, _pill(Ui::CreateChild<Info::Profile::BalancePill>(
	this,
	user,
	rpl::single(std::optional<QColor>()))) {
	_pill->setOpacity(0.);
	_pill->setAttribute(Qt::WA_TransparentForMouseEvents);
	setCursor(style::cur_pointer);
	resize(width(), CardHeight());

	const auto refreshIcon = [=] {
		_icon = Ui::Earn::IconCurrencyTwoTone(
			st::infoMusicButtonCover / 2,
			st::currencyFg->c);
		update();
	};
	refreshIcon();
	style::PaletteChanged(
	) | rpl::on_next(refreshIcon, lifetime());

	rpl::combine(
		tr::lng_wallet_profile_balance(),
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::WalletCard
		) | rpl::map([=] {
			const auto card = user->walletCard();
			return card ? card->balance : int64();
		}) | rpl::distinct_until_changed()
	) | rpl::on_next([=](const QString &label, int64 balance) {
		_label = label;
		setBalance(balance);
	}, lifetime());
}

void BalanceCard::setBalance(int64 balance) {
	if (balance <= 0) {
		_amount = QString();
		_hasBalance = false;
		return;
	}
	_amount = Ui::FormatTonAmount(balance).full + u" TON"_q;
	_hasBalance = true;
	update();
}

not_null<Info::Profile::BalancePill*> BalanceCard::pill() const {
	return _pill;
}

rpl::producer<bool> BalanceCard::hasBalanceValue() const {
	return _hasBalance.value();
}

int BalanceCard::textLeft() const {
	return st::infoMusicButtonPadding.left()
		+ st::infoMusicButtonCover
		+ st::infoMusicButtonSkip;
}

int BalanceCard::naturalCardWidth(int maxWidth) {
	const auto text = std::max(
		st::semiboldFont->width(_amount),
		st::normalFont->width(_label));
	const auto natural = textLeft()
		+ text
		+ st::infoMusicButtonSkip
		+ st::topicButtonArrow.width()
		+ st::infoMusicButtonPadding.right();
	return std::min(natural, maxWidth);
}

int BalanceCard::resizeGetHeight(int newWidth) {
	const auto height = CardHeight();
	_pill->move(
		(newWidth - _pill->width()) / 2,
		(height - _pill->height()) / 2);
	return height;
}

void BalanceCard::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::infoProfileMusicRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(isOver() ? st::windowBgOver : st::windowBg);
	p.drawRoundedRect(rect(), radius, radius);

	const auto size = st::infoMusicButtonCover;
	const auto cover = QRect(
		st::infoMusicButtonPadding.left(),
		(height() - size) / 2,
		size,
		size);
	p.setBrush(isOver() ? st::windowBgRipple : st::windowBgOver);
	p.drawEllipse(cover);
	if (!_icon.isNull()) {
		const auto iconSize = _icon.size() / _icon.devicePixelRatio();
		p.drawImage(
			cover.x() + (size - iconSize.width()) / 2,
			cover.y() + (size - iconSize.height()) / 2,
			_icon);
	}

	const auto &arrow = st::topicButtonArrow;
	const auto arrowLeft = width()
		- st::infoMusicButtonPadding.right()
		- arrow.width();
	arrow.paint(
		p,
		arrowLeft,
		(height() - arrow.height()) / 2,
		width(),
		st::windowSubTextFg->c);

	const auto left = textLeft();
	const auto available = std::max(
		arrowLeft - st::infoMusicButtonSkip - left,
		0);
	const auto &bold = st::semiboldFont;
	const auto &small = st::normalFont;
	const auto lines = bold->height + small->height;
	const auto top = (height() - lines) / 2;
	p.setFont(bold);
	p.setPen(st::windowBoldFg);
	p.drawTextLeft(
		left,
		top,
		width(),
		bold->elided(_amount, available));
	p.setFont(small);
	p.setPen(st::windowSubTextFg);
	p.drawTextLeft(
		left,
		top + bold->height,
		width(),
		small->elided(_label, available));
}


} // namespace

void AddProfileMusicBalance(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer) {
	const auto wrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::PaddingWrap<Ui::RpWidget>>>(
			layout,
			object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
				layout,
				object_ptr<Ui::RpWidget>(layout),
				st::infoProfileMusicPadding)));
	// The entity of nested wraps is the innermost widget already.
	const auto row = wrap->entity();

	struct State {
		Info::Profile::AyuMusicButton *music = nullptr;
		BalanceCard *balance = nullptr;
		rpl::variable<bool> hasMusic = false;
		rpl::variable<bool> hasBalance = false;
	};
	const auto state = row->lifetime().make_state<State>();

	const auto relayout = [=] {
		const auto width = row->width();
		if (width <= 0) {
			return;
		}
		const auto gap = st::infoProfileCardSkip;
		const auto music = state->hasMusic.current() ? state->music : nullptr;
		const auto balance = state->hasBalance.current()
			? state->balance
			: nullptr;
		auto balanceWidth = 0;
		if (balance) {
			balanceWidth = music
				? std::min(balance->naturalCardWidth(width / 2), width / 2)
				: width;
			if (!music) {
				balance->naturalCardWidth(width);
			}
			balance->resizeToWidth(balanceWidth);
			balance->moveToRight(0, 0, width);
		}
		auto height = 0;
		if (music) {
			const auto musicWidth = balance
				? std::max(width - balanceWidth - gap, 0)
				: width;
			music->resizeToWidth(musicWidth);
			music->moveToLeft(0, 0, width);
			height = music->height();
		}
		if (balance) {
			height = std::max(height, balance->height());
		}
		row->resize(width, height);
	};

	row->widthValue() | rpl::on_next([=](int) {
		relayout();
	}, row->lifetime());

	if (const auto user = peer->asUser()) {
		const auto balance = Ui::CreateChild<BalanceCard>(row, user);
		state->balance = balance;
		const auto pill = balance->pill();
		balance->setClickedCallback([=] {
			if (state->hasBalance.current()) {
				Wallet::ShowCardView(
					controller->parentController(),
					user,
					pill);
			}
		});
		balance->hasBalanceValue() | rpl::on_next([=](bool shown) {
			balance->setVisible(shown);
			state->hasBalance = shown;
			relayout();
		}, balance->lifetime());
	}

	auto musicValue = Data::SavedMusic::Supported(peer->id)
		? Data::SavedMusicList(
			peer,
			nullptr,
			1
		) | rpl::map([=](const Data::SavedMusicSlice &data) {
			return data.size() ? data[0].get() : nullptr;
		}) | rpl::type_erased
		: rpl::single<HistoryItem*>(nullptr);

	std::move(
		musicValue
	) | rpl::on_next([=](HistoryItem *item) {
		delete base::take(state->music);
		const auto document = ItemDocument(item);
		if (!document) {
			state->hasMusic = false;
			relayout();
			return;
		}
		const auto music = Ui::CreateChild<Info::Profile::AyuMusicButton>(
			row,
			MusicData(document, item),
			std::nullopt,
			[=] { controller->showSection(Info::Saved::MakeMusic(peer)); });
		state->music = music;
		music->setAcceptBoth(true);
		music->clicks(
		) | rpl::filter([](Qt::MouseButton button) {
			return (button == Qt::RightButton);
		}) | rpl::on_next([=] {
			const auto menu = new Ui::PopupMenu(
				nullptr,
				st::popupMenuWithIcons);
			menu->setAttribute(Qt::WA_DeleteOnClose);
			menu->addAction(
				(AyuSettings::getInstance().adaptiveCoverColor()
					? tr::ayu_DisableColorfulCover(tr::now)
					: tr::ayu_EnableColorfulCover(tr::now)),
				[=] {
					auto &settings = AyuSettings::getInstance();
					settings.setAdaptiveCoverColor(
						!settings.adaptiveCoverColor());
					if (const auto now = ItemDocument(item)) {
						music->updateData(MusicData(now, item));
					}
				},
				&st::menuIconPalette);
			menu->popup(QCursor::pos());
		}, music->lifetime());
		music->onReady() | rpl::on_next([=] {
			music->update();
		}, music->lifetime());
		music->show();
		state->hasMusic = true;
		relayout();
	}, row->lifetime());

	wrap->toggleOn(
		rpl::combine(
			state->hasMusic.value(),
			state->hasBalance.value()
		) | rpl::map([](bool music, bool balance) {
			return music || balance;
		}),
		anim::type::normal);
	// The first state is taken at once, a track or a balance that comes
	// later slides the row open instead of pushing the profile down.
	wrap->finishAnimating();
}

} // namespace HoursGram::Customization
