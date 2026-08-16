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
	void SetSourceBindingSummary(int total, int enabled, int active, int activeEnabled);
	void SetSourceCircuitSummary(const QString &circuit, int total, int scoped, int enabled);
	void SetSourceCircuitActivity(const QString &circuit, float activity);
	bool SourceNetworkArmed() const;
	float SourceNetworkIntensity() const;
	bool SourceNetworkActiveSceneOnly() const;
	QString SourceNetworkCircuitProfile() const;
	float SourceNetworkCircuitGain(const QString &circuit) const;
	void SetSourceNetworkArmed(bool armed);
	void SetSourceNetworkIntensity(float intensity);
	void SetSourceNetworkActiveSceneOnly(bool activeSceneOnly);
	void SetSourceNetworkCircuitProfile(const QString &profile);
	void CycleSourceNetworkCircuitProfile();
	void ResetSourceNetworkCircuitGains();
	void TestSourceNetwork();
	void DisarmAndRestoreSourceNetwork();

signals:
	void PulseTriggered(float strength, const QString &origin);
	void LevelsUpdated(float master, float desktop, float microphone, float beat);
	void SourceNetworkArmedChanged(bool armed);
	void SourceNetworkIntensityChanged(float intensity);
	void SourceNetworkScopeChanged(bool activeSceneOnly);
	void SourceNetworkCircuitProfileChanged(const QString &profile);
	void SourceNetworkCircuitGainsChanged(float core, float frame, float chat, float plates, float alerts);
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
	bool SourceNetworkCircuitActive(const QString &circuit) const;
	QString SourceNetworkCircuitState(const QString &circuit) const;
	void RefreshSourceNetworkCircuitMonitor(const QString &circuit);
	void RefreshSourceNetworkCircuitMonitors();
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
	QPointer<QCheckBox> sourceNetworkActiveSceneOnly;
	QPointer<QComboBox> sourceNetworkCircuitProfile;
	QHash<QString, QPointer<QDoubleSpinBox>> sourceNetworkCircuitGains;
	QHash<QString, QPointer<QProgressBar>> sourceNetworkCircuitMeters;
	QHash<QString, QPointer<QLabel>> sourceNetworkCircuitStates;
	QHash<QString, float> sourceNetworkCircuitActivities;
	QHash<QString, int> sourceNetworkCircuitTotals;
	QHash<QString, int> sourceNetworkCircuitScoped;
	QHash<QString, int> sourceNetworkCircuitEnabled;
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
