<p align="center">
  <img src=".github/assets/logo.png" width="160" alt="HorusGram">
</p>

<h1 align="center">HorusGram Desktop</h1>

<p align="center">
  <b>Telegram-клиент для тех, кому мало обычного Telegram.</b><br>
  Свой музыкальный плеер, редактор кода с ИИ-ассистентом и полная кастомизация — прямо внутри мессенджера.
</p>

<p align="center">
  <a href="https://t.me/HoursGram"><img src="https://img.shields.io/badge/Telegram-@HoursGram-8993D8?style=flat-square&logo=telegram&logoColor=white" alt="Telegram"></a>
  <img src="https://img.shields.io/badge/status-beta%20soon-8993D8?style=flat-square" alt="Beta soon">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-8993D8?style=flat-square" alt="Platform">
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++20">
  <img src="https://img.shields.io/badge/Qt-6-41CD52?style=flat-square&logo=qt&logoColor=white" alt="Qt 6">
  <img src="https://img.shields.io/badge/license-GPLv3-8993D8?style=flat-square" alt="License GPLv3">
</p>

<p align="center">
  <b>Русский</b> · <a href="README-EN.md">English</a>
</p>

<p align="center">
  <img src=".github/assets/screenshot.png" alt="HorusGram Desktop" width="100%">
</p>

## Что такое HorusGram

HorusGram — нативный десктопный клиент Telegram на C++/Qt. Он работает с вашим обычным аккаунтом Telegram, но добавляет то, чего нет в официальном приложении: тёмный «графитовый» интерфейс, встроенный плеер, редактор кода и десятки настроек внешнего вида. Всё работает локально — клиент не собирает ваши данные.

## Возможности

### 🎵 Музыка
- Собственный плеер в отдельном окне — вкладка **«Музыка»** на боковой панели
- Плейлисты и очередь воспроизведения
- Построчные тексты песен (LRCLIB, Kugou, NetEase)
- 5-полосный эквалайзер с пресетами
- «Волна» — то, что сейчас в тренде на SoundCloud

### `</>` HM Code
- Редактор кода в отдельном окне — вкладка **«HM Code»** на боковой панели
- Работа с файлами в выбранной вами папке
- ИИ-агент Manul прямо внутри Telegram

### 🎨 Кастомизация
- Графитовый интерфейс со скруглёнными элементами и шрифтом Montserrat
- Свой раздел **«Кастомизация»** в настройках: палитра, оформление, тема в стиле macOS
- Видео вместо фона чата — с размытием и затемнением
- «Матовое стекло» при просмотре медиа
- Собственный курсор

### 💬 Чаты
- Скрытие чатов до перезапуска или в папку под паролем
- Отправка любого видео как кружка
- Копирование фото и файлов из сообщений в буфер — вставка сразу альбомом
- Выделение нескольких чатов сразу

## Скачать

Бета скоро. Следите за новостями в [@HoursGram](https://t.me/HoursGram).

## Сборка из исходников

```powershell
git clone --recursive https://github.com/plshours/HorusGram-Desktop.git horusgram
```

Дальше — по инструкции для своей системы: [Windows](docs/building-win.md) · [Linux](docs/building-linux.md) · [macOS](docs/building-mac.md).

## Лицензия и авторство

HorusGram — **неофициальный** клиент. Он не связан с Telegram FZ-LLC или Telegram Messenger Inc. и не одобрен ими; «Telegram» — товарный знак его владельца.

Проект распространяется под **GPLv3** и основан на чужом свободном коде, авторство которого сохранено:

| Проект | Автор | Лицензия |
|---|---|---|
| [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) | © The Telegram Desktop Authors | GPLv3 с исключением OpenSSL ([LICENSE](LICENSE), [LEGAL](LEGAL)) |
| Десктопный форк, из которого взяты настройки и кастомизация | — | GPLv3; исходники в `Telegram/SourceFiles/ayu/` с оригинальными заголовками |
| [Telegram for macOS](https://github.com/TelegramMessenger/telegram-macos) | © Telegram FZ-LLC | GPL-2.0; используются только значения цветов для macOS-темы |
| [Montserrat](https://github.com/JulietaUla/Montserrat) | © The Montserrat Project Authors | SIL OFL 1.1 ([OFL.txt](Telegram/Resources/fonts/OFL.txt)) |

Подробнее — в [NOTICE.md](NOTICE.md). О приватности и использовании кода — в [PRIVACY_AND_CREDITS.md](PRIVACY_AND_CREDITS.md).
