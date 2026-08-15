#include "TempestMediaBay.hpp"

#include <OBSApp.hpp>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

#include "moc_TempestMediaBay.cpp"

namespace {
constexpr char ConfigSection[] = "TempestMediaBay";
constexpr int SeekMaximum = 10000;
} // namespace

TempestMediaBay::TempestMediaBay(QWidget *parent) : OBSDock(parent)
{
	setObjectName(QStringLiteral("tempestMediaBay"));
	setWindowTitle(QStringLiteral("Signal Media Bay"));
	setMinimumWidth(360);
	setMinimumHeight(180);
	selectedSourceUuid =
		QString::fromUtf8(config_get_string(App()->GetUserConfig(), ConfigSection, "SelectedSourceUuid"));
	BuildInterface();
	EnableContentScaling(objectName());

	sourceRefreshTimer.setInterval(1000);
	connect(&sourceRefreshTimer, &QTimer::timeout, this, &TempestMediaBay::RefreshSources);
	sourceRefreshTimer.start();
	playbackTimer.setInterval(200);
	connect(&playbackTimer, &QTimer::timeout, this, &TempestMediaBay::RefreshPlaybackState);
	playbackTimer.start();
	RefreshSources();
}

void TempestMediaBay::BuildInterface()
{
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("tempestMediaRoot"));
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestMediaRoot { background: #07131e; }
		QLabel#mediaTitle { color: #45d9ff; font-size: 14px; font-weight: 700; letter-spacing: 2px; }
		QLabel#mediaSubtitle { color: #748fa4; font-size: 10px; letter-spacing: 1px; }
		QLabel#mediaState { color: #bdf6ff; font-weight: 700; padding: 5px 8px; border: 1px solid #0c7ccb; background: #06101a; }
		QLabel#mediaTime { color: #9eb7c8; font-family: monospace; }
		QComboBox { min-height: 28px; background: #06101a; border: 1px solid #1f506d; color: #bdf6ff; padding: 0 7px; }
		QPushButton { min-height: 30px; border: 1px solid #1f506d; background: #0d2230; color: #bdf6ff; font-weight: 700; padding: 0 9px; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QPushButton:disabled { color: #40576a; border-color: #172d3d; background: #091721; }
		QSlider::groove:horizontal { height: 5px; background: #16364a; }
		QSlider::sub-page:horizontal { background: #24bce7; }
		QSlider::handle:horizontal { width: 13px; margin: -5px 0; border-radius: 6px; background: #bdf6ff; }
	)"));

	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 8, 10, 8);
	layout->setSpacing(6);

	auto *headingRow = new QHBoxLayout();
	auto *heading = new QVBoxLayout();
	auto *title = new QLabel(QStringLiteral("SIGNAL MEDIA BAY"), root);
	title->setObjectName(QStringLiteral("mediaTitle"));
	auto *subtitle = new QLabel(QStringLiteral("Native visualizer and avatar transport"), root);
	subtitle->setObjectName(QStringLiteral("mediaSubtitle"));
	heading->addWidget(title);
	heading->addWidget(subtitle);
	headingRow->addLayout(heading, 1);
	stateLabel = new QLabel(QStringLiteral("NO SIGNAL"), root);
	stateLabel->setObjectName(QStringLiteral("mediaState"));
	headingRow->addWidget(stateLabel);
	layout->addLayout(headingRow);

	auto *sourceRow = new QHBoxLayout();
	sourceSelector = new QComboBox(root);
	sourceSelector->setObjectName(QStringLiteral("tempestMediaSourceSelector"));
	sourceSelector->setAccessibleName(QStringLiteral("Signal Media Bay source"));
	connect(sourceSelector, &QComboBox::currentIndexChanged, this, &TempestMediaBay::SelectSource);
	auto *refresh = new QPushButton(QStringLiteral("REFRESH"), root);
	refresh->setObjectName(QStringLiteral("tempestMediaRefresh"));
	connect(refresh, &QPushButton::clicked, this, &TempestMediaBay::RefreshSources);
	sourceRow->addWidget(sourceSelector, 1);
	sourceRow->addWidget(refresh);
	layout->addLayout(sourceRow);

	emptyLabel = new QLabel(
		QStringLiteral("Add a Media Source, VLC Video Source, or Image Slide Show to place it on this bus."),
		root);
	emptyLabel->setObjectName(QStringLiteral("mediaSubtitle"));
	emptyLabel->setWordWrap(true);
	layout->addWidget(emptyLabel);

	auto *transportRow = new QHBoxLayout();
	previousButton = new QPushButton(QStringLiteral("PREV"), root);
	restartButton = new QPushButton(QStringLiteral("RESTART"), root);
	playPauseButton = new QPushButton(QStringLiteral("PLAY"), root);
	stopButton = new QPushButton(QStringLiteral("STOP"), root);
	nextButton = new QPushButton(QStringLiteral("NEXT"), root);
	previousButton->setObjectName(QStringLiteral("tempestMediaPrevious"));
	restartButton->setObjectName(QStringLiteral("tempestMediaRestart"));
	playPauseButton->setObjectName(QStringLiteral("tempestMediaPlayPause"));
	stopButton->setObjectName(QStringLiteral("tempestMediaStop"));
	nextButton->setObjectName(QStringLiteral("tempestMediaNext"));
	connect(previousButton, &QPushButton::clicked, this, &TempestMediaBay::PreviousItem);
	connect(restartButton, &QPushButton::clicked, this, &TempestMediaBay::RestartPlayback);
	connect(playPauseButton, &QPushButton::clicked, this, &TempestMediaBay::TogglePlayback);
	connect(stopButton, &QPushButton::clicked, this, &TempestMediaBay::StopPlayback);
	connect(nextButton, &QPushButton::clicked, this, &TempestMediaBay::NextItem);
	transportRow->addWidget(previousButton);
	transportRow->addWidget(restartButton);
	transportRow->addWidget(playPauseButton, 1);
	transportRow->addWidget(stopButton);
	transportRow->addWidget(nextButton);
	layout->addLayout(transportRow);

	auto *timelineRow = new QHBoxLayout();
	elapsedLabel = new QLabel(QStringLiteral("00:00"), root);
	durationLabel = new QLabel(QStringLiteral("00:00"), root);
	elapsedLabel->setObjectName(QStringLiteral("mediaTime"));
	durationLabel->setObjectName(QStringLiteral("mediaTime"));
	seekSlider = new QSlider(Qt::Horizontal, root);
	seekSlider->setObjectName(QStringLiteral("tempestMediaSeek"));
	seekSlider->setRange(0, SeekMaximum);
	seekSlider->setAccessibleName(QStringLiteral("Signal Media Bay timeline"));
	connect(seekSlider, &QSlider::sliderPressed, this, &TempestMediaBay::BeginSeek);
	connect(seekSlider, &QSlider::sliderReleased, this, &TempestMediaBay::FinishSeek);
	timelineRow->addWidget(elapsedLabel);
	timelineRow->addWidget(seekSlider, 1);
	timelineRow->addWidget(durationLabel);
	layout->addLayout(timelineRow);

	setWidget(root);
}

bool TempestMediaBay::EnumMediaSource(void *data, obs_source_t *source)
{
	if (!(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA))
		return true;
	auto *sources = static_cast<QVector<SourceInfo> *>(data);
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	if (uuid && name)
		sources->push_back({QString::fromUtf8(uuid), QString::fromUtf8(name)});
	return true;
}

QVector<TempestMediaBay::SourceInfo> TempestMediaBay::EnumerateMediaSources() const
{
	QVector<SourceInfo> sources;
	obs_enum_sources(EnumMediaSource, &sources);
	std::sort(sources.begin(), sources.end(), [](const SourceInfo &a, const SourceInfo &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return sources;
}

void TempestMediaBay::RefreshSources()
{
	const QVector<SourceInfo> sources = EnumerateMediaSources();
	QString fingerprint;
	for (const SourceInfo &source : sources)
		fingerprint += source.uuid + QLatin1Char('|') + source.name + QLatin1Char('\n');
	if (fingerprint != sourceFingerprint || sourceSelector->count() == 0) {
		sourceFingerprint = fingerprint;
		RebuildSourceSelector(sources);
	}
	RefreshPlaybackState();
}

void TempestMediaBay::RebuildSourceSelector(const QVector<SourceInfo> &sources)
{
	QSignalBlocker blocker(sourceSelector);
	sourceSelector->clear();
	sourceSelector->addItem(QStringLiteral("Select controllable media..."), QString());
	for (const SourceInfo &source : sources)
		sourceSelector->addItem(source.name, source.uuid);

	int index = selectedSourceUuid.isEmpty() ? -1 : sourceSelector->findData(selectedSourceUuid);
	if (index < 0 && sources.size() == 1)
		index = 1;
	sourceSelector->setCurrentIndex(std::max(index, 0));
	selectedSourceUuid = sourceSelector->currentData().toString();
	SaveSelectedSource();
}

void TempestMediaBay::SelectSource()
{
	selectedSourceUuid = sourceSelector->currentData().toString();
	SaveSelectedSource();
	RefreshPlaybackState();
}

void TempestMediaBay::SaveSelectedSource()
{
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, "SelectedSourceUuid", selectedSourceUuid.toUtf8().constData());
	config_save_safe(config, "tmp", nullptr);
}

OBSSource TempestMediaBay::GetSelectedSource() const
{
	if (selectedSourceUuid.isEmpty())
		return nullptr;
	OBSSourceAutoRelease source = obs_get_source_by_uuid(selectedSourceUuid.toUtf8().constData());
	return source ? OBSSource(source.Get()) : OBSSource();
}

bool TempestMediaBay::ApplyMediaAction(const QString &sourceUuid, const QString &action)
{
	if (sourceUuid.isEmpty() || action == QStringLiteral("keep"))
		return true;
	OBSSourceAutoRelease source = obs_get_source_by_uuid(sourceUuid.toUtf8().constData());
	if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA))
		return false;
	if (action == QStringLiteral("play"))
		obs_source_media_play_pause(source, false);
	else if (action == QStringLiteral("pause"))
		obs_source_media_play_pause(source, true);
	else if (action == QStringLiteral("restart"))
		obs_source_media_restart(source);
	else if (action == QStringLiteral("stop"))
		obs_source_media_stop(source);
	else if (action == QStringLiteral("previous"))
		obs_source_media_previous(source);
	else if (action == QStringLiteral("next"))
		obs_source_media_next(source);
	else
		return false;
	return true;
}

bool TempestMediaBay::LoadMediaFile(const QString &sourceUuid, const QString &filePath, bool loop, bool restart)
{
	if (sourceUuid.isEmpty() || filePath.isEmpty())
		return false;
	OBSSourceAutoRelease source = obs_get_source_by_uuid(sourceUuid.toUtf8().constData());
	if (!source || strcmp(obs_source_get_unversioned_id(source), "ffmpeg_source") != 0)
		return false;
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", filePath.toUtf8().constData());
	obs_data_set_bool(settings, "looping", loop);
	obs_data_set_bool(settings, "restart_on_activate", true);
	obs_data_set_bool(settings, "clear_on_media_end", true);
	obs_data_set_bool(settings, "close_when_inactive", false);
	obs_source_update(source, settings);
	if (restart)
		obs_source_media_restart(source);
	return true;
}

void TempestMediaBay::SelectSourceUuid(const QString &sourceUuid)
{
	selectedSourceUuid = sourceUuid;
	RefreshSources();
	const int index = sourceSelector->findData(sourceUuid);
	if (index >= 0) {
		QSignalBlocker blocker(sourceSelector);
		sourceSelector->setCurrentIndex(index);
	}
	SaveSelectedSource();
	RefreshPlaybackState();
}

void TempestMediaBay::TogglePlayback()
{
	OBSSource source = GetSelectedSource();
	if (!source)
		return;
	const obs_media_state state = obs_source_media_get_state(source);
	if (state == OBS_MEDIA_STATE_PLAYING || state == OBS_MEDIA_STATE_OPENING || state == OBS_MEDIA_STATE_BUFFERING)
		obs_source_media_play_pause(source, true);
	else if (state == OBS_MEDIA_STATE_STOPPED || state == OBS_MEDIA_STATE_ENDED || state == OBS_MEDIA_STATE_NONE ||
		 state == OBS_MEDIA_STATE_ERROR)
		obs_source_media_restart(source);
	else
		obs_source_media_play_pause(source, false);
	RefreshPlaybackState();
}

void TempestMediaBay::RestartPlayback()
{
	ApplyMediaAction(selectedSourceUuid, QStringLiteral("restart"));
}

void TempestMediaBay::StopPlayback()
{
	ApplyMediaAction(selectedSourceUuid, QStringLiteral("stop"));
}

void TempestMediaBay::PreviousItem()
{
	ApplyMediaAction(selectedSourceUuid, QStringLiteral("previous"));
}

void TempestMediaBay::NextItem()
{
	ApplyMediaAction(selectedSourceUuid, QStringLiteral("next"));
}

void TempestMediaBay::BeginSeek()
{
	seeking = true;
}

void TempestMediaBay::FinishSeek()
{
	OBSSource source = GetSelectedSource();
	if (source) {
		const int64_t duration = obs_source_media_get_duration(source);
		if (duration > 0)
			obs_source_media_set_time(source, duration * seekSlider->value() / SeekMaximum);
	}
	seeking = false;
	RefreshPlaybackState();
}

void TempestMediaBay::RefreshPlaybackState()
{
	OBSSource source = GetSelectedSource();
	const bool available = source && (obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA);
	SetControlsEnabled(available);
	emptyLabel->setVisible(!available);
	if (!available) {
		stateLabel->setText(sourceFingerprint.isEmpty() ? QStringLiteral("NO MEDIA SOURCES")
								: QStringLiteral("SELECT A SIGNAL"));
		elapsedLabel->setText(QStringLiteral("00:00"));
		durationLabel->setText(QStringLiteral("00:00"));
		if (!seeking)
			seekSlider->setValue(0);
		playPauseButton->setText(QStringLiteral("PLAY"));
		return;
	}

	const obs_media_state state = obs_source_media_get_state(source);
	const int64_t elapsed = std::max<int64_t>(0, obs_source_media_get_time(source));
	const int64_t duration = std::max<int64_t>(0, obs_source_media_get_duration(source));
	stateLabel->setText(QStringLiteral("%1 // %2")
				    .arg(StateName(state), QString::fromUtf8(obs_source_get_name(source)).toUpper()));
	elapsedLabel->setText(FormatTime(elapsed));
	durationLabel->setText(FormatTime(duration));
	playPauseButton->setText(state == OBS_MEDIA_STATE_PLAYING || state == OBS_MEDIA_STATE_OPENING ||
						 state == OBS_MEDIA_STATE_BUFFERING
					 ? QStringLiteral("PAUSE")
					 : QStringLiteral("PLAY"));
	seekSlider->setEnabled(duration > 0);
	if (!seeking)
		seekSlider->setValue(duration > 0 ? static_cast<int>(elapsed * SeekMaximum / duration) : 0);
}

void TempestMediaBay::SetControlsEnabled(bool enabled)
{
	previousButton->setEnabled(enabled);
	restartButton->setEnabled(enabled);
	playPauseButton->setEnabled(enabled);
	stopButton->setEnabled(enabled);
	nextButton->setEnabled(enabled);
	seekSlider->setEnabled(enabled);
}

QString TempestMediaBay::FormatTime(int64_t milliseconds)
{
	const int64_t totalSeconds = milliseconds / 1000;
	const int64_t hours = totalSeconds / 3600;
	const int64_t minutes = (totalSeconds / 60) % 60;
	const int64_t seconds = totalSeconds % 60;
	if (hours > 0)
		return QStringLiteral("%1:%2:%3")
			.arg(hours, 2, 10, QLatin1Char('0'))
			.arg(minutes, 2, 10, QLatin1Char('0'))
			.arg(seconds, 2, 10, QLatin1Char('0'));
	return QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString TempestMediaBay::StateName(obs_media_state state)
{
	switch (state) {
	case OBS_MEDIA_STATE_PLAYING:
		return QStringLiteral("PLAYING");
	case OBS_MEDIA_STATE_OPENING:
		return QStringLiteral("OPENING");
	case OBS_MEDIA_STATE_BUFFERING:
		return QStringLiteral("BUFFERING");
	case OBS_MEDIA_STATE_PAUSED:
		return QStringLiteral("PAUSED");
	case OBS_MEDIA_STATE_STOPPED:
		return QStringLiteral("STOPPED");
	case OBS_MEDIA_STATE_ENDED:
		return QStringLiteral("ENDED");
	case OBS_MEDIA_STATE_ERROR:
		return QStringLiteral("ERROR");
	default:
		return QStringLiteral("READY");
	}
}
