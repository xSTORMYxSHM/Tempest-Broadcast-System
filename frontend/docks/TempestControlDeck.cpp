#include "TempestControlDeck.hpp"

#include <OBSApp.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>

#include <QDateTime>
#include <QDir>
#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
constexpr char ConfigSection[] = "TempestControlDeck";

struct ModeDefinition {
	const char *id;
	const char *label;
	const char *filename;
	const char *sourceName;
	const char *kicker;
	const char *idleText;
	const char *defaultTitle;
	const char *defaultStatus;
	const char *defaultMessages;
};

constexpr std::array<ModeDefinition, 4> Modes{{
	{"starting", "Starting Soon", "starting-soon.html", "Tempest // Starting Soon", "TRANSMISSION INITIALIZING",
	 "UPLINK PENDING", "STORM HORIZON RADIO", "OPERATOR LINK // STANDBY",
	 "INDEXING FRACTAL ARCHIVE\nCALIBRATING STORM HORIZON SIGNAL\nTEMPEST FIELD SYNCHRONIZATION ACTIVE"},
	{"brb", "Be Right Back", "brb.html", "Tempest // BRB", "OPERATOR TEMPORARILY OFFLINE", "RETURN PENDING",
	 "SIGNAL HOLD", "MAINFRAME AUTOPILOT // ACTIVE",
	 "MAINTAINING BROADCAST CARRIER\nARCHIVE PLAYBACK CONTINUES\nOPERATOR RETURN WINDOW ACTIVE"},
	{"ending", "Stream Ending", "stream-ending.html", "Tempest // Stream Ending", "TRANSMISSION ARCHIVING",
	 "ARCHIVE SEALED", "SIGNAL DESCENDING", "SESSION RECORD // COMPLETE",
	 "WRITING SESSION TO THE LIVING ARCHIVE\nSTORM HORIZON CARRIER DISENGAGING\nUNTIL THE NEXT TRANSMISSION"},
	{"live", "Live HUD", "live-hud.html", "Tempest // Live HUD", "UPLINK ACTIVE", "LIVE SIGNAL",
	 "STORM HORIZON RADIO", "OPERATOR LINK // LIVE",
	 "TEMPEST FIELD NOMINAL\nARCHIVE INGEST ACTIVE\nSIGNAL ROUTING STABLE"},
}};

const ModeDefinition &FindMode(const QString &id)
{
	for (const ModeDefinition &mode : Modes) {
		if (id == QString::fromUtf8(mode.id))
			return mode;
	}
	return Modes.front();
}

