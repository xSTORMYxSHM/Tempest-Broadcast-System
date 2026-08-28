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
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QUuid>
#include <QVBoxLayout>

using namespace json11;

namespace {
constexpr char TempestStreamInfoSection[] = "TempestStreamInfo";
constexpr char TempestStreamInfoChannelKey[] = "Channel";
constexpr char TempestStreamInfoViewKey[] = "View";
constexpr char TempestStreamInfoActivityUuidKey[] = "ActivityUuid";
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

QString NormalizeTwitchView(const QString &view, const QString &channel)
{
	if (view == QStringLiteral("dashboard"))
		return view;
	if (channel.isEmpty())
		return QStringLiteral("dashboard");
	if (view == QStringLiteral("chat") || view == QStringLiteral("activity") || view == QStringLiteral("info"))
		return view;
	return QStringLiteral("info");
}

QString TwitchOperationsUrl(const QString &channel, const QString &view, const QString &activityUuid)
{
	if (view == QStringLiteral("chat"))
		return QStringLiteral("https://www.twitch.tv/popout/%1/chat").arg(channel);
	if (view == QStringLiteral("activity"))
		return QStringLiteral("https://dashboard.twitch.tv/popout/u/%1/stream-manager/activity-feed?uuid=%2")
			.arg(channel, activityUuid);
	if (view == QStringLiteral("info"))
		return QStringLiteral("https://dashboard.twitch.tv/popout/u/%1/stream-manager/edit-stream-info")
			.arg(channel);
	return QStringLiteral("https://dashboard.twitch.tv/");
}

QString TwitchOperationsStatus(const QString &channel, const QString &view)
{
	if (view == QStringLiteral("chat"))
		return QStringLiteral("CHAT RELAY // @%1").arg(channel);
	if (view == QStringLiteral("activity"))
		return QStringLiteral("ACTIVITY FEED // @%1").arg(channel);
	if (view == QStringLiteral("info"))
		return QStringLiteral("STREAM INFO // @%1").arg(channel);
	return channel.isEmpty() ? QStringLiteral("DASHBOARD // SIGN IN THROUGH TWITCH")
				 : QStringLiteral("DASHBOARD // @%1").arg(channel);
}

QString TwitchOperationsStartupScript(const QString &channel)
{
	QString script = QStringLiteral("localStorage.setItem('twilight.theme', 1);");
	if (!channel.isEmpty()) {
		script += QStringLiteral("try { Object.defineProperty(document, 'referrer', { get: () => "
					 "'https://www.twitch.tv/%1/dashboard/live' }); } catch (error) {}")
				  .arg(channel);
	}
	return script;
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
	if (!err.empty()) {
		return;
	}

	Json::array array = json.array_items();
	if (!array.empty()) {
		extraBrowserMenuDocksSeparator = ui->menuDocks->addSeparator();
	}

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
	if (browser && panel_version >= 1) {
		browser->allowAllPopups(true);
	}

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
			script += username.toStdString();
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
	const char *savedView = config_get_string(config, TempestStreamInfoSection, TempestStreamInfoViewKey);
	const QString activeView = NormalizeTwitchView(QString::fromUtf8(savedView ? savedView : ""), channel);
	const char *savedActivityUuid =
		config_get_string(config, TempestStreamInfoSection, TempestStreamInfoActivityUuidKey);
	QString activityUuid = QString::fromUtf8(savedActivityUuid ? savedActivityUuid : "");
	if (activityUuid.isEmpty()) {
		activityUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
		config_set_string(config, TempestStreamInfoSection, TempestStreamInfoActivityUuidKey,
				  QT_TO_UTF8(activityUuid));
		config_save_safe(config, "tmp", nullptr);
	}

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
		QPushButton:checked { color: #ffffff; border-color: #45d9ff; background: #0c456b; }
		QPushButton:disabled { color: #40576a; border-color: #172d3d; background: #091721; }
	)"));
	auto *surfaceLayout = new QVBoxLayout(surface);
	surfaceLayout->setContentsMargins(0, 0, 0, 0);
	surfaceLayout->setSpacing(0);
	auto *header = new QWidget(surface);
	header->setObjectName(QStringLiteral("tempestStreamInfoHeader"));
	auto *headerLayout = new QVBoxLayout(header);
	headerLayout->setContentsMargins(8, 7, 8, 7);
	headerLayout->setSpacing(5);
	auto *title = new QLabel(QStringLiteral("BROADCAST OPERATIONS CONSOLE"), header);
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
	loadChannel->setAccessibleName(QStringLiteral("Load Twitch operations channel"));
	channelRow->addWidget(channelInput, 1);
	channelRow->addWidget(loadChannel);
	headerLayout->addLayout(channelRow);
	auto *viewRow = new QHBoxLayout();
	viewRow->setSpacing(5);
	auto *streamInfo = new QPushButton(QStringLiteral("STREAM INFO"), header);
	auto *chat = new QPushButton(QStringLiteral("CHAT"), header);
	auto *activity = new QPushButton(QStringLiteral("ACTIVITY"), header);
	streamInfo->setAccessibleName(QStringLiteral("Open Twitch stream information"));
	chat->setAccessibleName(QStringLiteral("Open Twitch operator chat"));
	activity->setAccessibleName(QStringLiteral("Open Twitch activity feed"));
	auto *viewGroup = new QButtonGroup(header);
	viewGroup->setExclusive(true);
	for (QPushButton *button : {streamInfo, chat, activity}) {
		button->setCheckable(true);
		button->setEnabled(!channel.isEmpty());
		viewGroup->addButton(button);
		viewRow->addWidget(button, 1);
	}
	streamInfo->setChecked(activeView == QStringLiteral("info"));
	chat->setChecked(activeView == QStringLiteral("chat"));
	activity->setChecked(activeView == QStringLiteral("activity"));
	headerLayout->addLayout(viewRow);
	auto *utilityRow = new QHBoxLayout();
	utilityRow->setSpacing(5);
	auto *status = new QLabel(TwitchOperationsStatus(channel, activeView), header);
	status->setObjectName(QStringLiteral("tempestStreamInfoStatus"));
	status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	auto *dashboard = new QPushButton(QStringLiteral("HOME"), header);
	dashboard->setAccessibleName(QStringLiteral("Open Twitch creator dashboard"));
	auto *reload = new QPushButton(QStringLiteral("RELOAD"), header);
	reload->setAccessibleName(QStringLiteral("Reload Twitch operations view"));
	utilityRow->addWidget(status, 1);
	utilityRow->addWidget(dashboard);
	utilityRow->addWidget(reload);
	headerLayout->addLayout(utilityRow);
	surfaceLayout->addWidget(header);

	QCefWidget *browser = cef->create_widget(
		surface, QT_TO_UTF8(TwitchOperationsUrl(channel, activeView, activityUuid)), panel_cookies);
	if (!browser) {
		delete dock;
		return;
	}
	if (obs_browser_qcef_version() >= 1)
		browser->allowAllPopups(true);
	browser->setStartupScript(QT_TO_UTF8(TwitchOperationsStartupScript(channel)));
	cef->add_popup_whitelist_url("about:blank#blocked", dock);
	if (!channel.isEmpty()) {
		const QString moderationUrl =
			QStringLiteral("https://www.twitch.tv/%1/dashboard/settings/moderation?no-reload=true")
				.arg(channel);
		cef->add_force_popup_url(QT_TO_UTF8(moderationUrl), dock);
	}
	surfaceLayout->addWidget(browser, 1);
	dock->setWidget(surface);
	dock->cefWidget.reset(browser);
	dock->EnableContentScaling(QString::fromUtf8(TempestStreamInfoDockName));

	auto selectView = [dock, channelInput, status, browser, config, streamInfo, chat, activityUuid,
			   activity](const QString &requestedView) {
		QString selectedChannel = channelInput->text().trimmed();
		if (selectedChannel.startsWith('@'))
			selectedChannel.remove(0, 1);
		const QString view = NormalizeTwitchView(requestedView, selectedChannel);
		if (view != QStringLiteral("dashboard") && !IsValidTwitchChannel(selectedChannel)) {
			status->setText(QStringLiteral("CHANNEL REQUIRED // ENTER NAME AND LOAD"));
			channelInput->setFocus();
			return;
		}

		const bool channelValid = IsValidTwitchChannel(selectedChannel);
		streamInfo->setEnabled(channelValid);
		chat->setEnabled(channelValid);
		activity->setEnabled(channelValid);
		streamInfo->setChecked(view == QStringLiteral("info"));
		chat->setChecked(view == QStringLiteral("chat"));
		activity->setChecked(view == QStringLiteral("activity"));
		dock->setProperty("tempestTwitchView", view);
		config_set_string(config, TempestStreamInfoSection, TempestStreamInfoViewKey, QT_TO_UTF8(view));
		config_save_safe(config, "tmp", nullptr);
		status->setText(TwitchOperationsStatus(selectedChannel, view));
		browser->setStartupScript(QT_TO_UTF8(TwitchOperationsStartupScript(selectedChannel)));
		if (channelValid) {
			const QString moderationUrl =
				QStringLiteral("https://www.twitch.tv/%1/dashboard/settings/moderation?no-reload=true")
					.arg(selectedChannel);
			cef->add_force_popup_url(QT_TO_UTF8(moderationUrl), dock);
		}
		browser->setURL(QT_TO_UTF8(TwitchOperationsUrl(selectedChannel, view, activityUuid)));
	};

	auto loadRequestedChannel = [channelInput, status, config, selectView]() {
		QString requested = channelInput->text().trimmed();
		if (requested.startsWith('@'))
			requested.remove(0, 1);
		if (requested.isEmpty()) {
			config_remove_value(config, TempestStreamInfoSection, TempestStreamInfoChannelKey);
			channelInput->clear();
			selectView(QStringLiteral("dashboard"));
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
		selectView(QStringLiteral("info"));
	};
	connect(loadChannel, &QPushButton::clicked, dock, loadRequestedChannel);
	connect(channelInput, &QLineEdit::returnPressed, dock, loadRequestedChannel);
	connect(streamInfo, &QPushButton::clicked, dock, [selectView]() { selectView(QStringLiteral("info")); });
	connect(chat, &QPushButton::clicked, dock, [selectView]() { selectView(QStringLiteral("chat")); });
	connect(activity, &QPushButton::clicked, dock, [selectView]() { selectView(QStringLiteral("activity")); });
	connect(dashboard, &QPushButton::clicked, dock, [selectView]() { selectView(QStringLiteral("dashboard")); });
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
	if (config_has_user_value(main->Config(), "Panels", "CookieId")) {
		return;
	}

	config_set_string(main->Config(), "Panels", "CookieId", GenId().c_str());
}

#ifdef BROWSER_AVAILABLE
static void InitPanelCookieManager()
{
	if (!cef) {
		return;
	}
	if (panel_cookies) {
		return;
	}

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
			if (!dstDir.exists()) {
				dstDir.mkdir(dst_path_full.Get());
			}

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
	if (!cef) {
		return;
	}
	if (cef->init_browser()) {
		InitPanelCookieManager();
		return;
	}

	ExecThreadedWithoutBlocking([] { cef->wait_for_browser_init(); }, QTStr("BrowserPanelInit.Title"),
				    QTStr("BrowserPanelInit.Text"));
	InitPanelCookieManager();
#endif
}
