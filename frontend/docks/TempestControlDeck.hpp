#pragma once

#include "OBSDock.hpp"

#include <obs.h>
#include <obs-audio-controls.h>

#include <QString>

#include <atomic>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;

class TempestControlDeck : public OBSDock {
	Q_OBJECT

public:
	explicit TempestControlDeck(QWidget *parent = nullptr);
	~TempestControlDeck() override;
	void ActivateMode(const QString &modeId, bool beginCountdown = false);

private slots:
	void ChangeOverlayMode(int index);
	void QueueOverlayRender();
	void RenderOverlay();
	void StartCountdown();
	void ResetCountdown();
	void UpdateCountdownPreview();
	void CreateOrUpdateSource();
	void RefreshAudioSources();
	void AttachAudioMeter();
	void WriteAudioTelemetry();

private:
	static void AudioLevelCallback(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
				       const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);
	void BuildInterface();
	void LoadState();
	void SaveState();
	void LoadModeState(const QString &mode);
	void SaveModeState(const QString &mode);
	bool EnsureOverlayDirectory();
	void UpdateOverlayPath();
	QString BuildOverlayHtml() const;
	QString CurrentModeId() const;
	QString CurrentModeLabel() const;
	QString CurrentSourceName() const;
	void RefreshExistingSource();
	void ApplySourceSettings(obs_source_t *source);
	void SetStatus(const QString &message, bool isError = false);

	QComboBox *overlayMode = nullptr;
	QLineEdit *streamTitle = nullptr;
	QLineEdit *statusLine = nullptr;
	QPlainTextEdit *rotationMessages = nullptr;
	QSpinBox *rotationSeconds = nullptr;
	QSpinBox *countdownMinutes = nullptr;
	QLabel *countdownPreview = nullptr;
	QLabel *outputPathLabel = nullptr;
	QLabel *statusLabel = nullptr;
	QComboBox *audioSourceCombo = nullptr;
	QProgressBar *audioLevelMeter = nullptr;
	QPushButton *refreshAudioButton = nullptr;
	QPushButton *startCountdownButton = nullptr;
	QPushButton *resetCountdownButton = nullptr;
	QPushButton *createSourceButton = nullptr;
	QTimer *renderDebounce = nullptr;
	QTimer *clockTimer = nullptr;
	QTimer *telemetryTimer = nullptr;

	QString overlayDirectory;
	QString overlayPath;
	QString telemetryPath;
	QString activeModeId;
	QString configuredAudioSourceUuid;
	qint64 countdownEndMs = 0;
	bool countdownRunning = false;
	bool audioSourcesLoaded = false;
	quint64 renderRevision = 0;
	obs_volmeter_t *audioMeter = nullptr;
	std::atomic<float> audioLevel{0.0f};
	float smoothedAudioLevel = 0.0f;
};
