#include "TempestMainframeBar.hpp"

#include "OBSBasic.hpp"

#include <obs.h>

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

TempestMainframeBar::TempestMainframeBar(OBSBasic *main) : QFrame(main), main(main)
{
	setObjectName(QStringLiteral("tempestMainframeBar"));
	setMinimumHeight(68);
	setMaximumHeight(76);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	BuildInterface();

	connect(main, &OBSBasic::StreamingPreparing, this, [this]() {
		streamStarting = true;
		SetTransmissionState(QStringLiteral("PREPARING"), QStringLiteral("VALIDATING TRANSMISSION PATH"),
				     QStringLiteral("#edb74a"));
		UpdateActionButtons();
	});
	connect(main, &OBSBasic::StreamingStarting, this, [this](bool) {
		streamStarting = true;
		SetTransmissionState(QStringLiteral("INITIALIZING"), QStringLiteral("ESTABLISHING UPLINK"),
				     QStringLiteral("#edb74a"));
		UpdateActionButtons();
	});
	connect(main, &OBSBasic::StreamingStarted, this, [this](bool) {
		streamStarting = false;
		streamStopping = false;
		armButton->setChecked(false);
		streamElapsed.restart();
		SetTransmissionState(QStringLiteral("LIVE TRANSMISSION"), QStringLiteral("UPLINK STABLE"),
				     QStringLiteral("#45d9ff"));
		UpdateActionButtons();
	});
	connect(main, &OBSBasic::StreamingStopping, this, [this]() {
		streamStopping = true;
		SetTransmissionState(QStringLiteral("TERMINATING"), QStringLiteral("CLOSING UPLINK"),
				     QStringLiteral("#ff799c"));
		UpdateActionButtons();
	});
	connect(main, &OBSBasic::StreamingStopped, this, [this](bool) {
		streamStarting = false;
		streamStopping = false;
		streamElapsed.invalidate();
		SetTransmissionState(armButton->isChecked() ? QStringLiteral("SYSTEM ARMED")
							    : QStringLiteral("OFFLINE"),
				     armButton->isChecked() ? QStringLiteral("AWAITING TRANSMISSION COMMAND")
							    : QStringLiteral("MAINFRAME STANDBY"),
				     armButton->isChecked() ? QStringLiteral("#edb74a") : QStringLiteral("#748fa4"));
		UpdateActionButtons();
	});
	connect(main, &OBSBasic::StreamingStartFailed, this, [this]() {
		streamStarting = false;
		streamStopping = false;
		SetTransmissionState(armButton->isChecked() ? QStringLiteral("SYSTEM ARMED")
							    : QStringLiteral("OFFLINE"),
				     QStringLiteral("UPLINK START FAILED"), QStringLiteral("#ff799c"));
		UpdateActionButtons();
	});

	connect(main, &OBSBasic::RecordingStarted, this, [this](bool) {
		recording = true;
		UpdateActionButtons();
	});
	connect(main, &OBSBasic::RecordingStopped, this, [this]() {
		recording = false;
		UpdateActionButtons();
	});

	telemetryTimer = new QTimer(this);
	telemetryTimer->setInterval(500);
	connect(telemetryTimer, &QTimer::timeout, this, &TempestMainframeBar::RefreshTelemetry);
	telemetryTimer->start();
	RefreshTelemetry();
	UpdateActionButtons();
}

