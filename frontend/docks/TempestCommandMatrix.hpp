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
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QTimer;
class OBSBasic;
class SourceTree;
class TempestControlDeck;
class TempestHUDComposer;
class TempestSignalReactor;
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
	void SetSignalReactor(TempestSignalReactor *reactor);
	void RunProtocol(const QString &protocolId);

protected:
	void resizeEvent(QResizeEvent *event) override;
	void contentScaleChanged() override;

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

	struct SceneSourceInfo {
		int64_t itemId = 0;
		QString uuid;
		QString name;
		QString typeName;
		bool visible = false;
		bool locked = false;
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

	struct SourceReaction {
		QString sceneUuid;
		QString sourceUuid;
		QString sourceName;
		int64_t itemId = 0;
		QString signal = QStringLiteral("master");
		QString effect = QStringLiteral("scale");
		double amount = 12.0;
		double threshold = 0.08;
		bool enabled = true;
		bool baselineCaptured = false;
		bool baselineVisible = true;
		bool visibilityActive = true;
		bool runtimeApplied = false;
		obs_transform_info baseline = {};
	};

	static bool EnumScene(void *data, obs_source_t *source);
	static bool EnumSource(void *data, obs_source_t *source);
	static bool EnumSceneSource(obs_scene_t *scene, obs_sceneitem_t *item, void *data);
	static bool SetOverlayVisibility(obs_scene_t *scene, obs_sceneitem_t *item, void *data);
	static void ProtocolHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static void WebSocketRunProtocol(obs_data_t *request, obs_data_t *response, void *data);
	static void WebSocketRouteScene(obs_data_t *request, obs_data_t *response, void *data);
	static void WebSocketSetOverlayState(obs_data_t *request, obs_data_t *response, void *data);
	static void WebSocketRunSequence(obs_data_t *request, obs_data_t *response, void *data);
	static void WebSocketControlSequence(obs_data_t *request, obs_data_t *response, void *data);
	static void WebSocketTriggerSignal(obs_data_t *request, obs_data_t *response, void *data);

	void BuildInterface();
	QVector<SceneInfo> EnumerateScenes() const;
	void RebuildSceneGrid(const QVector<SceneInfo> &scenes);
	void RefreshSourcePanel(const QString &sceneUuid, const QString &sceneName);
	void RelayoutRoutingGrids();
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
	obs_sceneitem_t *SelectedInspectorItem() const;
	obs_source_t *SelectedInspectorSource() const;
	void UpdateSourceInspector();
	void TriggerMainAction(const char *objectName);
	void RefreshSelectedBrowser();
	void ApplySelectedMediaAction(const char *action);
	void ToggleSelectedMute();
	void DuplicateSelectedSource();
	void RefreshLayoutConsole();
	void ApplyLayoutConsole();
	void NudgeSelectedSource(float deltaX, float deltaY);
	void SnapSelectedSource(int horizontal, int vertical);
	void SaveLayoutSnapshot(const char *slot);
	void RecallLayoutSnapshot(const char *slot);
	QString SelectedReactionKey() const;
	obs_sceneitem_t *FindReactionItem(const SourceReaction &reaction) const;
	void LoadSourceReactions();
	void SaveSourceReactions();
	void RefreshReactionConsole();
	void UpdateReactionAmountField();
	void ApplyReactionBinding();
	void CaptureReactionBaseline();
	void TestReactionBinding();
	void RemoveReactionBinding();
	void ApplyReactionLevels(float master, float desktop, float microphone, float beat);
	void RestoreReaction(SourceReaction &reaction);
	void RestoreAllReactions();
	void RegisterTransformUndo(obs_scene_t *scene, obs_data_t *undoData, obs_data_t *redoData,
				   const QString &actionName);
	void SetStatus(const QString &message, bool error = false);
	ProtocolWidgets *FindProtocol(const QString &id);

	QPointer<OBSBasic> main;
	QPointer<TempestControlDeck> controlDeck;
	QPointer<TempestHUDComposer> hudComposer;
	QPointer<TempestSignalReactor> signalReactor;
	QPointer<TempestSequenceDirector> sequenceDirector;
	QPointer<QGridLayout> sceneGrid;
	QPointer<QGridLayout> protocolGrid;
	QPointer<QLabel> emptySceneLabel;
	QPointer<QLabel> currentSceneLabel;
	QPointer<QLabel> sourceSceneLabel;
	QPointer<QLabel> statusLabel;
	QPointer<QLabel> routerLabel;
	QPointer<QPushButton> basicViewButton;
	QPointer<QPushButton> protocolViewButton;
	QPointer<QStackedWidget> viewStack;
	QPointer<SourceTree> sourceTree;
	QPointer<QWidget> sourceInspectorPanel;
	QPointer<QLabel> inspectorSourceLabel;
	QPointer<QLabel> inspectorTypeLabel;
	QPointer<QPushButton> inspectorFitButton;
	QPointer<QPushButton> inspectorCenterButton;
	QPointer<QPushButton> inspectorResetButton;
	QPointer<QPushButton> inspectorPropertiesButton;
	QPointer<QPushButton> inspectorFiltersButton;
	QPointer<QPushButton> inspectorRenameButton;
	QPointer<QPushButton> inspectorDuplicateButton;
	QPointer<QPushButton> inspectorInteractButton;
	QPointer<QPushButton> inspectorRefreshButton;
	QPointer<QPushButton> inspectorPlayButton;
	QPointer<QPushButton> inspectorPauseButton;
	QPointer<QPushButton> inspectorRestartButton;
	QPointer<QPushButton> inspectorMuteButton;
	QPointer<QPushButton> layoutToggleButton;
	QPointer<QWidget> layoutConsolePanel;
	QPointer<QDoubleSpinBox> layoutPosX;
	QPointer<QDoubleSpinBox> layoutPosY;
	QPointer<QDoubleSpinBox> layoutWidth;
	QPointer<QDoubleSpinBox> layoutHeight;
	QPointer<QDoubleSpinBox> layoutRotation;
	QPointer<QDoubleSpinBox> layoutCropLeft;
	QPointer<QDoubleSpinBox> layoutCropRight;
	QPointer<QDoubleSpinBox> layoutCropTop;
	QPointer<QDoubleSpinBox> layoutCropBottom;
	QPointer<QDoubleSpinBox> layoutNudgeStep;
	QPointer<QDoubleSpinBox> layoutSafeMargin;
	QPointer<QCheckBox> layoutAspectLock;
	QPointer<QPushButton> reactionToggleButton;
	QPointer<QWidget> reactionConsolePanel;
	QPointer<QCheckBox> reactionEnabled;
	QPointer<QComboBox> reactionSignal;
	QPointer<QComboBox> reactionEffect;
	QPointer<QDoubleSpinBox> reactionAmount;
	QPointer<QDoubleSpinBox> reactionThreshold;
	QPointer<QLabel> reactionStatusLabel;
	QPointer<QWidget> basicViewPage;
	QPointer<QWidget> protocolViewPage;
	QPointer<QCheckBox> isolateOverlay;
	QPointer<QCheckBox> startCountdown;
	QPointer<QTimer> refreshTimer;
	QVector<ProtocolWidgets> protocols;
	QHash<QString, QString> configuredSceneUuids;
	QHash<QString, ProtocolActionConfig> actionConfigs;
	QHash<QString, SourceReaction> sourceReactions;
	QHash<obs_hotkey_id, QString> protocolHotkeys;
	QHash<QString, QPointer<QPushButton>> sceneButtons;
	QVector<SceneInfo> currentScenes;
	QString sceneFingerprint;
	QString sourceFingerprint;
	int routingColumnCount = 0;
	quint64 executionRevision = 0;
	void *webSocketVendor = nullptr;
	bool webSocketReady = false;
	bool layoutSyncing = false;
	bool reactionSyncing = false;
	double layoutAspectRatio = 1.0;
	double reactionPhase = 0.0;
	QString reactionTestKey;
	qint64 reactionTestUntil = 0;
};
