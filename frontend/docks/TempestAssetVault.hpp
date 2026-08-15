#pragma once

#include "OBSDock.hpp"

#include <QHash>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <memory>

class QCheckBox;
class QComboBox;
class QDirIterator;
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

private:
	struct Asset {
		QString path;
		QString name;
		QString bank;
		qint64 bytes = 0;
	};

	void BuildInterface();
	void LoadState();
	void SaveRoots();
	void SaveBanks();
	void RebuildRootSelector();
	const Asset *SelectedAsset() const;
	QString EnsureAssetBus(const QString &filePath, bool playNow);
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
	QTimer scanTimer;
	QStringList roots;
	QHash<QString, QString> banks;
	QVector<Asset> assets;
	std::unique_ptr<QDirIterator> iterator;
	int scanRootIndex = 0;
	bool scanning = false;
};