void TempestMainframeBar::BuildInterface()
{
	setStyleSheet(QStringLiteral(R"(
		QFrame#tempestMainframeBar {
			background: #06101a;
			border-top: 1px solid #1f506d;
			border-bottom: 1px solid #0c7ccb;
		}
		QFrame#tempestMainframeBar QLabel { background: transparent; border: none; }
		QLabel#tempestIdentity { color: #bdf6ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#tempestSubline { color: #748fa4; font-size: 9px; letter-spacing: 1px; }
		QLabel#tempestState { font-size: 14px; font-weight: 700; letter-spacing: 1px; }
		QLabel#tempestClock { color: #bdf6ff; font-family: Consolas; font-size: 20px; font-weight: 700; }
		QLabel#tempestScene { color: #45d9ff; font-size: 11px; font-weight: 600; }
		QLabel#tempestTelemetry { color: #748fa4; font-family: Consolas; font-size: 10px; }
		QLabel#tempestRecordState { color: #ff799c; font-size: 9px; font-weight: 700; letter-spacing: 1px; }
		QPushButton {
			min-height: 30px; padding: 0 12px; border-radius: 2px;
			border: 1px solid #1f506d; background: #0d2230; color: #bdf6ff;
			font-size: 10px; font-weight: 700; letter-spacing: 1px;
		}
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QPushButton:checked { border-color: #45d9ff; background: #073c5f; color: white; }
		QPushButton:disabled { color: #41596c; border-color: #1f3242; background: #09141d; }
		QPushButton#tempestArm:checked { border-color: #edb74a; color: #ffd777; background: #4a3510; }
		QPushButton#tempestDockManager { min-width: 82px; }
		QPushButton#tempestStream { min-width: 132px; }
		QPushButton#tempestRecord:checked { border-color: #ff4b70; color: white; background: #64142b; }
		QPushButton#tempestEmergency { color: #ff799c; border-color: #82203a; }
		QPushButton#tempestEmergency:hover { color: white; background: #821e3a; border-color: #ff4b70; }
	)"));

	auto *root = new QHBoxLayout(this);
	root->setContentsMargins(14, 7, 14, 7);
	root->setSpacing(14);

	auto *identity = new QVBoxLayout();
	identity->setSpacing(0);
	auto *identityLabel = new QLabel(QStringLiteral("TEMPEST MAINFRAME"), this);
	identityLabel->setObjectName(QStringLiteral("tempestIdentity"));
	auto *identitySubline = new QLabel(QStringLiteral("BROADCAST COMMAND NEXUS"), this);
	identitySubline->setObjectName(QStringLiteral("tempestSubline"));
	identity->addWidget(identityLabel);
	identity->addWidget(identitySubline);
	root->addLayout(identity);

	auto *workspaceLayout = new QHBoxLayout();
	workspaceLayout->setSpacing(4);
	commandWorkspaceButton = new QPushButton(QStringLiteral("COMMAND"), this);
	engineeringWorkspaceButton = new QPushButton(QStringLiteral("ENGINEERING"), this);
	commandWorkspaceButton->setCheckable(true);
	engineeringWorkspaceButton->setCheckable(true);
	auto *workspaceGroup = new QButtonGroup(this);
	workspaceGroup->setExclusive(true);
	workspaceGroup->addButton(commandWorkspaceButton);
	workspaceGroup->addButton(engineeringWorkspaceButton);
	workspaceLayout->addWidget(commandWorkspaceButton);
	workspaceLayout->addWidget(engineeringWorkspaceButton);
	dockManagerButton = new QPushButton(QStringLiteral("LAYOUT"), this);
	dockManagerButton->setObjectName(QStringLiteral("tempestDockManager"));
	dockManagerButton->setToolTip(QStringLiteral("Open Mainframe Dock Layout Director"));
	workspaceLayout->addWidget(dockManagerButton);
	root->addLayout(workspaceLayout);

	connect(commandWorkspaceButton, &QPushButton::clicked, this, [this]() { emit WorkspaceRequested(true); });
	connect(engineeringWorkspaceButton, &QPushButton::clicked, this, [this]() { emit WorkspaceRequested(false); });
	connect(dockManagerButton, &QPushButton::clicked, this, [this]() { emit DockManagerRequested(); });

	auto *stateLayout = new QVBoxLayout();
	stateLayout->setSpacing(0);
	stateLabel = new QLabel(QStringLiteral("OFFLINE"), this);
	stateLabel->setObjectName(QStringLiteral("tempestState"));
	detailLabel = new QLabel(QStringLiteral("MAINFRAME STANDBY"), this);
	detailLabel->setObjectName(QStringLiteral("tempestSubline"));
	stateLayout->addWidget(stateLabel);
	stateLayout->addWidget(detailLabel);
	root->addLayout(stateLayout);

	root->addStretch(1);

	auto *telemetryLayout = new QVBoxLayout();
	telemetryLayout->setSpacing(0);
	sceneLabel = new QLabel(QStringLiteral("SCENE // INITIALIZING"), this);
	sceneLabel->setObjectName(QStringLiteral("tempestScene"));
	telemetryLabel = new QLabel(QStringLiteral("RENDER --.- FPS  //  CPU --.-%  //  LAG --.--%"), this);
	telemetryLabel->setObjectName(QStringLiteral("tempestTelemetry"));
	telemetryLayout->addWidget(sceneLabel, 0, Qt::AlignRight);
	telemetryLayout->addWidget(telemetryLabel, 0, Qt::AlignRight);
	root->addLayout(telemetryLayout);

	auto *clockLayout = new QVBoxLayout();
	clockLayout->setSpacing(0);
	clockLabel = new QLabel(QStringLiteral("00:00:00"), this);
	clockLabel->setObjectName(QStringLiteral("tempestClock"));
	recordStateLabel = new QLabel(QStringLiteral("ARCHIVE IDLE"), this);
	recordStateLabel->setObjectName(QStringLiteral("tempestRecordState"));
	clockLayout->addWidget(clockLabel, 0, Qt::AlignRight);
	clockLayout->addWidget(recordStateLabel, 0, Qt::AlignRight);
	root->addLayout(clockLayout);

	armButton = new QPushButton(QStringLiteral("ARM"), this);
	armButton->setObjectName(QStringLiteral("tempestArm"));
	armButton->setCheckable(true);
	streamButton = new QPushButton(QStringLiteral("UPLINK LOCKED"), this);
	streamButton->setObjectName(QStringLiteral("tempestStream"));
	recordButton = new QPushButton(QStringLiteral("RECORD"), this);
	recordButton->setObjectName(QStringLiteral("tempestRecord"));
	recordButton->setCheckable(true);
	emergencyButton = new QPushButton(QStringLiteral("CUT UPLINK"), this);
	emergencyButton->setObjectName(QStringLiteral("tempestEmergency"));

	root->addWidget(armButton);
	root->addWidget(streamButton);
	root->addWidget(recordButton);
	root->addWidget(emergencyButton);

	connect(armButton, &QPushButton::toggled, this, &TempestMainframeBar::ToggleArm);
	connect(streamButton, &QPushButton::clicked, this, &TempestMainframeBar::TriggerStream);
	connect(recordButton, &QPushButton::clicked, this, &TempestMainframeBar::TriggerRecord);
	connect(emergencyButton, &QPushButton::clicked, this, &TempestMainframeBar::EmergencyCut);

	SetTransmissionState(QStringLiteral("OFFLINE"), QStringLiteral("MAINFRAME STANDBY"), QStringLiteral("#748fa4"));
}

void TempestMainframeBar::SetCommandWorkspace(bool commandMode)
{
	commandWorkspaceButton->setChecked(commandMode);
	engineeringWorkspaceButton->setChecked(!commandMode);
}

void TempestMainframeBar::ToggleArm(bool armed)
{
	if (!main || main->StreamingActive())
		return;

	SetTransmissionState(armed ? QStringLiteral("SYSTEM ARMED") : QStringLiteral("OFFLINE"),
			     armed ? QStringLiteral("AWAITING TRANSMISSION COMMAND")
				   : QStringLiteral("MAINFRAME STANDBY"),
			     armed ? QStringLiteral("#edb74a") : QStringLiteral("#748fa4"));
	UpdateActionButtons();
}

void TempestMainframeBar::TriggerStream()
{
	if (!main || streamStarting || streamStopping)
		return;

	if (main->StreamingActive()) {
		main->StopStreaming();
	} else if (armButton->isChecked()) {
		main->StartStreaming();
	}
}

void TempestMainframeBar::TriggerRecord()
{
	if (!main)
		return;

	if (main->RecordingActive())
		main->StopRecording();
	else
		main->StartRecording();
}

void TempestMainframeBar::EmergencyCut()
{
	if (!main || !main->StreamingActive())
		return;

	auto result = QMessageBox::warning(this, QStringLiteral("Emergency Uplink Cut"),
					   QStringLiteral("Immediately terminate the active transmission?\n\n"
							  "Local recording will continue."),
					   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
	if (result == QMessageBox::Yes)
		main->ForceStopStreaming();
}

void TempestMainframeBar::RefreshTelemetry()
{
	if (!main)
		return;

	OBSSource scene = main->GetCurrentSceneSource();
	const char *sceneName = scene ? obs_source_get_name(scene) : nullptr;
	sceneLabel->setText(
		QStringLiteral("SCENE // %1")
			.arg(sceneName ? QString::fromUtf8(sceneName).toUpper() : QStringLiteral("NO ACTIVE SCENE")));

	const double fps = obs_get_active_fps();
	const uint64_t totalFrames = obs_get_total_frames();
	const uint64_t laggedFrames = obs_get_lagged_frames();
	const double lagPercent = totalFrames > 0 ? (double)laggedFrames * 100.0 / (double)totalFrames : 0.0;
	telemetryLabel->setText(QStringLiteral("RENDER %1 FPS  //  CPU %2%  //  LAG %3%")
					.arg(fps, 0, 'f', 1)
					.arg(main->GetCPUUsage(), 0, 'f', 1)
					.arg(lagPercent, 0, 'f', 2));

	if (main->StreamingActive()) {
		clockLabel->setText(ElapsedText());
		if (!streamStopping)
			SetTransmissionState(QStringLiteral("LIVE TRANSMISSION"), QStringLiteral("UPLINK STABLE"),
					     QStringLiteral("#45d9ff"));
	} else if (!streamStarting && !streamStopping) {
		clockLabel->setText(QStringLiteral("00:00:00"));
	}

	recording = main->RecordingActive();
	UpdateActionButtons();
}

void TempestMainframeBar::SetTransmissionState(const QString &state, const QString &detail, const QString &color)
{
	stateLabel->setText(state);
	detailLabel->setText(detail);
	stateLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(color));
}

void TempestMainframeBar::UpdateActionButtons()
{
	if (!main)
		return;

	const bool live = main->StreamingActive();
	const bool busy = streamStarting || streamStopping;
	armButton->setEnabled(!live && !busy);
	streamButton->setEnabled(!busy && (live || armButton->isChecked()));
	streamButton->setText(live                     ? QStringLiteral("END TRANSMISSION")
			      : armButton->isChecked() ? QStringLiteral("INITIATE UPLINK")
						       : QStringLiteral("UPLINK LOCKED"));
	emergencyButton->setEnabled(live && !streamStopping);
	recordButton->setChecked(recording);
	recordButton->setText(recording ? QStringLiteral("STOP RECORD") : QStringLiteral("RECORD"));
	recordStateLabel->setText(recording ? QStringLiteral("ARCHIVE RECORDING") : QStringLiteral("ARCHIVE IDLE"));
}

QString TempestMainframeBar::ElapsedText() const
{
	if (!streamElapsed.isValid())
		return QStringLiteral("00:00:00");

	const qint64 totalSeconds = streamElapsed.elapsed() / 1000;
	const qint64 hours = totalSeconds / 3600;
	const qint64 minutes = (totalSeconds / 60) % 60;
	const qint64 seconds = totalSeconds % 60;
	return QStringLiteral("%1:%2:%3")
		.arg(hours, 2, 10, QLatin1Char('0'))
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds, 2, 10, QLatin1Char('0'));
}
