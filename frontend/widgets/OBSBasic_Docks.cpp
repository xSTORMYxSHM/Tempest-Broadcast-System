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

#include "../TempestAppearance.hpp"
#include "OBSQTDisplay.hpp"

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
#include <QColorDialog>
#include <QComboBox>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr char TempestUiConfigSection[] = "TempestUI";
constexpr char TempestUiScaleKey[] = "ScalePercent";
constexpr char TempestAutoSizeKey[] = "AutoSizeOnStartup";
constexpr char TempestAccentPresetKey[] = "AccentPreset";
constexpr char TempestCustomAccentKey[] = "CustomAccent";
constexpr char TempestResponsiveConfigSection[] = "TempestResponsiveLayouts";
constexpr char TempestAutoProfileKey[] = "AutoSwitchProfiles";
constexpr char TempestAutoReflowKey[] = "AutoReflowDocks";
constexpr char TempestManualProfileKey[] = "ManualProfile";
constexpr char TempestActiveProfileKey[] = "ActiveProfile";
constexpr int MinimumTempestUiScale = 60;
constexpr int MaximumTempestUiScale = 160;
constexpr int TempestUiScaleStep = 10;
constexpr int StandardWorkstationWidth = 1920;
constexpr int StandardWorkstationHeight = 1080;
constexpr qreal UltrawideAspectThreshold = 2.0;
constexpr qreal SuperUltrawideAspectThreshold = 3.0;
constexpr qreal MaximumWorkstationAspect = 32.0 / 9.0;

QString NormalizedResponsiveProfile(const QString &profile)
{
	if (profile == QStringLiteral("ultrawide") || profile == QStringLiteral("super_ultrawide"))
		return profile;
	return QStringLiteral("standard");
}

QString ResponsiveProfileLabel(const QString &profile)
{
	if (profile == QStringLiteral("super_ultrawide"))
		return QStringLiteral("SUPER ULTRAWIDE");
	if (profile == QStringLiteral("ultrawide"))
		return QStringLiteral("ULTRAWIDE");
	return QStringLiteral("STANDARD 16:9");
}

QString NormalizedAccentPreset(const QString &preset)
{
	if (preset == QStringLiteral("ultraviolet") || preset == QStringLiteral("magenta") ||
	    preset == QStringLiteral("ember") || preset == QStringLiteral("emerald") ||
	    preset == QStringLiteral("ice") || preset == QStringLiteral("custom"))
		return preset;
	return QStringLiteral("tempest");
}

QIcon AccentIcon(const QColor &color)
{
	QPixmap swatch(18, 18);
	swatch.fill(color);
	return QIcon(swatch);
}

QByteArray ResponsiveStateKey(bool commandMode, const QString &profile)
{
	return QStringLiteral("%1_%2")
		.arg(commandMode ? QStringLiteral("Command") : QStringLiteral("Engineering"), profile)
		.toUtf8();
}

QFont ScaledApplicationFont(QFont font, qreal scale)
{
	if (font.pixelSize() > 0)
		font.setPixelSize(std::max(1, qRound(font.pixelSize() * scale)));
	else if (font.pointSizeF() > 0)
		font.setPointSizeF(std::max(1.0, font.pointSizeF() * scale));
	return font;
}

int ScaledWindowMetric(int value, qreal scale)
{
	if (value <= 0)
		return value;
	return std::max(1, qRound(value * scale));
}

QSize ScaledWindowMinimum(const QSize &size, qreal scale)
{
	return {ScaledWindowMetric(size.width(), scale), ScaledWindowMetric(size.height(), scale)};
}

QSize ScaledWindowMaximum(const QSize &size, qreal scale)
{
	const int width = size.width() >= QWIDGETSIZE_MAX ? QWIDGETSIZE_MAX : ScaledWindowMetric(size.width(), scale);
	const int height = size.height() >= QWIDGETSIZE_MAX ? QWIDGETSIZE_MAX
							    : ScaledWindowMetric(size.height(), scale);
	return {width, height};
}

QString ScaledWindowStyleSheet(const QString &source, qreal scale)
{
	QString result = source;
	const QRegularExpression pixels(QStringLiteral("(\\d+(?:\\.\\d+)?)px"));
	QList<QRegularExpressionMatch> matches;
	auto matchIterator = pixels.globalMatch(source);
	while (matchIterator.hasNext())
		matches.push_back(matchIterator.next());
	for (auto it = matches.crbegin(); it != matches.crend(); ++it) {
		const QRegularExpressionMatch &match = *it;
		const qreal value = match.captured(1).toDouble();
		const int scaled = value <= 0.0 ? 0 : std::max(1, qRound(value * scale));
		result.replace(match.capturedStart(), match.capturedLength(), QStringLiteral("%1px").arg(scaled));
	}
	return result;
}

