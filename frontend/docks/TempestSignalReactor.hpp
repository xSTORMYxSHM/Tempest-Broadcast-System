#pragma once

#include "OBSDock.hpp"

#include <obs-audio-controls.h>

#include <QPointer>
#include <QHash>
#include <QString>

#include <atomic>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class OBSBasic;

class TempestSignalReactor : public OBSDock {
	Q_OBJECT

public:
	explicit TempestSignalReactor(OBSBasic *main, QWidget *parent = nullptr);
	~TempestSignalReactor() override;
	void RegisterHotkeys();
	void UnregisterHotkeys();
	void TriggerPulse(float strength, const QString &origin = QStringLiteral("external"));
	void SetWebSocketReady(bool ready);
	void SetSourceBindingSummary(int total, int enabled);
	bool SourceNetworkArmed() const;
	float SourceNetworkIntensity() const;

signals:
	void PulseTriggered(float strength, const QString &origin);
	void LevelsUpdated(float master, float desktop, float microphone, float beat);
	void SourceNetworkArmedChanged(bool armed);
	void SourceNetworkIntensityChanged(float intensity);
	void SourceNetworkTestRequested();
	void SourceNetworkRestoreRequested();

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
	static void HotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	void BuildInterface();
	void LoadState();
	void LoadHotkey(obs_hotkey_id id, const QByteArray &name);
	bool EnsureOutputDirectory();
	void CreateMeter(SignalChannel &channel);
	void DestroyMeter(SignalChannel &channel);
	void AttachChannel(SignalChannel &channel, QComboBox *selector, const QString &label);
	void UpdateControlBridgeState();
	void SetStatus(const QString &message, bool error = false);

	QPointer<OBSBasic> main;
	QPointer<QCheckBox> reactorEnabled;
	QPointer<QComboBox> desktopSource;
	QPointer<QComboBox> microphoneSource;
	QPointer<QDoubleSpinBox> desktopSensitivity;
	QPointer<QDoubleSpinBox> microphoneSensitivity;
	QPointer<QDoubleSpinBox> beatSensitivity;
	QPointer<QDoubleSpinBox> smoothing;
	QPointer<QProgressBar> desktopMeter;
	QPointer<QProgressBar> microphoneMeter;
	QPointer<QProgressBar> beatMeter;
	QPointer<QProgressBar> masterMeter;
	QPointer<QCheckBox> sourceNetworkArmed;
	QPointer<QDoubleSpinBox> sourceNetworkIntensity;
	QPointer<QLabel> sourceNetworkStatus;
	QPointer<QLabel> statusLabel;
	QPointer<QLabel> controlLabel;
	QPointer<QPushButton> pulseButton;
	QPointer<QPushButton> peakButton;
	QPointer<QTimer> telemetryTimer;

	SignalChannel desktopChannel;
	SignalChannel microphoneChannel;
	QHash<obs_hotkey_id, float> pulseHotkeys;
	QHash<obs_hotkey_id, QString> networkHotkeys;
	QString configuredDesktopUuid;
	QString configuredMicrophoneUuid;
	QString telemetryPath;
	float manualPulse = 0.0f;
	float beatBaseline = 0.0f;
	float beatLevel = 0.0f;
	bool audioSourcesLoaded = false;
	bool loadingState = false;
	bool webSocketReady = false;
};
