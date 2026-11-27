// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/wrap/vertical_blocks.h"

#include "base/invoke_queued.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "styles/style_widgets.h"

#include <QtCore/QPointer>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

namespace Ui {
namespace {

enum class Neighbor {
	None,
	Content,
	Divider,
};

class Divider final : public RpWidget {
public:
	Divider(QWidget *parent, int skip, bool text, bool cards = false);

	[[nodiscard]] FlatLabel *label() const;
	bool setNeighbors(Neighbor above, Neighbor below);

	QAccessible::Role accessibilityRole() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] int topSkip() const;
	[[nodiscard]] int bottomSkip() const;

	const style::DividerBar &_st;
	const int _skip = 0;
	const bool _cards = false;
	object_ptr<FlatLabel> _label = { nullptr };
	Neighbor _above = Neighbor::Content;
	Neighbor _below = Neighbor::Content;
	bool _resizing = false;

};

Divider::Divider(QWidget *parent, int skip, bool text, bool cards)
: RpWidget(parent)
, _st(st::defaultDividerLabel.bar)
, _skip(skip)
, _cards(cards) {
	if (text) {
		_label.create(this, st::defaultDividerLabel.label);
		_label->heightValue() | rpl::on_next([=] {
			if (!_resizing) {
				resizeToWidth(widthNoMargins());
			}
		}, _label->lifetime());
	}
	resizeToWidth(widthNoMargins());
}

FlatLabel *Divider::label() const {
	return _label.data();
}

bool Divider::setNeighbors(Neighbor above, Neighbor below) {
	if (_above == above && _below == below) {
		return false;
	}
	_above = above;
	_below = below;
	resizeToWidth(widthNoMargins());
	update();
	return true;
}

QAccessible::Role Divider::accessibilityRole() {
	return _label
		? RpWidget::accessibilityRole()
		: QAccessible::Role::Separator;
}

int Divider::topSkip() const {
	return (_above == Neighbor::Content) ? _skip : 0;
}

int Divider::bottomSkip() const {
	return (_below == Neighbor::Content) ? _skip : 0;
}

int Divider::resizeGetHeight(int newWidth) {
	if (_cards && !_label) {
		// Between two cards the divider is only the gap.
		return _skip;
	}
	const auto top = _cards ? 0 : topSkip();
	if (!_label) {
		return top + st::boxDividerHeight + bottomSkip();
	}
	const auto &padding = st::defaultBoxDividerLabelPadding;
	_resizing = true;
	_label->resizeToWidth(newWidth - padding.left() - padding.right());
	_resizing = false;
	_label->moveToLeft(padding.left(), top + padding.top(), newWidth);
	return top
		+ padding.top()
		+ _label->height()
		+ ((_below == Neighbor::Divider) ? padding.top() : padding.bottom())
		+ bottomSkip();
}

void Divider::paintEvent(QPaintEvent *e) {
	if (_cards) {
		// The text under a card is plain text on the panel.
		return;
	}
	auto p = QPainter(this);

	const auto top = topSkip();
	const auto bottom = height() - bottomSkip();
	p.fillRect(0, top, width(), bottom - top, _st.bg);
	if (_above != Neighbor::Divider) {
		_st.top.fill(p, QRect(0, top, width(), _st.top.height()));
	}
	if (_below != Neighbor::Divider) {
		_st.bottom.fill(p, QRect(
			0,
			bottom - _st.bottom.height(),
			width(),
			_st.bottom.height()));
	}
}

// HorusGram: a run of blocks reads as a card, a rounded shape inset from
// the sides of the panel with the rows drawn on top of it. The rows paint
// their own background across the full width of the card, so four small
// squares sit in its corners above them, each cutting a quarter circle out
// of itself and leaving the panel color behind. Neighbouring blocks with no
// divider between them make one card, so only the outer corners are cut.
class Card final : public PaddingWrap<RpWidget> {
public:
	Card(
		QWidget *parent,
		object_ptr<RpWidget> content,
		const VerticalBlocks::Cards &cards);

	void setEdges(bool top, bool bottom);

protected:
	int resizeGetHeight(int newWidth) override;
	void wrappedSizeUpdated(QSize size) override;
	void paintEvent(QPaintEvent *e) override;

private:
	static constexpr auto kCorners = 4;

