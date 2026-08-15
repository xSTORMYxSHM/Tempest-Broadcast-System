/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasic.hpp"

#ifdef BROWSER_AVAILABLE
#include <dialogs/OBSExtraBrowsers.hpp>
#include <docks/BrowserDock.hpp>

#include <json11.hpp>
#include <qt-wrappers.hpp>

#include <QCloseEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

using namespace json11;

namespace {
constexpr char TempestStreamInfoSection[] = "TempestStreamInfo";
constexpr char TempestStreamInfoChannelKey[] = "Channel";
constexpr char TempestStreamInfoDockName[] = "tempestStreamInfoWeb";

class TempestStreamInfoBrowserDock final : public BrowserDock {
public:
	using BrowserDock::BrowserDock;

protected:
	void closeEvent(QCloseEvent *event) override
	{
		OBSBasic *main = OBSBasic::Get();
		if (main && main->isClosing())
			BrowserDock::closeEvent(event);
		else
			OBSDock::closeEvent(event);
	}
};

QString TwitchStreamInfoUrl(const QString &channel)
{
	if (channel.isEmpty())
		return QStringLiteral("https://dashboard.twitch.tv/");
	return QStringLiteral("https://dashboard.twitch.tv/popout/u/%1/stream-manager/edit-stream-info").arg(channel);
}

bool IsValidTwitchChannel(const QString &channel)
{
	static const QRegularExpression validChannel(QStringLiteral("^[A-Za-z0-9_]{1,25}$"));
	return validChannel.match(channel).hasMatch();
}
} // namespace
#endif

#include <random>

struct QCef;
struct QCefCookieManager;

QCef *cef = nullptr;
QCefCookieManager *panel_cookies = nullptr;
bool cef_js_avail = false;

#ifdef BROWSER_AVAILABLE
void OBSBasic::ClearExtraBrowserDocks()
{
	extraBrowserDockTargets.clear();
	extraBrowserDockNames.clear();
	extraBrowserDocks.clear();
}

void OBSBasic::LoadExtraBrowserDocks()
{
	const char *jsonStr = config_get_string(App()->GetUserConfig(), "BasicWindow", "ExtraBrowserDocks");

	std::string err;
	Json json = Json::parse(jsonStr, err);
	if (!err.empty())
		return;

	Json::array array = json.array_items();
	if (!array.empty())
		extraBrowserMenuDocksSeparator = ui->menuDocks->addSeparator();

	for (Json &item : array) {
		std::string title = item["title"].string_value();
		std::string url = item["url"].string_value();
		std::string uuid = item["uuid"].string_value();

		AddExtraBrowserDock(title.c_str(), url.c_str(), uuid.c_str(), false);
	}
}

void OBSBasic::SaveExtraBrowserDocks()
{
	Json::array array;
	for (int i = 0; i < extraBrowserDocks.size(); i++) {
		QDockWidget *dock = extraBrowserDocks[i].get();
		QString title = extraBrowserDockNames[i];
		QString url = extraBrowserDockTargets[i];
		QString uuid = dock->property("uuid").toString();
		Json::object obj{
			{"title", QT_TO_UTF8(title)},
			{"url", QT_TO_UTF8(url)},
			{"uuid", QT_TO_UTF8(uuid)},
		};
		array.push_back(obj);
	}

	std::string output = Json(array).dump();
	config_set_string(App()->GetUserConfig(), "BasicWindow", "ExtraBrowserDocks", output.c_str());
}

void OBSBasic::ManageExtraBrowserDocks()
{
	if (!extraBrowsers.isNull()) {
		extraBrowsers->show();
		extraBrowsers->raise();
		return;
	}

	extraBrowsers = new OBSExtraBrowsers(this);
	extraBrowsers->show();
}

