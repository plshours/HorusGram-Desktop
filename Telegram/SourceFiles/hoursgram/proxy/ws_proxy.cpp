/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "hoursgram/proxy/ws_proxy.h"

#include "ayu/ayu_settings.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"
#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QProcess>

namespace HoursGram::Proxy {
namespace {

constexpr auto kPort = uint32(1443);
const auto kSecret = u"a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6"_q;
const auto kPassword = u"dda1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6"_q;

#ifdef Q_OS_WIN
const auto kExeName = u"TgWsProxy_windows.exe"_q;
#else
const auto kExeName = u"TgWsProxy_linux"_q;
#endif

QProcess *ProcessInstance = nullptr;

[[nodiscard]] QString ExePath() {
	return QDir(QCoreApplication::applicationDirPath())
		.filePath(kExeName);
}

[[nodiscard]] MTP::ProxyData WsProxyData() {
	auto proxy = MTP::ProxyData();
	proxy.type = MTP::ProxyData::Type::Mtproto;
	proxy.host = u"127.0.0.1"_q;
	proxy.port = kPort;
	proxy.password = kPassword;
	return proxy;
}

// Earlier versions switched everyone to this proxy on every start. Where it
// is not used that leaves the client without a connection, so it is undone.
void ForgetProxy() {
	const auto proxy = WsProxyData();
	auto &proxySettings = Core::App().settings().proxy();
	if (proxySettings.indexInList(proxy) < 0) {
		return;
	}
	const auto current = proxySettings.isEnabled()
		&& (proxySettings.selected() == proxy);
	[[maybe_unused]] const auto removed = proxySettings.removeFromList(proxy);
	if (current) {
		Core::App().setCurrentProxy(
			MTP::ProxyData(),
			MTP::ProxyData::Settings::System);
	}
	Core::App().saveSettingsDelayed();
}

void ConfigureProxy() {
	const auto proxy = WsProxyData();
	auto &proxySettings = Core::App().settings().proxy();
	if (proxySettings.indexInList(proxy) < 0) {
		proxySettings.addToList(proxy);
	}
	Core::App().setCurrentProxy(proxy, MTP::ProxyData::Settings::Enabled);
}

} // namespace

void StartWsProxy() {
	if (ProcessInstance) {
		return;
	}
	const auto path = ExePath();
	if (!AyuSettings::getInstance().wsProxyAutostart()
		|| !QFile::exists(path)) {
		ForgetProxy();
		return;
	}
	// A proxy the user chose, or none at all, is left as it is: replacing it
	// would show the address of the user where they hid it.
	const auto &proxySettings = Core::App().settings().proxy();
	if (proxySettings.isDisabled()
		|| (proxySettings.isEnabled()
			&& !(proxySettings.selected() == WsProxyData()))) {
		return;
	}

	auto *process = new QProcess();
	process->setProgram(path);
	process->setArguments({
		u"--host"_q, u"127.0.0.1"_q,
		u"--port"_q, QString::number(kPort),
		u"--secret"_q, kSecret,
	});
	process->setStandardOutputFile(QProcess::nullDevice());
	process->setStandardErrorFile(QProcess::nullDevice());
	process->start();
	if (!process->waitForStarted(3000)) {
		delete process;
		return;
	}
	ProcessInstance = process;
	ConfigureProxy();
}

void StopWsProxy() {
	if (!ProcessInstance) {
		return;
	}
	ProcessInstance->terminate();
	if (!ProcessInstance->waitForFinished(2000)) {
		ProcessInstance->kill();
		ProcessInstance->waitForFinished(1000);
	}
	delete ProcessInstance;
	ProcessInstance = nullptr;
}

} // namespace HoursGram::Proxy
