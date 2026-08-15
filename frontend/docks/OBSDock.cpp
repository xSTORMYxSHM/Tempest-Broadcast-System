#include "OBSDock.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <QCheckBox>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

#include "moc_OBSDock.cpp"

namespace {
constexpr char DockScaleConfigSection[] = "TempestDockScale";
constexpr int MinimumDockScale = 60;
constexpr int MaximumDockScale = 160;
constexpr int DockScaleStep = 10;

int ScaledMetric(int value, qreal scale)
{
	if (value <= 0)
		return value;
	return std::max(1, qRound(value * scale));
}

QSize ScaledMinimumSize(const QSize &size, qreal scale)
{
	return {ScaledMetric(size.width(), scale), ScaledMetric(size.height(), scale)};
}

QSize ScaledMaximumSize(const QSize &size, qreal scale)
{
	const int width = size.width() >= QWIDGETSIZE_MAX ? QWIDGETSIZE_MAX : ScaledMetric(size.width(), scale);
	const int height = size.height() >= QWIDGETSIZE_MAX ? QWIDGETSIZE_MAX : ScaledMetric(size.height(), scale);
	return {width, height};
}

QString ScaledStyleSheet(const QString &source, qreal scale)
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
		result.replace(match.capturedStart(0), match.capturedLength(0), QStringLiteral("%1px").arg(scaled));
	}
	return result;
}
} // namespace

