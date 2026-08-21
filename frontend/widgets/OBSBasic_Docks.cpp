/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasic.hpp"

#include <docks/TempestControlDeck.hpp>
#include <docks/TempestSignalReactor.hpp>
#include <docks/TempestCommandMatrix.hpp>
#include <docks/TempestMediaBay.hpp>
#include <docks/TempestSequenceDirector.hpp>
#include <docks/TempestAssetVault.hpp>
#include <docks/TempestHUDComposer.hpp>
#ifdef BROWSER_AVAILABLE
#include <docks/BrowserDock.hpp>
#endif
#include "TempestMainframeBar.hpp"

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {
constexpr char TempestUiConfigSection[] = "TempestUI";
constexpr char TempestUiScaleKey[] = "ScalePercent";
constexpr char TempestAutoSizeKey[] = "AutoSizeOnStartup";
constexpr int MinimumTempestUiScale = 60;
constexpr int MaximumTempestUiScale = 160;
constexpr int TempestUiScaleStep = 10;
constexpr int StandardWorkstationWidth = 1920;
constexpr int StandardWorkstationHeight = 1080;
constexpr qreal UltrawideAspectThreshold = 2.0;
constexpr qreal MaximumWorkstationAspect = 32.0 / 9.0;

QFont ScaledApplicationFont(QFont font, qreal scale)
{
	if (font.pixelSize() > 0)
		font.setPixelSize(std::max(1, qRound(font.pixelSize() * scale)));
	else if (font.pointSizeF() > 0)
		font.setPointSizeF(std::max(1.0, font.pointSizeF() * scale));
	return font;
}
} // namespace

void setupDockAction(QDockWidget *dock)
{
	QAction *action = dock->toggleViewAction();

	auto neverDisable = [action]() {
		QSignalBlocker block(action);
		action->setEnabled(true);
	};

	auto newToggleView = [dock](bool check) {
		QSignalBlocker block(dock);
		dock->setVisible(check);
	};

	// Replace the slot connected by default
	QObject::disconnect(action, &QAction::triggered, nullptr, 0);
	QObject::connect(action, &QAction::triggered, dock, newToggleView);

	// Make the action unable to be disabled
	QObject::connect(action, &QAction::enabledChanged, action, neverDisable);
}

void OBSBasic::InitializeTempestUiScaling()
{
	if (!qApp->property("tempestBaseApplicationFont").isValid())
		qApp->setProperty("tempestBaseApplicationFont", qApp->font());

	const QList<OBSDock *> docks = {tempestCommandMatrix, tempestControlDeck,      tempestSignalReactor,
					tempestMediaBay,      tempestSequenceDirector, tempestAssetVault,
					tempestHUDComposer};
	for (OBSDock *dock : docks)
		RegisterTempestScaleDock(dock);

	connect(tempestMainframeBar, &TempestMainframeBar::UiScaleRequested, this,
		[this](int percent) { SetTempestUiScalePercent(percent); });
	qApp->installEventFilter(this);

	config_t *config = App()->GetUserConfig();
	if (!config_has_user_value(config, TempestUiConfigSection, TempestAutoSizeKey))
		config_set_bool(config, TempestUiConfigSection, TempestAutoSizeKey, true);
	const int savedScale = (int)config_get_int(config, TempestUiConfigSection, TempestUiScaleKey);
	SetTempestUiScalePercent(
		savedScale >= MinimumTempestUiScale && savedScale <= MaximumTempestUiScale ? savedScale : 100, false);
}

bool OBSBasic::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		auto *key = static_cast<QKeyEvent *>(event);
		const Qt::KeyboardModifiers modifiers = key->modifiers();
		if ((modifiers & Qt::ControlModifier) && !(modifiers & (Qt::AltModifier | Qt::MetaModifier))) {
			int adjustment = 0;
			bool handled = true;
			switch (key->key()) {
			case Qt::Key_Plus:
			case Qt::Key_Equal:
				adjustment = TempestUiScaleStep;
				break;
			case Qt::Key_Minus:
				adjustment = -TempestUiScaleStep;
				break;
			case Qt::Key_0:
				break;
			default:
				handled = false;
				break;
			}
			if (handled) {
				SetTempestUiScalePercent(adjustment == 0 ? 100 : tempestUiScalePercent + adjustment);
				key->accept();
				return true;
			}
		}
	}
	return OBSMainWindow::eventFilter(watched, event);
}