bool IsTempestScalableWindow(QWidget *window, const OBSBasic *main)
{
	if (!window || !window->isWindow() || window == main || window->isFullScreen() ||
	    window->inherits("OBSProjector"))
		return false;
	if (auto *dock = qobject_cast<OBSDock *>(window); dock && dock->HasContentScaling())
		return false;
	const Qt::WindowType type = window->windowType();
	if (type == Qt::Popup || type == Qt::ToolTip || type == Qt::SplashScreen || type == Qt::Desktop)
		return false;
	return qobject_cast<QDialog *>(window) || qobject_cast<QMainWindow *>(window) ||
	       qobject_cast<QDockWidget *>(window);
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

	const QList<OBSDock *> docks = {tempestCommandMatrix, tempestSourceInspectorDock,
					tempestControlDeck,   tempestSignalReactor,
					tempestMediaBay,      tempestSequenceDirector,
					tempestAssetVault,    tempestHUDComposer};
	for (OBSDock *dock : docks)
		RegisterTempestScaleDock(dock);

	connect(tempestMainframeBar, &TempestMainframeBar::UiScaleRequested, this,
		[this](int percent) { SetTempestUiScalePercent(percent); });
	qApp->installEventFilter(this);

	config_t *config = App()->GetUserConfig();
	if (!config_has_user_value(config, TempestUiConfigSection, TempestAutoSizeKey))
		config_set_bool(config, TempestUiConfigSection, TempestAutoSizeKey, true);
	if (!config_has_user_value(config, TempestUiConfigSection, TempestAccentPresetKey))
		config_set_string(config, TempestUiConfigSection, TempestAccentPresetKey, "tempest");
	if (!config_has_user_value(config, TempestUiConfigSection, TempestCustomAccentKey))
		config_set_string(config, TempestUiConfigSection, TempestCustomAccentKey, "#45d9ff");
	const QString savedPreset =
		QString::fromUtf8(config_get_string(config, TempestUiConfigSection, TempestAccentPresetKey));
	const QColor savedCustomColor(
		QString::fromUtf8(config_get_string(config, TempestUiConfigSection, TempestCustomAccentKey)));
	SetTempestApplicationColor(savedPreset, savedCustomColor, false);
	const int savedScale = (int)config_get_int(config, TempestUiConfigSection, TempestUiScaleKey);
	SetTempestUiScalePercent(
		savedScale >= MinimumTempestUiScale && savedScale <= MaximumTempestUiScale ? savedScale : 100, false);
}

void OBSBasic::SetTempestApplicationColor(const QString &preset, const QColor &customColor, bool save)
{
	const QString normalizedPreset = NormalizedAccentPreset(preset);
	const QColor normalizedCustom = customColor.isValid() ? customColor : TempestAppearance::DefaultAccent();
	const QColor accent = normalizedPreset == QStringLiteral("custom")
				      ? normalizedCustom
				      : TempestAppearance::PresetColor(normalizedPreset);
	qApp->setProperty(TempestAppearance::AccentPresetProperty, normalizedPreset);
	qApp->setProperty(TempestAppearance::AccentColorProperty, accent.name(QColor::HexRgb));
	TempestAppearance::ApplyApplication(qApp);

	if (tempestMainframeBar)
		tempestMainframeBar->SetUiScalePercent(tempestUiScalePercent);
	for (OBSDock *dock : findChildren<OBSDock *>()) {
		if (dock->HasContentScaling())
			dock->SetContentScalePercent(tempestUiScalePercent, false);
	}
	for (QWidget *window : QApplication::topLevelWidgets()) {
		ApplyTempestWindowScaling(window, false);
		TempestAppearance::ApplyWidgetTree(window);
	}

	if (save) {
		config_t *config = App()->GetUserConfig();
		config_set_string(config, TempestUiConfigSection, TempestAccentPresetKey,
				  normalizedPreset.toUtf8().constData());
		config_set_string(config, TempestUiConfigSection, TempestCustomAccentKey,
				  normalizedCustom.name(QColor::HexRgb).toUtf8().constData());
		config_save_safe(config, "tmp", nullptr);
	}
}

bool OBSBasic::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == this && !tempestInteractiveWindowMove &&
	    (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
	     event->type() == QEvent::WindowStateChange))
		ScheduleTempestResponsiveWorkspaceRefresh();

	if (event->type() == QEvent::Show) {
		if (auto *window = qobject_cast<QWidget *>(watched); IsTempestScalableWindow(window, this)) {
			QPointer<QWidget> guard(window);
			QTimer::singleShot(0, this, [this, guard]() {
				if (guard) {
					ApplyTempestWindowScaling(guard, true);
					TempestAppearance::ApplyWidgetTree(guard);
				}
			});
		}
	} else if (event->type() == QEvent::ChildAdded) {
		if (auto *child = qobject_cast<QWidget *>(watched)) {
			QWidget *window = child->window();
			if (IsTempestScalableWindow(window, this) &&
			    window->property("tempestWindowScaleInitialized").toBool() &&
			    !window->property("tempestWindowScaleRefreshQueued").toBool()) {
				window->setProperty("tempestWindowScaleRefreshQueued", true);
				QPointer<QWidget> guard(window);
				QTimer::singleShot(0, this, [this, guard]() {
					if (!guard)
						return;
					ApplyTempestWindowScaling(guard, false);
					guard->setProperty("tempestWindowScaleRefreshQueued", false);
				});
			}
		}
	}

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

