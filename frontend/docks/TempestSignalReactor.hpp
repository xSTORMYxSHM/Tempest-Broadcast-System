#pragma once

#include "OBSDock.hpp"

#include <obs-audio-controls.h>

#include <QPointer>
#include <QString>

#include <atomic>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

class TempestSignalReactor : public OBSDock {
	Q_OBJECT

public:
	explicit TempestSignalReactor(QWidget *parent = nullptr);
	~TempestSignalReactor() override;

private slots:
	void RefreshAudioSources();
	void AttachDesktopSource();
	void AttachMicrophoneSource();
	void PublishTelemetry();
	void SaveState();

private:
	struct SignalChannel {
		obs_volmeter_t *meter = nullptr;
		std::atomic<float> rawLevel{0.0f};
		float smoothedLevel = 0.0f;
	};

	static void AudioLevelCallback(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
				       const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);
	void BuildInterface();
	void LoadState();
	bool EnsureOutputDirectory();
	void CreateMeter(SignalChannel &channel);
	void DestroyMeter(SignalChannel &channel);
	void AttachChannel(SignalChannel &channel, QComboBox *selector, const QString &label);
	void TriggerManualPulse(float strength);
	void SetStatus(const QString &message, bool error = false);

	QPointer<QCheckBox> reactorEnabled;
	QPointer<QComboBox> desktopSource;
	QPointer<QComboBox> microphoneSource;
	QPointer<QDoubleSpinBox> desktopSensitivity;
	QPointer<QDoubleSpinBox> microphoneSensitivity;
	QPointer<QDoubleSpinBox> smoothing;
	QPointer<QProgressBar> desktopMeter;
	QPointer<QProgressBar> microphoneMeter;
	QPointer<QProgressBar> masterMeter;
	QPointer<QLabel> statusLabel;
	QPointer<QPushButton> pulseButton;
	QPointer<QPushButton> peakButton;
	QPointer<QTimer> telemetryTimer;

	SignalChannel desktopChannel;
	SignalChannel microphoneChannel;
	QString configuredDesktopUuid;
	QString configuredMicrophoneUuid;
	QString telemetryPath;
	float manualPulse = 0.0f;
	bool audioSourcesLoaded = false;
	bool loadingState = false;
};
