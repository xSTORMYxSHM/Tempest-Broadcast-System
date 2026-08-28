#include "OBSAbout.hpp"

#include <widgets/OBSBasic.hpp>
#include <utility/RemoteTextThread.hpp>

#include <qt-wrappers.hpp>

#include <json11.hpp>

#include "moc_OBSAbout.cpp"

using namespace json11;

extern bool steam;

OBSAbout::OBSAbout(QWidget *parent) : QDialog(parent), ui(new Ui::OBSAbout)
{
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	ui->setupUi(this);

	QString bitness;

	if (sizeof(void *) == 4) {
		bitness = " (32 bit)";
	} else if (sizeof(void *) == 8) {
		bitness = " (64 bit)";
	}

	QString ver = obs_get_version_string();

	ui->version->setText(ver + bitness);

	if (steam) {
		delete ui->donate;
		ui->donate = nullptr;
	} else {
		ui->donate->setText("&nbsp;&nbsp;<a href='https://obsproject.com/contribute'>" + QTStr("About.Donate") +
				    "</a>");
		ui->donate->setTextInteractionFlags(Qt::TextBrowserInteraction);
		ui->donate->setOpenExternalLinks(true);
	}

	ui->getInvolved->setText("&nbsp;&nbsp;<a href='https://obsproject.com/developer-contributing'>" +
				 QTStr("About.GetInvolved") + "</a>");
	ui->getInvolved->setTextInteractionFlags(Qt::TextBrowserInteraction);
	ui->getInvolved->setOpenExternalLinks(true);

	ui->about->setText("<a href='#'>" + QTStr("About.Tempest") + "</a>");
	ui->obsProject->setText("<a href='#'>" + QTStr("About.OBSProject") + "</a>");
	ui->authors->setText("<a href='#'>" + QTStr("About.Authors") + "</a>");
	ui->license->setText("<a href='#'>" + QTStr("About.License") + "</a>");

	ui->name->setProperty("class", "text-heading");
	ui->version->setProperty("class", "text-large");
	ui->about->setProperty("class", "bg-base");
	ui->obsProject->setProperty("class", "bg-base");
	ui->authors->setProperty("class", "bg-base");
	ui->license->setProperty("class", "bg-base");
	ui->info->setProperty("class", "");

	connect(ui->about, &ClickableLabel::clicked, this, &OBSAbout::ShowTempest);
	connect(ui->obsProject, &ClickableLabel::clicked, this, &OBSAbout::ShowOBSProject);
	connect(ui->authors, &ClickableLabel::clicked, this, &OBSAbout::ShowAuthors);
	connect(ui->license, &ClickableLabel::clicked, this, &OBSAbout::ShowLicense);

	ShowTempest();
}

void OBSAbout::SetOBSResourcesVisible(bool visible)
{
	ui->contribute->setVisible(visible);
	if (ui->donate)
		ui->donate->setVisible(visible);
	ui->getInvolved->setVisible(visible);
}

void OBSAbout::ShowTempest()
{
	activePage = QStringLiteral("tempest");
	ui->info->setVisible(true);
	ui->info->setText(QTStr("About.Info"));
	SetOBSResourcesVisible(false);
	ui->textBrowser->setHtml(QStringLiteral(
		"<h2>Tempest Broadcast System</h2>"
		"<p>A focused live-production environment that combines the OBS broadcast engine with a scalable "
		"command interface, modular reactive overlays, asset workflows, and direct automation bridges.</p>"
		"<h3>Integrated systems</h3>"
		"<ul><li>Scene routing and source operations</li>"
		"<li>Audio-, event-, and color-reactive overlay elements</li>"
		"<li>Content profiles, message libraries, and reusable assets</li>"
		"<li>Studio, Warudo, WebSocket, and external-control workflows</li></ul>"
		"<p>Tempest branding and workflow features are maintained separately from the upstream OBS Project. "
		"Upstream attribution and resources are available on the <b>OBS Project</b> tab.</p>"));
}

