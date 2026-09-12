#pragma once

#include "OBSDock.hpp"

#include <obs.hpp>

#include <QPointer>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QSpinBox;
class OBSBasic;

class TempestDestinationCoordinator : public OBSDock {
	Q_OBJECT

public:
	explicit TempestDestinationCoordinator(OBSBasic *main, QWidget *parent = nullptr);
	~TempestDestinationCoordinator() override;

	bool PrepareForMainStream(QString &error);
	void CancelPreparedStart();
	void ReloadProfile();

protected:
	void showEvent(QShowEvent *event) override;

private slots:
	void SaveSettings();
	void CheckReadiness();
	void UpdateEditorState();
	void MainStreamStarted(bool withDelay);
	void MainStreamStopping();
	void MainStreamStopped(bool withDelay);
	void HandleKickStarted();
	void HandleKickStopped(int code, const QString &lastError);

private:
	void BuildInterface();
	void LoadSettings();
	void PopulateEncoders();
	void RefreshPrimarySummary();
	void SetStatus(const QString &message, bool error = false, bool live = false);
	bool ValidateSettings(QString &error, QString *encoderId = nullptr) const;
	QString ResolveEncoderId() const;
	bool CreateKickOutput(const QString &encoderId, QString &error);
	void StartKickOutput();
	void StopKickOutput(bool force = false);
	void ReleaseKickOutput();
	bool IsBusy() const;

	static void KickOutputStarted(void *data, calldata_t *params);
	static void KickOutputStopped(void *data, calldata_t *params);

	QPointer<OBSBasic> main;
	QPointer<QLabel> primaryStatus;
	QPointer<QCheckBox> kickEnabled;
	QPointer<QLineEdit> kickServer;
	QPointer<QLineEdit> kickKey;
	QPointer<QComboBox> kickEncoder;
	QPointer<QSpinBox> kickVideoBitrate;
	QPointer<QSpinBox> kickAudioBitrate;
	QPointer<QPushButton> saveButton;
	QPointer<QPushButton> readinessButton;
	QPointer<QLabel> statusLabel;

	OBSOutputAutoRelease output;
	OBSServiceAutoRelease service;
	OBSEncoderAutoRelease videoEncoder;
	OBSEncoderAutoRelease audioEncoder;
	OBSSignal outputStartedSignal;
	OBSSignal outputStoppedSignal;
	bool prepared = false;
	bool startRequested = false;
	bool stopRequested = false;
	bool loading = false;
};
