#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QPointer>

class QLabel;
class QPushButton;
class QTimer;
class OBSBasic;

class TempestMainframeBar : public QFrame {
	Q_OBJECT

public:
	explicit TempestMainframeBar(OBSBasic *main);
	void SetCommandWorkspace(bool commandMode);

signals:
	void WorkspaceRequested(bool commandMode);
	void DockManagerRequested();

private slots:
	void ToggleArm(bool armed);
	void TriggerStream();
	void TriggerRecord();
	void EmergencyCut();
	void RefreshTelemetry();

private:
	void BuildInterface();
	void SetTransmissionState(const QString &state, const QString &detail, const QString &color);
	void UpdateActionButtons();
	QString ElapsedText() const;

	QPointer<OBSBasic> main;
	QPointer<QLabel> stateLabel;
	QPointer<QLabel> detailLabel;
	QPointer<QLabel> clockLabel;
	QPointer<QLabel> sceneLabel;
	QPointer<QLabel> telemetryLabel;
	QPointer<QLabel> recordStateLabel;
	QPointer<QPushButton> commandWorkspaceButton;
	QPointer<QPushButton> engineeringWorkspaceButton;
	QPointer<QPushButton> dockManagerButton;
	QPointer<QPushButton> armButton;
	QPointer<QPushButton> streamButton;
	QPointer<QPushButton> recordButton;
	QPointer<QPushButton> emergencyButton;
	QPointer<QTimer> telemetryTimer;
	QElapsedTimer streamElapsed;
	bool streamStarting = false;
	bool streamStopping = false;
	bool recording = false;
};