void OBSBasic::RegisterTempestScaleDock(OBSDock *dock)
{
	if (!dock || !dock->HasContentScaling())
		return;
	if (!dock->property("tempestApplicationScaleConnected").toBool()) {
		connect(dock, &OBSDock::ApplicationScaleRequested, this,
			[this](int percent) { SetTempestUiScalePercent(percent); });
		dock->setProperty("tempestApplicationScaleConnected", true);
	}
	dock->SetContentScalePercent(tempestUiScalePercent, false);
}

void OBSBasic::SetTempestUiScalePercent(int percent, bool save)
{
	tempestUiScalePercent = std::clamp(percent, MinimumTempestUiScale, MaximumTempestUiScale);
	const qreal scale = tempestUiScalePercent / 100.0;
	const QFont baseFont = qApp->property("tempestBaseApplicationFont").value<QFont>();
	if (!baseFont.family().isEmpty())
		qApp->setFont(ScaledApplicationFont(baseFont, scale));
	if (tempestMainframeBar)
		tempestMainframeBar->SetUiScalePercent(tempestUiScalePercent);

	for (OBSDock *dock : findChildren<OBSDock *>()) {
		if (dock->HasContentScaling())
			dock->SetContentScalePercent(tempestUiScalePercent, false);
	}
	if (auto *dock = qobject_cast<OBSDock *>(tempestStreamInfoDock.data()); dock && dock->HasContentScaling())
		dock->SetContentScalePercent(tempestUiScalePercent, false);

	if (save) {
		config_t *config = App()->GetUserConfig();
		config_set_int(config, TempestUiConfigSection, TempestUiScaleKey, tempestUiScalePercent);
		config_save_safe(config, "tmp", nullptr);
	}
}

void OBSBasic::ApplyTempestStartupSizing()
{
	if (!config_get_bool(App()->GetUserConfig(), TempestUiConfigSection, TempestAutoSizeKey) || isFullScreen())
		return;

	QScreen *targetScreen = screen() ? screen() : QGuiApplication::primaryScreen();
	if (!targetScreen)
		return;
	const QRect available = targetScreen->availableGeometry();
	const qreal availableAspect = available.height() > 0 ? available.width() / (qreal)available.height()
							     : 16.0 / 9.0;
	const qreal workstationAspect = availableAspect >= UltrawideAspectThreshold
						? std::min(availableAspect, MaximumWorkstationAspect)
						: StandardWorkstationWidth / (qreal)StandardWorkstationHeight;
	const qreal sizeScale = std::clamp(tempestUiScalePercent / 100.0, 0.8, 1.35);
	QSize desired(qRound(StandardWorkstationHeight * workstationAspect * sizeScale),
		      qRound(StandardWorkstationHeight * sizeScale));
	const QMargins frameMargins = windowHandle() ? windowHandle()->frameMargins() : QMargins();
	const QSize maximumContentSize(std::max(1, available.width() - frameMargins.left() - frameMargins.right()),
				       std::max(1, available.height() - frameMargins.top() - frameMargins.bottom()));
	desired.setWidth(std::min(desired.width(), maximumContentSize.width()));
	desired.setHeight(std::min(desired.height(), maximumContentSize.height()));
	if (isMaximized())
		showNormal();
	resize(desired);
	const QSize outerSize(desired.width() + frameMargins.left() + frameMargins.right(),
			      desired.height() + frameMargins.top() + frameMargins.bottom());
	const QPoint frameTopLeft = available.center() - QPoint(outerSize.width() / 2, outerSize.height() / 2);
	move(frameTopLeft + QPoint(frameMargins.left(), frameMargins.top()));
}

