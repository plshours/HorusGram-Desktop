// This is the source code of HorusGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
#include "hoursgram/customization/profile_music_balance.h"

#include "ayu/ayu_settings.h"
#include "ayu/ui/components/saved_music.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_balance_pill.h"
#include "info/profile/info_profile_music_button.h"
#include "info/saved/info_saved_music_widget.h"
#include "lang_auto.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/text/format_song_document_name.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "wallet/wallet_card_view.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

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

// The second part of the row: a card drawn like the track card, with the
// balance pill of the wallet in it. The pill stays the one Telegram draws,
// so the wallet card still opens out of it.
class BalanceCard final : public Ui::AbstractButton {
public:
	BalanceCard(QWidget *parent, not_null<UserData*> user);

	[[nodiscard]] not_null<Info::Profile::BalancePill*> pill() const;
	[[nodiscard]] int naturalCardWidth(int maxWidth);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	const not_null<Info::Profile::BalancePill*> _pill;

};

BalanceCard::BalanceCard(QWidget *parent, not_null<UserData*> user)
: AbstractButton(parent)
, _pill(Ui::CreateChild<Info::Profile::BalancePill>(
	this,
	user,
	rpl::single(std::optional<QColor>()))) {
	_pill->setOpacity(1.);
	_pill->setAttribute(Qt::WA_TransparentForMouseEvents);
	setCursor(style::cur_pointer);
	resize(width(), CardHeight());
}

not_null<Info::Profile::BalancePill*> BalanceCard::pill() const {
	return _pill;
}

int BalanceCard::naturalCardWidth(int maxWidth) {
	const auto &padding = st::infoMusicButtonPadding;
	const auto side = padding.left() + padding.right();
	_pill->resizeToNaturalWidth(std::max(maxWidth - side, 0));
	const auto fitted = _pill->fittedWidth();
	if (fitted > 0 && fitted < _pill->width()) {
		_pill->resizeToWidth(fitted);
	}
	return _pill->width() + side;
}

int BalanceCard::resizeGetHeight(int newWidth) {
	const auto height = CardHeight();
	_pill->move(
		(newWidth - _pill->width()) / 2,
		(height - _pill->height()) / 2);
	return height;
}

void BalanceCard::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::infoProfileMusicRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(isOver() ? st::windowBgOver : st::windowBg);
	p.drawRoundedRect(rect(), radius, radius);
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
	const auto row = wrap->entity()->entity();

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
			if (!pill->isHidden()) {
				Wallet::ShowCardView(
					controller->parentController(),
					user,
					pill);
			}
		});
		pill->shownValue() | rpl::on_next([=](bool shown) {
			balance->setVisible(shown);
			state->hasBalance = shown;
			relayout();
		}, balance->lifetime());
		pill->naturalWidthValue() | rpl::on_next([=](int) {
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
		anim::type::instant);
	wrap->finishAnimating();
}

} // namespace HoursGram::Customization