void OBSAbout::ShowOBSProject()
{
	activePage = QStringLiteral("obs");
	ui->info->setVisible(true);
	ui->info->setText(QTStr("About.OBSInfo"));
	ui->contribute->setText(QTStr("About.OBSResources"));
	SetOBSResourcesVisible(true);

	OBSBasic *main = OBSBasic::Get();
	if (main->patronJson.empty() && !main->patronJsonThread) {
		RemoteTextThread *thread =
			new RemoteTextThread("https://obsproject.com/patreon/about-box.json", "application/json");
		QObject::connect(thread, &RemoteTextThread::Result, main, &OBSBasic::UpdatePatronJson);
		QObject::connect(thread, &RemoteTextThread::Result, this, [this]() {
			if (activePage == QStringLiteral("obs"))
				ShowOBSProject();
		});
		main->patronJsonThread.reset(thread);
		thread->start();
	}

	QString text = QStringLiteral(
		"<h2>OBS Studio</h2>"
		"<p>Tempest Broadcast System is a modified distribution of the open-source OBS Studio project. "
		"Learn more at <a href=\"https://obsproject.com\">obsproject.com</a> or review the "
		"<a href=\"https://github.com/obsproject/obs-studio\">upstream source repository</a>.</p>");
	if (main->patronJson.empty()) {
		text += QStringLiteral("<p>Upstream contributor information is loading.</p>");
		ui->textBrowser->setHtml(text);
		return;
	}
	std::string error;
	Json json = Json::parse(main->patronJson, error);
	const Json::array &patrons = json.array_items();

	text += "<h2>Top Patreon contributors</h2>";
	text += "<p style=\"font-size:16px;\">";
	bool first = true;
	bool top = true;

	for (const Json &patron : patrons) {
		std::string name = patron["name"].string_value();
		std::string link = patron["link"].string_value();
		int amount = patron["amount"].int_value();

		if (top && amount < 5000) {
			text += "</p>";
			top = false;
		} else if (!first) {
			text += "<br/>";
		}

		if (!link.empty()) {
			text += "<a href=\"";
			text += QT_UTF8(link.c_str()).toHtmlEscaped();
			text += "\">";
		}
		text += QT_UTF8(name.c_str()).toHtmlEscaped();
		if (!link.empty()) {
			text += "</a>";
		}

		if (first) {
			first = false;
		}
	}

	ui->textBrowser->setHtml(text);
}

void OBSAbout::ShowAuthors()
{
	activePage = QStringLiteral("authors");
	ui->info->setVisible(false);
	SetOBSResourcesVisible(false);
	std::string path;
	QString error = QTStr("About.Error").arg("https://github.com/obsproject/obs-studio/blob/master/AUTHORS");

#ifdef __APPLE__
	if (!GetDataFilePath("AUTHORS", path)) {
#else
	if (!GetDataFilePath("authors/AUTHORS", path)) {
#endif
		ui->textBrowser->setPlainText(error);
		return;
	}

	ui->textBrowser->setPlainText(QString::fromStdString(path));

	BPtr<char> text = os_quick_read_utf8_file(path.c_str());

	if (!text || !*text) {
		ui->textBrowser->setPlainText(error);
		return;
	}

	ui->textBrowser->setPlainText(QT_UTF8(text));
}

void OBSAbout::ShowLicense()
{
	activePage = QStringLiteral("license");
	ui->info->setVisible(false);
	SetOBSResourcesVisible(false);
	std::string path;
	QString error = QTStr("About.Error").arg("https://github.com/obsproject/obs-studio/blob/master/COPYING");

	if (!GetDataFilePath("license/gplv2.txt", path)) {
		ui->textBrowser->setPlainText(error);
		return;
	}

	BPtr<char> text = os_quick_read_utf8_file(path.c_str());

	if (!text || !*text) {
		ui->textBrowser->setPlainText(error);
		return;
	}

	ui->textBrowser->setPlainText(QT_UTF8(text));
}
