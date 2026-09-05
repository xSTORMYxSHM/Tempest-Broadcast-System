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
	bool TriggerExternalEvent(const QString &type, const QString &name = QString(), float strength = 0.0f,
				  int durationMs = 0, const QString &circuit = QString(),
				  const QString &accent = QString(), const QString &effect = QString(),
				  const QString &origin = QStringLiteral("warudo"), const QString &dedupeId = QString(),
				  int cooldownMs = -1);
	void ClearExternalEvent();
	bool ExternalEventBridgeArmed() const;
	void SetWebSocketReady(bool ready);
	void SetSourceBindingSummary(int total, int enabled, int active, int activeEnabled);
	void SetSourceCircuitSummary(const QString &circuit, int total, int scoped, int enabled);
	void SetSourceCircuitActivity(const QString &circuit, float activity);
	bool SourceNetworkArmed() const;
	float SourceNetworkIntensity() const;
	bool SourceNetworkActiveSceneOnly() const;
	QString SourceNetworkCircuitProfile() const;
	float SourceNetworkCircuitGain(const QString &circuit) const;
	QString SourceNetworkSoloCircuit() const;
	void SetSourceNetworkArmed(bool armed);
	void SetSourceNetworkIntensity(float intensity);
	void SetSourceNetworkActiveSceneOnly(bool activeSceneOnly);
	void SetSourceNetworkCircuitProfile(const QString &profile);
	void SetSourceNetworkSoloCircuit(const QString &circuit);
	void CycleSourceNetworkCircuitProfile();
	void ResetSourceNetworkCircuitGains();
	void TestSourceNetwork();
	void DisarmAndRestoreSourceNetwork();

signals:
	void PulseTriggered(float strength, const QString &origin);
	void ExternalEventTriggered(const QString &type, const QString &name, float strength, int durationMs,
				    const QString &circuit, const QString &accent, const QString &effect,
				    const QString &origin);
	void ExternalEventCleared();
	void LevelsUpdated(float master, float desktop, float microphone, float beat);
	void SourceNetworkArmedChanged(bool armed);
	void SourceNetworkIntensityChanged(float intensity);
	void SourceNetworkScopeChanged(bool activeSceneOnly);
	void SourceNetworkCircuitProfileChanged(const QString &profile);
	void SourceNetworkCircuitGainsChanged(float core, float frame, float chat, float plates, float alerts);
	void SourceNetworkCircuitSoloChanged(const QString &circuit);
	void SourceNetworkTestRequested();
	void SourceNetworkCircuitTestRequested(const QString &circuit);
	void SourceNetworkRestoreRequested();

private slots:
	void RefreshAudioSources();
	void AttachDesktopSource();
	void AttachMediaSource();
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
	void ApplyReactivityProfile();
	void ToggleSourceNetworkCircuitMute(const QString &circuit);
	void ToggleSourceNetworkCircuitSolo(const QString &circuit);
	void SetStatus(const QString &message, bool error = false);

	QPointer<OBSBasic> main;
	QPointer<QCheckBox> reactorEnabled;
	QPointer<QComboBox> desktopSource;
	QPointer<QComboBox> mediaSource;
	QPointer<QComboBox> microphoneSource;
	QPointer<QDoubleSpinBox> desktopSensitivity;
	QPointer<QDoubleSpinBox> mediaSensitivity;
	QPointer<QDoubleSpinBox> microphoneSensitivity;
	QPointer<QDoubleSpinBox> beatSensitivity;
	QPointer<QDoubleSpinBox> smoothing;
	QPointer<QComboBox> reactionProfile;
	QPointer<QComboBox> reactionPalette;
	QPointer<QDoubleSpinBox> reactionThreshold;
	QPointer<QDoubleSpinBox> reactionAttack;
	QPointer<QDoubleSpinBox> reactionMotion;
	QPointer<QDoubleSpinBox> reactionGlow;
	QPointer<QDoubleSpinBox> reactionTestStrength;
	QPointer<QCheckBox> reducedMotion;
	QPointer<QProgressBar> desktopMeter;
	QPointer<QProgressBar> mediaMeter;
	QPointer<QProgressBar> microphoneMeter;
	QPointer<QProgressBar> beatMeter;
	QPointer<QProgressBar> masterMeter;
	QPointer<QCheckBox> sourceNetworkArmed;
	QPointer<QCheckBox> sourceNetworkActiveSceneOnly;
	QPointer<QComboBox> sourceNetworkCircuitProfile;
	QHash<QString, QPointer<QDoubleSpinBox>> sourceNetworkCircuitGains;
	QHash<QString, QPointer<QProgressBar>> sourceNetworkCircuitMeters;
	QHash<QString, QPointer<QLabel>> sourceNetworkCircuitStates;
	QHash<QString, QPointer<QPushButton>> sourceNetworkCircuitMuteButtons;
	QHash<QString, QPointer<QPushButton>> sourceNetworkCircuitSoloButtons;
	QHash<QString, QPointer<QPushButton>> sourceNetworkCircuitTestButtons;
	QHash<QString, float> sourceNetworkCircuitActivities;
	QHash<QString, double> sourceNetworkCircuitRestoreGains;
	QHash<QString, int> sourceNetworkCircuitTotals;
	QHash<QString, int> sourceNetworkCircuitScoped;
	QHash<QString, int> sourceNetworkCircuitEnabled;
	QPointer<QDoubleSpinBox> sourceNetworkIntensity;
	QPointer<QCheckBox> externalEventBridgeArmed;
	QPointer<QComboBox> externalDanceCircuit;
	QPointer<QComboBox> externalTwitchCircuit;
	QPointer<QDoubleSpinBox> externalEventCooldown;
	QPointer<QLabel> externalEventStatus;
	QPointer<QLabel> sourceNetworkStatus;
	QPointer<QLabel> statusLabel;
	QPointer<QLabel> controlLabel;
	QPointer<QPushButton> pulseButton;
	QPointer<QPushButton> peakButton;
	QPointer<QTimer> telemetryTimer;

	SignalChannel desktopChannel;
	SignalChannel mediaChannel;
	SignalChannel microphoneChannel;
	QHash<obs_hotkey_id, float> pulseHotkeys;
	QHash<obs_hotkey_id, QString> networkHotkeys;
	QHash<obs_hotkey_id, QString> externalEventHotkeys;
	QHash<QString, qint64> externalEventLastTrigger;
	QString configuredDesktopUuid;
	QString configuredMediaUuid;
	QString configuredMicrophoneUuid;
	QString telemetryPath;
	float manualPulse = 0.0f;
	float beatBaseline = 0.0f;
	float beatLevel = 0.0f;
	QString sourceNetworkSoloCircuit;
	QString activeExternalEventType;
	QString activeExternalEventName;
	QString activeExternalEventCircuit;
	QString activeExternalEventAccent;
	QString activeExternalEventEffect;
	QString activeExternalEventOrigin;
	qint64 activeExternalEventUntil = 0;
	quint64 externalEventSequence = 0;
	float activeExternalEventStrength = 0.0f;
	QString audioSourceFingerprint;
	bool audioSourcesLoaded = false;
	bool loadingState = false;
	bool webSocketReady = false;
};
