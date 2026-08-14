#pragma once

#include "OBSDock.hpp"

#include <obs.h>

#include <QString>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class TempestControlDeck : public OBSDock {
	Q_OBJECT

public:
	explicit TempestControlDeck(QWidget *parent = nullptr);

private slots:
	void QueueOverlayRender();
	void RenderOverlay();
	void StartCountdown();
	void ResetCountdown();
	void UpdateCountdownPreview();
	void CreateOrUpdateSource();

private:
	void BuildInterface();
	void LoadState();
	void SaveState();
	bool EnsureOverlayDirectory();
	QString BuildOverlayHtml() const;
	void RefreshExistingSource();
	void ApplySourceSettings(obs_source_t *source);
	void SetStatus(const QString &message, bool isError = false);

	QLineEdit *streamTitle = nullptr;
	QLineEdit *statusLine = nullptr;
	QPlainTextEdit *rotationMessages = nullptr;
	QSpinBox *rotationSeconds = nullptr;
	QSpinBox *countdownMinutes = nullptr;
	QLabel *countdownPreview = nullptr;
	QLabel *outputPathLabel = nullptr;
	QLabel *statusLabel = nullptr;
	QPushButton *startCountdownButton = nullptr;
	QPushButton *resetCountdownButton = nullptr;
	QPushButton *createSourceButton = nullptr;
	QTimer *renderDebounce = nullptr;
	QTimer *clockTimer = nullptr;

	QString overlayDirectory;
	QString overlayPath;
	qint64 countdownEndMs = 0;
	bool countdownRunning = false;
	quint64 renderRevision = 0;
};
