#include "TempestSequenceDirector.hpp"

#include "TempestCommandMatrix.hpp"
#include "TempestControlDeck.hpp"
#include "TempestMediaBay.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

#include "moc_TempestSequenceDirector.cpp"

namespace {
constexpr char ConfigSection[] = "TempestSequenceDirector";

QString SequenceConfigKey(const QString &sequenceId)
{
	return QStringLiteral("Sequence_%1").arg(sequenceId);
}

void SetComboData(QComboBox *combo, const QString &value, const QString &unavailableLabel = {})
{
	int index = combo->findData(value);
	if (index < 0 && !value.isEmpty()) {
		combo->addItem(unavailableLabel.isEmpty() ? QStringLiteral("Unavailable") : unavailableLabel, value);
		index = combo->count() - 1;
	}
	combo->setCurrentIndex(std::max(index, 0));
}

bool IsSequenceId(const QString &sequenceId)
{
	return sequenceId == QStringLiteral("starting") || sequenceId == QStringLiteral("live") ||
	       sequenceId == QStringLiteral("brb") || sequenceId == QStringLiteral("ending");
}
} // namespace

TempestSequenceDirector::TempestSequenceDirector(OBSBasic *main, TempestCommandMatrix *matrix,
						 TempestControlDeck *controlDeck, QWidget *parent)
	: OBSDock(parent),
	  main(main),
	  matrix(matrix),
	  controlDeck(controlDeck)
{
	setObjectName(QStringLiteral("tempestSequenceDirector"));
	setWindowTitle(QStringLiteral("Mainframe Sequence Director"));
	setMinimumWidth(360);
	BuildInterface();
	EnableContentScaling(objectName());
	LoadSequences();
	activeSequenceId = CurrentSequenceId();
	RebuildCueList();

	sequenceTimer.setInterval(100);
	connect(&sequenceTimer, &QTimer::timeout, this, &TempestSequenceDirector::TickSequence);
	sequenceTimer.start();
}

TempestSequenceDirector::~TempestSequenceDirector()
{
	UnregisterHotkeys();
}

