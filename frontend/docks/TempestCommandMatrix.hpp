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

	struct ProtocolWidgets {
		QString id;
		QString label;
		QString sourceName;
		QPointer<QPushButton> button;
		QPointer<QComboBox> sceneCombo;
	};

	static bool EnumScene(void *data, obs_source_t *source);
	static bool SetOverlayVisibility(obs_scene_t *scene, obs_sceneitem_t *item, void *data);

	void BuildInterface();
	QVector<SceneInfo> EnumerateScenes() const;
	void RebuildSceneGrid(const QVector<SceneInfo> &scenes);
	void RebuildAssignments(const QVector<SceneInfo> &scenes);
	void ExecuteProtocol(const QString &protocolId);
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
	QHash<QString, QPointer<QPushButton>> sceneButtons;
	QString sceneFingerprint;
};