void OBSBasic::ConfigureTempestCommandLayout()
{
	ui->scenesDock->setFloating(false);
	ui->sourcesDock->setFloating(false);
	ui->mixerDock->setFloating(false);
	tempestControlDeck->setFloating(false);
	tempestSignalReactor->setFloating(false);
	tempestCommandMatrix->setFloating(false);
	tempestMediaBay->setFloating(false);
	tempestSequenceDirector->setFloating(false);
	tempestAssetVault->setFloating(false);
	tempestHUDComposer->setFloating(false);
	if (tempestStreamInfoDock)
		tempestStreamInfoDock->setFloating(false);

	addDockWidget(Qt::LeftDockWidgetArea, tempestCommandMatrix);
	addDockWidget(Qt::RightDockWidgetArea, tempestControlDeck);
	addDockWidget(Qt::RightDockWidgetArea, tempestSignalReactor);
	tabifyDockWidget(tempestControlDeck, tempestSignalReactor);
	tabifyDockWidget(tempestSignalReactor, tempestSequenceDirector);
	tabifyDockWidget(tempestSequenceDirector, tempestAssetVault);
	tabifyDockWidget(tempestAssetVault, tempestHUDComposer);
	if (tempestStreamInfoDock)
		tabifyDockWidget(tempestHUDComposer, tempestStreamInfoDock);
	addDockWidget(Qt::BottomDockWidgetArea, ui->mixerDock);
	splitDockWidget(ui->mixerDock, tempestMediaBay, Qt::Horizontal);

	tempestCommandMatrix->setVisible(true);
	ui->scenesDock->setVisible(false);
	ui->sourcesDock->setVisible(false);
	ui->mixerDock->setVisible(true);
	tempestControlDeck->setVisible(true);
	tempestSignalReactor->setVisible(true);
	tempestMediaBay->setVisible(true);
	tempestSequenceDirector->setVisible(true);
	tempestAssetVault->setVisible(true);
	tempestHUDComposer->setVisible(true);
	if (tempestStreamInfoDock)
		tempestStreamInfoDock->setVisible(true);
	tempestControlDeck->raise();
	ui->transitionsDock->setVisible(false);
	controlsDock->setVisible(false);
	statsDock->setVisible(false);

	const int leftWidth = std::clamp(width() * 15 / 100, 260, 320);
	const int commandWidth = std::clamp(width() * 20 / 100, 350, 430);
	const int mixerHeight = std::clamp(height() * 24 / 100, 210, 280);
	resizeDocks({tempestCommandMatrix, tempestControlDeck}, {leftWidth, commandWidth}, Qt::Horizontal);
	resizeDocks({ui->mixerDock}, {mixerHeight}, Qt::Vertical);
	resizeDocks({ui->mixerDock, tempestMediaBay}, {width() * 3 / 5, width() * 2 / 5}, Qt::Horizontal);
}