void TempestSequenceDirector::BuildInterface()
{
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("tempestSequenceRoot"));
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestSequenceRoot { background: #07131e; }
		QLabel#sequenceTitle { color: #45d9ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#sequenceSubtitle { color: #748fa4; font-size: 10px; letter-spacing: 1px; }
		QLabel#sequenceClock { color: #bdf6ff; font-size: 20px; font-family: monospace; font-weight: 700; }
		QLabel#sequenceState { color: #45d9ff; font-weight: 700; padding: 5px 8px; border: 1px solid #0c7ccb; background: #06101a; }
		QLabel#sequenceStatus { color: #45d9ff; font-size: 10px; }
		QComboBox, QListWidget { background: #06101a; border: 1px solid #1f506d; color: #bdf6ff; }
		QComboBox { min-height: 29px; padding: 0 7px; }
		QListWidget::item { min-height: 38px; border-bottom: 1px solid #102c3e; padding: 4px; }
		QListWidget::item:selected { background: #073c5f; border: 1px solid #45d9ff; }
		QPushButton { min-height: 31px; border: 1px solid #1f506d; background: #0d2230; color: #bdf6ff; font-weight: 700; padding: 0 8px; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QPushButton:disabled { color: #40576a; border-color: #172d3d; background: #091721; }
		QProgressBar { min-height: 5px; max-height: 5px; border: none; background: #16364a; }
		QProgressBar::chunk { background: #24bce7; }
	)"));

	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(7);
	auto *title = new QLabel(QStringLiteral("SEQUENCE DIRECTOR"), root);
	title->setObjectName(QStringLiteral("sequenceTitle"));
	auto *subtitle = new QLabel(QStringLiteral("Timed archive cues and transmission handoffs"), root);
	subtitle->setObjectName(QStringLiteral("sequenceSubtitle"));
	layout->addWidget(title);
	layout->addWidget(subtitle);

	sequenceSelector = new QComboBox(root);
	sequenceSelector->setObjectName(QStringLiteral("tempestSequenceSelector"));
	sequenceSelector->addItem(QStringLiteral("STARTING SEQUENCE"), QStringLiteral("starting"));
	sequenceSelector->addItem(QStringLiteral("LIVE SEQUENCE"), QStringLiteral("live"));
	sequenceSelector->addItem(QStringLiteral("BRB SEQUENCE"), QStringLiteral("brb"));
	sequenceSelector->addItem(QStringLiteral("ENDING SEQUENCE"), QStringLiteral("ending"));
	connect(sequenceSelector, &QComboBox::currentIndexChanged, this, &TempestSequenceDirector::ChangeSequence);
	layout->addWidget(sequenceSelector);

	auto *telemetryRow = new QHBoxLayout();
	clockLabel = new QLabel(QStringLiteral("00:00.0"), root);
	clockLabel->setObjectName(QStringLiteral("sequenceClock"));
	stateLabel = new QLabel(QStringLiteral("IDLE"), root);
	stateLabel->setObjectName(QStringLiteral("sequenceState"));
	telemetryRow->addWidget(clockLabel);
	telemetryRow->addStretch(1);
	telemetryRow->addWidget(stateLabel);
	layout->addLayout(telemetryRow);
	progress = new QProgressBar(root);
	progress->setTextVisible(false);
	progress->setRange(0, 1);
	layout->addWidget(progress);

	cueList = new QListWidget(root);
	cueList->setObjectName(QStringLiteral("tempestCueList"));
	cueList->setAccessibleName(QStringLiteral("Tempest sequence cues"));
	connect(cueList, &QListWidget::itemDoubleClicked, this, &TempestSequenceDirector::EditSelectedCue);
	layout->addWidget(cueList, 1);

	auto *editRow = new QHBoxLayout();
	auto *add = new QPushButton(QStringLiteral("ADD"), root);
	auto *edit = new QPushButton(QStringLiteral("EDIT"), root);
	auto *remove = new QPushButton(QStringLiteral("REMOVE"), root);
	add->setObjectName(QStringLiteral("tempestCueAdd"));
	edit->setObjectName(QStringLiteral("tempestCueEdit"));
	remove->setObjectName(QStringLiteral("tempestCueRemove"));
	connect(add, &QPushButton::clicked, this, &TempestSequenceDirector::AddCue);
	connect(edit, &QPushButton::clicked, this, &TempestSequenceDirector::EditSelectedCue);
	connect(remove, &QPushButton::clicked, this, &TempestSequenceDirector::RemoveSelectedCue);
	editRow->addWidget(add);
	editRow->addWidget(edit);
	editRow->addWidget(remove);
	layout->addLayout(editRow);

	auto *transportGrid = new QGridLayout();
	runButton = new QPushButton(QStringLiteral("RUN SEQUENCE"), root);
	holdButton = new QPushButton(QStringLiteral("HOLD"), root);
	nextButton = new QPushButton(QStringLiteral("NEXT CUE"), root);
	stopButton = new QPushButton(QStringLiteral("STOP"), root);
	runButton->setObjectName(QStringLiteral("tempestSequenceRun"));
	holdButton->setObjectName(QStringLiteral("tempestSequenceHold"));
	nextButton->setObjectName(QStringLiteral("tempestSequenceNext"));
	stopButton->setObjectName(QStringLiteral("tempestSequenceStop"));
	connect(runButton, &QPushButton::clicked, this, &TempestSequenceDirector::RunCurrentSequence);
	connect(holdButton, &QPushButton::clicked, this, &TempestSequenceDirector::ToggleHold);
	connect(nextButton, &QPushButton::clicked, this, &TempestSequenceDirector::ExecuteNextCue);
	connect(stopButton, &QPushButton::clicked, this, &TempestSequenceDirector::StopSequence);
	transportGrid->addWidget(runButton, 0, 0, 1, 2);
	transportGrid->addWidget(holdButton, 1, 0);
	transportGrid->addWidget(nextButton, 1, 1);
	transportGrid->addWidget(stopButton, 2, 0, 1, 2);
	layout->addLayout(transportGrid);

	statusLabel = new QLabel(QStringLiteral("DIRECTOR READY"), root);
	statusLabel->setObjectName(QStringLiteral("sequenceStatus"));
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	setWidget(root);
	SetRunningState(false, false);
}

void TempestSequenceDirector::LoadSequences()
{
	config_t *config = App()->GetUserConfig();
	const QStringList ids = {QStringLiteral("starting"), QStringLiteral("live"), QStringLiteral("brb"),
				 QStringLiteral("ending")};
	for (const QString &id : ids) {
		QVector<Cue> cues;
		const QByteArray key = SequenceConfigKey(id).toUtf8();
		const QByteArray json = QByteArray(config_get_string(config, ConfigSection, key.constData()));
		const QJsonDocument document = QJsonDocument::fromJson(json);
		if (document.isArray()) {
			for (const QJsonValue &value : document.array()) {
				const QJsonObject object = value.toObject();
				Cue cue;
				cue.atMs = std::max(0, object.value(QStringLiteral("atMs")).toInt());
				cue.label = object.value(QStringLiteral("label")).toString();
				cue.mediaSourceUuid = object.value(QStringLiteral("mediaSourceUuid")).toString();
				cue.mediaFilePath = object.value(QStringLiteral("mediaFilePath")).toString();
				cue.mediaAction =
					object.value(QStringLiteral("mediaAction")).toString(QStringLiteral("keep"));
				cue.sceneItemName = object.value(QStringLiteral("sceneItemName")).toString();
				cue.visibilityAction = object.value(QStringLiteral("visibilityAction"))
							       .toString(QStringLiteral("keep"));
				cue.updateOverlay = object.value(QStringLiteral("updateOverlay")).toBool();
				cue.transmission = object.value(QStringLiteral("transmission")).toString();
				cue.status = object.value(QStringLiteral("status")).toString();
				cue.messages = object.value(QStringLiteral("messages")).toString();
				cue.protocolAction = object.value(QStringLiteral("protocolAction")).toString();
				if (!cue.label.isEmpty())
					cues.push_back(cue);
			}
		}
		if (id == QStringLiteral("starting") && !config_has_user_value(config, ConfigSection, key.constData()))
			cues = DefaultStartingSequence();
		std::sort(cues.begin(), cues.end(), [](const Cue &a, const Cue &b) { return a.atMs < b.atMs; });
		sequences.insert(id, cues);
		if (id == QStringLiteral("starting") && !config_has_user_value(config, ConfigSection, key.constData()))
			SaveSequence(id);
	}
}

void TempestSequenceDirector::SaveSequence(const QString &sequenceId)
{
	QJsonArray array;
	for (const Cue &cue : sequences.value(sequenceId)) {
		QJsonObject object;
		object.insert(QStringLiteral("atMs"), cue.atMs);
		object.insert(QStringLiteral("label"), cue.label);
		object.insert(QStringLiteral("mediaSourceUuid"), cue.mediaSourceUuid);
		object.insert(QStringLiteral("mediaFilePath"), cue.mediaFilePath);
		object.insert(QStringLiteral("mediaAction"), cue.mediaAction);
		object.insert(QStringLiteral("sceneItemName"), cue.sceneItemName);
		object.insert(QStringLiteral("visibilityAction"), cue.visibilityAction);
		object.insert(QStringLiteral("updateOverlay"), cue.updateOverlay);
		object.insert(QStringLiteral("transmission"), cue.transmission);
		object.insert(QStringLiteral("status"), cue.status);
		object.insert(QStringLiteral("messages"), cue.messages);
		object.insert(QStringLiteral("protocolAction"), cue.protocolAction);
		array.append(object);
	}
	const QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
	const QByteArray key = SequenceConfigKey(sequenceId).toUtf8();
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, key.constData(), json.constData());
	config_save_safe(config, "tmp", nullptr);
}

QVector<TempestSequenceDirector::Cue> TempestSequenceDirector::DefaultStartingSequence() const
{
	Cue recovery;
	recovery.atMs = 0;
	recovery.label = QStringLiteral("Recovery lineage online");
	recovery.updateOverlay = true;
	recovery.transmission = QStringLiteral("STORM HORIZON RADIO // 2526");
	recovery.status = QStringLiteral("RECOVERY LINEAGE ONLINE");
	recovery.messages =
		QStringLiteral("FIRST STORM ARCHIVE // 2491\nRECOVERY CASCADE // EIGHTEEN YEARS\nLIVING ARCHIVE AWAKE");

	Cue archive;
	archive.atMs = 15000;
	archive.label = QStringLiteral("Archive carrier acquired");
	archive.updateOverlay = true;
	archive.transmission = QStringLiteral("TEMPEST MAINFRAME // ARCHIVE CARRIER");
	archive.status = QStringLiteral("RECOVERING LOST SIGNALS");
	archive.messages = QStringLiteral("FORGOTTEN IDEAS\nBROKEN MEMORIES\nWARNINGS AND CORRUPTED HISTORIES");

	Cue preservation;
	preservation.atMs = 30000;
	preservation.label = QStringLiteral("Preservation question");
	preservation.updateOverlay = true;
	preservation.transmission = QStringLiteral("TEMPEST MAINFRAME // LIVING ARCHIVE");
	preservation.status = QStringLiteral("PRESERVATION PRIORITY ACTIVE");
	preservation.messages = QStringLiteral("WHAT IS WORTH PRESERVING\nWHEN EVERY SYSTEM EVENTUALLY FAILS?");
	return {recovery, archive, preservation};
}

void TempestSequenceDirector::ChangeSequence()
{
	if (running || held)
		StopSequence();
	activeSequenceId = CurrentSequenceId();
	nextCueIndex = 0;
	RebuildCueList();
	SetStatus(QStringLiteral("%1 STACK READY").arg(CurrentSequenceLabel()));
}

QString TempestSequenceDirector::CurrentSequenceId() const
{
	return sequenceSelector ? sequenceSelector->currentData().toString() : QStringLiteral("starting");
}

QString TempestSequenceDirector::CurrentSequenceLabel() const
{
	return sequenceSelector ? sequenceSelector->currentText() : QStringLiteral("STARTING SEQUENCE");
}

void TempestSequenceDirector::RebuildCueList()
{
	QSignalBlocker blocker(cueList);
	cueList->clear();
	const QVector<Cue> cues = sequences.value(CurrentSequenceId());
	for (const Cue &cue : cues) {
		auto *item = new QListWidgetItem(
			QStringLiteral("%1  %2\n%3").arg(FormatTime(cue.atMs), cue.label, CueSummary(cue)), cueList);
		item->setToolTip(CueSummary(cue));
	}
	if (!cues.isEmpty())
		cueList->setCurrentRow(std::clamp(nextCueIndex, 0, static_cast<int>(cues.size()) - 1));
	const int duration = cues.isEmpty() ? 1 : std::max(1, cues.last().atMs);
	progress->setRange(0, duration);
	progress->setValue(0);
	SetStatus(QStringLiteral("%1 // %2 CUE%3")
			  .arg(CurrentSequenceLabel())
			  .arg(cues.size())
			  .arg(cues.size() == 1 ? QString() : QStringLiteral("S")));
}

QString TempestSequenceDirector::CueSummary(const Cue &cue) const
{
	QStringList actions;
	if (cue.mediaAction != QStringLiteral("keep"))
		actions.push_back(QStringLiteral("MEDIA %1").arg(cue.mediaAction.toUpper()));
	if (!cue.mediaFilePath.isEmpty())
		actions.push_back(QStringLiteral("ASSET %1").arg(QFileInfo(cue.mediaFilePath).fileName().toUpper()));
	if (cue.visibilityAction != QStringLiteral("keep"))
		actions.push_back(QStringLiteral("ITEM %1").arg(cue.visibilityAction.toUpper()));
	if (cue.updateOverlay)
		actions.push_back(QStringLiteral("OVERLAY UPDATE"));
	if (!cue.protocolAction.isEmpty())
		actions.push_back(QStringLiteral("HANDOFF %1").arg(cue.protocolAction.toUpper()));
	return actions.isEmpty() ? QStringLiteral("MARKER ONLY") : actions.join(QStringLiteral(" // "));
}

void TempestSequenceDirector::AddCue()
{
	Cue cue;
	const QVector<Cue> current = sequences.value(CurrentSequenceId());
	cue.atMs = current.isEmpty() ? 0 : current.last().atMs + 5000;
	cue.label = QStringLiteral("New archive cue");
	if (!OpenCueEditor(cue, QStringLiteral("Add Sequence Cue")))
		return;
	auto &cues = sequences[CurrentSequenceId()];
	cues.push_back(cue);
	std::sort(cues.begin(), cues.end(), [](const Cue &a, const Cue &b) { return a.atMs < b.atMs; });
	SaveSequence(CurrentSequenceId());
	nextCueIndex = 0;
	RebuildCueList();
}

void TempestSequenceDirector::EditSelectedCue()
{
	int row = cueList->currentRow();
	const QList<QListWidgetItem *> selected = cueList->selectedItems();
	if (!selected.isEmpty())
		row = cueList->row(selected.first());
	auto &cues = sequences[CurrentSequenceId()];
	if (row < 0 || row >= cues.size())
		return;
	Cue cue = cues[row];
	if (!OpenCueEditor(cue, QStringLiteral("Edit Sequence Cue")))
		return;
	cues[row] = cue;
	std::sort(cues.begin(), cues.end(), [](const Cue &a, const Cue &b) { return a.atMs < b.atMs; });
	SaveSequence(CurrentSequenceId());
	nextCueIndex = 0;
	RebuildCueList();
}

void TempestSequenceDirector::RemoveSelectedCue()
{
	int row = cueList->currentRow();
	const QList<QListWidgetItem *> selected = cueList->selectedItems();
	if (!selected.isEmpty())
		row = cueList->row(selected.first());
	auto &cues = sequences[CurrentSequenceId()];
	if (row < 0 || row >= cues.size())
		return;
	cues.removeAt(row);
	SaveSequence(CurrentSequenceId());
	nextCueIndex = 0;
	RebuildCueList();
}

bool TempestSequenceDirector::OpenCueEditor(Cue &cue, const QString &title)
{
	const QVector<SourceInfo> mediaSources = EnumerateMediaSources();
	const QVector<SourceInfo> videoSources = EnumerateVideoSources();
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("tempestCueEditor"));
	dialog.setWindowTitle(title);
	dialog.resize(680, 760);
	dialog.setStyleSheet(QStringLiteral(R"(
		QDialog { background: #07131e; color: #bdf6ff; }
		QGroupBox { border: 1px solid #1f506d; margin-top: 12px; padding-top: 10px; color: #45d9ff; font-weight: 700; }
		QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
		QLabel, QCheckBox { color: #9eb7c8; }
		QComboBox, QDoubleSpinBox, QLineEdit, QPlainTextEdit { min-height: 28px; background: #06101a; border: 1px solid #1f506d; color: #bdf6ff; padding: 0 6px; }
		QPushButton { min-height: 30px; border: 1px solid #1f506d; background: #0d2230; color: #bdf6ff; padding: 0 12px; font-weight: 700; }
	)"));
	auto *layout = new QVBoxLayout(&dialog);

	auto *timingGroup = new QGroupBox(QStringLiteral("CUE IDENTITY"), &dialog);
	auto *timingForm = new QFormLayout(timingGroup);
	auto *time = new QDoubleSpinBox(timingGroup);
	time->setObjectName(QStringLiteral("cueTimeSeconds"));
	time->setRange(0.0, 3600.0);
	time->setDecimals(1);
	time->setSingleStep(0.5);
	time->setSuffix(QStringLiteral(" sec"));
	time->setValue(cue.atMs / 1000.0);
	auto *label = new QLineEdit(cue.label, timingGroup);
	label->setObjectName(QStringLiteral("cueLabel"));
	timingForm->addRow(QStringLiteral("Sequence time"), time);
	timingForm->addRow(QStringLiteral("Cue label"), label);
	layout->addWidget(timingGroup);

	auto *mediaGroup = new QGroupBox(QStringLiteral("SIGNAL MEDIA"), &dialog);
	auto *mediaForm = new QFormLayout(mediaGroup);
	auto *mediaSource = new QComboBox(mediaGroup);
	mediaSource->addItem(QStringLiteral("No media source selected"), QString());
	for (const SourceInfo &source : mediaSources)
		mediaSource->addItem(source.name, source.uuid);
	auto *mediaAction = new QComboBox(mediaGroup);
	mediaAction->addItem(QStringLiteral("KEEP"), QStringLiteral("keep"));
	mediaAction->addItem(QStringLiteral("PLAY"), QStringLiteral("play"));
	mediaAction->addItem(QStringLiteral("PAUSE"), QStringLiteral("pause"));
	mediaAction->addItem(QStringLiteral("RESTART"), QStringLiteral("restart"));
	mediaAction->addItem(QStringLiteral("STOP"), QStringLiteral("stop"));
	mediaAction->addItem(QStringLiteral("PREVIOUS"), QStringLiteral("previous"));
	mediaAction->addItem(QStringLiteral("NEXT"), QStringLiteral("next"));
	SetComboData(mediaSource, cue.mediaSourceUuid, QStringLiteral("Unavailable media source"));
	SetComboData(mediaAction, cue.mediaAction);
	mediaForm->addRow(QStringLiteral("Source"), mediaSource);
	auto *fileRow = new QWidget(mediaGroup);
	auto *fileLayout = new QHBoxLayout(fileRow);
	fileLayout->setContentsMargins(0, 0, 0, 0);
	auto *mediaFile = new QLineEdit(cue.mediaFilePath, fileRow);
	mediaFile->setPlaceholderText(QStringLiteral("Optional file loaded before the media action"));
	auto *browseMedia = new QPushButton(QStringLiteral("BROWSE"), fileRow);
	fileLayout->addWidget(mediaFile, 1);
	fileLayout->addWidget(browseMedia);
	connect(browseMedia, &QPushButton::clicked, &dialog, [&dialog, mediaFile]() {
		const QString file = QFileDialog::getOpenFileName(
			&dialog, QStringLiteral("Select sequence media"), mediaFile->text(),
			QStringLiteral("Video files (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.gif);;All files (*.*)"));
		if (!file.isEmpty())
			mediaFile->setText(file);
	});
	mediaForm->addRow(QStringLiteral("Asset file"), fileRow);
	mediaForm->addRow(QStringLiteral("Action"), mediaAction);
	layout->addWidget(mediaGroup);

	auto *visibilityGroup = new QGroupBox(QStringLiteral("SCENE ITEM VISIBILITY"), &dialog);
	auto *visibilityForm = new QFormLayout(visibilityGroup);
	auto *sceneItem = new QComboBox(visibilityGroup);
	sceneItem->setEditable(true);
	sceneItem->addItem(QStringLiteral("No scene item selected"), QString());
	for (const SourceInfo &source : videoSources)
		sceneItem->addItem(source.name, source.name);
	auto *visibilityAction = new QComboBox(visibilityGroup);
	visibilityAction->addItem(QStringLiteral("KEEP"), QStringLiteral("keep"));
	visibilityAction->addItem(QStringLiteral("SHOW"), QStringLiteral("show"));
	visibilityAction->addItem(QStringLiteral("HIDE"), QStringLiteral("hide"));
	visibilityAction->addItem(QStringLiteral("TOGGLE"), QStringLiteral("toggle"));
	SetComboData(sceneItem, cue.sceneItemName, QStringLiteral("Named scene item"));
	SetComboData(visibilityAction, cue.visibilityAction);
	visibilityForm->addRow(QStringLiteral("Source name"), sceneItem);
	visibilityForm->addRow(QStringLiteral("Action"), visibilityAction);
	layout->addWidget(visibilityGroup);

	auto *overlayGroup = new QGroupBox(QStringLiteral("LORE AND OVERLAY STATE"), &dialog);
	auto *overlayForm = new QFormLayout(overlayGroup);
	auto *updateOverlay = new QCheckBox(QStringLiteral("Apply text to the matching overlay mode"), overlayGroup);
	updateOverlay->setChecked(cue.updateOverlay);
	auto *transmission = new QLineEdit(cue.transmission, overlayGroup);
	auto *status = new QLineEdit(cue.status, overlayGroup);
	auto *messages = new QPlainTextEdit(cue.messages, overlayGroup);
	messages->setMaximumHeight(90);
	overlayForm->addRow(QString(), updateOverlay);
	overlayForm->addRow(QStringLiteral("Transmission"), transmission);
	overlayForm->addRow(QStringLiteral("Status"), status);
	overlayForm->addRow(QStringLiteral("Lore lines"), messages);
	layout->addWidget(overlayGroup);

	auto *handoffGroup = new QGroupBox(QStringLiteral("PROTOCOL HANDOFF"), &dialog);
	auto *handoffForm = new QFormLayout(handoffGroup);
	auto *protocol = new QComboBox(handoffGroup);
	protocol->addItem(QStringLiteral("No handoff"), QString());
	protocol->addItem(QStringLiteral("Run STARTING protocol"), QStringLiteral("starting"));
	protocol->addItem(QStringLiteral("Run LIVE protocol"), QStringLiteral("live"));
	protocol->addItem(QStringLiteral("Run BRB protocol"), QStringLiteral("brb"));
	protocol->addItem(QStringLiteral("Run ENDING protocol"), QStringLiteral("ending"));
	SetComboData(protocol, cue.protocolAction);
	handoffForm->addRow(QStringLiteral("After cue actions"), protocol);
	layout->addWidget(handoffGroup);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	if (label->text().trimmed().isEmpty())
		return false;

	cue.atMs = static_cast<int>(time->value() * 1000.0);
	cue.label = label->text().trimmed();
	cue.mediaSourceUuid = mediaSource->currentData().toString();
	cue.mediaFilePath = mediaFile->text().trimmed();
	cue.mediaAction = mediaAction->currentData().toString();
	cue.sceneItemName = sceneItem->currentData().toString();
	if (sceneItem->currentIndex() < 0)
		cue.sceneItemName = sceneItem->currentText().trimmed();
	cue.visibilityAction = visibilityAction->currentData().toString();
	cue.updateOverlay = updateOverlay->isChecked();
	cue.transmission = transmission->text().trimmed();
	cue.status = status->text().trimmed();
	cue.messages = messages->toPlainText().trimmed();
	cue.protocolAction = protocol->currentData().toString();
	return true;
}

bool TempestSequenceDirector::EnumSource(void *data, obs_source_t *source)
{
	auto *sources = static_cast<QVector<SourceInfo> *>(data);
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	if (uuid && name)
		sources->push_back({QString::fromUtf8(uuid), QString::fromUtf8(name)});
	return true;
}

QVector<TempestSequenceDirector::SourceInfo> TempestSequenceDirector::EnumerateMediaSources() const
{
	QVector<SourceInfo> sources;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA))
				return true;
			return EnumSource(data, source);
		},
		&sources);
	std::sort(sources.begin(), sources.end(), [](const SourceInfo &a, const SourceInfo &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return sources;
}

QVector<TempestSequenceDirector::SourceInfo> TempestSequenceDirector::EnumerateVideoSources() const
{
	QVector<SourceInfo> sources;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT ||
			    !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
				return true;
			return EnumSource(data, source);
		},
		&sources);
	std::sort(sources.begin(), sources.end(), [](const SourceInfo &a, const SourceInfo &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return sources;
}

void TempestSequenceDirector::RunCurrentSequence()
{
	RunSequence(CurrentSequenceId());
}

bool TempestSequenceDirector::AddAssetCue(const QString &filePath, const QString &label, const QString &mediaSourceUuid)
{
	if (filePath.isEmpty() || mediaSourceUuid.isEmpty())
		return false;
	Cue cue;
	auto &cues = sequences[CurrentSequenceId()];
	cue.atMs = cues.isEmpty() ? 0 : cues.last().atMs + 5000;
	cue.label = label.isEmpty() ? QFileInfo(filePath).completeBaseName() : label;
	cue.mediaSourceUuid = mediaSourceUuid;
	cue.mediaFilePath = filePath;
	cue.mediaAction = QStringLiteral("restart");
	cues.push_back(cue);
	std::sort(cues.begin(), cues.end(), [](const Cue &a, const Cue &b) { return a.atMs < b.atMs; });
	SaveSequence(CurrentSequenceId());
	nextCueIndex = 0;
	RebuildCueList();
	SetStatus(QStringLiteral("ASSET QUEUED // %1").arg(cue.label.toUpper()));
	return true;
}

void TempestSequenceDirector::RunSequence(const QString &sequenceId)
{
	const QString normalized = sequenceId.toLower();
	if (!IsSequenceId(normalized)) {
		SetStatus(QStringLiteral("SEQUENCE REJECTED // UNKNOWN STACK"), true);
		return;
	}
	const int selectorIndex = sequenceSelector->findData(normalized);
	if (selectorIndex >= 0 && selectorIndex != sequenceSelector->currentIndex()) {
		QSignalBlocker blocker(sequenceSelector);
		sequenceSelector->setCurrentIndex(selectorIndex);
		activeSequenceId = normalized;
		RebuildCueList();
	}
	const QVector<Cue> cues = sequences.value(normalized);
	if (cues.isEmpty()) {
		SetStatus(QStringLiteral("%1 STACK HAS NO CUES").arg(normalized.toUpper()), true);
		return;
	}
	activeSequenceId = normalized;
	elapsedBaseMs = 0;
	nextCueIndex = 0;
	running = true;
	held = false;
	sequenceClock.restart();
	controlDeck->ActivateMode(normalized, false);
	SetRunningState(true, false);
	SetStatus(QStringLiteral("%1 SEQUENCE RUNNING").arg(normalized.toUpper()));
	TickSequence();
}

void TempestSequenceDirector::ToggleHold()
{
	if (!running && !held)
		return;
	if (running) {
		elapsedBaseMs += sequenceClock.elapsed();
		running = false;
		held = true;
		SetRunningState(false, true);
		SetStatus(QStringLiteral("SEQUENCE HELD // NEXT CUE ARMED"));
	} else {
		running = true;
		held = false;
		sequenceClock.restart();
		SetRunningState(true, false);
		SetStatus(QStringLiteral("SEQUENCE RESUMED"));
	}
}

void TempestSequenceDirector::RestartSequence()
{
	RunSequence(activeSequenceId.isEmpty() ? CurrentSequenceId() : activeSequenceId);
}

void TempestSequenceDirector::ExecuteNextCue()
{
	const QVector<Cue> cues = sequences.value(activeSequenceId.isEmpty() ? CurrentSequenceId() : activeSequenceId);
	if (nextCueIndex < 0 || nextCueIndex >= cues.size()) {
		SetStatus(QStringLiteral("NO FURTHER CUES"), true);
		return;
	}
	ExecuteCue(cues[nextCueIndex], nextCueIndex);
	++nextCueIndex;
	if (nextCueIndex >= cues.size() && (running || held)) {
		running = false;
		held = false;
		SetRunningState(false, false);
		SetStatus(QStringLiteral("SEQUENCE COMPLETE"));
	}
}

void TempestSequenceDirector::StopSequence()
{
	running = false;
	held = false;
	elapsedBaseMs = 0;
	nextCueIndex = 0;
	clockLabel->setText(QStringLiteral("00:00.0"));
	progress->setValue(0);
	SetRunningState(false, false);
	SetStatus(QStringLiteral("SEQUENCE STOPPED // STACK RESET"));
	if (!sequences.value(CurrentSequenceId()).isEmpty())
		cueList->setCurrentRow(0);
}

void TempestSequenceDirector::ControlSequence(const QString &action)
{
	const QString normalized = action.toLower();
	if (normalized.startsWith(QStringLiteral("run:")))
		RunSequence(normalized.mid(4));
	else if (normalized == QStringLiteral("hold") && running)
		ToggleHold();
	else if (normalized == QStringLiteral("resume") && held)
		ToggleHold();
	else if (normalized == QStringLiteral("togglehold"))
		ToggleHold();
	else if (normalized == QStringLiteral("next"))
		ExecuteNextCue();
	else if (normalized == QStringLiteral("restart"))
		RestartSequence();
	else if (normalized == QStringLiteral("stop"))
		StopSequence();
}

qint64 TempestSequenceDirector::CurrentElapsedMs() const
{
	return running ? elapsedBaseMs + sequenceClock.elapsed() : elapsedBaseMs;
}

void TempestSequenceDirector::TickSequence()
{
	const qint64 elapsed = CurrentElapsedMs();
	clockLabel->setText(FormatTime(elapsed));
	progress->setValue(static_cast<int>(std::min<qint64>(elapsed, progress->maximum())));
	if (!running)
		return;
	const QVector<Cue> cues = sequences.value(activeSequenceId);
	while (nextCueIndex < cues.size() && cues[nextCueIndex].atMs <= elapsed) {
		ExecuteCue(cues[nextCueIndex], nextCueIndex);
		++nextCueIndex;
	}
	if (nextCueIndex >= cues.size()) {
		running = false;
		held = false;
		SetRunningState(false, false);
		SetStatus(QStringLiteral("%1 SEQUENCE COMPLETE").arg(activeSequenceId.toUpper()));
	}
}

void TempestSequenceDirector::ExecuteCue(const Cue &cue, int index)
{
	if (!cue.mediaFilePath.isEmpty() && !cue.mediaSourceUuid.isEmpty())
		TempestMediaBay::LoadMediaFile(cue.mediaSourceUuid, cue.mediaFilePath, false, false);
	if (!cue.mediaSourceUuid.isEmpty() && cue.mediaAction != QStringLiteral("keep"))
		TempestMediaBay::ApplyMediaAction(cue.mediaSourceUuid, cue.mediaAction);
	ApplySceneItemAction(cue.sceneItemName, cue.visibilityAction);
	if (cue.updateOverlay)
		controlDeck->UpdateOverlayText(activeSequenceId, cue.transmission, cue.status, cue.messages);
	if (!cue.protocolAction.isEmpty() && matrix)
		matrix->RunProtocol(cue.protocolAction);
	if (index >= 0 && index < cueList->count())
		cueList->setCurrentRow(index);
	SetStatus(QStringLiteral("CUE %1 // %2").arg(index + 1).arg(cue.label.toUpper()));
}

void TempestSequenceDirector::ApplySceneItemAction(const QString &sourceName, const QString &action)
{
	if (sourceName.isEmpty() || action == QStringLiteral("keep") || !main)
		return;
	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return;
	const QByteArray name = sourceName.toUtf8();
	obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, name.constData());
	if (!item)
		return;
	if (action == QStringLiteral("show"))
		obs_sceneitem_set_visible(item, true);
	else if (action == QStringLiteral("hide"))
		obs_sceneitem_set_visible(item, false);
	else if (action == QStringLiteral("toggle"))
		obs_sceneitem_set_visible(item, !obs_sceneitem_visible(item));
}

void TempestSequenceDirector::SetRunningState(bool isRunning, bool isHeld)
{
	stateLabel->setText(isHeld      ? QStringLiteral("HELD")
			    : isRunning ? QStringLiteral("RUNNING")
					: QStringLiteral("IDLE"));
	runButton->setText(isRunning || isHeld ? QStringLiteral("RESTART SEQUENCE") : QStringLiteral("RUN SEQUENCE"));
	holdButton->setText(isHeld ? QStringLiteral("RESUME") : QStringLiteral("HOLD"));
	holdButton->setEnabled(isRunning || isHeld);
	stopButton->setEnabled(isRunning || isHeld);
}

void TempestSequenceDirector::SetStatus(const QString &message, bool error)
{
	statusLabel->setText(message);
	statusLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
					   .arg(error ? QStringLiteral("#ff799c") : QStringLiteral("#45d9ff")));
}

QString TempestSequenceDirector::FormatTime(qint64 milliseconds)
{
	const qint64 totalSeconds = std::max<qint64>(0, milliseconds) / 1000;
	const qint64 minutes = totalSeconds / 60;
	const qint64 seconds = totalSeconds % 60;
	const qint64 tenths = (std::max<qint64>(0, milliseconds) % 1000) / 100;
	return QStringLiteral("%1:%2.%3")
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds, 2, 10, QLatin1Char('0'))
		.arg(tenths);
}

void TempestSequenceDirector::RegisterHotkeys()
{
	UnregisterHotkeys();
	struct Definition {
		const char *name;
		const char *description;
		const char *action;
	};
	constexpr Definition definitions[] = {
		{"TempestMainframe.Sequence.RunStarting", "Tempest Mainframe: Run STARTING Sequence", "run:starting"},
		{"TempestMainframe.Sequence.RunLive", "Tempest Mainframe: Run LIVE Sequence", "run:live"},
		{"TempestMainframe.Sequence.RunBRB", "Tempest Mainframe: Run BRB Sequence", "run:brb"},
		{"TempestMainframe.Sequence.RunEnding", "Tempest Mainframe: Run ENDING Sequence", "run:ending"},
		{"TempestMainframe.Sequence.Hold", "Tempest Mainframe: Hold / Resume Sequence", "togglehold"},
		{"TempestMainframe.Sequence.Next", "Tempest Mainframe: Execute Next Cue", "next"},
		{"TempestMainframe.Sequence.Restart", "Tempest Mainframe: Restart Active Sequence", "restart"},
		{"TempestMainframe.Sequence.Stop", "Tempest Mainframe: Stop Active Sequence", "stop"},
	};
	for (const Definition &definition : definitions) {
		const obs_hotkey_id id =
			obs_hotkey_register_frontend(definition.name, definition.description, HotkeyCallback, this);
		if (id == OBS_INVALID_HOTKEY_ID)
			continue;
		hotkeyActions.insert(id, QString::fromUtf8(definition.action));
		LoadHotkey(id, QByteArray(definition.name));
	}
}

void TempestSequenceDirector::UnregisterHotkeys()
{
	for (auto it = hotkeyActions.cbegin(); it != hotkeyActions.cend(); ++it)
		obs_hotkey_unregister(it.key());
	hotkeyActions.clear();
}

void TempestSequenceDirector::LoadHotkey(obs_hotkey_id id, const QByteArray &name)
{
	if (!main)
		return;
	const char *json = config_get_string(main->Config(), "Hotkeys", name.constData());
	if (!json || !*json)
		return;
	OBSDataAutoRelease data = obs_data_create_from_json(json);
	if (!data)
		return;
	OBSDataArrayAutoRelease bindings = obs_data_get_array(data, "bindings");
	if (bindings)
		obs_hotkey_load(id, bindings);
}

void TempestSequenceDirector::HotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *director = static_cast<TempestSequenceDirector *>(data);
	const QString action = director->hotkeyActions.value(id);
	if (action.isEmpty())
		return;
	QPointer<TempestSequenceDirector> guarded(director);
	QMetaObject::invokeMethod(
		director,
		[guarded, action]() {
			if (guarded)
				guarded->ControlSequence(action);
		},
		Qt::QueuedConnection);
}
