#pragma once

#include "OBSDock.hpp"

#include <obs.h>
#include <QJsonObject>
#include <QString>

class QComboBox;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class TempestControlDeck : public OBSDock {
	Q_OBJECT

public:
	explicit TempestControlDeck(QWidget *parent = nullptr);
	void ActivateMode(const QString &modeId, bool beginCountdown = false);
	void UpdateOverlayText(const QString &modeId, const QString &transmission, const QString &status,
			       const QString &messages);

private slots:
	void ChangeOverlayMode(int index);
	void QueueOverlayRender();
	void RenderOverlay();
	void StartCountdown();
	void ResetCountdown();
	void UpdateCountdownPreview();
	void CreateOrUpdateSource();
	void ComposeRotationMessage();
	void OpenRotationLibrary();
	void OpenMessageVariables();
	void ChangeContentProfile(int index);
	void NewContentProfile();
	void DuplicateContentProfile();
	void OpenContentProfileFolder();

private:
	void BuildInterface();
	void LoadState();
	void SaveState();
	void LoadModeState(const QString &mode);
	void SaveModeState(const QString &mode);
	bool EnsureOverlayDirectory();
	bool EnsureRotationLibrary();
	bool EnsureProfilesDirectory();
	bool EnsureMessageFile(const QString &path, const QString &description) const;
	bool EnsureVariablesFile(const QString &path, const QString &description) const;
	QJsonObject CreateProfileDocument(const QString &name, bool migrateExisting) const;
	bool WriteProfileDocument(const QString &profileId, const QJsonObject &document);
	bool LoadContentProfile(const QString &profileId);
	void RefreshContentProfiles(const QString &selectProfile = QString());
	void WatchContentProfileFiles();
	QString ProfileFilePath(const QString &profileId) const;
	QString ProfileMessagePath(const QString &profileId) const;
	QString ProfileVariablesPath(const QString &profileId) const;
	QStringList MessageLibraryRelativePaths(bool vaultElement) const;
	QStringList VariableLibraryRelativePaths(bool vaultElement) const;
	static QString ProfileIdForName(const QString &name);
	QStringList ReadRotationMessages(const QString &fallback = QString(), const QString &modeId = QString()) const;
	QJsonObject ReadMessageVariables() const;
	bool AppendRotationMessages(const QString &messages, const QString &playlist = QStringLiteral("common"));
	void RefreshMessagePlaylists(const QString &selectPlaylist = QString());
	void RefreshRotationLibrarySummary();
	void UpdateOverlayPath();
	QString BuildOverlayStateJson(const QString &modeId = QString(), bool vaultElement = false) const;
	QString BuildOverlayHtml(const QString &modeId = QString()) const;
	QString BuildVaultElementHtml(const QString &elementId, int width, int height) const;
	bool RenderVaultElements();
	QString CurrentModeId() const;
	QString CurrentModeLabel() const;
	QString CurrentSourceName() const;
	void RefreshExistingSource();
	void ApplySourceSettings(obs_source_t *source);
	void SetStatus(const QString &message, bool isError = false);

	QComboBox *contentProfile = nullptr;
	QComboBox *overlayMode = nullptr;
	QLineEdit *streamTitle = nullptr;
	QLineEdit *statusLine = nullptr;
	QPushButton *rotationLibraryButton = nullptr;
	QPushButton *composeMessageButton = nullptr;
	QPushButton *messageVariablesButton = nullptr;
	QComboBox *messageOrder = nullptr;
	QComboBox *messagePlaylist = nullptr;
	QSpinBox *rotationSeconds = nullptr;
	QSpinBox *countdownMinutes = nullptr;
	QLabel *countdownPreview = nullptr;
	QLabel *outputPathLabel = nullptr;
	QLabel *statusLabel = nullptr;
	QPushButton *startCountdownButton = nullptr;
	QPushButton *resetCountdownButton = nullptr;
	QPushButton *createSourceButton = nullptr;
	QPushButton *newProfileButton = nullptr;
	QPushButton *duplicateProfileButton = nullptr;
	QPushButton *openProfileFolderButton = nullptr;
	QFileSystemWatcher *rotationLibraryWatcher = nullptr;
	QTimer *renderDebounce = nullptr;
	QTimer *clockTimer = nullptr;

	QString overlayDirectory;
	QString overlayPath;
	QString profilesDirectory;
	QString globalRotationLibraryPath;
	QString globalMessageVariablesPath;
	QString rotationLibraryPath;
	QString messageVariablesPath;
	QString activeProfileId;
	QJsonObject activeProfileDocument;
	QString activeModeId;
	qint64 countdownEndMs = 0;
	bool countdownRunning = false;
	bool loadingProfile = false;
	quint64 renderRevision = 0;
};
