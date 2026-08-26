#pragma once

#include "OBSDock.hpp"

#include <obs.h>

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVector>

class QLabel;
class QListWidget;
class QComboBox;
class QProgressBar;
class QPushButton;
class OBSBasic;
class TempestCommandMatrix;
class TempestControlDeck;

class TempestSequenceDirector : public OBSDock {
	Q_OBJECT

public:
	TempestSequenceDirector(OBSBasic *main, TempestCommandMatrix *matrix, TempestControlDeck *controlDeck,
				QWidget *parent = nullptr);
	~TempestSequenceDirector() override;
	void RegisterHotkeys();
	void UnregisterHotkeys();
	bool AddAssetCue(const QString &filePath, const QString &label, const QString &mediaSourceUuid);

public slots:
	void RunSequence(const QString &sequenceId);
	void ControlSequence(const QString &action);

private slots:
	void ChangeSequence();
	void AddCue();
	void EditSelectedCue();
	void RemoveSelectedCue();
	void ToggleHold();
	void RunCurrentSequence();
	void RestartSequence();
	void ExecuteNextCue();
	void StopSequence();
	void TickSequence();

private:
	struct Cue {
		int atMs = 0;
		QString label;
		QString mediaSourceUuid;
		QString mediaFilePath;
		QString mediaAction = QStringLiteral("keep");
		QString sceneItemName;
		QString visibilityAction = QStringLiteral("keep");
		bool updateOverlay = false;
		QString transmission;
		QString status;
		QString messages;
		QString protocolAction;
	};

	struct SourceInfo {
		QString uuid;
		QString name;
	};

	static void HotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static bool EnumSource(void *data, obs_source_t *source);
	void BuildInterface();
	void LoadSequences();
	void SaveSequence(const QString &sequenceId);
	QVector<Cue> DefaultStartingSequence() const;
	void RebuildCueList();
	bool OpenCueEditor(Cue &cue, const QString &title);
	void ExecuteCue(const Cue &cue, int index);
	void ApplySceneItemAction(const QString &sourceName, const QString &action);
	QVector<SourceInfo> EnumerateMediaSources() const;
	QVector<SourceInfo> EnumerateVideoSources() const;
	QString CurrentSequenceId() const;
	QString CurrentSequenceLabel() const;
	QString CueSummary(const Cue &cue) const;
	qint64 CurrentElapsedMs() const;
	void SetStatus(const QString &message, bool error = false);
	void SetRunningState(bool running, bool held);
	void LoadHotkey(obs_hotkey_id id, const QByteArray &name);
	static QString FormatTime(qint64 milliseconds);

	QPointer<OBSBasic> main;
	QPointer<TempestCommandMatrix> matrix;
	QPointer<TempestControlDeck> controlDeck;
	QPointer<QComboBox> sequenceSelector;
	QPointer<QListWidget> cueList;
	QPointer<QLabel> clockLabel;
	QPointer<QLabel> stateLabel;
	QPointer<QLabel> statusLabel;
	QPointer<QProgressBar> progress;
	QPointer<QPushButton> runButton;
	QPointer<QPushButton> holdButton;
	QPointer<QPushButton> nextButton;
	QPointer<QPushButton> stopButton;
	QTimer sequenceTimer;
	QElapsedTimer sequenceClock;
	QHash<QString, QVector<Cue>> sequences;
	QHash<obs_hotkey_id, QString> hotkeyActions;
	QString activeSequenceId;
	qint64 elapsedBaseMs = 0;
	int nextCueIndex = 0;
	bool running = false;
	bool held = false;
};