	void createCorners();
	void placeCorners();
	void scheduleCornerUpdate();
	[[nodiscard]] QRect cardRect() const;
	[[nodiscard]] int cardRadius(QSize size) const;

	const int _radius = 0;
	std::array<RpWidget*, kCorners> _corners = { nullptr };
	bool _top = true;
	bool _bottom = true;
	bool _cornerUpdateScheduled = false;

};

Card::Card(
	QWidget *parent,
	object_ptr<RpWidget> content,
	const VerticalBlocks::Cards &cards)
: PaddingWrap(parent, std::move(content), cards.padding)
, _radius(cards.radius) {
	createCorners();
	sizeValue() | rpl::on_next([=](QSize) {
		placeCorners();
	}, lifetime());
	events() | rpl::filter([](not_null<QEvent*> event) {
		const auto type = event->type();
		return (type == QEvent::ChildAdded)
			|| (type == QEvent::LayoutRequest);
	}) | rpl::on_next([=](not_null<QEvent*>) {
		scheduleCornerUpdate();
	}, lifetime());
}

void Card::setEdges(bool top, bool bottom) {
	if (_top == top && _bottom == bottom) {
		return;
	}
	_top = top;
	_bottom = bottom;
	placeCorners();
	update();
}

QRect Card::cardRect() const {
	const auto padding = this->padding();
	return rect().marginsRemoved(
		QMargins(padding.left(), 0, padding.right(), 0));
}

int Card::cardRadius(QSize size) const {
	return std::max(1, std::min(
		_radius,
		std::min(size.width(), size.height()) / 2));
}

void Card::createCorners() {
	for (auto i = 0; i != kCorners; ++i) {
		const auto corner = CreateChild<RpWidget>(this);
		corner->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto right = (i % 2) != 0;
		const auto bottom = (i / 2) != 0;
		corner->paintRequest() | rpl::on_next([=] {
			const auto side = corner->width();
			auto cut = QPainterPath();
			cut.addRect(corner->rect());
			auto circle = QPainterPath();
			circle.addEllipse(QRectF(
				right ? -side : 0,
				bottom ? -side : 0,
				2 * side,
				2 * side));
			auto p = QPainter(corner);
			auto hq = PainterHighQualityEnabler(p);
			p.fillPath(cut.subtracted(circle), st::boxBg);
		}, corner->lifetime());
		_corners[i] = corner;
	}
}

void Card::placeCorners() {
	const auto card = cardRect();
	const auto side = card.isEmpty() ? 0 : cardRadius(card.size());
	for (auto i = 0; i != kCorners; ++i) {
		const auto corner = _corners[i];
		const auto right = (i % 2) != 0;
		const auto bottom = (i / 2) != 0;
		if (!side || (bottom ? !_bottom : !_top)) {
			corner->hide();
			continue;
		}
		corner->setGeometry(
			right ? (card.x() + card.width() - side) : card.x(),
			bottom ? (card.y() + card.height() - side) : card.y(),
			side,
			side);
		corner->show();
		corner->raise();
		corner->update();
	}
}

// The rows of a block are created again when it is rebuilt and a fresh
// child sits above the corners. Any such event puts them back on top, once
// per event loop pass.
void Card::scheduleCornerUpdate() {
	if (_cornerUpdateScheduled) {
		return;
	}
	_cornerUpdateScheduled = true;
	PostponeCall(this, [=] {
		_cornerUpdateScheduled = false;
		placeCorners();
	});
}

// A block that hides itself collapses to no height at all, and the card
// collapses with it so that no empty rounded shape is left behind.
int Card::resizeGetHeight(int newWidth) {
	const auto result = PaddingWrap::resizeGetHeight(newWidth);
	const auto weak = wrapped();
	return (weak && weak->height()) ? result : 0;
}

void Card::wrappedSizeUpdated(QSize size) {
	if (size.height() > 0) {
		PaddingWrap::wrappedSizeUpdated(size);
		return;
	}
	const auto padding = this->padding();
	resize(padding.left() + size.width() + padding.right(), 0);
}

void Card::paintEvent(QPaintEvent *e) {
	const auto card = cardRect();
	if (card.isEmpty()) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = cardRadius(card.size());
	auto path = QPainterPath();
	path.addRoundedRect(card, radius, radius);
	if (!_top) {
		path.addRect(QRect(card.x(), card.y(), card.width(), radius));
	}
	if (!_bottom) {
		path.addRect(QRect(
			card.x(),
			card.y() + card.height() - radius,
			card.width(),
			radius));
	}
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBg);
	p.drawPath(path.simplified());
}

} // namespace