void OBSBasic::AddExtraBrowserDock(const QString &title, const QString &url, const QString &uuid, bool firstCreate)
{
	static int panel_version = -1;
	if (panel_version == -1) {
		panel_version = obs_browser_qcef_version();
	}

	BrowserDock *dock = new BrowserDock(title);
	QString bId(uuid.isEmpty() ? QUuid::createUuid().toString() : uuid);
	bId.replace(QRegularExpression("[{}-]"), "");
	dock->setProperty("uuid", bId);
	dock->setObjectName(title + OBJ_NAME_SUFFIX);
	dock->resize(460, 600);
	dock->setMinimumSize(80, 80);
	dock->setWindowTitle(title);
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);

	QCefWidget *browser = cef->create_widget(dock, QT_TO_UTF8(url), nullptr);
	if (browser && panel_version >= 1)
		browser->allowAllPopups(true);

	dock->SetWidget(browser);

	/* Add support for Twitch Dashboard panels */
	if (url.contains("twitch.tv/popout") && url.contains("dashboard/live")) {
		QRegularExpression re("twitch.tv\\/popout\\/([^/]+)\\/");
		QRegularExpressionMatch match = re.match(url);
		QString username = match.captured(1);
		if (username.length() > 0) {
			std::string script;
			script = "Object.defineProperty(document, 'referrer', { get: () => '";
			script += "https://twitch.tv/";
			script += QT_TO_UTF8(username);
			script += "/dashboard/live";
			script += "'});";
			browser->setStartupScript(script);
		}
	}

	AddDockWidget(dock, Qt::RightDockWidgetArea, true);
	extraBrowserDocks.push_back(std::shared_ptr<QDockWidget>(dock));
	extraBrowserDockNames.push_back(title);
	extraBrowserDockTargets.push_back(url);

	if (firstCreate) {
		dock->setFloating(true);

		QPoint curPos = pos();
		QSize wSizeD2 = size() / 2;
		QSize dSizeD2 = dock->size() / 2;

		curPos.setX(curPos.x() + wSizeD2.width() - dSizeD2.width());
		curPos.setY(curPos.y() + wSizeD2.height() - dSizeD2.height());

		dock->move(curPos);
		dock->setVisible(true);
	}
}

