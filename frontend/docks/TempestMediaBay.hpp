#pragma once

#include "OBSDock.hpp"

#include <obs.hpp>

#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVector>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

class TempestMediaBay : public OBSDock {
	Q_OBJECT

public:
	explicit TempestMediaBay(QWidget *parent = nullptr);
	static bool ApplyMediaAction(const QString &sourceUuid, const QString &action);
	static bool LoadMediaFile(const QString &sourceUuid, const QString &filePath, bool loop, bool restart);
	void SelectSourceUuid(const QString &sourceUuid);

private slots:
	void RefreshSources();
	void SelectSource();
	void TogglePlayback();
	void RestartPlayback();
	void StopPlayback();
	void PreviousItem();
	void NextItem();
	void BeginSeek();
	void FinishSeek();
	void RefreshPlaybackState();

private:
	struct SourceInfo {
		QString uuid;
		QString name;
	};

	static bool EnumMediaSource(void *data, obs_source_t *source);
	QVector<SourceInfo> EnumerateMediaSources() const;
	OBSSource GetSelectedSource() const;
	void BuildInterface();
	void RebuildSourceSelector(const QVector<SourceInfo> &sources);
	void SaveSelectedSource();
	void SetControlsEnabled(bool enabled);
	static QString FormatTime(int64_t milliseconds);
	static QString StateName(obs_media_state state);

	QPointer<QComboBox> sourceSelector;
	QPointer<QLabel> stateLabel;
	QPointer<QLabel> elapsedLabel;
	QPointer<QLabel> durationLabel;
	QPointer<QLabel> emptyLabel;
	QPointer<QPushButton> playPauseButton;
	QPointer<QPushButton> restartButton;
	QPointer<QPushButton> stopButton;
	QPointer<QPushButton> previousButton;
	QPointer<QPushButton> nextButton;
	QPointer<QSlider> seekSlider;
	QTimer sourceRefreshTimer;
	QTimer playbackTimer;
	QString selectedSourceUuid;
	QString sourceFingerprint;
	bool seeking = false;
};