void OBSDock::EnableContentScaling(const QString &configKey)
{
	if (scaledContent || !widget() || configKey.isEmpty())
		return;

	QWidget *content = widget();
	content->setParent(nullptr);
	contentScaleConfigKey = configKey;
	baseDockMinimumSize = minimumSize();

	auto *shell = new QWidget(this);
	shell->setObjectName(QStringLiteral("tempestDockScaleShell"));
	shell->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestDockScaleShell { background: #07131e; }
		QWidget#tempestDockScaleBar { background: #06101a; border-bottom: 1px solid #183a50; }
		QLabel#tempestDockScaleCaption { color: #748fa4; font-size: 9px; font-weight: 700; letter-spacing: 1px; }
		QToolButton { min-width: 24px; min-height: 20px; padding: 0 5px; color: #9edbea; background: #0d2230; border: 1px solid #1f506d; font-weight: 700; }
		QToolButton:hover { color: #ffffff; border-color: #45d9ff; background: #0c456b; }
		QToolButton#tempestDockScaleReset { min-width: 48px; }
		QScrollArea { border: none; background: #07131e; }
	)"));
	auto *shellLayout = new QVBoxLayout(shell);
	shellLayout->setContentsMargins(0, 0, 0, 0);
	shellLayout->setSpacing(0);

	auto *scaleBar = new QWidget(shell);
	scaleBar->setObjectName(QStringLiteral("tempestDockScaleBar"));
	auto *scaleLayout = new QHBoxLayout(scaleBar);
	scaleLayout->setContentsMargins(6, 3, 6, 3);
	scaleLayout->setSpacing(4);
	auto *scaleCaption = new QLabel(QStringLiteral("DOCK SCALE"), scaleBar);
	scaleCaption->setObjectName(QStringLiteral("tempestDockScaleCaption"));
	scaleLayout->addWidget(scaleCaption);
	scaleLayout->addStretch(1);
	auto *scaleDown = new QToolButton(scaleBar);
	scaleDown->setText(QStringLiteral("-"));
	scaleDown->setAccessibleName(QStringLiteral("Decrease dock scale"));
	scaleResetButton = new QToolButton(scaleBar);
	scaleResetButton->setObjectName(QStringLiteral("tempestDockScaleReset"));
	scaleResetButton->setAccessibleName(QStringLiteral("Reset dock scale"));
	scaleResetButton->setToolTip(QStringLiteral("Reset to 100%. Ctrl+mouse wheel also changes dock scale."));
	auto *scaleUp = new QToolButton(scaleBar);
	scaleUp->setText(QStringLiteral("+"));
	scaleUp->setAccessibleName(QStringLiteral("Increase dock scale"));
	scaleLayout->addWidget(scaleDown);
	scaleLayout->addWidget(scaleResetButton);
	scaleLayout->addWidget(scaleUp);
	shellLayout->addWidget(scaleBar);

	contentScroll = new QScrollArea(shell);
	contentScroll->setObjectName(QStringLiteral("tempestDockContentScroll"));
	contentScroll->setFrameShape(QFrame::NoFrame);
	contentScroll->setWidgetResizable(true);
	contentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	contentScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	contentScroll->setWidget(content);
	shellLayout->addWidget(contentScroll, 1);
	setWidget(shell);
	scaledContent = content;

	connect(scaleDown, &QToolButton::clicked, this,
		[this]() { ApplyContentScale(contentScalePercent - DockScaleStep); });
	connect(scaleUp, &QToolButton::clicked, this,
		[this]() { ApplyContentScale(contentScalePercent + DockScaleStep); });
	connect(scaleResetButton, &QToolButton::clicked, this, [this]() { ApplyContentScale(100); });

	auto *zoomIn = new QShortcut(QKeySequence::ZoomIn, this);
	zoomIn->setContext(Qt::WidgetWithChildrenShortcut);
	connect(zoomIn, &QShortcut::activated, this,
		[this]() { ApplyContentScale(contentScalePercent + DockScaleStep); });
	auto *zoomOut = new QShortcut(QKeySequence::ZoomOut, this);
	zoomOut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(zoomOut, &QShortcut::activated, this,
		[this]() { ApplyContentScale(contentScalePercent - DockScaleStep); });
	auto *zoomReset = new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this);
	zoomReset->setContext(Qt::WidgetWithChildrenShortcut);
	connect(zoomReset, &QShortcut::activated, this, [this]() { ApplyContentScale(100); });

	CaptureScaleMetrics();
	InstallScaleEventFilters();
	config_t *config = App()->GetUserConfig();
	const QByteArray key = contentScaleConfigKey.toUtf8();
	const int savedScale = (int)config_get_int(config, DockScaleConfigSection, key.constData());
	ApplyContentScale(savedScale >= MinimumDockScale && savedScale <= MaximumDockScale ? savedScale : 100, false);
}

void OBSDock::CaptureScaleMetrics()
{
	if (!scaledContent)
		return;

	if (!scaledContent->property("tempestScaleBaseFont").isValid())
		scaledContent->setProperty("tempestScaleBaseFont", scaledContent->font());

	QList<QWidget *> widgets = scaledContent->findChildren<QWidget *>();
	widgets.prepend(scaledContent);
	for (QWidget *child : std::as_const(widgets)) {
		if (!child->property("tempestScaleBaseMinimum").isValid())
			child->setProperty("tempestScaleBaseMinimum", child->minimumSize());
		if (!child->property("tempestScaleBaseMaximum").isValid())
			child->setProperty("tempestScaleBaseMaximum", child->maximumSize());
		if (!child->property("tempestScaleStyleManaged").isValid() && !child->styleSheet().isEmpty()) {
			child->setProperty("tempestScaleStyleManaged", true);
			child->setProperty("tempestScaleBaseStyle", child->styleSheet());
		}
	}

	const QList<QLayout *> layouts = scaledContent->findChildren<QLayout *>();
	for (QLayout *layout : layouts) {
		if (!layout->property("tempestScaleBaseMargins").isValid())
			layout->setProperty("tempestScaleBaseMargins", QVariant::fromValue(layout->contentsMargins()));
		if (!layout->property("tempestScaleBaseSpacing").isValid())
			layout->setProperty("tempestScaleBaseSpacing", layout->spacing());
		if (auto *grid = qobject_cast<QGridLayout *>(layout)) {
			if (!grid->property("tempestScaleBaseHorizontalSpacing").isValid())
				grid->setProperty("tempestScaleBaseHorizontalSpacing", grid->horizontalSpacing());
			if (!grid->property("tempestScaleBaseVerticalSpacing").isValid())
				grid->setProperty("tempestScaleBaseVerticalSpacing", grid->verticalSpacing());
		} else if (auto *form = qobject_cast<QFormLayout *>(layout)) {
			if (!form->property("tempestScaleBaseHorizontalSpacing").isValid())
				form->setProperty("tempestScaleBaseHorizontalSpacing", form->horizontalSpacing());
			if (!form->property("tempestScaleBaseVerticalSpacing").isValid())
				form->setProperty("tempestScaleBaseVerticalSpacing", form->verticalSpacing());
		}
	}
}

void OBSDock::ApplyContentScale(int percent, bool save)
{
	if (!scaledContent)
		return;
	contentScalePercent = std::clamp(percent, MinimumDockScale, MaximumDockScale);
	const qreal scale = contentScalePercent / 100.0;
	CaptureScaleMetrics();

	QFont font = scaledContent->property("tempestScaleBaseFont").value<QFont>();
	if (font.pixelSize() > 0)
		font.setPixelSize(ScaledMetric(font.pixelSize(), scale));
	else if (font.pointSizeF() > 0)
		font.setPointSizeF(std::max(1.0, font.pointSizeF() * scale));
	scaledContent->setFont(font);

	QList<QWidget *> widgets = scaledContent->findChildren<QWidget *>();
	widgets.prepend(scaledContent);
	for (QWidget *child : std::as_const(widgets)) {
		const QSize minimum = child->property("tempestScaleBaseMinimum").toSize();
		const QSize maximum = child->property("tempestScaleBaseMaximum").toSize();
		child->setMinimumSize(ScaledMinimumSize(minimum, scale));
		child->setMaximumSize(ScaledMaximumSize(maximum, scale));
		if (child->property("tempestScaleStyleManaged").toBool())
			child->setStyleSheet(
				ScaledStyleSheet(child->property("tempestScaleBaseStyle").toString(), scale));
	}

	const QList<QLayout *> layouts = scaledContent->findChildren<QLayout *>();
	for (QLayout *layout : layouts) {
		const QMargins margins = layout->property("tempestScaleBaseMargins").value<QMargins>();
		layout->setContentsMargins(ScaledMetric(margins.left(), scale), ScaledMetric(margins.top(), scale),
					   ScaledMetric(margins.right(), scale), ScaledMetric(margins.bottom(), scale));
		const int spacing = layout->property("tempestScaleBaseSpacing").toInt();
		if (spacing >= 0)
			layout->setSpacing(ScaledMetric(spacing, scale));
		if (auto *grid = qobject_cast<QGridLayout *>(layout)) {
			const int horizontal = grid->property("tempestScaleBaseHorizontalSpacing").toInt();
			const int vertical = grid->property("tempestScaleBaseVerticalSpacing").toInt();
			if (horizontal >= 0)
				grid->setHorizontalSpacing(ScaledMetric(horizontal, scale));
			if (vertical >= 0)
				grid->setVerticalSpacing(ScaledMetric(vertical, scale));
		} else if (auto *form = qobject_cast<QFormLayout *>(layout)) {
			const int horizontal = form->property("tempestScaleBaseHorizontalSpacing").toInt();
			const int vertical = form->property("tempestScaleBaseVerticalSpacing").toInt();
			if (horizontal >= 0)
				form->setHorizontalSpacing(ScaledMetric(horizontal, scale));
			if (vertical >= 0)
				form->setVerticalSpacing(ScaledMetric(vertical, scale));
		}
	}

	setMinimumSize(ScaledMinimumSize(baseDockMinimumSize, scale));
	if (scaleResetButton)
		scaleResetButton->setText(QStringLiteral("%1%").arg(contentScalePercent));
	InstallScaleEventFilters();
	contentScaleChanged();
	if (save) {
		config_t *config = App()->GetUserConfig();
		const QByteArray key = contentScaleConfigKey.toUtf8();
		config_set_int(config, DockScaleConfigSection, key.constData(), contentScalePercent);
		config_save_safe(config, "tmp", nullptr);
	}
}

void OBSDock::InstallScaleEventFilters()
{
	if (!scaledContent)
		return;
	scaledContent->installEventFilter(this);
	for (QWidget *child : scaledContent->findChildren<QWidget *>())
		child->installEventFilter(this);
	if (contentScroll && contentScroll->viewport())
		contentScroll->viewport()->installEventFilter(this);
}

bool OBSDock::eventFilter(QObject *watched, QEvent *event)
{
	if (scaledContent && event->type() == QEvent::Wheel) {
		auto *wheel = static_cast<QWheelEvent *>(event);
		QWidget *target = qobject_cast<QWidget *>(watched);
		if ((wheel->modifiers() & Qt::ControlModifier) && target &&
		    (target == scaledContent || scaledContent->isAncestorOf(target) ||
		     (contentScroll && target == contentScroll->viewport()))) {
			const int direction = wheel->angleDelta().y() >= 0 ? DockScaleStep : -DockScaleStep;
			ApplyContentScale(contentScalePercent + direction);
			wheel->accept();
			return true;
		}
	}
	return QDockWidget::eventFilter(watched, event);
}

void OBSDock::closeEvent(QCloseEvent *event)
{
	auto msgBox = []() {
		QMessageBox msgbox(App()->GetMainWindow());
		msgbox.setWindowTitle(QTStr("DockCloseWarning.Title"));
		msgbox.setText(QTStr("DockCloseWarning.Text"));
		msgbox.setIcon(QMessageBox::Icon::Information);
		msgbox.addButton(QMessageBox::Ok);

		QCheckBox *cb = new QCheckBox(QTStr("DoNotShowAgain"));
		msgbox.setCheckBox(cb);

		msgbox.exec();

		if (cb->isChecked()) {
			config_set_bool(App()->GetUserConfig(), "General", "WarnedAboutClosingDocks", true);
			config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
		}
	};

	bool warned = config_get_bool(App()->GetUserConfig(), "General", "WarnedAboutClosingDocks");
	if (!OBSBasic::Get()->isClosing() && !warned) {
		QMetaObject::invokeMethod(App(), "Exec", Qt::QueuedConnection, Q_ARG(VoidFunc, msgBox));
	}

	QDockWidget::closeEvent(event);

	if (widget() && event->isAccepted()) {
		QEvent widgetEvent(QEvent::Type(QEvent::User + QEvent::Close));
		qApp->sendEvent(widget(), &widgetEvent);
	}
}

void OBSDock::showEvent(QShowEvent *event)
{
	QDockWidget::showEvent(event);
}