void OBSBasic::OpenTempestDockManager()
{
	struct DockEntry {
		QString label;
		QPointer<QDockWidget> dock;
	};
	struct DockControls {
		QPointer<QDockWidget> dock;
		QPointer<QCheckBox> visible;
		QPointer<QCheckBox> floating;
	};

	const QVector<DockEntry> entries = {
		{QStringLiteral("Transmission Matrix"), tempestCommandMatrix},
		{QStringLiteral("Control Deck"), tempestControlDeck},
		{QStringLiteral("Signal Reactor"), tempestSignalReactor},
		{QStringLiteral("Signal Media Bay"), tempestMediaBay},
		{QStringLiteral("Sequence Director"), tempestSequenceDirector},
		{QStringLiteral("Asset Vault"), tempestAssetVault},
		{QStringLiteral("HUD Composer"), tempestHUDComposer},
		{QStringLiteral("Stream Information"), tempestStreamInfoDock},
	};

	QDialog dialog(this);
	dialog.setWindowTitle(QStringLiteral("Mainframe Dock Layout Director"));
	dialog.setModal(true);
	const qreal dialogScale = tempestUiScalePercent / 100.0;
	dialog.resize(qRound(680 * dialogScale), qRound(500 * dialogScale));
	dialog.setStyleSheet(QStringLiteral(R"(
		QDialog { background: #07131e; color: #bdf6ff; }
		QLabel#layoutTitle { color: #45d9ff; font-size: 16px; font-weight: 700; letter-spacing: 2px; }
		QLabel#layoutSubtitle, QLabel#layoutHeader { color: #748fa4; font-size: 10px; letter-spacing: 1px; }
		QCheckBox { color: #9eb7c8; }
		QComboBox { min-height: 28px; padding: 0 7px; color: #bdf6ff; background: #06101a; border: 1px solid #1f506d; }
		QComboBox:disabled { color: #40576a; border-color: #172d3d; background: #091721; }
		QPushButton { min-height: 30px; padding: 0 10px; color: #bdf6ff; background: #0d2230; border: 1px solid #1f506d; font-weight: 700; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QPushButton:disabled { color: #40576a; border-color: #172d3d; background: #091721; }
	)"));
	auto *root = new QVBoxLayout(&dialog);
	root->setContentsMargins(14, 14, 14, 14);
	root->setSpacing(10);
	auto *title = new QLabel(QStringLiteral("DOCK LAYOUT DIRECTOR"), &dialog);
	title->setObjectName(QStringLiteral("layoutTitle"));
	auto *subtitle = new QLabel(
		QStringLiteral("Visibility, floating state, scale, and recovery for the Mainframe workstation."),
		&dialog);
	subtitle->setObjectName(QStringLiteral("layoutSubtitle"));
	root->addWidget(title);
	root->addWidget(subtitle);

	auto *applicationScaleRow = new QHBoxLayout();
	auto *applicationScaleLabel = new QLabel(QStringLiteral("APPLICATION UI SCALE"), &dialog);
	applicationScaleLabel->setObjectName(QStringLiteral("layoutHeader"));
	auto *applicationScale = new QComboBox(&dialog);
	for (int percent = MinimumTempestUiScale; percent <= MaximumTempestUiScale; percent += TempestUiScaleStep)
		applicationScale->addItem(QStringLiteral("%1%").arg(percent), percent);
	applicationScale->setCurrentIndex(applicationScale->findData(tempestUiScalePercent));
	applicationScale->setAccessibleName(QStringLiteral("Application UI scale"));
	auto *autoSizeOnStartup = new QCheckBox(QStringLiteral("AUTO-SIZE WINDOW ON STARTUP"), &dialog);
	autoSizeOnStartup->setChecked(
		config_get_bool(App()->GetUserConfig(), TempestUiConfigSection, TempestAutoSizeKey));
	autoSizeOnStartup->setToolTip(
		QStringLiteral("Fit a 1920x1080 workstation to the available screen and current UI scale."));
	applicationScaleRow->addWidget(applicationScaleLabel);
	applicationScaleRow->addWidget(applicationScale);
	applicationScaleRow->addStretch(1);
	applicationScaleRow->addWidget(autoSizeOnStartup);
	root->addLayout(applicationScaleRow);

	auto *grid = new QGridLayout();
	grid->setHorizontalSpacing(12);
	grid->setVerticalSpacing(7);
	const QStringList headings = {QStringLiteral("DOCK"), QStringLiteral("VISIBLE"), QStringLiteral("FLOATING"),
				      QStringLiteral("ACTION")};
	for (int column = 0; column < headings.size(); ++column) {
		auto *heading = new QLabel(headings[column], &dialog);
		heading->setObjectName(QStringLiteral("layoutHeader"));
		grid->addWidget(heading, 0, column);
	}
	grid->setColumnStretch(0, 1);

	QVector<DockControls> controls;
	for (int index = 0; index < entries.size(); ++index) {
		const DockEntry &entry = entries[index];
		const int row = index + 1;
		auto *name = new QLabel(entry.label, &dialog);
		if (!entry.dock)
			name->setText(QStringLiteral("%1 // UNAVAILABLE").arg(entry.label));
		auto *visible = new QCheckBox(&dialog);
		auto *floating = new QCheckBox(&dialog);
		auto *focus = new QPushButton(QStringLiteral("FOCUS"), &dialog);
		visible->setAccessibleName(QStringLiteral("%1 visible").arg(entry.label));
		floating->setAccessibleName(QStringLiteral("%1 floating").arg(entry.label));
		focus->setAccessibleName(QStringLiteral("Focus %1").arg(entry.label));

		const bool available = entry.dock != nullptr;
		visible->setChecked(available && !entry.dock->isHidden());
		floating->setChecked(available && entry.dock->isFloating());
		visible->setEnabled(available);
		floating->setEnabled(available);
		focus->setEnabled(available);
		connect(focus, &QPushButton::clicked, &dialog, [guarded = entry.dock]() {
			if (!guarded)
				return;
			guarded->setVisible(true);
			guarded->show();
			guarded->raise();
			guarded->activateWindow();
		});

		grid->addWidget(name, row, 0);
		grid->addWidget(visible, row, 1, Qt::AlignCenter);
		grid->addWidget(floating, row, 2, Qt::AlignCenter);
		grid->addWidget(focus, row, 3);
		controls.push_back({entry.dock, visible, floating});
	}
	root->addLayout(grid);
	root->addStretch(1);

	auto *utilityRow = new QHBoxLayout();
	auto *showAll = new QPushButton(QStringLiteral("SHOW ALL"), &dialog);
	auto *resetScales = new QPushButton(QStringLiteral("RESET UI SCALE"), &dialog);
	auto *recover = new QPushButton(QStringLiteral("RECOVER COMMAND LAYOUT"), &dialog);
	recover->setToolTip(QStringLiteral("Dock every Mainframe panel back into the canonical Command workspace."));
	utilityRow->addWidget(showAll);
	utilityRow->addWidget(resetScales);
	utilityRow->addWidget(recover);
	root->addLayout(utilityRow);
	connect(showAll, &QPushButton::clicked, &dialog, [&controls]() {
		for (DockControls &control : controls) {
			if (control.visible && control.visible->isEnabled())
				control.visible->setChecked(true);
		}
	});
	connect(resetScales, &QPushButton::clicked, &dialog,
		[applicationScale]() { applicationScale->setCurrentIndex(applicationScale->findData(100)); });
	bool recoverCommandLayout = false;
	connect(recover, &QPushButton::clicked, &dialog, [&dialog, &recoverCommandLayout]() {
		recoverCommandLayout = true;
		dialog.accept();
	});

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("APPLY LAYOUT"));
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	root->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	SetTempestUiScalePercent(applicationScale->currentData().toInt());
	config_set_bool(App()->GetUserConfig(), TempestUiConfigSection, TempestAutoSizeKey,
			autoSizeOnStartup->isChecked());

	if (recoverCommandLayout) {
		tempestCommandDockState.clear();
		tempestCommandWorkspace = true;
		ConfigureTempestCommandLayout();
		tempestMainframeBar->SetCommandWorkspace(true);
		config_set_string(App()->GetUserConfig(), "BasicWindow", "TempestWorkspace", "command");
	} else {
		for (DockControls &control : controls) {
			if (!control.dock)
				continue;
			control.dock->setFloating(control.floating->isChecked());
			control.dock->setVisible(control.visible->isChecked());
			if (control.visible->isChecked() && control.floating->isChecked()) {
				control.dock->show();
				control.dock->raise();
			}
		}
	}
	SaveTempestWorkspaceState();
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
}

void OBSBasic::IntegrateTempestStreamInfoDock(QDockWidget *dock, bool reveal)
{
	if (!dock)
		return;
	const bool nativeDock = dock->objectName() == QStringLiteral("twitchInfo");
	const bool webDock = dock->objectName() == QStringLiteral("tempestStreamInfoWeb");
	if (!nativeDock && !webDock)
		return;

	if (webDock) {
		tempestStreamInfoWebDock = dock;
		if (tempestStreamInfoDock && tempestStreamInfoDock->objectName() == QStringLiteral("twitchInfo")) {
			dock->setVisible(false);
			dock->toggleViewAction()->setVisible(false);
			return;
		}
	} else if (tempestStreamInfoWebDock && tempestStreamInfoWebDock != dock) {
		tempestStreamInfoWebDock->setVisible(false);
		tempestStreamInfoWebDock->toggleViewAction()->setVisible(false);
	}

	tempestStreamInfoDock = dock;
	dock->toggleViewAction()->setVisible(true);
#ifdef BROWSER_AVAILABLE
	if (auto *browserDock = qobject_cast<BrowserDock *>(dock); browserDock && !browserDock->HasContentScaling())
		browserDock->EnableContentScaling(dock->objectName());
#endif
	if (auto *scalableDock = qobject_cast<OBSDock *>(dock))
		RegisterTempestScaleDock(scalableDock);
	const bool visible = reveal || dock->isVisible();
	dock->setFloating(false);
	addDockWidget(Qt::RightDockWidgetArea, dock);
	if (tempestHUDComposer)
		tabifyDockWidget(tempestHUDComposer, dock);
	dock->setVisible(visible);
	if (reveal)
		dock->raise();
}

void OBSBasic::SetTempestWorkspace(bool commandMode, bool initial)
{
	if (!initial && commandMode == tempestCommandWorkspace)
		return;

	if (initial) {
		if (tempestEngineeringDockState.isEmpty())
			tempestEngineeringDockState = saveState();
	} else if (tempestCommandWorkspace) {
		tempestCommandDockState = saveState();
	} else {
		tempestEngineeringDockState = saveState();
	}

	tempestCommandWorkspace = commandMode;
	if (commandMode) {
		if (!tempestCommandDockState.isEmpty() && !restoreState(tempestCommandDockState))
			tempestCommandDockState.clear();
		if (tempestCommandDockState.isEmpty())
			ConfigureTempestCommandLayout();
		menuBar()->setVisible(false);
	} else {
		if (!tempestEngineeringDockState.isEmpty())
			restoreState(tempestEngineeringDockState);
		else
			on_resetDocks_triggered(true);
		menuBar()->setVisible(true);
	}
	if (tempestStreamInfoDock)
		IntegrateTempestStreamInfoDock(tempestStreamInfoDock);

	tempestCommandToolbar->setVisible(true);
	tempestMainframeBar->SetCommandWorkspace(commandMode);
	config_set_string(App()->GetUserConfig(), "BasicWindow", "TempestWorkspace",
			  commandMode ? "command" : "engineering");
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
}

void OBSBasic::SaveTempestWorkspaceState()
{
	if (tempestCommandWorkspace)
		tempestCommandDockState = saveState();
	else
		tempestEngineeringDockState = saveState();

	config_set_string(App()->GetUserConfig(), "BasicWindow", "TempestCommandDockState",
			  tempestCommandDockState.toBase64().constData());
	config_set_string(App()->GetUserConfig(), "BasicWindow", "TempestEngineeringDockState",
			  tempestEngineeringDockState.toBase64().constData());
	config_set_string(App()->GetUserConfig(), "BasicWindow", "TempestWorkspace",
			  tempestCommandWorkspace ? "command" : "engineering");
}

void OBSBasic::on_resetDocks_triggered(bool force)
{
#ifdef BROWSER_AVAILABLE
	if ((extraDocks.size() || extraCustomDocks.size() || extraBrowserDocks.size()) && !force)
#else
	if ((extraDocks.size() || extraCustomDocks.size()) && !force)
#endif
	{
		QMessageBox::StandardButton button =
			OBSMessageBox::question(this, QTStr("ResetUIWarning.Title"), QTStr("ResetUIWarning.Text"));

		if (button == QMessageBox::No)
			return;
	}

#define RESET_DOCKLIST(dockList)                                                                               \
	for (int i = dockList.size() - 1; i >= 0; i--) {                                                       \
		dockList[i]->setVisible(true);                                                                 \
		dockList[i]->setFloating(true);                                                                \
		dockList[i]->move(frameGeometry().topLeft() + rect().center() - dockList[i]->rect().center()); \
		dockList[i]->setVisible(false);                                                                \
	}

	RESET_DOCKLIST(extraDocks)
	RESET_DOCKLIST(extraCustomDocks)
#ifdef BROWSER_AVAILABLE
	RESET_DOCKLIST(extraBrowserDocks)
#endif
#undef RESET_DOCKLIST

	restoreState(startingDockLayout);
	ui->sideDocks->setChecked(true);

	int cx = width();
	int bottomDocksHeight = height();

	bottomDocksHeight = bottomDocksHeight * 225 / 1000;

	ui->scenesDock->setVisible(true);
	ui->sourcesDock->setVisible(true);
	ui->mixerDock->setVisible(true);
	ui->transitionsDock->setVisible(true);
	controlsDock->setVisible(true);
	tempestControlDeck->setVisible(true);
	tempestSignalReactor->setVisible(true);
	tempestSequenceDirector->setVisible(true);
	tempestAssetVault->setVisible(true);
	tempestHUDComposer->setVisible(true);
	if (tempestStreamInfoDock)
		IntegrateTempestStreamInfoDock(tempestStreamInfoDock, true);
	tempestControlDeck->raise();
	tempestCommandMatrix->setVisible(false);
	statsDock->setVisible(false);
	statsDock->setFloating(true);

	QList<QDockWidget *> bottomDocks{ui->mixerDock, ui->transitionsDock, controlsDock, tempestMediaBay};

	resizeDocks(bottomDocks, {bottomDocksHeight, bottomDocksHeight, bottomDocksHeight, bottomDocksHeight},
		    Qt::Vertical);
	resizeDocks(bottomDocks, {cx * 37 / 100, cx * 13 / 100, cx * 14 / 100, cx * 22 / 100}, Qt::Horizontal);

	int sideDockWidth = std::min(width() * 30 / 100, 280);
	resizeDocks({ui->scenesDock, ui->sourcesDock}, {sideDockWidth, sideDockWidth}, Qt::Horizontal);

	activateWindow();
}

void OBSBasic::on_lockDocks_toggled(bool lock)
{
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	QDockWidget::DockWidgetFeatures mainFeatures = features;
	mainFeatures &= ~QDockWidget::QDockWidget::DockWidgetClosable;

	ui->scenesDock->setFeatures(mainFeatures);
	ui->sourcesDock->setFeatures(mainFeatures);
	ui->mixerDock->setFeatures(mainFeatures);
	ui->transitionsDock->setFeatures(mainFeatures);
	controlsDock->setFeatures(mainFeatures);
	statsDock->setFeatures(features);
	tempestControlDeck->setFeatures(features);
	tempestSignalReactor->setFeatures(features);
	tempestCommandMatrix->setFeatures(features);
	tempestMediaBay->setFeatures(features);
	tempestSequenceDirector->setFeatures(features);
	tempestAssetVault->setFeatures(features);
	tempestHUDComposer->setFeatures(features);

	for (int i = extraDocks.size() - 1; i >= 0; i--)
		extraDocks[i]->setFeatures(features);

	for (int i = extraCustomDocks.size() - 1; i >= 0; i--)
		extraCustomDocks[i]->setFeatures(features);

#ifdef BROWSER_AVAILABLE
	for (int i = extraBrowserDocks.size() - 1; i >= 0; i--)
		extraBrowserDocks[i]->setFeatures(features);
#endif
}

void OBSBasic::on_sideDocks_toggled(bool side)
{
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SideDocks", side);

	setDockCornersVertical(side);
}

void OBSBasic::AddDockWidget(QDockWidget *dock, Qt::DockWidgetArea area, bool extraBrowser)
{
	if (dock->objectName().isEmpty())
		return;

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	setupDockAction(dock);
	dock->setFeatures(features);
	addDockWidget(area, dock);
	if (dock->objectName() == QStringLiteral("twitchInfo") ||
	    dock->objectName() == QStringLiteral("tempestStreamInfoWeb")) {
		QPointer<QDockWidget> guardedDock(dock);
		QMetaObject::invokeMethod(
			this,
			[this, guardedDock]() {
				if (guardedDock)
					IntegrateTempestStreamInfoDock(guardedDock, true);
			},
			Qt::QueuedConnection);
	}

#ifdef BROWSER_AVAILABLE
	if (extraBrowser && extraBrowserMenuDocksSeparator.isNull())
		extraBrowserMenuDocksSeparator = ui->menuDocks->addSeparator();

	if (!extraBrowser && !extraBrowserMenuDocksSeparator.isNull())
		ui->menuDocks->insertAction(extraBrowserMenuDocksSeparator, dock->toggleViewAction());
	else
		ui->menuDocks->addAction(dock->toggleViewAction());

	if (extraBrowser)
		return;
#else
	UNUSED_PARAMETER(extraBrowser);

	ui->menuDocks->addAction(dock->toggleViewAction());
#endif

	extraDockNames.push_back(dock->objectName());
	extraDocks.push_back(std::shared_ptr<QDockWidget>(dock));
}

void OBSBasic::RemoveDockWidget(const QString &name)
{
	const bool restoreWebDock = name == QStringLiteral("twitchInfo") && tempestStreamInfoWebDock;
	if (name == QStringLiteral("twitchInfo") &&
	    (!tempestStreamInfoDock || tempestStreamInfoDock->objectName() == QStringLiteral("twitchInfo")))
		tempestStreamInfoDock.clear();
	if (name == QStringLiteral("tempestStreamInfoWeb")) {
		if (tempestStreamInfoDock == tempestStreamInfoWebDock)
			tempestStreamInfoDock.clear();
		tempestStreamInfoWebDock.clear();
	}
	if (extraDockNames.contains(name)) {
		int idx = extraDockNames.indexOf(name);
		extraDockNames.removeAt(idx);
		extraDocks[idx].reset();
		extraDocks.removeAt(idx);
	} else if (extraCustomDockNames.contains(name)) {
		int idx = extraCustomDockNames.indexOf(name);
		extraCustomDockNames.removeAt(idx);
		removeDockWidget(extraCustomDocks[idx]);
		extraCustomDocks.removeAt(idx);
	}
	if (restoreWebDock) {
		QPointer<QDockWidget> guardedDock(tempestStreamInfoWebDock);
		QMetaObject::invokeMethod(
			this,
			[this, guardedDock]() {
				if (guardedDock)
					IntegrateTempestStreamInfoDock(guardedDock, true);
			},
			Qt::QueuedConnection);
	}
}

bool OBSBasic::IsDockObjectNameUsed(const QString &name)
{
	QStringList list;
	list << "scenesDock"
	     << "sourcesDock"
	     << "mixerDock"
	     << "transitionsDock"
	     << "controlsDock"
	     << "statsDock";
	list << extraDockNames;
	list << extraCustomDockNames;

	return list.contains(name);
}

void OBSBasic::AddCustomDockWidget(QDockWidget *dock)
{
	// Prevent the object name from being changed
	connect(dock, &QObject::objectNameChanged, this, &OBSBasic::RepairCustomExtraDockName);

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	dock->setFeatures(features);
	addDockWidget(Qt::RightDockWidgetArea, dock);

	extraCustomDockNames.push_back(dock->objectName());
	extraCustomDocks.push_back(dock);
}

void OBSBasic::setDockCornersVertical(bool vertical)
{
	if (vertical) {
		setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
		setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
		setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
		setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	} else {
		setCorner(Qt::TopLeftCorner, Qt::TopDockWidgetArea);
		setCorner(Qt::TopRightCorner, Qt::TopDockWidgetArea);
		setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
		setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
	}
}

void OBSBasic::RepairCustomExtraDockName()
{
	QDockWidget *dock = reinterpret_cast<QDockWidget *>(sender());
	int idx = extraCustomDocks.indexOf(dock);
	QSignalBlocker block(dock);

	if (idx == -1) {
		blog(LOG_WARNING, "A custom dock got its object name changed");
		return;
	}

	blog(LOG_WARNING, "The custom dock '%s' got its object name restored", QT_TO_UTF8(extraCustomDockNames[idx]));

	dock->setObjectName(extraCustomDockNames[idx]);
}
