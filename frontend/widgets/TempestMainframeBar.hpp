#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QPointer>

class QLabel;
class QPushButton;
class QResizeEvent;
class QTimer;
class OBSBasic;

class TempestMainframeBar : public QFrame {
	Q_OBJECT

public:
	explicit TempestMainframeBar(OBSBasic *main);
	void SetCommandWorkspace(bool commandMode);
	void SetUiScalePercent(int percent);
	void SetResponsiveProfile(const QString &profileLabel, bool automatic);
	void SetCanvasVisible(bool visible);
	void SetCanvasControlEnabled(bool enabled);

signals:
	void WorkspaceRequested(bool commandMode);
	void DockManagerRequested();
	void UiScaleRequested(int percent);
	void CanvasVisibilityRequested(bool visible);

protected:
	void resizeEvent(QResizeEvent *event) override;

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
	void ApplyAdaptiveVisibility();
	QString ElapsedText() const;

	QPointer<OBSBasic> main;
	QPointer<QLabel> stateLabel;
	QPointer<QLabel> detailLabel;
	QPointer<QLabel> clockLabel;
	QPointer<QLabel> sceneLabel;
	QPointer<QLabel> telemetryLabel;
	QPointer<QLabel> recordStateLabel;
	QPointer<QLabel> identitySublineLabel;
	QPointer<QPushButton> commandWorkspaceButton;
	QPointer<QPushButton> engineeringWorkspaceButton;
	QPointer<QPushButton> dockManagerButton;
	QPointer<QPushButton> uiScaleResetButton;
	QPointer<QPushButton> canvasButton;
	QPointer<QPushButton> armButton;
	QPointer<QPushButton> streamButton;
	QPointer<QPushButton> recordButton;
	QPointer<QPushButton> emergencyButton;
	QPointer<QTimer> telemetryTimer;
	QElapsedTimer streamElapsed;
	QString baseStyleSheet;
	int uiScalePercent = 100;
	int baseMinimumHeight = 68;
	int baseMaximumHeight = 76;
	bool streamStarting = false;
	bool streamStopping = false;
	bool recording = false;
};