void OBSBasic::CreateTempestStreamInfoWebDock()
{
	if (!cef || tempestStreamInfoWebDock)
		return;

	InitBrowserPanelSafeBlock();
	config_t *config = App()->GetUserConfig();
	const char *savedChannel = config_get_string(config, TempestStreamInfoSection, TempestStreamInfoChannelKey);
	QString channel = QString::fromUtf8(savedChannel ? savedChannel : "").trimmed();
	if (channel.startsWith('@'))
		channel.remove(0, 1);
	if (!channel.isEmpty() && !IsValidTwitchChannel(channel))
		channel.clear();

	auto *dock = new TempestStreamInfoBrowserDock(QStringLiteral("Stream Information"));
	dock->setObjectName(QString::fromUtf8(TempestStreamInfoDockName));
	dock->resize(380, 680);
	dock->setMinimumSize(260, 320);
	dock->setWindowTitle(QStringLiteral("Stream Information"));
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);

	auto *surface = new QWidget(dock);
	surface->setObjectName(QStringLiteral("tempestStreamInfoSurface"));
	surface->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestStreamInfoSurface { background: #07131e; color: #bdf6ff; }
		QWidget#tempestStreamInfoHeader { background: #06101a; border-bottom: 1px solid #183a50; }
		QLabel#tempestStreamInfoTitle { color: #45d9ff; font-size: 11px; font-weight: 700; letter-spacing: 2px; }
		QLabel#tempestStreamInfoStatus { color: #748fa4; font-size: 9px; letter-spacing: 1px; }
		QLineEdit { min-height: 28px; padding: 0 8px; color: #bdf6ff; background: #07131e; border: 1px solid #1f506d; }
		QLineEdit:focus { border-color: #45d9ff; }
		QPushButton { min-height: 28px; padding: 0 9px; color: #bdf6ff; background: #0d2230; border: 1px solid #1f506d; font-weight: 700; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
	)"));
	auto *surfaceLayout = new QVBoxLayout(surface);
	surfaceLayout->setContentsMargins(0, 0, 0, 0);
	surfaceLayout->setSpacing(0);
	auto *header = new QWidget(surface);
	header->setObjectName(QStringLiteral("tempestStreamInfoHeader"));
	auto *headerLayout = new QVBoxLayout(header);
	headerLayout->setContentsMargins(8, 7, 8, 7);
	headerLayout->setSpacing(5);
	auto *title = new QLabel(QStringLiteral("TWITCH TRANSMISSION CONSOLE"), header);
	title->setObjectName(QStringLiteral("tempestStreamInfoTitle"));
	headerLayout->addWidget(title);
	auto *channelRow = new QHBoxLayout();
	channelRow->setSpacing(5);
	auto *channelInput = new QLineEdit(channel, header);
	channelInput->setPlaceholderText(QStringLiteral("Twitch channel name"));
	channelInput->setAccessibleName(QStringLiteral("Twitch channel name"));
	channelInput->setToolTip(QStringLiteral(
		"Only the public channel name is stored. Sign-in remains inside Twitch's own browser session."));
	auto *loadChannel = new QPushButton(QStringLiteral("LOAD"), header);
	loadChannel->setAccessibleName(QStringLiteral("Load Twitch stream information channel"));
	channelRow->addWidget(channelInput, 1);
	channelRow->addWidget(loadChannel);
	headerLayout->addLayout(channelRow);
	auto *utilityRow = new QHBoxLayout();
	utilityRow->setSpacing(5);
	auto *status = new QLabel(channel.isEmpty() ? QStringLiteral("DASHBOARD // SIGN IN THROUGH TWITCH")
						    : QStringLiteral("CHANNEL // @%1").arg(channel),
				  header);
	status->setObjectName(QStringLiteral("tempestStreamInfoStatus"));
	status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	auto *dashboard = new QPushButton(QStringLiteral("HOME"), header);
	dashboard->setAccessibleName(QStringLiteral("Open Twitch creator dashboard"));
	auto *reload = new QPushButton(QStringLiteral("RELOAD"), header);
	reload->setAccessibleName(QStringLiteral("Reload Twitch stream information"));
	utilityRow->addWidget(status, 1);
	utilityRow->addWidget(dashboard);
	utilityRow->addWidget(reload);
	headerLayout->addLayout(utilityRow);
	surfaceLayout->addWidget(header);

	QCefWidget *browser = cef->create_widget(surface, QT_TO_UTF8(TwitchStreamInfoUrl(channel)), panel_cookies);
	if (!browser) {
		delete dock;
		return;
	}
	if (obs_browser_qcef_version() >= 1)
		browser->allowAllPopups(true);
	browser->setStartupScript("localStorage.setItem('twilight.theme', 1);");
	surfaceLayout->addWidget(browser, 1);
	dock->setWidget(surface);
	dock->cefWidget.reset(browser);
	dock->EnableContentScaling(QString::fromUtf8(TempestStreamInfoDockName));

	auto loadRequestedChannel = [channelInput, status, browser, config]() {
		QString requested = channelInput->text().trimmed();
		if (requested.startsWith('@'))
			requested.remove(0, 1);
		if (requested.isEmpty()) {
			config_remove_value(config, TempestStreamInfoSection, TempestStreamInfoChannelKey);
			config_save_safe(config, "tmp", nullptr);
			status->setText(QStringLiteral("DASHBOARD // SIGN IN THROUGH TWITCH"));
			browser->setURL("https://dashboard.twitch.tv/");
			return;
		}
		if (!IsValidTwitchChannel(requested)) {
			status->setText(QStringLiteral("INVALID CHANNEL // LETTERS, NUMBERS, UNDERSCORES"));
			channelInput->setFocus();
			channelInput->selectAll();
			return;
		}
		channelInput->setText(requested);
		config_set_string(config, TempestStreamInfoSection, TempestStreamInfoChannelKey, QT_TO_UTF8(requested));
		config_save_safe(config, "tmp", nullptr);
		status->setText(QStringLiteral("CHANNEL // @%1").arg(requested));
		browser->setURL(QT_TO_UTF8(TwitchStreamInfoUrl(requested)));
	};
	connect(loadChannel, &QPushButton::clicked, dock, loadRequestedChannel);
	connect(channelInput, &QLineEdit::returnPressed, dock, loadRequestedChannel);
	connect(dashboard, &QPushButton::clicked, dock, [status, browser]() {
		status->setText(QStringLiteral("DASHBOARD // TWITCH HOME"));
		browser->setURL("https://dashboard.twitch.tv/");
	});
	connect(reload, &QPushButton::clicked, dock, [browser]() { browser->reloadPage(); });

	tempestStreamInfoWebDock = dock;
	AddDockWidget(dock, Qt::RightDockWidgetArea);
	IntegrateTempestStreamInfoDock(dock);
}
#endif

static std::string GenId()
{
	std::random_device rd;
	std::mt19937_64 e2(rd());
	std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFF);

	uint64_t id = dist(e2);

	char id_str[20];
	snprintf(id_str, sizeof(id_str), "%016llX", (unsigned long long)id);
	return std::string(id_str);
}

void CheckExistingCookieId()
{
	OBSBasic *main = OBSBasic::Get();
	if (config_has_user_value(main->Config(), "Panels", "CookieId"))
		return;

	config_set_string(main->Config(), "Panels", "CookieId", GenId().c_str());
}

#ifdef BROWSER_AVAILABLE
static void InitPanelCookieManager()
{
	if (!cef)
		return;
	if (panel_cookies)
		return;

	CheckExistingCookieId();

	OBSBasic *main = OBSBasic::Get();
	const char *cookie_id = config_get_string(main->Config(), "Panels", "CookieId");

	std::string sub_path;
	sub_path += "obs_profile_cookies/";
	sub_path += cookie_id;

	panel_cookies = cef->create_cookie_manager(sub_path);
}
#endif

void DestroyPanelCookieManager()
{
#ifdef BROWSER_AVAILABLE
	if (panel_cookies) {
		panel_cookies->FlushStore();
		delete panel_cookies;
		panel_cookies = nullptr;
	}
#endif
}

void DeleteCookies()
{
#ifdef BROWSER_AVAILABLE
	if (panel_cookies) {
		panel_cookies->DeleteCookies("", "");
	}
#endif
}

void DuplicateCurrentCookieProfile(ConfigFile &config)
{
#ifdef BROWSER_AVAILABLE
	if (cef) {
		OBSBasic *main = OBSBasic::Get();
		std::string cookie_id = config_get_string(main->Config(), "Panels", "CookieId");

		std::string src_path;
		src_path += "obs_profile_cookies/";
		src_path += cookie_id;

		std::string new_id = GenId();

		std::string dst_path;
		dst_path += "obs_profile_cookies/";
		dst_path += new_id;

		BPtr<char> src_path_full = cef->get_cookie_path(src_path);
		BPtr<char> dst_path_full = cef->get_cookie_path(dst_path);

		QDir srcDir(src_path_full.Get());
		QDir dstDir(dst_path_full.Get());

		if (srcDir.exists()) {
			if (!dstDir.exists())
				dstDir.mkdir(dst_path_full.Get());

			QStringList files = srcDir.entryList(QDir::Files);
			for (const QString &file : files) {
				QString src = QString(src_path_full);
				QString dst = QString(dst_path_full);
				src += QDir::separator() + file;
				dst += QDir::separator() + file;
				QFile::copy(src, dst);
			}
		}

		config_set_string(config, "Panels", "CookieId", cookie_id.c_str());
		config_set_string(main->Config(), "Panels", "CookieId", new_id.c_str());
	}
#else
	UNUSED_PARAMETER(config);
#endif
}

void OBSBasic::InitBrowserPanelSafeBlock()
{
#ifdef BROWSER_AVAILABLE
	if (!cef)
		return;
	if (cef->init_browser()) {
		InitPanelCookieManager();
		return;
	}

	ExecThreadedWithoutBlocking([] { cef->wait_for_browser_init(); }, QTStr("BrowserPanelInit.Title"),
				    QTStr("BrowserPanelInit.Text"));
	InitPanelCookieManager();
#endif
}
