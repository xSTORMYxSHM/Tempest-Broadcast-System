#pragma once

#include "OBSDock.hpp"

#include <obs.h>

#include <QHash>
#include <QPointer>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QTimer;
class OBSBasic;
class TempestControlDeck;

class TempestCommandMatrix : public OBSDock {
	Q_OBJECT

public:
	TempestCommandMatrix(OBSBasic *main, TempestControlDeck *controlDeck, QWidget *parent = nullptr);

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

	void BuildInterface();
	QVector<SceneInfo> EnumerateScenes() const;
	void RebuildSceneGrid(const QVector<SceneInfo> &scenes);
	void RebuildAssignments(const QVector<SceneInfo> &scenes);
	void ExecuteProtocol(const QString &protocolId);
	void CompleteProtocolRoute(const QString &protocolId, const QString &sceneUuid, quint64 revision,
				   bool launchFailed = false);
	void ApplyAudioAction(const QString &sourceUuid, const QString &action);
	void ApplyRecordingAction(const QString &action);
	bool LaunchConfiguredProgram(const ProtocolActionConfig &config);
	void OpenActionEditor();
	void LoadActionConfigs();
	void SaveActionConfigs();
	QVector<SourceInfo> EnumerateAudioSources() const;
	QVector<SourceInfo> EnumerateTransitions() const;
	void SwitchScene(const QString &uuid, const QString &name);
	void ApplyProtocolOverlay(obs_source_t *sceneSource, const QString &sourceName);
	void UpdateActiveScene();
	void SetStatus(const QString &message, bool error = false);
	ProtocolWidgets *FindProtocol(const QString &id);

	QPointer<OBSBasic> main;
	QPointer<TempestControlDeck> controlDeck;
	QPointer<QGridLayout> sceneGrid;
	QPointer<QLabel> currentSceneLabel;
	QPointer<QLabel> statusLabel;
	QPointer<QCheckBox> isolateOverlay;
	QPointer<QCheckBox> startCountdown;
	QPointer<QTimer> refreshTimer;
	QVector<ProtocolWidgets> protocols;
	QHash<QString, QString> configuredSceneUuids;
	QHash<QString, ProtocolActionConfig> actionConfigs;
	QHash<QString, QPointer<QPushButton>> sceneButtons;
	QString sceneFingerprint;
	quint64 executionRevision = 0;
};
