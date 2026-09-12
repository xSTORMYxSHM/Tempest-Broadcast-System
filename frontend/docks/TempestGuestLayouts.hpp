#pragma once

#include "OBSDock.hpp"

#include <QPointer>
#include <QString>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QShowEvent;
class QComboBox;
class OBSBasic;

class TempestGuestLayouts : public OBSDock {
	Q_OBJECT

public:
	explicit TempestGuestLayouts(OBSBasic *main, QWidget *parent = nullptr);

protected:
	void showEvent(QShowEvent *event) override;

private slots:
	void UpdateTemplateSummary();
	void CreateSelectedLayout();
	void RefreshManagedScenes();
	void OpenManagedScene(QListWidgetItem *item);

private:
	void BuildInterface();
	void SetStatus(const QString &message, bool error = false);
	QString UniqueSourceName(const QString &baseName) const;
	bool CreatePlaceholderScene(const QString &sceneName, const QString &slotLabel, int slotIndex, int canvasWidth,
				    int canvasHeight);

	QPointer<OBSBasic> main;
	QPointer<QComboBox> templateSelector;
	QPointer<QLabel> templateSummary;
	QPointer<QPushButton> createButton;
	QPointer<QPushButton> refreshButton;
	QPointer<QListWidget> managedScenes;
	QPointer<QLabel> statusLabel;
};