QByteArray ModeKey(const char *base, const QString &mode)
{
	return QByteArray(base) + '_' + mode.toUtf8();
}

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

	audioMeter = obs_volmeter_create(OBS_FADER_LOG);
	if (audioMeter) {
		obs_volmeter_set_peak_meter_type(audioMeter, SAMPLE_PEAK_METER);
		obs_volmeter_add_callback(audioMeter, AudioLevelCallback, this);
	}

	telemetryTimer = new QTimer(this);
	telemetryTimer->setInterval(50);
	connect(telemetryTimer, &QTimer::timeout, this, &TempestControlDeck::WriteAudioTelemetry);
	telemetryTimer->start();

	connect(overlayMode, &QComboBox::currentIndexChanged, this, &TempestControlDeck::ChangeOverlayMode);
	connect(streamTitle, &QLineEdit::textChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(statusLine, &QLineEdit::textChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(rotationMessages, &QPlainTextEdit::textChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(rotationSeconds, &QSpinBox::valueChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(countdownMinutes, &QSpinBox::valueChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(startCountdownButton, &QPushButton::clicked, this, &TempestControlDeck::StartCountdown);
	connect(resetCountdownButton, &QPushButton::clicked, this, &TempestControlDeck::ResetCountdown);
	connect(createSourceButton, &QPushButton::clicked, this, &TempestControlDeck::CreateOrUpdateSource);
	connect(refreshAudioButton, &QPushButton::clicked, this, &TempestControlDeck::RefreshAudioSources);
	connect(audioSourceCombo, &QComboBox::currentIndexChanged, this, &TempestControlDeck::AttachAudioMeter);

	RenderOverlay();
	UpdateCountdownPreview();
	QTimer::singleShot(3500, this, &TempestControlDeck::RefreshAudioSources);
}

TempestControlDeck::~TempestControlDeck()
{
	if (audioMeter) {
		obs_volmeter_remove_callback(audioMeter, AudioLevelCallback, this);
		obs_volmeter_detach_source(audioMeter);
		obs_volmeter_destroy(audioMeter);
	}
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
	overlayMode = new QComboBox(body);
	for (const ModeDefinition &mode : Modes)
		overlayMode->addItem(QString::fromUtf8(mode.label), QString::fromUtf8(mode.id));
	form->addRow(QStringLiteral("Overlay mode"), overlayMode);

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

	QFormLayout *audioForm = new QFormLayout();
	audioForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	QWidget *audioPicker = new QWidget(body);
	QHBoxLayout *audioPickerLayout = new QHBoxLayout(audioPicker);
	audioPickerLayout->setContentsMargins(0, 0, 0, 0);
	audioPickerLayout->setSpacing(6);
	audioSourceCombo = new QComboBox(audioPicker);
	audioSourceCombo->addItem(QStringLiteral("Waiting for OBS audio sources..."), QString());
	refreshAudioButton = new QPushButton(QStringLiteral("Refresh"), audioPicker);
	audioPickerLayout->addWidget(audioSourceCombo, 1);
	audioPickerLayout->addWidget(refreshAudioButton);
	audioForm->addRow(QStringLiteral("Reactive audio"), audioPicker);
	audioLevelMeter = new QProgressBar(body);
	audioLevelMeter->setRange(0, 1000);
	audioLevelMeter->setValue(0);
	audioLevelMeter->setTextVisible(false);
	audioLevelMeter->setMaximumHeight(8);
	audioForm->addRow(QStringLiteral("Signal level"), audioLevelMeter);
	layout->addLayout(audioForm);

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
	QString savedMode = ConfigString("OverlayMode", "starting");
	int modeIndex = overlayMode->findData(savedMode);
	overlayMode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
	activeModeId = CurrentModeId();

	int rotation = (int)config_get_int(config, ConfigSection, "RotationSeconds");
	rotationSeconds->setValue(rotation > 0 ? rotation : 6);
	int minutes = (int)config_get_int(config, ConfigSection, "CountdownMinutes");
	countdownMinutes->setValue(minutes > 0 ? minutes : 10);
	configuredAudioSourceUuid = ConfigString("AudioSourceUuid", "");
	LoadModeState(activeModeId);
}

void TempestControlDeck::SaveState()
{
	config_t *config = App()->GetUserConfig();
	SaveModeState(activeModeId);
	config_set_string(config, ConfigSection, "OverlayMode", activeModeId.toUtf8().constData());
	config_set_int(config, ConfigSection, "RotationSeconds", rotationSeconds->value());
	config_set_int(config, ConfigSection, "CountdownMinutes", countdownMinutes->value());
	QString audioUuid = audioSourceCombo->currentData().toString();
	if (audioSourcesLoaded) {
		configuredAudioSourceUuid = audioUuid;
		config_set_string(config, ConfigSection, "AudioSourceUuid", audioUuid.toUtf8().constData());
	}
	config_save_safe(config, "tmp", nullptr);
}

void TempestControlDeck::LoadModeState(const QString &modeId)
{
	config_t *config = App()->GetUserConfig();
	const ModeDefinition &mode = FindMode(modeId);
	auto modeString = [config, &modeId](const char *base, const char *fallback) {
		QByteArray key = ModeKey(base, modeId);
		const char *value = config_get_string(config, ConfigSection, key.constData());
		if (value && *value)
			return QString::fromUtf8(value);
		if (modeId == QStringLiteral("starting")) {
			value = config_get_string(config, ConfigSection, base);
			if (value && *value)
				return QString::fromUtf8(value);
		}
		return QString::fromUtf8(fallback);
	};

	streamTitle->setText(modeString("StreamTitle", mode.defaultTitle));
	statusLine->setText(modeString("StatusLine", mode.defaultStatus));
	rotationMessages->setPlainText(modeString("RotationMessages", mode.defaultMessages));

	QByteArray endKey = ModeKey("CountdownEndMs", modeId);
	QByteArray runningKey = ModeKey("CountdownRunning", modeId);
	countdownEndMs = config_get_int(config, ConfigSection, endKey.constData());
	countdownRunning = config_get_bool(config, ConfigSection, runningKey.constData());
	if (modeId == QStringLiteral("starting") && !config_has_user_value(config, ConfigSection, endKey.constData())) {
		countdownEndMs = config_get_int(config, ConfigSection, "CountdownEndMs");
		countdownRunning = config_get_bool(config, ConfigSection, "CountdownRunning");
	}
}

void TempestControlDeck::SaveModeState(const QString &modeId)
{
	if (modeId.isEmpty())
		return;
	config_t *config = App()->GetUserConfig();
	QByteArray titleKey = ModeKey("StreamTitle", modeId);
	QByteArray statusKey = ModeKey("StatusLine", modeId);
	QByteArray messagesKey = ModeKey("RotationMessages", modeId);
	QByteArray endKey = ModeKey("CountdownEndMs", modeId);
	QByteArray runningKey = ModeKey("CountdownRunning", modeId);
	config_set_string(config, ConfigSection, titleKey.constData(), streamTitle->text().toUtf8().constData());
	config_set_string(config, ConfigSection, statusKey.constData(), statusLine->text().toUtf8().constData());
	config_set_string(config, ConfigSection, messagesKey.constData(), rotationMessages->toPlainText().toUtf8().constData());
	config_set_int(config, ConfigSection, endKey.constData(), countdownEndMs);
	config_set_bool(config, ConfigSection, runningKey.constData(), countdownRunning);
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

	telemetryPath = QDir(overlayDirectory).filePath(QStringLiteral("telemetry.json"));
	UpdateOverlayPath();
	return true;
}

QString TempestControlDeck::CurrentModeId() const
{
	return overlayMode ? overlayMode->currentData().toString() : QStringLiteral("starting");
}

QString TempestControlDeck::CurrentModeLabel() const
{
	return QString::fromUtf8(FindMode(activeModeId).label);
}

QString TempestControlDeck::CurrentSourceName() const
{
	return QString::fromUtf8(FindMode(activeModeId).sourceName);
}

void TempestControlDeck::UpdateOverlayPath()
{
	if (overlayDirectory.isEmpty())
		return;
	const ModeDefinition &mode = FindMode(activeModeId);
	overlayPath = QDir(overlayDirectory).filePath(QString::fromUtf8(mode.filename));
	outputPathLabel->setText(QStringLiteral("Overlay file: %1").arg(QDir::toNativeSeparators(overlayPath)));
	createSourceButton->setText(QStringLiteral("Create / Update %1 Source").arg(QString::fromUtf8(mode.label)));
}

void TempestControlDeck::ChangeOverlayMode(int)
{
	SaveModeState(activeModeId);
	activeModeId = CurrentModeId();
	{
		QSignalBlocker blockTitle(streamTitle);
		QSignalBlocker blockStatus(statusLine);
		QSignalBlocker blockMessages(rotationMessages);
		LoadModeState(activeModeId);
	}
	UpdateOverlayPath();
	UpdateCountdownPreview();
	RenderOverlay();
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
	const ModeDefinition &mode = FindMode(activeModeId);
	state.insert(QStringLiteral("mode"), activeModeId);
	state.insert(QStringLiteral("kicker"), QString::fromUtf8(mode.kicker));
	state.insert(QStringLiteral("idleText"), QString::fromUtf8(mode.idleText));
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
:root{--void:#03090f;--panel:rgba(5,16,26,.84);--cyan:#45d9ff;--ice:#bdf6ff;--blue:#0c7ccb;--deep:#06345d;--muted:#748fa4;--danger:#ff4b70;--audio:0;--glow:12px;--audioAlpha:.04;--coreScale:1;--reactorScale:.88;--reactorOpacity:.14;--titleScale:1}
*{box-sizing:border-box}html,body{width:100%;height:100%;margin:0;overflow:hidden;background:transparent;color:var(--ice);font-family:"Segoe UI",Arial,sans-serif}
body:before{content:"";position:absolute;inset:0;background:repeating-linear-gradient(0deg,rgba(69,217,255,.025) 0,rgba(69,217,255,.025) 1px,transparent 1px,transparent 5px);pointer-events:none}
.frame{position:absolute;inset:4.5%;border:1px solid rgba(69,217,255,.24);box-shadow:inset 0 0 var(--glow) rgba(69,217,255,var(--audioAlpha));clip-path:polygon(0 0,18% 0,19% 1px,81% 1px,82% 0,100% 0,100% 100%,82% 100%,81% calc(100% - 1px),19% calc(100% - 1px),18% 100%,0 100%)}
.frame:before,.frame:after{content:"";position:absolute;width:16%;height:2px;top:-1px;background:linear-gradient(90deg,transparent,var(--cyan),transparent);animation:sweep 4s linear infinite}.frame:after{top:auto;bottom:-1px;right:0;animation-direction:reverse}
.top{position:absolute;left:6.5%;right:6.5%;top:6.5%;display:flex;justify-content:space-between;align-items:center;font-size:clamp(11px,1vw,18px);letter-spacing:.22em;text-transform:uppercase;color:var(--muted)}
.sig{display:flex;align-items:center;gap:12px;color:var(--cyan);font-weight:700}.core{width:14px;height:14px;border:2px solid var(--cyan);transform:rotate(45deg) scale(var(--coreScale));box-shadow:0 0 var(--glow) var(--cyan);animation:pulse 1.8s ease-in-out infinite}
.status{padding:8px 14px;border:1px solid rgba(69,217,255,.35);background:rgba(3,9,15,.66)}
.hero{position:absolute;left:10%;right:10%;top:24%;text-align:center;text-transform:uppercase}
.kicker{font-size:clamp(13px,1.2vw,22px);letter-spacing:.48em;color:var(--cyan);margin-bottom:18px}
.title{font-size:clamp(40px,6vw,112px);line-height:.92;font-weight:800;letter-spacing:.06em;text-shadow:0 0 var(--glow) rgba(69,217,255,.52);transform:scale(var(--titleScale))}
.rule{height:1px;width:72%;margin:28px auto;background:linear-gradient(90deg,transparent,var(--blue),var(--cyan),var(--blue),transparent);position:relative}.rule:after{content:"";position:absolute;left:50%;top:-4px;width:9px;height:9px;background:var(--ice);transform:rotate(45deg);box-shadow:0 0 16px var(--cyan)}
.countdown{font-variant-numeric:tabular-nums;font-size:clamp(54px,8vw,148px);line-height:1;font-weight:300;letter-spacing:.12em;color:var(--ice);text-shadow:0 0 24px rgba(69,217,255,.48)}
.message{margin-top:22px;min-height:1.5em;font-size:clamp(15px,1.5vw,28px);letter-spacing:.3em;color:var(--cyan);transition:opacity .24s ease}
.bottom{position:absolute;left:7%;right:7%;bottom:7%;display:flex;align-items:center;gap:18px;color:var(--muted);font-size:clamp(10px,.9vw,16px);letter-spacing:.18em}.line{height:1px;flex:1;background:linear-gradient(90deg,var(--deep),var(--cyan),var(--deep))}.packet{color:var(--cyan)}
.corner{position:absolute;width:54px;height:54px;border-color:var(--cyan);opacity:.72}.c1{left:4.5%;top:4.5%;border-left:3px solid;border-top:3px solid}.c2{right:4.5%;top:4.5%;border-right:3px solid;border-top:3px solid}.c3{left:4.5%;bottom:4.5%;border-left:3px solid;border-bottom:3px solid}.c4{right:4.5%;bottom:4.5%;border-right:3px solid;border-bottom:3px solid}
.reactor{position:absolute;left:50%;top:54%;width:56vw;height:56vw;max-width:900px;max-height:900px;transform:translate(-50%,-50%) scale(var(--reactorScale));border:1px solid rgba(69,217,255,.25);border-radius:50%;opacity:var(--reactorOpacity);box-shadow:0 0 var(--glow) rgba(12,124,203,.3);pointer-events:none}.reactor:before,.reactor:after{content:"";position:absolute;border:1px dashed rgba(69,217,255,.35);border-radius:50%;inset:12%;animation:orbit 18s linear infinite}.reactor:after{inset:27%;animation-duration:11s;animation-direction:reverse}
.eq{position:absolute;left:17%;right:17%;bottom:12%;height:34px;display:flex;gap:5px;align-items:flex-end;justify-content:center;opacity:.72}.eq i{display:block;width:3px;height:3px;background:var(--cyan);box-shadow:0 0 8px var(--cyan);transition:height 45ms linear}
@keyframes pulse{50%{opacity:.55}}@keyframes sweep{from{transform:translateX(-15%)}to{transform:translateX(620%)}}@keyframes orbit{to{transform:rotate(360deg)}}
</style>
</head>
<body>
<div class="frame"></div><div class="reactor"></div><div class="eq" id="eq"></div><i class="corner c1"></i><i class="corner c2"></i><i class="corner c3"></i><i class="corner c4"></i>
<header class="top"><div class="sig"><i class="core"></i><span>TEMPEST MAINFRAME // BROADCAST UPLINK</span></div><div class="status" id="status"></div></header>
<main class="hero"><div class="kicker" id="kicker"></div><div class="title" id="title"></div><div class="rule"></div><div class="countdown" id="countdown"></div><div class="message" id="message"></div></main>
<footer class="bottom"><span>ARCHIVE NODE 2526</span><i class="line"></i><span class="packet">SIGNAL LOCKED</span><i class="line"></i><span>OPERATOR xSTORMYx</span></footer>
<script id="tempest-state" type="application/json">{{STATE_JSON}}</script>
<script>
const state=JSON.parse(document.getElementById('tempest-state').textContent);const root=document.documentElement,title=document.getElementById('title'),status=document.getElementById('status'),countdown=document.getElementById('countdown'),message=document.getElementById('message'),kicker=document.getElementById('kicker'),eq=document.getElementById('eq');
title.textContent=state.title||'STORM HORIZON RADIO';status.textContent=state.status||'OPERATOR LINK // STANDBY';kicker.textContent=state.kicker;let index=0,audio=0;for(let i=0;i<32;i++)eq.appendChild(document.createElement('i'));
function rotate(){message.style.opacity='0';setTimeout(()=>{message.textContent=state.messages[index++%state.messages.length];message.style.opacity='1'},240)}rotate();setInterval(rotate,Math.max(2000,state.rotationMs||6000));
function tick(){if(!state.countdownEndMs){countdown.textContent=state.idleText;return}const remaining=Math.max(0,state.countdownEndMs-Date.now()),seconds=Math.ceil(remaining/1000),minutes=Math.floor(seconds/60),secs=seconds%60;countdown.textContent=remaining<=0?(state.mode==='brb'?'OPERATOR RETURN':'UPLINK READY'):String(minutes).padStart(2,'0')+':'+String(secs).padStart(2,'0')}tick();setInterval(tick,250);
async function telemetry(){try{const response=await fetch('./telemetry.json?t='+Date.now(),{cache:'no-store'});if(response.ok){const data=await response.json();audio=Math.max(Number(data.level)||0,audio*.74)}}catch(_){audio*=.82}root.style.setProperty('--audio',audio.toFixed(3));root.style.setProperty('--glow',(12+audio*92)+'px');root.style.setProperty('--audioAlpha',(.04+audio*.24).toFixed(3));root.style.setProperty('--coreScale',(1+audio*.55).toFixed(3));root.style.setProperty('--reactorScale',(.88+audio*.16).toFixed(3));root.style.setProperty('--reactorOpacity',(.14+audio*.5).toFixed(3));root.style.setProperty('--titleScale',(1+audio*.012).toFixed(4));[...eq.children].forEach((bar,i)=>{const wave=.28+.72*Math.abs(Math.sin(performance.now()/180+i*.61));bar.style.height=(3+audio*31*wave)+'px'})}telemetry();setInterval(telemetry,60);
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
	const ModeDefinition &mode = FindMode(activeModeId);
	if (!countdownRunning || countdownEndMs <= 0) {
		countdownPreview->setText(QString::fromUtf8(mode.idleText));
		return;
	}

	qint64 remaining = countdownEndMs - QDateTime::currentMSecsSinceEpoch();
	if (remaining <= 0) {
		countdownPreview->setText(activeModeId == QStringLiteral("starting")
						  ? QStringLiteral("UPLINK READY")
						  : QString::fromUtf8(mode.idleText));
		return;
	}

	qint64 seconds = (remaining + 999) / 1000;
	qint64 minutes = seconds / 60;
	seconds %= 60;
	countdownPreview->setText(QStringLiteral("%1:%2")
					  .arg(minutes, 2, 10, QChar('0'))
					  .arg(seconds, 2, 10, QChar('0')));
}

void TempestControlDeck::AudioLevelCallback(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
					    const float peak[MAX_AUDIO_CHANNELS],
					    const float inputPeak[MAX_AUDIO_CHANNELS])
{
	(void)magnitude;
	(void)inputPeak;
	auto *deck = static_cast<TempestControlDeck *>(param);
	float peakDb = -96.0f;
	for (size_t channel = 0; channel < MAX_AUDIO_CHANNELS; ++channel) {
		if (std::isfinite(peak[channel]))
			peakDb = std::max(peakDb, peak[channel]);
	}
	float normalized = std::clamp((peakDb + 55.0f) / 55.0f, 0.0f, 1.0f);
	deck->audioLevel.store(std::pow(normalized, 0.72f), std::memory_order_relaxed);
}

void TempestControlDeck::RefreshAudioSources()
{
	struct AudioEntry {
		QString name;
		QString uuid;
	};
	std::vector<AudioEntry> entries;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
				return true;
			const char *name = obs_source_get_name(source);
			const char *uuid = obs_source_get_uuid(source);
			if (name && *name && uuid && *uuid)
				static_cast<std::vector<AudioEntry> *>(param)->push_back(
					{QString::fromUtf8(name), QString::fromUtf8(uuid)});
			return true;
		},
		&entries);
	std::sort(entries.begin(), entries.end(), [](const AudioEntry &a, const AudioEntry &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});

	QString wanted = configuredAudioSourceUuid;
	if (wanted.isEmpty())
		wanted = audioSourceCombo->currentData().toString();
	QSignalBlocker blocker(audioSourceCombo);
	audioSourceCombo->clear();
	audioSourceCombo->addItem(QStringLiteral("No audio reactivity"), QString());
	for (const AudioEntry &entry : entries)
		audioSourceCombo->addItem(entry.name, entry.uuid);
	audioSourcesLoaded = true;

	int selected = wanted.isEmpty() ? -1 : audioSourceCombo->findData(wanted);
	if (selected < 0 && !entries.empty()) {
		for (int i = 1; i < audioSourceCombo->count(); ++i) {
			if (audioSourceCombo->itemText(i).contains(QStringLiteral("Desktop Audio"), Qt::CaseInsensitive)) {
				selected = i;
				break;
			}
		}
		if (selected < 0)
			selected = 1;
	}
	audioSourceCombo->setCurrentIndex(std::max(0, selected));
	AttachAudioMeter();
}

void TempestControlDeck::AttachAudioMeter()
{
	if (!audioMeter)
		return;
	obs_volmeter_detach_source(audioMeter);
	audioLevel.store(0.0f, std::memory_order_relaxed);
	smoothedAudioLevel = 0.0f;

	QString uuid = audioSourceCombo->currentData().toString();
	if (uuid.isEmpty()) {
		SaveState();
		return;
	}
	OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!source || !obs_volmeter_attach_source(audioMeter, source)) {
		SetStatus(QStringLiteral("Unable to attach reactive telemetry to the selected audio source."), true);
		return;
	}
	configuredAudioSourceUuid = uuid;
	SaveState();
	SetStatus(QStringLiteral("Reactive telemetry linked to %1.").arg(audioSourceCombo->currentText()));
}

void TempestControlDeck::WriteAudioTelemetry()
{
	float current = audioLevel.load(std::memory_order_relaxed);
	smoothedAudioLevel = std::max(current, smoothedAudioLevel * 0.82f);
	if (audioLevelMeter)
		audioLevelMeter->setValue((int)(smoothedAudioLevel * 1000.0f));
	if (telemetryPath.isEmpty())
		return;

	QFile file(telemetryPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		return;
	QByteArray json = "{\"level\":" + QByteArray::number(smoothedAudioLevel, 'f', 4) +
			  ",\"timestamp\":" + QByteArray::number(QDateTime::currentMSecsSinceEpoch()) + "}";
	file.write(json);
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
	QByteArray sourceName = CurrentSourceName().toUtf8();
	OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.constData());
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

	QByteArray sourceName = CurrentSourceName().toUtf8();
	OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.constData());
	if (source) {
		if (strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0) {
			SetStatus(QStringLiteral("A non-browser source already uses the name '%1'.")
					  .arg(CurrentSourceName()),
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
		source = obs_source_create(sourceType, sourceName.constData(), settings, nullptr);
		if (!source) {
			SetStatus(QStringLiteral("Unable to create the %1 Browser Source.").arg(CurrentModeLabel()), true);
			return;
		}
		ApplySourceSettings(source);
	}

	if (!obs_scene_find_source(scene, sourceName.constData()))
		obs_scene_add(scene, source);
	main->SaveProject();
	SetStatus(QStringLiteral("%1 source linked to the active scene.").arg(CurrentModeLabel()));
}

void TempestControlDeck::SetStatus(const QString &message, bool isError)
{
	if (!statusLabel)
		return;
	statusLabel->setText(message);
	statusLabel->setStyleSheet(isError ? QStringLiteral("color:#ff4b70;") : QStringLiteral("color:#45d9ff;"));
}
