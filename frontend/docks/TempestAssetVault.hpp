#pragma once

#include "OBSDock.hpp"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <memory>

class QCheckBox;
class QComboBox;
class QDirIterator;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class OBSBasic;
class TempestMediaBay;
class TempestSequenceDirector;

class TempestAssetVault : public OBSDock {
	Q_OBJECT

public:
	TempestAssetVault(OBSBasic *main, TempestSequenceDirector *director, TempestMediaBay *mediaBay,
			  QWidget *parent = nullptr);
	~TempestAssetVault() override;

private slots:
	void AddRootFolder();
	void RemoveRootFolder();
	void StartScan();
	void ScanBatch();
	void RebuildAssetList();
	void SelectAsset();
	void AssignBank();
	void LoadSelectedAsset();
	void AddSelectedToSequence();
	void OpenSelectedFolder();
	void ScheduleRescan(const QString &changedPath);

private:
	struct Asset {
		QString path;
		QString name;
		QString displayName;
		QString bank;
		qint64 bytes = 0;
	};

	void BuildInterface();
	void LoadState();
	void SaveRoots();
	void SaveBanks();
	void RebuildRootSelector();
	void RefreshWatchPaths();
	const Asset *SelectedAsset() const;
	QString EnsureAssetBus(const QString &filePath, bool playNow);
	QString EnsureBrowserAsset(const QString &filePath);
	void SetStatus(const QString &message, bool error = false);
	static QString NormalizePath(const QString &path);
	static QString FormatBytes(qint64 bytes);

	QPointer<OBSBasic> main;
	QPointer<TempestSequenceDirector> director;
	QPointer<TempestMediaBay> mediaBay;
	QPointer<QComboBox> rootsSelector;
	QPointer<QLineEdit> searchField;
	QPointer<QComboBox> bankFilter;
	QPointer<QListWidget> assetList;
	QPointer<QLabel> detailLabel;
	QPointer<QLabel> statusLabel;
	QPointer<QComboBox> assignBankSelector;
	QPointer<QCheckBox> loopOnBus;
	QPointer<QPushButton> loadButton;
	QPointer<QPushButton> queueButton;
	QPointer<QFileSystemWatcher> directoryWatcher;
	QTimer scanTimer;
	QTimer watcherDebounce;
	QStringList roots;
	QHash<QString, QString> banks;
	QSet<QString> scannedPaths;
	QVector<Asset> assets;
	std::unique_ptr<QDirIterator> iterator;
	int scanRootIndex = 0;
	bool scanning = false;
};