struct VerticalBlocks::State {
	enum class Kind {
		Part,
		Note,
		About,
		Separator,
	};
	struct Item {
		Kind kind = Kind::Part;
		QPointer<RpWidget> part;
		SlideWrap<RpWidget> *slide = nullptr;
		QPointer<RpWidget> card;
		QPointer<SlideWrap<Divider>> divider;
		bool shown = true;
		bool filled = false;
		bool visible = false;
		Neighbor above = Neighbor::None;
		Neighbor below = Neighbor::None;
	};

	State(not_null<VerticalLayout*> layout, Args args);

	void addPart(Part &&part);
	int addDivider(Kind kind);
	void addText(Kind kind, Text &&text);
	[[nodiscard]] Fn<void(bool)> setter(int index, bool Item::*field);
	[[nodiscard]] bool present(const Item &item) const;
	void refresh();
	void compute();
	void apply();

	const not_null<VerticalLayout*> layout;
	const Args args;
	std::vector<Item> items;
	bool building = true;
	bool refreshing = false;
	bool refreshAgain = false;
	bool relayoutQueued = false;
};

VerticalBlocks::State::State(not_null<VerticalLayout*> layout, Args args)
: layout(layout)
, args(args) {
	InvokeQueued(layout, [=] {
		refresh();
		building = false;
	});
}

void VerticalBlocks::State::addPart(Part &&part) {
	auto widget = std::move(part.widget);
	const auto inner = dynamic_cast<SlideWrap<RpWidget>*>(widget.data());
	auto card = (Card*)nullptr;
	if (args.cards) {
		auto owned = object_ptr<Card>(layout, std::move(widget), *args.cards);
		card = owned.data();
		widget = std::move(owned);
	}
	const auto raw = layout->add(std::move(widget));
	const auto index = int(items.size());
	const auto slide = inner
		? inner
		: dynamic_cast<SlideWrap<RpWidget>*>(raw);
	items.push_back({
		.kind = Kind::Part,
		.part = raw,
		.slide = slide,
		.card = card,
	});
	if (slide) {
		slide->toggledValue() | rpl::on_next([=] {
			refresh();
		}, raw->lifetime());
	}
	if (part.shown) {
		std::move(
			part.shown
		) | rpl::on_next(setter(index, &Item::shown), raw->lifetime());
	}
	raw->heightValue() | rpl::map([](int height) {
		return (height > 0);
	}) | rpl::distinct_until_changed() | rpl::on_next([=] {
		refresh();
	}, raw->lifetime());
}

int VerticalBlocks::State::addDivider(Kind kind) {
	const auto text = (kind != Kind::Separator);
	const auto wrap = layout->add(object_ptr<SlideWrap<Divider>>(
		layout,
		object_ptr<Divider>(
			layout,
			args.cards ? args.cards->skip : args.skip,
			text,
			args.cards.has_value())));
	if (args.duration) {
		wrap->setDuration(*args.duration);
	}
	wrap->hide(anim::type::instant);
	items.push_back({ .kind = kind, .divider = wrap });
	return int(items.size()) - 1;
}

void VerticalBlocks::State::addText(Kind kind, Text &&text) {
	const auto index = addDivider(kind);
	const auto wrap = items[index].divider;
	const auto label = wrap->entity()->label();
	if (text.setup) {
		text.setup(label);
	}
	std::move(
		text.text
	) | rpl::on_next([=](const TextWithEntities &value) {
		const auto filled = !value.empty();
		if (filled) {
			label->setMarkedText(value);
		}
		setter(index, &Item::filled)(filled);
	}, wrap->lifetime());
	if (text.shown) {
		std::move(
			text.shown
		) | rpl::on_next(setter(index, &Item::shown), wrap->lifetime());
	}
	refresh();
}

Fn<void(bool)> VerticalBlocks::State::setter(int index, bool Item::*field) {
	return [=](bool value) {
		if (items[index].*field != value) {
			items[index].*field = value;
			refresh();
		}
	};
}