void OBSBasic::ApplyTempestWindowScaling(QWidget *window, bool initializeGeometry)
{
	if (!IsTempestScalableWindow(window, this))
		return;

	window->ensurePolished();
	const qreal scale = tempestUiScalePercent / 100.0;
	if (!window->property("tempestWindowScaleBaseSize").isValid())
		window->setProperty("tempestWindowScaleBaseSize", window->size());

	QList<QWidget *> widgets = window->findChildren<QWidget *>();
	widgets.prepend(window);
	for (QWidget *child : std::as_const(widgets)) {
		if (!child->property("tempestWindowScaleBaseMinimum").isValid())
			child->setProperty("tempestWindowScaleBaseMinimum", child->minimumSize());
		if (!child->property("tempestWindowScaleBaseMaximum").isValid())
			child->setProperty("tempestWindowScaleBaseMaximum", child->maximumSize());
		if (!child->property("tempestWindowScaleStyleManaged").isValid() && !child->styleSheet().isEmpty()) {
			child->setProperty("tempestWindowScaleStyleManaged", true);
			child->setProperty("tempestWindowScaleBaseStyle", child->styleSheet());
		}
	}

	QList<QLayout *> layouts = window->findChildren<QLayout *>();
	if (window->layout())
		layouts.prepend(window->layout());
	for (QLayout *layout : std::as_const(layouts)) {
		if (!layout->property("tempestWindowScaleBaseMargins").isValid())
			layout->setProperty("tempestWindowScaleBaseMargins",
					    QVariant::fromValue(layout->contentsMargins()));
		if (!layout->property("tempestWindowScaleBaseSpacing").isValid())
			layout->setProperty("tempestWindowScaleBaseSpacing", layout->spacing());
		if (auto *grid = qobject_cast<QGridLayout *>(layout)) {
			if (!grid->property("tempestWindowScaleBaseHorizontalSpacing").isValid())
				grid->setProperty("tempestWindowScaleBaseHorizontalSpacing", grid->horizontalSpacing());
			if (!grid->property("tempestWindowScaleBaseVerticalSpacing").isValid())
				grid->setProperty("tempestWindowScaleBaseVerticalSpacing", grid->verticalSpacing());
		} else if (auto *form = qobject_cast<QFormLayout *>(layout)) {
			if (!form->property("tempestWindowScaleBaseHorizontalSpacing").isValid())
				form->setProperty("tempestWindowScaleBaseHorizontalSpacing", form->horizontalSpacing());
			if (!form->property("tempestWindowScaleBaseVerticalSpacing").isValid())
				form->setProperty("tempestWindowScaleBaseVerticalSpacing", form->verticalSpacing());
		}
	}

	for (QWidget *child : std::as_const(widgets)) {
		child->setMinimumSize(
			ScaledWindowMinimum(child->property("tempestWindowScaleBaseMinimum").toSize(), scale));
		child->setMaximumSize(
			ScaledWindowMaximum(child->property("tempestWindowScaleBaseMaximum").toSize(), scale));
		if (child->property("tempestWindowScaleStyleManaged").toBool())
			TempestAppearance::SetManagedStyleSheet(
				child, ScaledWindowStyleSheet(child->property("tempestWindowScaleBaseStyle").toString(),
							      scale));
	}
	for (QLayout *layout : std::as_const(layouts)) {
		const QMargins margins = layout->property("tempestWindowScaleBaseMargins").value<QMargins>();
		layout->setContentsMargins(ScaledWindowMetric(margins.left(), scale),
					   ScaledWindowMetric(margins.top(), scale),
					   ScaledWindowMetric(margins.right(), scale),
					   ScaledWindowMetric(margins.bottom(), scale));
		const int spacing = layout->property("tempestWindowScaleBaseSpacing").toInt();
		if (spacing >= 0)
			layout->setSpacing(ScaledWindowMetric(spacing, scale));
		if (auto *grid = qobject_cast<QGridLayout *>(layout)) {
			const int horizontal = grid->property("tempestWindowScaleBaseHorizontalSpacing").toInt();
			const int vertical = grid->property("tempestWindowScaleBaseVerticalSpacing").toInt();
			if (horizontal >= 0)
				grid->setHorizontalSpacing(ScaledWindowMetric(horizontal, scale));
			if (vertical >= 0)
				grid->setVerticalSpacing(ScaledWindowMetric(vertical, scale));
		} else if (auto *form = qobject_cast<QFormLayout *>(layout)) {
			const int horizontal = form->property("tempestWindowScaleBaseHorizontalSpacing").toInt();
			const int vertical = form->property("tempestWindowScaleBaseVerticalSpacing").toInt();
			if (horizontal >= 0)
				form->setHorizontalSpacing(ScaledWindowMetric(horizontal, scale));
			if (vertical >= 0)
				form->setVerticalSpacing(ScaledWindowMetric(vertical, scale));
		}
		layout->invalidate();
		layout->activate();
	}

	QScreen *targetScreen = window->screen() ? window->screen() : QGuiApplication::primaryScreen();
	if (!targetScreen)
		return;
	const QRect available = targetScreen->availableGeometry();
	const QMargins frameMargins = window->windowHandle() ? window->windowHandle()->frameMargins() : QMargins();
	const QSize maximumContent(std::max(1, available.width() - frameMargins.left() - frameMargins.right()),
				   std::max(1, available.height() - frameMargins.top() - frameMargins.bottom()));
	const QSize baseSize = window->property("tempestWindowScaleBaseSize").toSize();
	QSize desired = ScaledWindowMinimum(baseSize, scale).expandedTo(window->minimumSizeHint());
	desired.setWidth(std::min(desired.width(), maximumContent.width()));
	desired.setHeight(std::min(desired.height(), maximumContent.height()));
	QSize minimum = window->minimumSize().boundedTo(maximumContent);
	window->setMinimumSize(minimum);
	if (initializeGeometry || window->size() != desired)
		window->resize(desired);

	QRect frame = window->frameGeometry();
	QPoint correction;
	if (frame.left() < available.left())
		correction.rx() += available.left() - frame.left();
	if (frame.right() > available.right())
		correction.rx() -= frame.right() - available.right();
	if (frame.top() < available.top())
		correction.ry() += available.top() - frame.top();
	if (frame.bottom() > available.bottom())
		correction.ry() -= frame.bottom() - available.bottom();
	if (!correction.isNull())
		window->move(window->pos() + correction);
	window->setProperty("tempestWindowScaleInitialized", true);
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
	for (QWidget *window : QApplication::topLevelWidgets())
		ApplyTempestWindowScaling(window, false);

	if (save) {
		config_t *config = App()->GetUserConfig();
		config_set_int(config, TempestUiConfigSection, TempestUiScaleKey, tempestUiScalePercent);
		config_save_safe(config, "tmp", nullptr);
	}
	ScheduleTempestResponsiveWorkspaceRefresh();
}

