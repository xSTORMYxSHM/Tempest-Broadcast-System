#include "TempestControlDeck.hpp"

#include <OBSApp.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>

#include <QDateTime>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>

namespace {
constexpr char ConfigSection[] = "TempestControlDeck";
constexpr char OverlaySourceName[] = "Tempest // Starting Soon";

QString ConfigString(const char *key, const char *fallback)
{
	config_t *config = App()->GetUserConfig();
	const char *value = config_get_string(config, ConfigSection, key);
	return value && *value ? QString::fromUtf8(value) : QString::fromUtf8(fallback);
}
} // namespace

TempestControlDeck::TempestControlDeck(QWidget *parent) : OBSDock(parent)
{
	setObjectName(QStringLiteral("tempestControlDeck"));
	setWindowTitle(QStringLiteral("Tempest Control Deck"));
	setMinimumWidth(340);

	BuildInterface();
	LoadState();
	EnsureOverlayDirectory();

	renderDebounce = new QTimer(this);
	renderDebounce->setSingleShot(true);
	renderDebounce->setInterval(350);
	connect(renderDebounce, &QTimer::timeout, this, &TempestControlDeck::RenderOverlay);

	clockTimer = new QTimer(this);
	clockTimer->setInterval(250);
	connect(clockTimer, &QTimer::timeout, this, &TempestControlDeck::UpdateCountdownPreview);
	clockTimer->start();

	connect(streamTitle, &QLineEdit::textChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(statusLine, &QLineEdit::textChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(rotationMessages, &QPlainTextEdit::textChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(rotationSeconds, &QSpinBox::valueChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(countdownMinutes, &QSpinBox::valueChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(startCountdownButton, &QPushButton::clicked, this, &TempestControlDeck::StartCountdown);
	connect(resetCountdownButton, &QPushButton::clicked, this, &TempestControlDeck::ResetCountdown);
	connect(createSourceButton, &QPushButton::clicked, this, &TempestControlDeck::CreateOrUpdateSource);

	RenderOverlay();
	UpdateCountdownPreview();
}

void TempestControlDeck::BuildInterface()
{
	QWidget *body = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(body);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(10);

	QLabel *header = new QLabel(QStringLiteral(
		"<span style='color:#45d9ff;font-size:16px;font-weight:700;'>TEMPEST // CONTROL DECK</span>"
		"<br><span style='color:#748fa4;'>Broadcast overlay uplink</span>"),
		body);
	header->setTextFormat(Qt::RichText);
	layout->addWidget(header);

	QFrame *rule = new QFrame(body);
	rule->setFrameShape(QFrame::HLine);
	rule->setProperty("class", "separator");
	layout->addWidget(rule);

	QFormLayout *form = new QFormLayout();
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	streamTitle = new QLineEdit(body);
	streamTitle->setPlaceholderText(QStringLiteral("Storm Horizon Radio"));
	form->addRow(QStringLiteral("Transmission"), streamTitle);

	statusLine = new QLineEdit(body);
	statusLine->setPlaceholderText(QStringLiteral("Operator link // standby"));
	form->addRow(QStringLiteral("Status"), statusLine);

	rotationMessages = new QPlainTextEdit(body);
	rotationMessages->setPlaceholderText(QStringLiteral("One rotating message per line"));
	rotationMessages->setMinimumHeight(92);
	form->addRow(QStringLiteral("Rotating lines"), rotationMessages);

	rotationSeconds = new QSpinBox(body);
	rotationSeconds->setRange(2, 30);
	rotationSeconds->setSuffix(QStringLiteral(" sec"));
	form->addRow(QStringLiteral("Rotation speed"), rotationSeconds);

	countdownMinutes = new QSpinBox(body);
	countdownMinutes->setRange(1, 180);
	countdownMinutes->setSuffix(QStringLiteral(" min"));
	form->addRow(QStringLiteral("Countdown"), countdownMinutes);
	layout->addLayout(form);

	QHBoxLayout *countdownRow = new QHBoxLayout();
	startCountdownButton = new QPushButton(QStringLiteral("Start / Restart"), body);
	resetCountdownButton = new QPushButton(QStringLiteral("Clear"), body);
	countdownRow->addWidget(startCountdownButton, 2);
	countdownRow->addWidget(resetCountdownButton, 1);
	layout->addLayout(countdownRow);

	countdownPreview = new QLabel(QStringLiteral("UPLINK PENDING"), body);
	countdownPreview->setAlignment(Qt::AlignCenter);
	countdownPreview->setStyleSheet(QStringLiteral(
		"QLabel { color:#bdf6ff; background:#06131f; border:1px solid #0c7ccb; padding:10px; "
		"font-size:22px; font-weight:700; letter-spacing:2px; }"));
	layout->addWidget(countdownPreview);

	createSourceButton = new QPushButton(QStringLiteral("Create / Update Starting Soon Source"), body);
	createSourceButton->setMinimumHeight(38);
	createSourceButton->setProperty("class", "primary");
	layout->addWidget(createSourceButton);

	outputPathLabel = new QLabel(body);
	outputPathLabel->setWordWrap(true);
	outputPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	outputPathLabel->setStyleSheet(QStringLiteral("color:#748fa4; font-size:11px;"));
	layout->addWidget(outputPathLabel);

	statusLabel = new QLabel(QStringLiteral("Initializing overlay link..."), body);
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	layout->addStretch(1);

	setWidget(body);
}

void TempestControlDeck::LoadState()
{
	config_t *config = App()->GetUserConfig();
	streamTitle->setText(ConfigString("StreamTitle", "STORM HORIZON RADIO"));
	statusLine->setText(ConfigString("StatusLine", "OPERATOR LINK // STANDBY"));
	rotationMessages->setPlainText(ConfigString(
		"RotationMessages",
		"INDEXING FRACTAL ARCHIVE\nCALIBRATING STORM HORIZON SIGNAL\nTEMPEST FIELD SYNCHRONIZATION ACTIVE"));

	int rotation = (int)config_get_int(config, ConfigSection, "RotationSeconds");
	rotationSeconds->setValue(rotation > 0 ? rotation : 6);
	int minutes = (int)config_get_int(config, ConfigSection, "CountdownMinutes");
	countdownMinutes->setValue(minutes > 0 ? minutes : 10);
	countdownEndMs = config_get_int(config, ConfigSection, "CountdownEndMs");
	countdownRunning = config_get_bool(config, ConfigSection, "CountdownRunning");
}

void TempestControlDeck::SaveState()
{
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, "StreamTitle", streamTitle->text().toUtf8().constData());
	config_set_string(config, ConfigSection, "StatusLine", statusLine->text().toUtf8().constData());
	config_set_string(config, ConfigSection, "RotationMessages", rotationMessages->toPlainText().toUtf8().constData());
	config_set_int(config, ConfigSection, "RotationSeconds", rotationSeconds->value());
	config_set_int(config, ConfigSection, "CountdownMinutes", countdownMinutes->value());
	config_set_int(config, ConfigSection, "CountdownEndMs", countdownEndMs);
	config_set_bool(config, ConfigSection, "CountdownRunning", countdownRunning);
	config_save_safe(config, "tmp", nullptr);
}

bool TempestControlDeck::EnsureOverlayDirectory()
{
	char path[1024];
	if (GetAppConfigPath(path, sizeof(path), "tempest-broadcast-system/control-deck") <= 0) {
		SetStatus(QStringLiteral("Unable to resolve the Tempest configuration directory."), true);
		return false;
	}

	overlayDirectory = QString::fromUtf8(path);
	if (!QDir().mkpath(overlayDirectory)) {
		SetStatus(QStringLiteral("Unable to create the Control Deck output directory."), true);
		return false;
	}

	overlayPath = QDir(overlayDirectory).filePath(QStringLiteral("starting-soon.html"));
	outputPathLabel->setText(QStringLiteral("Overlay file: %1").arg(QDir::toNativeSeparators(overlayPath)));
	return true;
}

void TempestControlDeck::QueueOverlayRender()
{
	if (renderDebounce)
		renderDebounce->start();
}

void TempestControlDeck::RenderOverlay()
{
	SaveState();
	if (overlayPath.isEmpty() && !EnsureOverlayDirectory())
		return;

	QSaveFile file(overlayPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		SetStatus(QStringLiteral("Overlay write failed: %1").arg(file.errorString()), true);
		return;
	}

	QByteArray html = BuildOverlayHtml().toUtf8();
	if (file.write(html) != html.size() || !file.commit()) {
		SetStatus(QStringLiteral("Overlay save failed: %1").arg(file.errorString()), true);
		return;
	}

	++renderRevision;
	RefreshExistingSource();
	SetStatus(QStringLiteral("Overlay synchronized // revision %1").arg(renderRevision));
}

QString TempestControlDeck::BuildOverlayHtml() const
{
	QJsonArray messages;
	const QStringList lines = rotationMessages->toPlainText().split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
									      Qt::SkipEmptyParts);
	for (const QString &line : lines) {
		const QString trimmed = line.trimmed();
		if (!trimmed.isEmpty())
			messages.append(trimmed);
	}
	if (messages.isEmpty())
		messages.append(QStringLiteral("TEMPEST MAINFRAME ONLINE"));

	QJsonObject state;
	state.insert(QStringLiteral("title"), streamTitle->text().trimmed());
	state.insert(QStringLiteral("status"), statusLine->text().trimmed());
	state.insert(QStringLiteral("messages"), messages);
	state.insert(QStringLiteral("rotationMs"), rotationSeconds->value() * 1000);
	state.insert(QStringLiteral("countdownEndMs"), countdownRunning ? (double)countdownEndMs : 0.0);
	QString stateJson = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
	stateJson.replace(QStringLiteral("</"), QStringLiteral("<\\/"));

	QString html = QString::fromUtf8(R"TEMPEST(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--void:#03090f;--panel:rgba(5,16,26,.84);--cyan:#45d9ff;--ice:#bdf6ff;--blue:#0c7ccb;--deep:#06345d;--muted:#748fa4;--danger:#ff4b70}
*{box-sizing:border-box}html,body{width:100%;height:100%;margin:0;overflow:hidden;background:transparent;color:var(--ice);font-family:"Segoe UI",Arial,sans-serif}
body:before{content:"";position:absolute;inset:0;background:repeating-linear-gradient(0deg,rgba(69,217,255,.025) 0,rgba(69,217,255,.025) 1px,transparent 1px,transparent 5px);pointer-events:none}
.frame{position:absolute;inset:4.5%;border:1px solid rgba(69,217,255,.24);clip-path:polygon(0 0,18% 0,19% 1px,81% 1px,82% 0,100% 0,100% 100%,82% 100%,81% calc(100% - 1px),19% calc(100% - 1px),18% 100%,0 100%)}
.frame:before,.frame:after{content:"";position:absolute;width:16%;height:2px;top:-1px;background:linear-gradient(90deg,transparent,var(--cyan),transparent);animation:sweep 4s linear infinite}.frame:after{top:auto;bottom:-1px;right:0;animation-direction:reverse}
.top{position:absolute;left:6.5%;right:6.5%;top:6.5%;display:flex;justify-content:space-between;align-items:center;font-size:clamp(11px,1vw,18px);letter-spacing:.22em;text-transform:uppercase;color:var(--muted)}
.sig{display:flex;align-items:center;gap:12px;color:var(--cyan);font-weight:700}.core{width:14px;height:14px;border:2px solid var(--cyan);transform:rotate(45deg);box-shadow:0 0 18px var(--cyan);animation:pulse 1.8s ease-in-out infinite}
.status{padding:8px 14px;border:1px solid rgba(69,217,255,.35);background:rgba(3,9,15,.66)}
.hero{position:absolute;left:10%;right:10%;top:24%;text-align:center;text-transform:uppercase}
.kicker{font-size:clamp(13px,1.2vw,22px);letter-spacing:.48em;color:var(--cyan);margin-bottom:18px}
.title{font-size:clamp(40px,6vw,112px);line-height:.92;font-weight:800;letter-spacing:.06em;text-shadow:0 0 30px rgba(69,217,255,.32)}
.rule{height:1px;width:72%;margin:28px auto;background:linear-gradient(90deg,transparent,var(--blue),var(--cyan),var(--blue),transparent);position:relative}.rule:after{content:"";position:absolute;left:50%;top:-4px;width:9px;height:9px;background:var(--ice);transform:rotate(45deg);box-shadow:0 0 16px var(--cyan)}
.countdown{font-variant-numeric:tabular-nums;font-size:clamp(54px,8vw,148px);line-height:1;font-weight:300;letter-spacing:.12em;color:var(--ice);text-shadow:0 0 24px rgba(69,217,255,.48)}
.message{margin-top:22px;min-height:1.5em;font-size:clamp(15px,1.5vw,28px);letter-spacing:.3em;color:var(--cyan);transition:opacity .24s ease}
.bottom{position:absolute;left:7%;right:7%;bottom:7%;display:flex;align-items:center;gap:18px;color:var(--muted);font-size:clamp(10px,.9vw,16px);letter-spacing:.18em}.line{height:1px;flex:1;background:linear-gradient(90deg,var(--deep),var(--cyan),var(--deep))}.packet{color:var(--cyan)}
.corner{position:absolute;width:54px;height:54px;border-color:var(--cyan);opacity:.72}.c1{left:4.5%;top:4.5%;border-left:3px solid;border-top:3px solid}.c2{right:4.5%;top:4.5%;border-right:3px solid;border-top:3px solid}.c3{left:4.5%;bottom:4.5%;border-left:3px solid;border-bottom:3px solid}.c4{right:4.5%;bottom:4.5%;border-right:3px solid;border-bottom:3px solid}
@keyframes pulse{50%{opacity:.45;box-shadow:0 0 7px var(--cyan)}}@keyframes sweep{from{transform:translateX(-15%)}to{transform:translateX(620%)}}
</style>
</head>
<body>
<div class="frame"></div><i class="corner c1"></i><i class="corner c2"></i><i class="corner c3"></i><i class="corner c4"></i>
<header class="top"><div class="sig"><i class="core"></i><span>TEMPEST MAINFRAME // BROADCAST UPLINK</span></div><div class="status" id="status"></div></header>
<main class="hero"><div class="kicker">Transmission initializing</div><div class="title" id="title"></div><div class="rule"></div><div class="countdown" id="countdown"></div><div class="message" id="message"></div></main>
<footer class="bottom"><span>ARCHIVE NODE 2526</span><i class="line"></i><span class="packet">SIGNAL LOCKED</span><i class="line"></i><span>OPERATOR xSTORMYx</span></footer>
<script id="tempest-state" type="application/json">{{STATE_JSON}}</script>
<script>
const state=JSON.parse(document.getElementById('tempest-state').textContent);const title=document.getElementById('title'),status=document.getElementById('status'),countdown=document.getElementById('countdown'),message=document.getElementById('message');
title.textContent=state.title||'STORM HORIZON RADIO';status.textContent=state.status||'OPERATOR LINK // STANDBY';let index=0;
function rotate(){message.style.opacity='0';setTimeout(()=>{message.textContent=state.messages[index++%state.messages.length];message.style.opacity='1'},240)}rotate();setInterval(rotate,Math.max(2000,state.rotationMs||6000));
function tick(){if(!state.countdownEndMs){countdown.textContent='UPLINK PENDING';return}const remaining=Math.max(0,state.countdownEndMs-Date.now()),seconds=Math.ceil(remaining/1000),minutes=Math.floor(seconds/60),secs=seconds%60;countdown.textContent=remaining<=0?'UPLINK READY':String(minutes).padStart(2,'0')+':'+String(secs).padStart(2,'0')}tick();setInterval(tick,250);
</script>
</body>
</html>)TEMPEST");
	html.replace(QStringLiteral("{{STATE_JSON}}"), stateJson);
	return html;
}

void TempestControlDeck::StartCountdown()
{
	countdownEndMs = QDateTime::currentMSecsSinceEpoch() + (qint64)countdownMinutes->value() * 60 * 1000;
	countdownRunning = true;
	RenderOverlay();
	UpdateCountdownPreview();
}

void TempestControlDeck::ResetCountdown()
{
	countdownRunning = false;
	countdownEndMs = 0;
	RenderOverlay();
	UpdateCountdownPreview();
}

void TempestControlDeck::UpdateCountdownPreview()
{
	if (!countdownRunning || countdownEndMs <= 0) {
		countdownPreview->setText(QStringLiteral("UPLINK PENDING"));
		return;
	}

	qint64 remaining = countdownEndMs - QDateTime::currentMSecsSinceEpoch();
	if (remaining <= 0) {
		countdownPreview->setText(QStringLiteral("UPLINK READY"));
		return;
	}

	qint64 seconds = (remaining + 999) / 1000;
	qint64 minutes = seconds / 60;
	seconds %= 60;
	countdownPreview->setText(QStringLiteral("%1:%2")
					  .arg(minutes, 2, 10, QChar('0'))
					  .arg(seconds, 2, 10, QChar('0')));
}

void TempestControlDeck::ApplySourceSettings(obs_source_t *source)
{
	if (!source)
		return;

	obs_video_info ovi{};
	int width = 1920;
	int height = 1080;
	if (obs_get_video_info(&ovi)) {
		width = (int)ovi.base_width;
		height = (int)ovi.base_height;
	}

	OBSDataAutoRelease settings = obs_data_create();
	QByteArray path = QDir::toNativeSeparators(overlayPath).toUtf8();
	QByteArray css = QStringLiteral(
				 "body{background:rgba(0,0,0,0);margin:0;overflow:hidden;}/* tempest-revision:%1 */")
				 .arg(renderRevision)
				 .toUtf8();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", path.constData());
	obs_data_set_int(settings, "width", width);
	obs_data_set_int(settings, "height", height);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_data_set_bool(settings, "reroute_audio", false);
	obs_data_set_string(settings, "css", css.constData());
	obs_source_update(source, settings);
}

void TempestControlDeck::RefreshExistingSource()
{
	OBSSourceAutoRelease source = obs_get_source_by_name(OverlaySourceName);
	if (source && strcmp(obs_source_get_unversioned_id(source), "browser_source") == 0)
		ApplySourceSettings(source);
}

void TempestControlDeck::CreateOrUpdateSource()
{
	RenderOverlay();
	OBSBasic *main = OBSBasic::Get();
	OBSScene scene = main ? main->GetCurrentScene() : nullptr;
	if (!scene) {
		SetStatus(QStringLiteral("No active scene is available for the overlay source."), true);
		return;
	}

	OBSSourceAutoRelease source = obs_get_source_by_name(OverlaySourceName);
	if (source) {
		if (strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0) {
			SetStatus(QStringLiteral("A non-browser source already uses the name '%1'.")
					  .arg(QString::fromUtf8(OverlaySourceName)),
				  true);
			return;
		}
		ApplySourceSettings(source);
	} else {
		const char *sourceType = obs_get_latest_input_type_id("browser_source");
		if (!sourceType) {
			SetStatus(QStringLiteral("The OBS Browser Source module is not available."), true);
			return;
		}

		OBSDataAutoRelease settings = obs_data_create();
		source = obs_source_create(sourceType, OverlaySourceName, settings, nullptr);
		if (!source) {
			SetStatus(QStringLiteral("Unable to create the Starting Soon Browser Source."), true);
			return;
		}
		ApplySourceSettings(source);
	}

	if (!obs_scene_find_source(scene, OverlaySourceName))
		obs_scene_add(scene, source);
	main->SaveProject();
	SetStatus(QStringLiteral("Starting Soon source linked to the active scene."));
}

void TempestControlDeck::SetStatus(const QString &message, bool isError)
{
	if (!statusLabel)
		return;
	statusLabel->setText(message);
	statusLabel->setStyleSheet(isError ? QStringLiteral("color:#ff4b70;") : QStringLiteral("color:#45d9ff;"));
}
