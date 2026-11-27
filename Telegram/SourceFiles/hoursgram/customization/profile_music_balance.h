// This is the source code of HorusGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
#pragma once

class PeerData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info {
class Controller;
} // namespace Info

namespace HoursGram::Customization {

// The row under the header of a profile: the profile track as a card with
// its cover on the left and, for a user with money on the wallet card, the
// balance as the second part of the row on the right. The row is hidden
// while there is neither.
void AddProfileMusicBalance(
	not_null<Ui::VerticalLayout*> layout,
	not_null<Info::Controller*> controller,
	not_null<PeerData*> peer);

} // namespace HoursGram::Customization