QString OBSBasic::DetectTempestResponsiveProfile() const
{
	QScreen *targetScreen = screen() ? screen() : QGuiApplication::primaryScreen();
	if (!targetScreen)
		return QStringLiteral("standard");
	const QRect available = targetScreen->availableGeometry();
	const qreal aspect = available.height() > 0 ? available.width() / (qreal)available.height() : 16.0 / 9.0;
	if (aspect >= SuperUltrawideAspectThreshold)
		return QStringLiteral("super_ultrawide");
	if (aspect >= UltrawideAspectThreshold)
		return QStringLiteral("ultrawide");
	return QStringLiteral("standard");
}

bool OBSBasic::LoadTempestResponsiveProfileStates(const QString &profile)
{
	config_t *config = App()->GetUserConfig();
	auto loadState = [config, &profile](bool commandMode) {
		const QByteArray key = ResponsiveStateKey(commandMode, profile);
		const char *encoded = config_get_string(config, TempestResponsiveConfigSection, key.constData());
		return encoded && *encoded ? QByteArray::fromBase64(QByteArray(encoded)) : QByteArray();
	};
	tempestCommandDockState = loadState(true);
	tempestEngineeringDockState = loadState(false);
	return !tempestCommandDockState.isEmpty() || !tempestEngineeringDockState.isEmpty();
}

void OBSBasic::StoreTempestResponsiveProfileStates(const QString &profile)
{
	if (profile.isEmpty())
		return;
	config_t *config = App()->GetUserConfig();
	const QByteArray commandKey = ResponsiveStateKey(true, profile);
	const QByteArray engineeringKey = ResponsiveStateKey(false, profile);
	config_set_string(config, TempestResponsiveConfigSection, commandKey.constData(),
			  tempestCommandDockState.toBase64().constData());
	config_set_string(config, TempestResponsiveConfigSection, engineeringKey.constData(),
			  tempestEngineeringDockState.toBase64().constData());
}

void OBSBasic::InitializeTempestResponsiveWorkspaceProfiles()
{
	config_t *config = App()->GetUserConfig();
	if (!config_has_user_value(config, TempestResponsiveConfigSection, TempestAutoProfileKey))
		config_set_bool(config, TempestResponsiveConfigSection, TempestAutoProfileKey, true);
	if (!config_has_user_value(config, TempestResponsiveConfigSection, TempestAutoReflowKey))
		config_set_bool(config, TempestResponsiveConfigSection, TempestAutoReflowKey, true);

	const QByteArray legacyCommandState = tempestCommandDockState;
	const QByteArray legacyEngineeringState = tempestEngineeringDockState;
	auto hasStoredProfile = [config](const QString &profile) {
		const QByteArray commandKey = ResponsiveStateKey(true, profile);
		const QByteArray engineeringKey = ResponsiveStateKey(false, profile);
		return config_has_user_value(config, TempestResponsiveConfigSection, commandKey.constData()) ||
		       config_has_user_value(config, TempestResponsiveConfigSection, engineeringKey.constData());
	};
	auto seedProfile = [this, &legacyCommandState, &legacyEngineeringState](const QString &profile) {
		tempestCommandDockState = legacyCommandState;
		tempestEngineeringDockState = legacyEngineeringState;
		StoreTempestResponsiveProfileStates(profile);
	};

	if (!hasStoredProfile(QStringLiteral("standard")))
		seedProfile(QStringLiteral("standard"));
	const QString detectedProfile = DetectTempestResponsiveProfile();
	if (!hasStoredProfile(detectedProfile))
		seedProfile(detectedProfile);

	const bool automatic = config_get_bool(config, TempestResponsiveConfigSection, TempestAutoProfileKey);
	const QString manualProfile = NormalizedResponsiveProfile(
		QString::fromUtf8(config_get_string(config, TempestResponsiveConfigSection, TempestManualProfileKey)));
	tempestResponsiveProfile = automatic ? detectedProfile : manualProfile;
	if (!LoadTempestResponsiveProfileStates(tempestResponsiveProfile)) {
		tempestCommandDockState = legacyCommandState;
		tempestEngineeringDockState = legacyEngineeringState;
	}
	tempestResponsiveProfilesInitialized = true;
	config_set_string(config, TempestResponsiveConfigSection, TempestActiveProfileKey,
			  tempestResponsiveProfile.toUtf8().constData());
	if (tempestMainframeBar)
		tempestMainframeBar->SetResponsiveProfile(ResponsiveProfileLabel(tempestResponsiveProfile), automatic);
	if (windowHandle()) {
		connect(windowHandle(), &QWindow::screenChanged, this,
			[this](QScreen *) { ScheduleTempestResponsiveWorkspaceRefresh(); });
	}
}