// Resize events of hidden widgets are postponed, so heights are read here.
bool VerticalBlocks::State::present(const Item &item) const {
	if (!item.shown) {
		return false;
	} else if (item.kind != Kind::Part) {
		return item.filled;
	}
	const auto part = item.part.data();
	return part
		&& (!item.slide || item.slide->toggled())
		&& (part->height() > 0);
}

void VerticalBlocks::State::refresh() {
	if (refreshing) {
		refreshAgain = true;
		return;
	}
	refreshing = true;
	const auto guard = gsl::finally([&] { refreshing = false; });
	do {
		refreshAgain = false;
		compute();
		apply();
	} while (refreshAgain);
}

void VerticalBlocks::State::compute() {
	auto blockShown = false;
	auto last = Neighbor::None;
	auto lastText = -1;
	auto candidate = -1;
	const auto count = int(items.size());
	for (auto i = 0; i != count; ++i) {
		auto &item = items[i];
		item.visible = false;
		item.above = item.below = Neighbor::None;
		const auto shown = present(item);
		switch (item.kind) {
		case Kind::Part:
			if (!shown) {
				break;
			}
			item.visible = true;
			if (candidate >= 0) {
				auto &separator = items[candidate];
				separator.visible = true;
				separator.above = separator.below = Neighbor::Content;
				candidate = -1;
			} else if (lastText >= 0) {
				items[lastText].below = Neighbor::Content;
			}
			blockShown = true;
			last = Neighbor::Content;
			lastText = -1;
			break;
		case Kind::Note:
		case Kind::About:
			if (!shown || (item.kind == Kind::About && !blockShown)) {
				break;
			}
			item.visible = true;
			item.above = last;
			if (lastText >= 0) {
				items[lastText].below = Neighbor::Divider;
			}
			last = Neighbor::Divider;
			lastText = i;
			candidate = -1;
			break;
		case Kind::Separator:
			blockShown = false;
			if (last == Neighbor::Content && candidate < 0) {
				candidate = i;
			}
			break;
		}
	}
	if (candidate >= 0 && args.closingDivider) {
		items[candidate].visible = true;
		items[candidate].above = Neighbor::Content;
	}
}

void VerticalBlocks::State::apply() {
	const auto animated = building
		? anim::type::instant
		: anim::type::normal;
	if (args.cards) {
		const auto count = int(items.size());
		const auto isPart = [&](int index) {
			return (index >= 0)
				&& (index < count)
				&& (items[index].kind == Kind::Part);
		};
		auto previous = -1;
		for (auto i = 0; i != count; ++i) {
			if (!items[i].visible) {
				continue;
			}
			if (const auto card = static_cast<Card*>(items[i].card.data())) {
				auto next = i + 1;
				while (next < count && !items[next].visible) {
					++next;
				}
				card->setEdges(!isPart(previous), !isPart(next));
			}
			previous = i;
		}
	}
	auto changed = false;
	for (const auto &item : items) {
		const auto wrap = item.divider.data();
		if (!wrap) {
			continue;
		} else if (item.visible
			&& wrap->entity()->setNeighbors(item.above, item.below)) {
			SendPendingMoveResizeEvents(wrap);
		}
		if (wrap->toggled() != item.visible) {
			wrap->toggle(item.visible, animated);
			changed = true;
		}
	}
	if (!changed || relayoutQueued) {
		return;
	}
	relayoutQueued = true;
	// A toggle landing inside a layout resize pass is skipped there.
	InvokeQueued(layout, [=] {
		relayoutQueued = false;
		layout->resizeToWidth(layout->widthNoMargins());
	});
}

VerticalBlocks::VerticalBlocks(not_null<VerticalLayout*> layout, Args args)
: _state(layout->lifetime().make_state<State>(layout, args)) {
}

void VerticalBlocks::add(Part part) {
	if (part.widget) {
		_state->addPart(std::move(part));
	}
}

void VerticalBlocks::addSeparator() {
	const auto state = _state;
	const auto &items = state->items;
	if (!items.empty() && items.back().kind == State::Kind::Separator) {
		return;
	}
	state->addDivider(State::Kind::Separator);
	state->refresh();
}

void VerticalBlocks::addNote(Text text) {
	_state->addText(State::Kind::Note, std::move(text));
}

void VerticalBlocks::addAbout(Text text) {
	_state->addText(State::Kind::About, std::move(text));
	addSeparator();
}

not_null<VerticalLayout*> VerticalBlocks::layout() const {
	return _state->layout;
}

} // namespace Ui
