#pragma once

#include <QDockWidget>
#include <QPointer>
#include <QSize>

class QCloseEvent;
class QEvent;
class QScrollArea;
class QShowEvent;
class QString;
class QToolButton;
class QWidget;

class OBSDock : public QDockWidget {
	Q_OBJECT

public:
	inline OBSDock(QWidget *parent = nullptr) : QDockWidget(parent) {}
	inline OBSDock(const QString &title, QWidget *parent = nullptr) : QDockWidget(title, parent) {}
	void EnableContentScaling(const QString &configKey);
	int ContentScalePercent() const { return contentScalePercent; }
	bool HasContentScaling() const { return !scaledContent.isNull(); }
	void SetContentScalePercent(int percent, bool saveDockValue = false)
	{
		ApplyContentScale(percent, saveDockValue);
	}

	virtual void closeEvent(QCloseEvent *event);
	virtual void showEvent(QShowEvent *event);
	bool eventFilter(QObject *watched, QEvent *event) override;
	virtual void contentScaleChanged() {}

signals:
	void ApplicationScaleRequested(int percent);

private:
	void CaptureScaleMetrics();
	void ApplyContentScale(int percent, bool save = true);
	void RequestApplicationScale(int percent);
	void InstallScaleEventFilters();

	QPointer<QWidget> scaledContent;
	QPointer<QScrollArea> contentScroll;
	QPointer<QToolButton> scaleResetButton;
	QString contentScaleConfigKey;
	QSize baseDockMinimumSize;
	int contentScalePercent = 100;
	bool scaleRefreshQueued = false;
};