void OBSBasic::SetTempestResponsiveProfile(const QString &profile, bool force)
{
	const QString normalized = NormalizedResponsiveProfile(profile);
	if (!tempestResponsiveProfilesInitialized)
		return;
	config_t *config = App()->GetUserConfig();
	const bool automatic = config_get_bool(config, TempestResponsiveConfigSection, TempestAutoProfileKey);
	if (normalized == tempestResponsiveProfile) {
		if (tempestMainframeBar)
			tempestMainframeBar->SetResponsiveProfile(ResponsiveProfileLabel(normalized), automatic);
		ApplyTempestResponsiveDockPriorities(force);
		return;
	}

	SaveTempestWorkspaceState();
	tempestResponsiveProfile = normalized;
	const bool hasState = LoadTempestResponsiveProfileStates(tempestResponsiveProfile);
	if (tempestCommandWorkspace) {
		if (hasState && !tempestCommandDockState.isEmpty() && restoreState(tempestCommandDockState)) {
			menuBar()->setVisible(false);
		} else {
			tempestCommandDockState.clear();
			ConfigureTempestCommandLayout();
		}
	} else if (hasState && !tempestEngineeringDockState.isEmpty() && restoreState(tempestEngineeringDockState)) {
		menuBar()->setVisible(true);
	} else {
		on_resetDocks_triggered(true);
		menuBar()->setVisible(true);
	}
	if (tempestStreamInfoDock)
		IntegrateTempestStreamInfoDock(tempestStreamInfoDock);
	if (tempestMainframeBar)
		tempestMainframeBar->SetResponsiveProfile(ResponsiveProfileLabel(tempestResponsiveProfile), automatic);
	config_set_string(config, TempestResponsiveConfigSection, TempestActiveProfileKey,
			  tempestResponsiveProfile.toUtf8().constData());
	tempestResponsiveBreakpoint.clear();
	ApplyTempestResponsiveDockPriorities(true);
	SaveTempestWorkspaceState();
	config_save_safe(config, "tmp", nullptr);
}

void OBSBasic::ScheduleTempestResponsiveWorkspaceRefresh()
{
	if (!tempestResponsiveProfilesInitialized || tempestResponsiveRefreshQueued || tempestInteractiveWindowMove)
		return;
	tempestResponsiveRefreshQueued = true;
	QTimer::singleShot(120, this, [this]() {
		tempestResponsiveRefreshQueued = false;
		if (isMinimized() || tempestInteractiveWindowMove)
			return;
		config_t *config = App()->GetUserConfig();
		const bool automatic = config_get_bool(config, TempestResponsiveConfigSection, TempestAutoProfileKey);
		const QString configuredProfile =
			automatic ? DetectTempestResponsiveProfile()
				  : NormalizedResponsiveProfile(QString::fromUtf8(config_get_string(
					    config, TempestResponsiveConfigSection, TempestManualProfileKey)));
		if (configuredProfile != tempestResponsiveProfile)
			SetTempestResponsiveProfile(configuredProfile);
		else
			ApplyTempestResponsiveDockPriorities();
	});
}

void OBSBasic::SetTempestInteractiveWindowMove(bool moving)
{
	if (tempestInteractiveWindowMove == moving)
		return;

	tempestInteractiveWindowMove = moving;
	for (OBSQTDisplay *display : findChildren<OBSQTDisplay *>()) {
		obs_display_t *renderDisplay = display ? display->GetDisplay() : nullptr;
		if (!renderDisplay)
			continue;
		if (moving) {
			display->setProperty("tempestMoveDisplayWasEnabled", obs_display_enabled(renderDisplay));
			obs_display_set_enabled(renderDisplay, false);
			continue;
		}

		const QVariant priorState = display->property("tempestMoveDisplayWasEnabled");
		if (!priorState.isValid())
			continue;
		display->setProperty("tempestMoveDisplayWasEnabled", QVariant());
		obs_display_set_enabled(renderDisplay, priorState.toBool());
		if (priorState.toBool()) {
			display->OnMove();
			display->update();
		}
	}

#ifdef BROWSER_AVAILABLE
	for (BrowserDock *dock : findChildren<BrowserDock *>()) {
		QCefWidget *browser = dock && dock->cefWidget ? dock->cefWidget.data() : nullptr;
		if (!browser)
			continue;
		if (moving) {
			browser->setProperty("tempestMoveBrowserWasVisible", browser->isVisible());
			if (browser->isVisible())
				browser->hide();
			continue;
		}

		const QVariant priorState = browser->property("tempestMoveBrowserWasVisible");
		if (!priorState.isValid())
			continue;
		browser->setProperty("tempestMoveBrowserWasVisible", QVariant());
		if (priorState.toBool())
			browser->show();
	}
#endif

	if (!moving)
		ScheduleTempestResponsiveWorkspaceRefresh();
}

