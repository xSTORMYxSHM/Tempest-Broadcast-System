#pragma once

#include <QApplication>
#include <QColor>
#include <QRegularExpression>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <algorithm>

namespace TempestAppearance {
constexpr char AccentPresetProperty[] = "tempestAccentPreset";
constexpr char AccentColorProperty[] = "tempestAccentColor";
constexpr char BaseApplicationStyleProperty[] = "tempestAccentBaseApplicationStyle";
constexpr char AppliedApplicationStyleProperty[] = "tempestAccentAppliedApplicationStyle";
constexpr char BaseWidgetStyleProperty[] = "tempestAccentBaseWidgetStyle";
constexpr char AppliedWidgetStyleProperty[] = "tempestAccentAppliedWidgetStyle";

inline QColor DefaultAccent()
{
	return QColor(QStringLiteral("#45d9ff"));
}

inline QColor PresetColor(const QString &preset)
{
	if (preset == QStringLiteral("ultraviolet"))
		return QColor(QStringLiteral("#9b8cff"));
	if (preset == QStringLiteral("magenta"))
		return QColor(QStringLiteral("#ff4fd8"));
	if (preset == QStringLiteral("ember"))
		return QColor(QStringLiteral("#ff9b45"));
	if (preset == QStringLiteral("emerald"))
		return QColor(QStringLiteral("#4ee6a8"));
	if (preset == QStringLiteral("ice"))
		return QColor(QStringLiteral("#65a8ff"));
	return DefaultAccent();
}

inline QColor CurrentAccent()
{
	if (!qApp)
		return DefaultAccent();
	const QColor color(qApp->property(AccentColorProperty).toString());
	return color.isValid() ? color : DefaultAccent();
}

inline QColor Mix(const QColor &first, const QColor &second, qreal secondWeight)
{
	const qreal weight = std::clamp(secondWeight, 0.0, 1.0);
	return QColor(qRound(first.red() * (1.0 - weight) + second.red() * weight),
		      qRound(first.green() * (1.0 - weight) + second.green() * weight),
		      qRound(first.blue() * (1.0 - weight) + second.blue() * weight));
}

inline void ReplaceColor(QString &style, const QString &hex, const QColor &replacement)
{
	style.replace(hex, replacement.name(QColor::HexRgb), Qt::CaseInsensitive);
	const QColor original(hex);
	if (!original.isValid())
		return;
	const QString pattern = QStringLiteral("rgb\\(\\s*%1\\s*,\\s*%2\\s*,\\s*%3\\s*\\)")
					.arg(original.red())
					.arg(original.green())
					.arg(original.blue());
	style.replace(
		QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption),
		QStringLiteral("rgb(%1,%2,%3)").arg(replacement.red()).arg(replacement.green()).arg(replacement.blue()));
}

