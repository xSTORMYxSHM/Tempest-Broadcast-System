#include "BrowserDock.hpp"

#include <QCloseEvent>
#include "moc_BrowserDock.cpp"

void BrowserDock::closeEvent(QCloseEvent *event)
{
	OBSDock::closeEvent(event);

	if (!event->isAccepted()) {
		return;
	}

	static int panel_version = -1;
	if (panel_version == -1) {
		panel_version = obs_browser_qcef_version();
	}

	if (panel_version >= 2 && !!cefWidget) {
		cefWidget->closeBrowser();
	}
}

void BrowserDock::showEvent(QShowEvent *event)
{
	OBSDock::showEvent(event);
	setWindowTitle(title);
}

void BrowserDock::contentScaleChanged()
{
	if (!cefWidget)
		return;

	cefWidget->zoomPage(0);
	int zoomSteps = 0;
	switch (ContentScalePercent()) {
	case 60:
	case 70:
		zoomSteps = -4;
		break;
	case 80:
		zoomSteps = -2;
		break;
	case 90:
		zoomSteps = -1;
		break;
	case 110:
		zoomSteps = 1;
		break;
	case 120:
	case 130:
		zoomSteps = 2;
		break;
	case 140:
	case 150:
	case 160:
		zoomSteps = 3;
		break;
	default:
		break;
	}

	const int direction = zoomSteps < 0 ? -1 : 1;
	const int stepCount = zoomSteps < 0 ? -zoomSteps : zoomSteps;
	for (int step = 0; step < stepCount; ++step)
		cefWidget->zoomPage(direction);
}
