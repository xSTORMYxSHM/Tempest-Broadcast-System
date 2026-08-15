#pragma once

#include "OBSDock.hpp"

#include <obs.h>

#include <QByteArray>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class OBSBasic;
class TempestControlDeck;
class TempestHUDComposer;
class TempestSequenceDirector;

class TempestCommandMatrix : public OBSDock {
	Q_OBJECT

public:
	TempestCommandMatrix(OBSBasic *main, TempestControlDeck *controlDeck, QWidget *parent = nullptr);
	~TempestCommandMatrix() override;
	void RegisterHotkeys();
	void UnregisterHotkeys();
	void RegisterExternalControls();
	void SetSequenceDirector(TempestSequenceDirector *director);
	void SetHUDComposer(TempestHUDComposer *composer);
	void RunProtocol(const QString &protocolId);

private slots:
	void RefreshScenes();
	void SaveAssignments();

private:
	struct SceneInfo {
		QString uuid;
		QString name;
	};

	struct SourceInfo {
		QString uuid;
		QString name;
	};

	struct ProtocolActionConfig {
		int delayMs = 0;
		QString transitionUuid;
		int transitionDuration = 300;
		QString audioSourceAUuid;
		QString audioActionA = QStringLiteral("keep");
		QString audioSourceBUuid;
		QString audioActionB = QStringLiteral("keep");
		QString mediaSourceUuid;
		QString mediaAction = QStringLiteral("keep");
		QString recordingAction = QStringLiteral("keep");
		bool launchEnabled = false;
		QString programPath;
		QString programArguments;
	};

	struct ProtocolWidgets {
		QString id;
		QString label;
		QString sourceName;
		QPointer<QPushButton> button;
		QPointer<QComboBox> sceneCombo;
	};

	static bool EnumScene(void *data, obs_source_t *source);
	static bool EnumSource(void *data, obs_source_t *source);
	static bool SetOverlayVisibility(obs_scene_t *scene, obs_sceneitem_t *item, void *data);
	static void ProtocolHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static void WebSocketRunProtocol(obs_data_t *request, obs_data_t *response, void *data);
	static void WebSocketRouteScene(obs_data_t *request, obs_data_t *response, void *data);
	static void WebSocketSetOverlayState(obs_data_t *request, obs_data_t *response, void *data);
	static void WebSocketRunSequence(obs_data_t *request, obs_data_t *response, void *data);
	static void WebSocketControlSequence(obs_data_t *request, obs_data_t *response, void *data);

	void BuildInterface();
	QVector<SceneInfo> EnumerateScenes() const;
	void RebuildSceneGrid(const QVector<SceneInfo> &scenes);
	void RebuildAssignments(const QVector<SceneInfo> &scenes);
	void ExecuteProtocol(const QString &protocolId);
	void CompleteProtocolRoute(const QString &protocolId, const QString &sceneUuid, quint64 revision,
				   bool launchFailed = false);
	void ApplyAudioAction(const QString &sourceUuid, const QString &action);
	void ApplyMediaAction(const QString &sourceUuid, const QString &action);
	void ApplyRecordingAction(const QString &action);
	bool LaunchConfiguredProgram(const ProtocolActionConfig &config);
	void OpenActionEditor();
	void LoadActionConfigs();
	void SaveActionConfigs();
	void LoadHotkey(obs_hotkey_id id, const QByteArray &name);
	void RouteExternalScene(const QString &uuid, const QString &name);
	void EmitRouterEvent(const char *eventName, obs_data_t *eventData);
	void SetRouterState();
	QVector<SourceInfo> EnumerateAudioSources() const;
	QVector<SourceInfo> EnumerateMediaSources() const;
	QVector<SourceInfo> EnumerateTransitions() const;
	void SwitchScene(const QString &uuid, const QString &name);
	void ApplyProtocolOverlay(obs_source_t *sceneSource, const QString &sourceName);
	void SetViewMode(const QString &mode, bool save = true);
	void UpdateActiveScene();
	void SetStatus(const QString &message, bool error = false);
	ProtocolWidgets *FindProtocol(const QString &id);

	QPointer<OBSBasic> main;
	QPointer<TempestControlDeck> controlDeck;
	QPointer<TempestHUDComposer> hudComposer;
	QPointer<TempestSequenceDirector> sequenceDirector;
	QPointer<QGridLayout> sceneGrid;
	QPointer<QLabel> currentSceneLabel;
	QPointer<QLabel> statusLabel;
	QPointer<QLabel> routerLabel;
	QPointer<QPushButton> basicViewButton;
	QPointer<QPushButton> protocolViewButton;
	QPointer<QStackedWidget> viewStack;
	QPointer<QWidget> basicViewPage;
	QPointer<QWidget> protocolViewPage;
	QPointer<QCheckBox> isolateOverlay;
	QPointer<QCheckBox> startCountdown;
	QPointer<QTimer> refreshTimer;
	QVector<ProtocolWidgets> protocols;
	QHash<QString, QString> configuredSceneUuids;
	QHash<QString, ProtocolActionConfig> actionConfigs;
	QHash<obs_hotkey_id, QString> protocolHotkeys;
	QHash<QString, QPointer<QPushButton>> sceneButtons;
	QString sceneFingerprint;
	quint64 executionRevision = 0;
	void *webSocketVendor = nullptr;
	bool webSocketReady = false;
};