inline QString TransformStyleSheet(const QString &source)
{
	const QColor accent = CurrentAccent();
	if (!accent.isValid() ||
	    accent.name(QColor::HexRgb).compare(DefaultAccent().name(QColor::HexRgb), Qt::CaseInsensitive) == 0)
		return source;

	QString result = source;
	const QColor black(QStringLiteral("#000000"));
	const QColor white(QStringLiteral("#ffffff"));
	const QColor light = Mix(accent, white, 0.68);
	const QColor soft = Mix(accent, white, 0.36);
	const QColor primary = Mix(accent, black, 0.24);
	const QColor dark = Mix(accent, black, 0.62);
	const QColor darker = Mix(accent, black, 0.82);
	const QColor border = Mix(QColor(QStringLiteral("#24323d")), accent, 0.31);
	const QColor borderStrong = Mix(QColor(QStringLiteral("#263642")), accent, 0.46);
	const QColor muted = Mix(QColor(QStringLiteral("#7b8790")), accent, 0.20);
	const QColor mutedLight = Mix(QColor(QStringLiteral("#a8bac5")), accent, 0.30);
	const QColor surface1 = Mix(QColor(QStringLiteral("#03070b")), accent, 0.045);
	const QColor surface2 = Mix(QColor(QStringLiteral("#050b11")), accent, 0.060);
	const QColor surface3 = Mix(QColor(QStringLiteral("#071019")), accent, 0.075);
	const QColor surface4 = Mix(QColor(QStringLiteral("#0a151f")), accent, 0.090);
	const QColor surface5 = Mix(QColor(QStringLiteral("#0d1b27")), accent, 0.105);
	const QColor hover = Mix(accent, black, 0.52);

	ReplaceColor(result, QStringLiteral("#45d9ff"), accent);
	ReplaceColor(result, QStringLiteral("#bdf6ff"), light);
	ReplaceColor(result, QStringLiteral("#d8fbff"), Mix(accent, white, 0.80));
	ReplaceColor(result, QStringLiteral("#9edbea"), soft);
	ReplaceColor(result, QStringLiteral("#0c7ccb"), primary);
	ReplaceColor(result, QStringLiteral("#074577"), dark);
	ReplaceColor(result, QStringLiteral("#073c5f"), dark);
	ReplaceColor(result, QStringLiteral("#06345d"), darker);
	ReplaceColor(result, QStringLiteral("#031c34"), darker);
	ReplaceColor(result, QStringLiteral("#0c456b"), hover);
	ReplaceColor(result, QStringLiteral("#1f506d"), borderStrong);
	ReplaceColor(result, QStringLiteral("#183a50"), border);
	ReplaceColor(result, QStringLiteral("#17394f"), border);
	ReplaceColor(result, QStringLiteral("#748fa4"), muted);
	ReplaceColor(result, QStringLiteral("#9eb7c8"), mutedLight);
	ReplaceColor(result, QStringLiteral("#41596c"), Mix(border, white, 0.12));
	ReplaceColor(result, QStringLiteral("#40576a"), Mix(border, white, 0.10));
	ReplaceColor(result, QStringLiteral("#1f3242"), border);
	ReplaceColor(result, QStringLiteral("#07131e"), surface3);
	ReplaceColor(result, QStringLiteral("#06131f"), surface3);
	ReplaceColor(result, QStringLiteral("#06101a"), surface2);
	ReplaceColor(result, QStringLiteral("#050d16"), surface2);
	ReplaceColor(result, QStringLiteral("#04101a"), surface2);
	ReplaceColor(result, QStringLiteral("#03090f"), surface1);
	ReplaceColor(result, QStringLiteral("#02070b"), surface1);
	ReplaceColor(result, QStringLiteral("#09141e"), surface3);
	ReplaceColor(result, QStringLiteral("#0d1a26"), surface4);
	ReplaceColor(result, QStringLiteral("#0d2230"), surface5);
	ReplaceColor(result, QStringLiteral("#13222f"), surface5);
	return result;
}

inline void SetManagedStyleSheet(QWidget *widget, const QString &baseStyle)
{
	if (!widget)
		return;
	widget->setProperty(BaseWidgetStyleProperty, baseStyle);
	const QString applied = TransformStyleSheet(baseStyle);
	widget->setProperty(AppliedWidgetStyleProperty, applied);
	if (widget->styleSheet() != applied)
		widget->setStyleSheet(applied);
}

inline void ApplyWidgetStyle(QWidget *widget)
{
	if (!widget || widget->styleSheet().isEmpty())
		return;
	const QString current = widget->styleSheet();
	QString base = widget->property(BaseWidgetStyleProperty).toString();
	const QString previousApplied = widget->property(AppliedWidgetStyleProperty).toString();
	if (base.isEmpty() || current != previousApplied)
		base = current;
	SetManagedStyleSheet(widget, base);
}

inline void ApplyWidgetTree(QWidget *root)
{
	if (!root)
		return;
	ApplyWidgetStyle(root);
	for (QWidget *widget : root->findChildren<QWidget *>())
		ApplyWidgetStyle(widget);
}

inline void ApplyApplication(QApplication *application)
{
	if (!application)
		return;
	const QString current = application->styleSheet();
	QString base = application->property(BaseApplicationStyleProperty).toString();
	const QString previousApplied = application->property(AppliedApplicationStyleProperty).toString();
	if (base.isEmpty() || current != previousApplied) {
		base = current;
		application->setProperty(BaseApplicationStyleProperty, base);
	}
	const QString applied = TransformStyleSheet(base);
	application->setProperty(AppliedApplicationStyleProperty, applied);
	if (current != applied)
		application->setStyleSheet(applied);
	for (QWidget *widget : application->allWidgets())
		ApplyWidgetStyle(widget);
}
} // namespace TempestAppearance
