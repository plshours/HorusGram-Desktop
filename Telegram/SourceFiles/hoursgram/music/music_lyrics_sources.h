/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

// HorusGram: lyrics lookup over LRCLIB, Kugou and NetEase, picking the
// variant closest in length; the artwork lookup and the Genius source are
// left out, the in-memory cache is left to the caller, which keeps found
// lyrics on disk.
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <vector>

namespace HoursGram::Music::Sources {

// HoursGram: a word of a line with its own timing, from the KRC lyrics of
// Kugou, so the line fills word by word as it is sung.
struct LyricWord {
	qint64 timeMs = 0;
	qint64 durationMs = -1;
	QString text;
};

struct LyricLine {
	qint64 timeMs = 0;
	QString text;
	std::vector<LyricWord> words;
};

struct Lyrics {
	std::vector<LyricLine> lines;
	bool synced = false;
	bool found = false;
	bool unavailable = false;
	bool words = false;
};

// LRCLIB first, picking the variant closest in length (speed up
// and slowed versions get their timings stretched), then Kugou and NetEase,
// then a search by the title alone. `done` runs on the main thread.
void FetchLyrics(
	const QString &title,
	const QString &artist,
	qint64 durationMs,
	Fn<void(Lyrics)> done);

// HoursGram: lyrics timed by lines get their words timed too when Kugou has
// the same song in KRC. `done` gets the lyrics with word timings, or lyrics
// with `found` false when there is nothing better than `base`.
void FetchWordTimings(
	const QString &title,
	const QString &artist,
	qint64 durationMs,
	const Lyrics &base,
	Fn<void(Lyrics)> done);

// Название без хвостов вроде "(Official Video)" и без имени исполнителя в начале.
[[nodiscard]] QString CleanTitle(const QString &title, const QString &artist);

} // namespace HoursGram::Music::Sources