void OBSBasic::ApplyTempestResponsiveDockPriorities(bool force)
{
	if (!tempestCommandWorkspace || !tempestResponsiveProfilesInitialized)
		return;
	if (!force && !config_get_bool(App()->GetUserConfig(), TempestResponsiveConfigSection, TempestAutoReflowKey))
		return;
	const int logicalWidth = qRound(width() * 100.0 / std::max(60, tempestUiScalePercent));
	const QString breakpoint = logicalWidth < 1800    ? QStringLiteral("compact")
				   : logicalWidth >= 2300 ? QStringLiteral("wide")
							  : QStringLiteral("standard");
	if (!force && breakpoint == tempestResponsiveBreakpoint)
		return;
	tempestResponsiveBreakpoint = breakpoint;

	const qreal scale = tempestUiScalePercent / 100.0;
	auto scaled = [scale](int value) {
		return qRound(value * scale);
	};
	int leftWidth;
	int commandWidth;
	if (breakpoint == QStringLiteral("compact")) {
		leftWidth = std::clamp(width() * 16 / 100, scaled(230), scaled(290));
		commandWidth = std::clamp(width() * 22 / 100, scaled(310), scaled(390));
		if (!ui->mixerDock->isFloating() && !tempestMediaBay->isFloating()) {
			tabifyDockWidget(ui->mixerDock, tempestMediaBay);
			ui->mixerDock->raise();
		}
	} else {
		const bool wide = breakpoint == QStringLiteral("wide");
		leftWidth = std::clamp(width() * (wide ? 16 : 15) / 100, scaled(wide ? 300 : 260),
				       scaled(wide ? 440 : 320));
		commandWidth = std::clamp(width() * (wide ? 18 : 20) / 100, scaled(wide ? 370 : 350),
					  scaled(wide ? 540 : 430));
		if (!ui->mixerDock->isFloating() && !tempestMediaBay->isFloating())
			splitDockWidget(ui->mixerDock, tempestMediaBay, Qt::Horizontal);
	}
	const int mixerHeight = std::clamp(height() * 24 / 100, scaled(190), scaled(290));
	resizeDocks({tempestCommandMatrix, tempestControlDeck}, {leftWidth, commandWidth}, Qt::Horizontal);
	resizeDocks({ui->mixerDock}, {mixerHeight}, Qt::Vertical);
	if (breakpoint != QStringLiteral("compact")) {
		const int mixerShare = breakpoint == QStringLiteral("wide") ? 65 : 60;
		resizeDocks({ui->mixerDock, tempestMediaBay},
			    {width() * mixerShare / 100, width() * (100 - mixerShare) / 100}, Qt::Horizontal);
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
	tempestSourceInspectorDock->setFloating(false);
	tempestMediaBay->setFloating(false);
	tempestSequenceDirector->setFloating(false);
	tempestAssetVault->setFloating(false);
	tempestHUDComposer->setFloating(false);
	if (tempestStreamInfoDock)
		tempestStreamInfoDock->setFloating(false);

	addDockWidget(Qt::LeftDockWidgetArea, tempestCommandMatrix);
	addDockWidget(Qt::LeftDockWidgetArea, tempestSourceInspectorDock);
	tabifyDockWidget(tempestCommandMatrix, tempestSourceInspectorDock);
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
	tempestSourceInspectorDock->setVisible(true);
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
	tempestCommandMatrix->raise();
	tempestControlDeck->raise();
	ui->transitionsDock->setVisible(false);
	controlsDock->setVisible(false);
	statsDock->setVisible(false);

	tempestResponsiveBreakpoint.clear();
	ApplyTempestResponsiveDockPriorities(true);
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
		{QStringLiteral("Scene Control"), tempestCommandMatrix},
		{QStringLiteral("Source Operations"), tempestSourceInspectorDock},
		{QStringLiteral("Stream Overlay"), tempestControlDeck},
		{QStringLiteral("Audio Reactor"), tempestSignalReactor},
		{QStringLiteral("Media Controls"), tempestMediaBay},
		{QStringLiteral("Sequence Director"), tempestSequenceDirector},
		{QStringLiteral("Asset Library"), tempestAssetVault},
		{QStringLiteral("Overlay Designer"), tempestHUDComposer},
		{QStringLiteral("Stream Information"), tempestStreamInfoDock},
	};

	QDialog dialog(this);
	dialog.setWindowTitle(QStringLiteral("Tempest Workspace Layout"));
	dialog.setModal(true);
	const qreal dialogScale = tempestUiScalePercent / 100.0;
	dialog.resize(qRound(760 * dialogScale), qRound(590 * dialogScale));
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
	auto *title = new QLabel(QStringLiteral("WORKSPACE LAYOUT"), &dialog);
	title->setObjectName(QStringLiteral("layoutTitle"));
	auto *subtitle = new QLabel(
		QStringLiteral("Monitor profiles, responsive reflow, visibility, scale, and workspace recovery."),
		&dialog);
	subtitle->setObjectName(QStringLiteral("layoutSubtitle"));
	root->addWidget(title);
	root->addWidget(subtitle);
	config_t *config = App()->GetUserConfig();

	auto *profileRow = new QHBoxLayout();
	auto *profileLabel = new QLabel(QStringLiteral("RESPONSIVE PROFILE"), &dialog);
	profileLabel->setObjectName(QStringLiteral("layoutHeader"));
	auto *responsiveProfile = new QComboBox(&dialog);
	responsiveProfile->addItem(
		QStringLiteral("AUTOMATIC // %1").arg(ResponsiveProfileLabel(DetectTempestResponsiveProfile())),
		QStringLiteral("auto"));
	responsiveProfile->addItem(QStringLiteral("STANDARD // 16:9"), QStringLiteral("standard"));
	responsiveProfile->addItem(QStringLiteral("ULTRAWIDE // 21:9"), QStringLiteral("ultrawide"));
	responsiveProfile->addItem(QStringLiteral("SUPER ULTRAWIDE // 32:9"), QStringLiteral("super_ultrawide"));
	const bool automaticProfiles = config_get_bool(config, TempestResponsiveConfigSection, TempestAutoProfileKey);
	const QString selectedProfile =
		automaticProfiles
			? QStringLiteral("auto")
			: NormalizedResponsiveProfile(QString::fromUtf8(
				  config_get_string(config, TempestResponsiveConfigSection, TempestManualProfileKey)));
	responsiveProfile->setCurrentIndex(responsiveProfile->findData(selectedProfile));
	responsiveProfile->setAccessibleName(QStringLiteral("Responsive workspace profile"));
	auto *autoReflow = new QCheckBox(QStringLiteral("LIVE DOCK REFLOW"), &dialog);
	autoReflow->setChecked(config_get_bool(config, TempestResponsiveConfigSection, TempestAutoReflowKey));
	autoReflow->setToolTip(QStringLiteral(
		"Tab compact panels and expand operational docks when the window crosses layout breakpoints."));
	auto *safeAreaGuides = new QCheckBox(QStringLiteral("CANVAS SAFE-AREA GUIDES"), &dialog);
	safeAreaGuides->setChecked(config_get_bool(config, "BasicWindow", "ShowSafeAreas"));
	safeAreaGuides->setToolTip(
		QStringLiteral("Show broadcast-safe guides inside the 16:9 stream canvas on every display shape."));
	profileRow->addWidget(profileLabel);
	profileRow->addWidget(responsiveProfile, 1);
	profileRow->addWidget(autoReflow);
	profileRow->addWidget(safeAreaGuides);
	root->addLayout(profileRow);

	auto *applicationScaleRow = new QHBoxLayout();
	auto *applicationScaleLabel = new QLabel(QStringLiteral("APPLICATION UI SCALE"), &dialog);
	applicationScaleLabel->setObjectName(QStringLiteral("layoutHeader"));
	auto *applicationScale = new QComboBox(&dialog);
	for (int percent = MinimumTempestUiScale; percent <= MaximumTempestUiScale; percent += TempestUiScaleStep)
		applicationScale->addItem(QStringLiteral("%1%").arg(percent), percent);
	applicationScale->setCurrentIndex(applicationScale->findData(tempestUiScalePercent));
	applicationScale->setAccessibleName(QStringLiteral("Application UI scale"));
	auto *autoSizeOnStartup = new QCheckBox(QStringLiteral("AUTO-SIZE WINDOW ON STARTUP"), &dialog);
	autoSizeOnStartup->setChecked(config_get_bool(config, TempestUiConfigSection, TempestAutoSizeKey));
	autoSizeOnStartup->setToolTip(
		QStringLiteral("Fit the 1920x1080 baseline to the selected monitor profile and current UI scale."));
	applicationScaleRow->addWidget(applicationScaleLabel);
	applicationScaleRow->addWidget(applicationScale);
	applicationScaleRow->addStretch(1);
	applicationScaleRow->addWidget(autoSizeOnStartup);
	root->addLayout(applicationScaleRow);

	auto *applicationColorRow = new QHBoxLayout();
	auto *applicationColorLabel = new QLabel(QStringLiteral("APPLICATION COLOR"), &dialog);
	applicationColorLabel->setObjectName(QStringLiteral("layoutHeader"));
	auto *applicationColor = new QComboBox(&dialog);
	auto addColorPreset = [applicationColor](const QString &label, const QString &id, const QColor &color) {
		applicationColor->addItem(AccentIcon(color), label, id);
	};
	addColorPreset(QStringLiteral("TEMPEST CYAN // DEFAULT"), QStringLiteral("tempest"),
		       TempestAppearance::DefaultAccent());
	addColorPreset(QStringLiteral("ULTRAVIOLET"), QStringLiteral("ultraviolet"),
		       TempestAppearance::PresetColor(QStringLiteral("ultraviolet")));
	addColorPreset(QStringLiteral("NEON MAGENTA"), QStringLiteral("magenta"),
		       TempestAppearance::PresetColor(QStringLiteral("magenta")));
	addColorPreset(QStringLiteral("EMBER"), QStringLiteral("ember"),
		       TempestAppearance::PresetColor(QStringLiteral("ember")));
	addColorPreset(QStringLiteral("EMERALD"), QStringLiteral("emerald"),
		       TempestAppearance::PresetColor(QStringLiteral("emerald")));
	addColorPreset(QStringLiteral("ICE BLUE"), QStringLiteral("ice"),
		       TempestAppearance::PresetColor(QStringLiteral("ice")));
	QColor selectedCustomColor(
		QString::fromUtf8(config_get_string(config, TempestUiConfigSection, TempestCustomAccentKey)));
	if (!selectedCustomColor.isValid())
		selectedCustomColor = TempestAppearance::DefaultAccent();
	addColorPreset(QStringLiteral("CUSTOM"), QStringLiteral("custom"), selectedCustomColor);
	const QString activeAccentPreset =
		NormalizedAccentPreset(qApp->property(TempestAppearance::AccentPresetProperty).toString());
	applicationColor->setCurrentIndex(std::max(0, applicationColor->findData(activeAccentPreset)));
	applicationColor->setAccessibleName(QStringLiteral("Application color palette"));
	auto *customColorButton = new QPushButton(&dialog);
	auto updateCustomColorButton = [customColorButton, applicationColor, &selectedCustomColor]() {
		customColorButton->setIcon(AccentIcon(selectedCustomColor));
		customColorButton->setText(
			QStringLiteral("CUSTOM COLOR // %1").arg(selectedCustomColor.name(QColor::HexRgb).toUpper()));
		customColorButton->setEnabled(applicationColor->currentData().toString() == QStringLiteral("custom"));
		const int customIndex = applicationColor->findData(QStringLiteral("custom"));
		if (customIndex >= 0)
			applicationColor->setItemIcon(customIndex, AccentIcon(selectedCustomColor));
	};
	updateCustomColorButton();
	connect(applicationColor, &QComboBox::currentIndexChanged, &dialog,
		[updateCustomColorButton](int) { updateCustomColorButton(); });
	connect(customColorButton, &QPushButton::clicked, &dialog,
		[&dialog, &selectedCustomColor, updateCustomColorButton]() {
			const QColor selected = QColorDialog::getColor(
				selectedCustomColor, &dialog, QStringLiteral("Choose Tempest Application Color"),
				QColorDialog::DontUseNativeDialog);
			if (!selected.isValid())
				return;
			selectedCustomColor = selected;
			updateCustomColorButton();
		});
	applicationColorRow->addWidget(applicationColorLabel);
	applicationColorRow->addWidget(applicationColor, 1);
	applicationColorRow->addWidget(customColorButton);
	root->addLayout(applicationColorRow);

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
	recover->setToolTip(QStringLiteral("Dock every Tempest panel back into the standard Command workspace."));
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
	buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("APPLY + SAVE PROFILE"));
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	root->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	SetTempestUiScalePercent(applicationScale->currentData().toInt());
	SetTempestApplicationColor(applicationColor->currentData().toString(), selectedCustomColor);
	config_set_bool(config, TempestUiConfigSection, TempestAutoSizeKey, autoSizeOnStartup->isChecked());
	const QString profileSelection = responsiveProfile->currentData().toString();
	const bool useAutomaticProfile = profileSelection == QStringLiteral("auto");
	config_set_bool(config, TempestResponsiveConfigSection, TempestAutoProfileKey, useAutomaticProfile);
	if (!useAutomaticProfile)
		config_set_string(config, TempestResponsiveConfigSection, TempestManualProfileKey,
				  profileSelection.toUtf8().constData());
	config_set_bool(config, TempestResponsiveConfigSection, TempestAutoReflowKey, autoReflow->isChecked());
	config_set_bool(config, "BasicWindow", "ShowSafeAreas", safeAreaGuides->isChecked());
	UpdatePreviewSafeAreas();
	SetTempestResponsiveProfile(useAutomaticProfile ? DetectTempestResponsiveProfile() : profileSelection, true);

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
	config_save_safe(config, "tmp", nullptr);
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
	config_t *config = App()->GetUserConfig();
	if (commandMode && tempestSourceInspectorDock &&
	    !config_has_user_value(config, "BasicWindow", "TempestSourceOperationsDockInitialized")) {
		tempestSourceInspectorDock->setFloating(false);
		Qt::DockWidgetArea matrixArea = dockWidgetArea(tempestCommandMatrix);
		if (matrixArea == Qt::NoDockWidgetArea)
			matrixArea = Qt::LeftDockWidgetArea;
		addDockWidget(matrixArea, tempestSourceInspectorDock);
		tabifyDockWidget(tempestCommandMatrix, tempestSourceInspectorDock);
		tempestCommandMatrix->setVisible(true);
		tempestSourceInspectorDock->setVisible(true);
		tempestCommandMatrix->raise();
		config_set_bool(config, "BasicWindow", "TempestSourceOperationsDockInitialized", true);
		tempestCommandDockState = saveState();
		if (tempestResponsiveProfilesInitialized)
			StoreTempestResponsiveProfileStates(tempestResponsiveProfile);
	}
	if (tempestStreamInfoDock)
		IntegrateTempestStreamInfoDock(tempestStreamInfoDock);

	tempestCommandToolbar->setVisible(true);
	tempestMainframeBar->SetCommandWorkspace(commandMode);
	if (commandMode) {
		tempestResponsiveBreakpoint.clear();
		ApplyTempestResponsiveDockPriorities(true);
	}
	config_set_string(config, "BasicWindow", "TempestWorkspace", commandMode ? "command" : "engineering");
	config_save_safe(config, "tmp", nullptr);
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
	if (tempestResponsiveProfilesInitialized)
		StoreTempestResponsiveProfileStates(tempestResponsiveProfile);
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
	tempestSourceInspectorDock->setVisible(false);
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
	tempestSourceInspectorDock->setFeatures(features);
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
