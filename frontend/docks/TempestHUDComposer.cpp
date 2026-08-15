#include "TempestHUDComposer.hpp"

#include <OBSApp.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSize>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "moc_TempestHUDComposer.cpp"

class TempestReactionPreview : public QWidget {
public:
	explicit TempestReactionPreview(QWidget *parent = nullptr) : QWidget(parent)
	{
		setMinimumHeight(150);
		setAccessibleName(QStringLiteral("Tempest reaction live preview"));
		timer.setInterval(33);
		connect(&timer, &QTimer::timeout, this, [this]() { Tick(); });
		timer.start();
	}

	void SetTelemetryPath(const QString &path) { telemetryPath = path; }

	void SetState(const QString &newTitle, const QString &newAccent, const QString &newReaction,
		      const QString &newSignal, double newStrength, double newThreshold, double newAttack,
		      double newDecay, double newIdle)
	{
		title = newTitle.isEmpty() ? QStringLiteral("TEMPEST SIGNAL ELEMENT") : newTitle;
		accent = QColor(newAccent);
		if (!accent.isValid())
			accent = QColor(QStringLiteral("#45d9ff"));
		reaction = newReaction;
		signal = newSignal;
		strength = newStrength;
		threshold = newThreshold;
		attack = newAttack;
		decay = newDecay;
		idle = newIdle;
		update();
	}

	void TriggerTest()
	{
		testLevel = 1.0;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.fillRect(rect(), QColor(QStringLiteral("#050d16")));
		const double breathing = reaction == QStringLiteral("pulse") ? idle * (1.0 + 0.45 * std::sin(phase))
									     : 0.0;
		double react = std::max(envelope * strength, breathing);
		if (reaction == QStringLiteral("glow"))
			react *= 0.68;
		react = std::clamp(react, 0.0, 1.8);
		const int shift = reaction == QStringLiteral("glitch") && react > 0.62
					  ? (int)std::round(std::sin(phase * 11.0) * react * 5.0)
					  : 0;
		QRectF panel = rect().adjusted(14 + shift, 14, -14 + shift, -14);
		for (int width = 12; width >= 4; width -= 4) {
			QColor glow = accent;
			glow.setAlpha((int)(8 + react * (20 - width)));
			painter.setPen(QPen(glow, width));
			painter.drawRoundedRect(panel, 4, 4);
		}
		QColor border = accent;
		border.setAlpha((int)std::clamp(105.0 + react * 100.0, 0.0, 255.0));
		painter.setPen(QPen(border, 1.4));
		painter.setBrush(QColor(7, 22, 34, 235));
		painter.drawRoundedRect(panel, 4, 4);

		const qreal corner = 25.0;
		painter.setPen(QPen(accent, 3));
		painter.drawLine(panel.topLeft(), panel.topLeft() + QPointF(corner, 0));
		painter.drawLine(panel.topLeft(), panel.topLeft() + QPointF(0, corner));
		painter.drawLine(panel.bottomRight(), panel.bottomRight() - QPointF(corner, 0));
		painter.drawLine(panel.bottomRight(), panel.bottomRight() - QPointF(0, corner));

		const qreal coreSize = 42.0 + react * 7.0;
		QRectF core(panel.left() + 24, panel.center().y() - coreSize / 2, coreSize, coreSize);
		painter.save();
		painter.translate(core.center());
		painter.rotate(45);
		painter.translate(-core.center());
		painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), (int)(45 + react * 90)));
		painter.setPen(QPen(accent, 2));
		painter.drawRect(core);
		painter.restore();

		painter.setPen(QColor(QStringLiteral("#748fa4")));
		painter.setFont(QFont(QStringLiteral("Segoe UI"), 7, QFont::DemiBold));
		painter.drawText(QRectF(panel.left() + 90, panel.top() + 25, panel.width() - 125, 18),
				 Qt::AlignLeft | Qt::AlignVCenter,
				 QStringLiteral("REACTION LAB // %1 BUS").arg(signal.toUpper()));
		painter.setPen(QColor(QStringLiteral("#d8fbff")));
		painter.setFont(QFont(QStringLiteral("Segoe UI"), 13, QFont::Bold));
		painter.drawText(QRectF(panel.left() + 90, panel.top() + 48, panel.width() - 125, 30),
				 Qt::AlignLeft | Qt::AlignVCenter, title.toUpper());
		painter.setPen(accent);
		painter.setFont(QFont(QStringLiteral("Segoe UI"), 7, QFont::DemiBold));
		painter.drawText(QRectF(panel.left() + 90, panel.bottom() - 34, panel.width() - 125, 18),
				 Qt::AlignLeft | Qt::AlignVCenter,
				 QStringLiteral("%1 // T %2 // A %3 // D %4")
					 .arg(reaction.toUpper())
					 .arg(threshold, 0, 'f', 2)
					 .arg(attack, 0, 'f', 2)
					 .arg(decay, 0, 'f', 2));

		const QRectF meter(panel.right() - 18, panel.top() + 16, 5, panel.height() - 32);
		painter.fillRect(meter, QColor(accent.red(), accent.green(), accent.blue(), 24));
		QRectF levelBar = meter;
		levelBar.setTop(meter.bottom() - meter.height() * std::clamp(react, 0.0, 1.0));
		painter.fillRect(levelBar, accent);
		QColor scan = accent;
		scan.setAlpha((int)(15 + react * 55));
		painter.fillRect(QRectF(0, std::fmod(phase * 19.0, std::max(1, height())), width(), 1), scan);
	}

private:
	void Tick()
	{
		double raw = 0.0;
		if (!telemetryPath.isEmpty()) {
			QFile file(telemetryPath);
			if (file.open(QIODevice::ReadOnly)) {
				const QJsonObject data = QJsonDocument::fromJson(file.readAll()).object();
				const QString key = signal == QStringLiteral("desktop") ? QStringLiteral("desktop")
						    : signal == QStringLiteral("microphone")
							    ? QStringLiteral("microphone")
						    : signal == QStringLiteral("beat") ? QStringLiteral("beat")
										       : QStringLiteral("master");
				raw = data.value(key).toDouble(data.value(QStringLiteral("level")).toDouble());
				raw = std::max(raw, data.value(QStringLiteral("pulse")).toDouble());
			}
		}
		raw = std::max(raw, testLevel);
		testLevel *= 0.82;
		const double normalized = std::clamp((raw - threshold) / std::max(0.001, 1.0 - threshold), 0.0, 1.5);
		if (normalized > envelope)
			envelope += (normalized - envelope) * attack;
		else
			envelope *= decay;
		phase += 0.09;
		update();
	}

	QTimer timer{this};
	QString telemetryPath;
	QString title = QStringLiteral("TEMPEST SIGNAL ELEMENT");
	QString reaction = QStringLiteral("signal");
	QString signal = QStringLiteral("master");
	QColor accent = QColor(QStringLiteral("#45d9ff"));
	double strength = 1.0;
	double threshold = 0.08;
	double attack = 0.55;
	double decay = 0.82;
	double idle = 0.08;
	double envelope = 0.0;
	double testLevel = 0.0;
	double phase = 0.0;
};

namespace {
constexpr char ConfigSection[] = "TempestHUDComposer";
constexpr int HudSchemaVersion = 5;

QSize ElementSize(const QString &type)
{
	if (type == QStringLiteral("chat"))
		return {520, 680};
	if (type == QStringLiteral("radio"))
		return {680, 180};
	if (type == QStringLiteral("media"))
		return {640, 150};
	if (type == QStringLiteral("lore"))
		return {760, 250};
	return {720, 170};
}

QString CssContent(QString value)
{
	value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
	value.replace(QStringLiteral("\""), QStringLiteral("\\\""));
	value.replace(QStringLiteral("\r"), QString());
	value.replace(QStringLiteral("\n"), QStringLiteral("\\A "));
	return value;
}

QString RemoteChatCss(const QString &browserUrl, const QString &primary, const QString &secondary,
		      const QString &accent)
{
	const QUrl url(browserUrl);
	const bool twitch = url.host().endsWith(QStringLiteral("twitch.tv"), Qt::CaseInsensitive);
	QString css =
		QStringLiteral(R"TEMPESTCSS(
html,body{background:transparent!important;margin:0!important;overflow:hidden!important}
body{box-sizing:border-box!important;border:1px solid %1!important;box-shadow:inset 0 0 22px color-mix(in srgb,%1 34%,transparent),0 0 12px color-mix(in srgb,%1 28%,transparent)!important;padding:76px 12px 34px!important}
body:before{content:"%2\A %3";white-space:pre;box-sizing:border-box;position:fixed;z-index:2147483647;left:0;right:0;top:0;height:68px;padding:14px 18px;color:%1;background:linear-gradient(110deg,rgba(4,14,23,.97),rgba(5,28,44,.92));border-bottom:1px solid color-mix(in srgb,%1 52%,transparent);font:700 16px/1.35 "Segoe UI",Arial,sans-serif;letter-spacing:.16em;pointer-events:none;text-shadow:0 0 12px %1}
body:after{content:"RELAY ONLINE // TEMPEST CHANNEL";box-sizing:border-box;position:fixed;z-index:2147483647;left:0;right:0;bottom:0;height:28px;padding:7px 14px;color:%1;background:rgba(4,14,23,.96);border-top:1px solid color-mix(in srgb,%1 38%,transparent);font:700 9px/1 "Segoe UI",Arial,sans-serif;letter-spacing:.18em;pointer-events:none}
)TEMPESTCSS")
			.arg(accent, CssContent(primary.isEmpty() ? QStringLiteral("CHANNEL RELAY") : primary),
			     CssContent(secondary.isEmpty() ? QStringLiteral("TWITCH CHAT // ONLINE") : secondary));
	if (twitch) {
		css += QStringLiteral(R"TEMPESTCSS(
.stream-chat-header,.chat-input{display:none!important}
.chat-shell,.chat-room,.chat-room__content,.chat-scrollable-area__message-container{background:transparent!important}
.chat-shell{height:100%!important}
)TEMPESTCSS");
	}
	return css;
}

QString RemoteRadioCss(const QString &primary, const QString &secondary)
{
	return QStringLiteral(R"TEMPESTCSS(
html,body{margin:0!important;padding:0!important;width:100%!important;height:100%!important;background:transparent!important;overflow:hidden!important}
body{display:flex!important;align-items:center!important;justify-content:center!important;font-family:"Segoe UI",Arial,sans-serif!important}
.radio-player-widget{position:relative!important;box-sizing:border-box!important;width:620px!important;min-height:120px!important;margin:0!important;padding:40px 28px 18px!important;border:1px solid rgba(72,215,255,.42)!important;border-radius:22px!important;background:radial-gradient(circle at 15% 50%,rgba(0,210,255,.16),transparent 34%),radial-gradient(circle at 85% 50%,rgba(132,67,255,.17),transparent 38%),linear-gradient(135deg,rgba(5,13,23,.98),rgba(11,12,26,.98) 55%,rgba(18,8,31,.98))!important;box-shadow:0 0 10px rgba(0,210,255,.22),0 0 30px rgba(0,170,255,.1),inset 0 1px 0 rgba(255,255,255,.08),inset 0 0 28px rgba(0,0,0,.7)!important;color:#fff!important;overflow:hidden!important;animation:shrPanelGlow 5s ease-in-out infinite}
.radio-player-widget:before{content:"%1";position:absolute;top:14px;left:28px;color:rgba(224,248,255,.96);font-size:11px;font-weight:700;letter-spacing:4px;text-shadow:0 0 6px rgba(0,220,255,.8),0 0 15px rgba(0,180,255,.35);white-space:nowrap}
.radio-player-widget:after{content:"%2";position:absolute;top:15px;right:28px;color:#ff5879;font-size:9px;font-weight:700;letter-spacing:2px;text-shadow:0 0 5px rgba(255,40,85,1),0 0 12px rgba(255,40,85,.55);animation:shrLive 1.7s ease-in-out infinite}
.radio-player-widget .now-playing-details,.radio-player-widget .radio-controls{background:transparent!important;border:0!important;color:#fff!important;min-width:0!important}
.radio-player-widget .now-playing-details .now-playing-title{color:#fff!important;font-size:16px!important;font-weight:700!important;letter-spacing:.5px!important;text-shadow:0 0 8px rgba(0,205,255,.22)!important;text-overflow:clip!important;overflow:visible!important;white-space:normal!important;word-break:break-word!important}
.radio-player-widget .now-playing-details .now-playing-artist{margin-top:4px!important;color:rgba(190,230,245,.76)!important;font-size:10px!important;letter-spacing:1.5px!important;text-transform:uppercase!important;text-overflow:clip!important;overflow:visible!important;white-space:normal!important}
.radio-player-widget .now-playing-art,.radio-player-widget .now-playing-art img{border-radius:12px!important}.radio-player-widget .now-playing-art{overflow:hidden!important;box-shadow:0 0 10px rgba(0,210,255,.2)!important}
.radio-player-widget .radio-controls .radio-control-play-button{color:#dffaff!important;background:rgba(0,190,255,.08)!important;border:1px solid rgba(0,215,255,.32)!important;border-radius:50%!important;box-shadow:0 0 10px rgba(0,210,255,.18),inset 0 0 10px rgba(0,210,255,.05)!important}
.radio-player-widget .radio-controls .radio-control-play-button .icon{font-size:24px!important;filter:drop-shadow(0 0 5px rgba(0,220,255,.65))}
.card,.card-body,.public-page,.page-minimal,main{background:transparent!important;border:0!important;box-shadow:none!important}
@keyframes shrLive{0%,100%{opacity:1}50%{opacity:.35}}
@keyframes shrPanelGlow{0%,100%{box-shadow:0 0 10px rgba(0,210,255,.2),0 0 30px rgba(0,170,255,.09),inset 0 1px 0 rgba(255,255,255,.08),inset 0 0 28px rgba(0,0,0,.7)!important}50%{box-shadow:0 0 16px rgba(0,220,255,.32),0 0 42px rgba(120,65,255,.14),inset 0 1px 0 rgba(255,255,255,.1),inset 0 0 28px rgba(0,0,0,.7)!important}}
)TEMPESTCSS")
		.arg(CssContent(primary.isEmpty() ? QStringLiteral("STORM HORIZON RADIO") : primary),
		     CssContent(secondary.isEmpty() ? QStringLiteral("●  LIVE") : secondary));
}
} // namespace

TempestHUDComposer::TempestHUDComposer(OBSBasic *main, QWidget *parent) : OBSDock(parent), main(main)
{
	setObjectName(QStringLiteral("tempestHUDComposer"));
	setWindowTitle(QStringLiteral("Mainframe HUD Composer"));
	setMinimumWidth(390);
	BuildInterface();
	EnableContentScaling(objectName());
	EnsureOutputDirectory();
	if (reactionPreview && !outputDirectory.isEmpty())
		reactionPreview->SetTelemetryPath(QDir(outputDirectory).filePath(QStringLiteral("telemetry.json")));
	LoadElements();
	RebuildElementList();
	for (const Element &element : elements)
		RenderElement(element);
}

void TempestHUDComposer::BuildInterface()
{
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("tempestHUDRoot"));
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestHUDRoot { background: #07131e; }
		QLabel#hudTitle { color: #45d9ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#hudSubtitle { color: #748fa4; font-size: 10px; letter-spacing: 1px; }
		QLabel#hudHint { color: #748fa4; font-size: 10px; padding: 6px; border: 1px solid #183a50; background: #06101a; }
		QLabel#hudStatus { color: #45d9ff; font-size: 10px; }
		QComboBox, QLineEdit, QListWidget, QDoubleSpinBox { background: #06101a; border: 1px solid #1f506d; color: #bdf6ff; }
		QComboBox, QLineEdit, QDoubleSpinBox { min-height: 29px; padding: 0 7px; }
		QListWidget::item { min-height: 37px; border-bottom: 1px solid #102c3e; padding: 4px; }
		QListWidget::item:selected { background: #073c5f; border: 1px solid #45d9ff; }
		QPushButton { min-height: 31px; border: 1px solid #1f506d; background: #0d2230; color: #bdf6ff; font-weight: 700; padding: 0 8px; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QPushButton:disabled { color: #40576a; border-color: #172d3d; background: #091721; }
		QCheckBox { color: #9eb7c8; }
	)"));
	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(7);

	auto *title = new QLabel(QStringLiteral("HUD COMPOSER"), root);
	title->setObjectName(QStringLiteral("hudTitle"));
	auto *subtitle = new QLabel(QStringLiteral("Movable reactive frame and signal plates"), root);
	subtitle->setObjectName(QStringLiteral("hudSubtitle"));
	layout->addWidget(title);
	layout->addWidget(subtitle);

	elementList = new QListWidget(root);
	elementList->setObjectName(QStringLiteral("tempestHUDElementList"));
	elementList->setAccessibleName(QStringLiteral("Tempest HUD elements"));
	connect(elementList, &QListWidget::currentRowChanged, this, &TempestHUDComposer::SelectElement);
	layout->addWidget(elementList, 1);

	auto *newButton = new QPushButton(QStringLiteral("NEW ELEMENT"), root);
	connect(newButton, &QPushButton::clicked, this, &TempestHUDComposer::NewElement);
	layout->addWidget(newButton);

	auto *reactionLabel = new QLabel(QStringLiteral("REACTION LAB // LIVE SIGNAL PREVIEW"), root);
	reactionLabel->setObjectName(QStringLiteral("hudSubtitle"));
	layout->addWidget(reactionLabel);
	reactionPreview = new TempestReactionPreview(root);
	layout->addWidget(reactionPreview);
	auto *testReactionButton = new QPushButton(QStringLiteral("TEST SELECTED REACTION"), root);
	testReactionButton->setAccessibleName(QStringLiteral("Test selected HUD reaction"));
	connect(testReactionButton, &QPushButton::clicked, this, &TempestHUDComposer::TestReaction);
	layout->addWidget(testReactionButton);

	auto *form = new QFormLayout();
	nameField = new QLineEdit(root);
	nameField->setPlaceholderText(QStringLiteral("Element and OBS source name"));
	typeSelector = new QComboBox(root);
	typeSelector->addItem(QStringLiteral("CANVAS FRAME"), QStringLiteral("frame"));
	typeSelector->addItem(QStringLiteral("CHAT TERMINAL"), QStringLiteral("chat"));
	typeSelector->addItem(QStringLiteral("STORM HORIZON RADIO"), QStringLiteral("radio"));
	typeSelector->addItem(QStringLiteral("SIGNAL PLATE"), QStringLiteral("plate"));
	typeSelector->addItem(QStringLiteral("NOW PLAYING PLATE"), QStringLiteral("media"));
	typeSelector->addItem(QStringLiteral("LORE PANEL"), QStringLiteral("lore"));
	primaryField = new QLineEdit(root);
	secondaryField = new QLineEdit(root);
	browserUrlField = new QLineEdit(root);
	browserUrlField->setToolTip(QStringLiteral(
		"Chat Terminal: paste a Twitch chat URL. Storm Horizon Radio: paste the AzuraCast embed URL. Leave empty for a local standby renderer."));
	accentField = new QLineEdit(root);
	accentField->setPlaceholderText(QStringLiteral("#45d9ff"));
	reactionSelector = new QComboBox(root);
	reactionSelector->addItem(QStringLiteral("SIGNAL PULSE"), QStringLiteral("signal"));
	reactionSelector->addItem(QStringLiteral("GLOW ONLY"), QStringLiteral("glow"));
	reactionSelector->addItem(QStringLiteral("BREATHING CORE"), QStringLiteral("pulse"));
	reactionSelector->addItem(QStringLiteral("PEAK GLITCH"), QStringLiteral("glitch"));
	signalSelector = new QComboBox(root);
	signalSelector->addItem(QStringLiteral("MASTER MIX"), QStringLiteral("master"));
	signalSelector->addItem(QStringLiteral("DESKTOP ENERGY"), QStringLiteral("desktop"));
	signalSelector->addItem(QStringLiteral("MICROPHONE / VOICE"), QStringLiteral("microphone"));
	signalSelector->addItem(QStringLiteral("MUSIC TRANSIENT / BEAT"), QStringLiteral("beat"));
	strengthField = new QDoubleSpinBox(root);
	strengthField->setRange(0.0, 2.5);
	strengthField->setSingleStep(0.1);
	strengthField->setDecimals(1);
	thresholdField = new QDoubleSpinBox(root);
	thresholdField->setRange(0.0, 0.95);
	thresholdField->setSingleStep(0.05);
	thresholdField->setDecimals(2);
	thresholdField->setAccessibleName(QStringLiteral("Reaction activation threshold"));
	attackField = new QDoubleSpinBox(root);
	attackField->setRange(0.05, 1.0);
	attackField->setSingleStep(0.05);
	attackField->setDecimals(2);
	attackField->setAccessibleName(QStringLiteral("Reaction attack speed"));
	decayField = new QDoubleSpinBox(root);
	decayField->setRange(0.50, 0.98);
	decayField->setSingleStep(0.02);
	decayField->setDecimals(2);
	decayField->setAccessibleName(QStringLiteral("Reaction decay speed"));
	idleField = new QDoubleSpinBox(root);
	idleField->setRange(0.0, 0.50);
	idleField->setSingleStep(0.02);
	idleField->setDecimals(2);
	idleField->setAccessibleName(QStringLiteral("Reaction idle energy"));
	form->addRow(QStringLiteral("Element name"), nameField);
	form->addRow(QStringLiteral("Element type"), typeSelector);
	form->addRow(QStringLiteral("Primary text"), primaryField);
	form->addRow(QStringLiteral("Secondary text"), secondaryField);
	form->addRow(QStringLiteral("Browser URL"), browserUrlField);
	form->addRow(QStringLiteral("Accent color"), accentField);
	form->addRow(QStringLiteral("Reaction"), reactionSelector);
	form->addRow(QStringLiteral("Signal input"), signalSelector);
	form->addRow(QStringLiteral("Intensity"), strengthField);
	form->addRow(QStringLiteral("Threshold"), thresholdField);
	form->addRow(QStringLiteral("Attack"), attackField);
	form->addRow(QStringLiteral("Decay"), decayField);
	form->addRow(QStringLiteral("Idle energy"), idleField);
	layout->addLayout(form);
	connect(typeSelector, &QComboBox::currentIndexChanged, this, [this]() { UpdateBrowserUrlAvailability(); });
	connect(reactionSelector, &QComboBox::currentIndexChanged, this, &TempestHUDComposer::UpdateReactionPreview);
	connect(signalSelector, &QComboBox::currentIndexChanged, this, &TempestHUDComposer::UpdateReactionPreview);
	connect(strengthField, &QDoubleSpinBox::valueChanged, this, &TempestHUDComposer::UpdateReactionPreview);
	connect(thresholdField, &QDoubleSpinBox::valueChanged, this, &TempestHUDComposer::UpdateReactionPreview);
	connect(attackField, &QDoubleSpinBox::valueChanged, this, &TempestHUDComposer::UpdateReactionPreview);
	connect(decayField, &QDoubleSpinBox::valueChanged, this, &TempestHUDComposer::UpdateReactionPreview);
	connect(idleField, &QDoubleSpinBox::valueChanged, this, &TempestHUDComposer::UpdateReactionPreview);
	connect(accentField, &QLineEdit::textChanged, this, &TempestHUDComposer::UpdateReactionPreview);
	connect(primaryField, &QLineEdit::textChanged, this, &TempestHUDComposer::UpdateReactionPreview);

	auto *protocolLabel = new QLabel(QStringLiteral("VISIBLE IN PROTOCOL"), root);
	protocolLabel->setObjectName(QStringLiteral("hudSubtitle"));
	layout->addWidget(protocolLabel);
	auto *protocols = new QGridLayout();
	startingVisible = new QCheckBox(QStringLiteral("STARTING"), root);
	liveVisible = new QCheckBox(QStringLiteral("LIVE"), root);
	brbVisible = new QCheckBox(QStringLiteral("BRB"), root);
	endingVisible = new QCheckBox(QStringLiteral("ENDING"), root);
	protocols->addWidget(startingVisible, 0, 0);
	protocols->addWidget(liveVisible, 0, 1);
	protocols->addWidget(brbVisible, 1, 0);
	protocols->addWidget(endingVisible, 1, 1);
	layout->addLayout(protocols);

	saveButton = new QPushButton(QStringLiteral("SAVE / RENDER ELEMENT"), root);
	addButton = new QPushButton(QStringLiteral("ADD SELECTED TO CURRENT SCENE"), root);
	auto *deployButton = new QPushButton(QStringLiteral("DEPLOY ALL HUD ELEMENTS"), root);
	connect(saveButton, &QPushButton::clicked, this, &TempestHUDComposer::SaveElement);
	connect(addButton, &QPushButton::clicked, this, &TempestHUDComposer::AddSelectedToScene);
	connect(deployButton, &QPushButton::clicked, this, &TempestHUDComposer::DeployStarterHud);
	layout->addWidget(saveButton);
	layout->addWidget(addButton);
	layout->addWidget(deployButton);

	auto *hint = new QLabel(
		QStringLiteral(
			"Reaction Lab previews the live Signal Reactor bus. Threshold removes background noise; Attack controls the rise, Decay controls the tail, and Idle Energy keeps breathing effects alive."),
		root);
	hint->setObjectName(QStringLiteral("hudHint"));
	hint->setWordWrap(true);
	layout->addWidget(hint);
	statusLabel = new QLabel(QStringLiteral("HUD LIBRARY INITIALIZING"), root);
	statusLabel->setObjectName(QStringLiteral("hudStatus"));
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	setWidget(root);
}

void TempestHUDComposer::SeedStarterElements()
{
	Element frame;
	frame.id = QStringLiteral("signal-frame");
	frame.name = QStringLiteral("Signal Frame");
	frame.sourceName = SuggestedSourceName(frame.name);
	frame.type = QStringLiteral("frame");
	frame.primary = QStringLiteral("TEMPEST MAINFRAME");
	frame.secondary = QStringLiteral("BROADCAST SIGNAL // ONLINE");
	frame.reaction = QStringLiteral("signal");
	frame.signal = QStringLiteral("master");
	frame.strength = 1.1;

	Element chat;
	chat.id = QStringLiteral("chat-terminal");
	chat.name = QStringLiteral("Chat Terminal");
	chat.sourceName = SuggestedSourceName(chat.name);
	chat.type = QStringLiteral("chat");
	chat.primary = QStringLiteral("CHANNEL RELAY");
	chat.secondary = QStringLiteral("TWITCH LINK // FOUNDATION");
	chat.reaction = QStringLiteral("glow");
	chat.signal = QStringLiteral("microphone");
	chat.strength = 0.8;

	Element transmission;
	transmission.id = QStringLiteral("transmission-plate");
	transmission.name = QStringLiteral("Transmission Plate");
	transmission.sourceName = SuggestedSourceName(transmission.name);
	transmission.primary = QStringLiteral("TEMPEST MAINFRAME");
	transmission.secondary = QStringLiteral("BROADCAST UPLINK // STANDBY");
	transmission.reaction = QStringLiteral("pulse");
	transmission.signal = QStringLiteral("microphone");

	Element media;
	media.id = QStringLiteral("now-playing");
	media.name = QStringLiteral("Now Playing");
	media.sourceName = SuggestedSourceName(media.name);
	media.type = QStringLiteral("media");
	media.primary = QStringLiteral("SIGNAL MEDIA");
	media.secondary = QStringLiteral("ASSET BUS // STANDBY");
	media.reaction = QStringLiteral("signal");
	media.signal = QStringLiteral("desktop");
	media.ending = false;

	Element radio;
	radio.id = QStringLiteral("storm-horizon-radio");
	radio.name = QStringLiteral("Storm Horizon Radio");
	radio.sourceName = SuggestedSourceName(radio.name);
	radio.type = QStringLiteral("radio");
	radio.primary = QStringLiteral("STORM HORIZON RADIO");
	radio.secondary = QStringLiteral("●  LIVE");
	radio.reaction = QStringLiteral("glow");
	radio.signal = QStringLiteral("desktop");
	radio.strength = 0.8;
	radio.starting = false;
	radio.brb = false;
	radio.ending = false;
	elements = {frame, chat, transmission, media, radio};
}

void TempestHUDComposer::LoadElements()
{
	config_t *config = App()->GetUserConfig();
	const bool initialized = config_get_bool(config, ConfigSection, "Initialized");
	const int schemaVersion = (int)config_get_int(config, ConfigSection, "SchemaVersion");
	const char *raw = config_get_string(config, ConfigSection, "Elements");
	const QJsonDocument document = QJsonDocument::fromJson(raw ? QByteArray(raw) : QByteArray());
	if (document.isArray()) {
		for (const QJsonValue &value : document.array()) {
			const QJsonObject object = value.toObject();
			Element element;
			element.id = object.value(QStringLiteral("id")).toString();
			element.name = object.value(QStringLiteral("name")).toString();
			if (element.id.isEmpty() || element.name.isEmpty())
				continue;
			element.sourceName =
				object.value(QStringLiteral("sourceName")).toString(SuggestedSourceName(element.name));
			element.type = object.value(QStringLiteral("type")).toString(QStringLiteral("plate"));
			element.primary = object.value(QStringLiteral("primary")).toString();
			element.secondary = object.value(QStringLiteral("secondary")).toString();
			element.browserUrl = object.value(QStringLiteral("browserUrl")).toString();
			element.accent = object.value(QStringLiteral("accent")).toString(QStringLiteral("#45d9ff"));
			element.reaction = object.value(QStringLiteral("reaction")).toString(QStringLiteral("signal"));
			if (object.contains(QStringLiteral("signal"))) {
				element.signal =
					object.value(QStringLiteral("signal")).toString(QStringLiteral("master"));
			} else if (element.type == QStringLiteral("media") || element.type == QStringLiteral("radio")) {
				element.signal = QStringLiteral("desktop");
			} else if (element.type == QStringLiteral("chat") ||
				   element.id == QStringLiteral("transmission-plate")) {
				element.signal = QStringLiteral("microphone");
			}
			element.strength = object.value(QStringLiteral("strength")).toDouble(1.0);
			element.threshold = object.value(QStringLiteral("threshold")).toDouble(0.08);
			element.attack = object.value(QStringLiteral("attack")).toDouble(0.55);
			element.decay = object.value(QStringLiteral("decay")).toDouble(0.82);
			element.idle = object.value(QStringLiteral("idle")).toDouble(0.08);
			element.starting = object.value(QStringLiteral("starting")).toBool(true);
			element.live = object.value(QStringLiteral("live")).toBool(true);
			element.brb = object.value(QStringLiteral("brb")).toBool(true);
			element.ending = object.value(QStringLiteral("ending")).toBool(true);
			elements.push_back(element);
		}
	}
	if (!initialized) {
		SeedStarterElements();
		config_set_bool(config, ConfigSection, "Initialized", true);
		config_set_int(config, ConfigSection, "SchemaVersion", HudSchemaVersion);
		SaveElements();
	} else if (schemaVersion < HudSchemaVersion) {
		const bool hasRadio = std::any_of(elements.cbegin(), elements.cend(), [](const Element &element) {
			return element.type == QStringLiteral("radio");
		});
		if (!hasRadio) {
			Element radio;
			radio.id = QStringLiteral("storm-horizon-radio");
			radio.name = QStringLiteral("Storm Horizon Radio");
			radio.sourceName = SuggestedSourceName(radio.name);
			radio.type = QStringLiteral("radio");
			radio.primary = QStringLiteral("STORM HORIZON RADIO");
			radio.secondary = QStringLiteral("●  LIVE");
			radio.reaction = QStringLiteral("glow");
			radio.signal = QStringLiteral("desktop");
			radio.strength = 0.8;
			radio.starting = false;
			radio.brb = false;
			radio.ending = false;
			elements.push_back(radio);
		}
		config_set_int(config, ConfigSection, "SchemaVersion", HudSchemaVersion);
		SaveElements();
	}
	SetStatus(QStringLiteral("HUD LIBRARY READY // %1 ELEMENT%2")
			  .arg(elements.size())
			  .arg(elements.size() == 1 ? QString() : QStringLiteral("S")));
}

void TempestHUDComposer::SaveElements()
{
	QJsonArray array;
	for (const Element &element : elements) {
		QJsonObject object;
		object.insert(QStringLiteral("id"), element.id);
		object.insert(QStringLiteral("sourceName"), element.sourceName);
		object.insert(QStringLiteral("name"), element.name);
		object.insert(QStringLiteral("type"), element.type);
		object.insert(QStringLiteral("primary"), element.primary);
		object.insert(QStringLiteral("secondary"), element.secondary);
		object.insert(QStringLiteral("browserUrl"), element.browserUrl);
		object.insert(QStringLiteral("accent"), element.accent);
		object.insert(QStringLiteral("reaction"), element.reaction);
		object.insert(QStringLiteral("signal"), element.signal);
		object.insert(QStringLiteral("strength"), element.strength);
		object.insert(QStringLiteral("threshold"), element.threshold);
		object.insert(QStringLiteral("attack"), element.attack);
		object.insert(QStringLiteral("decay"), element.decay);
		object.insert(QStringLiteral("idle"), element.idle);
		object.insert(QStringLiteral("starting"), element.starting);
		object.insert(QStringLiteral("live"), element.live);
		object.insert(QStringLiteral("brb"), element.brb);
		object.insert(QStringLiteral("ending"), element.ending);
		array.append(object);
	}
	const QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, "Elements", json.constData());
	config_set_bool(config, ConfigSection, "Initialized", true);
	config_set_int(config, ConfigSection, "SchemaVersion", HudSchemaVersion);
	config_save_safe(config, "tmp", nullptr);
}

void TempestHUDComposer::RebuildElementList(const QString &selectedId)
{
	QString wanted = selectedId;
	if (wanted.isEmpty() && SelectedElement())
		wanted = SelectedElement()->id;
	QSignalBlocker blocker(elementList);
	elementList->clear();
	for (int index = 0; index < elements.size(); ++index) {
		const Element &element = elements[index];
		auto *item = new QListWidgetItem(
			QStringLiteral("%1 // %2\n%3").arg(TypeLabel(element.type), element.name, element.sourceName),
			elementList);
		item->setData(Qt::UserRole, index);
		if (element.id == wanted)
			elementList->setCurrentItem(item);
	}
	if (elementList->currentRow() < 0 && elementList->count() > 0)
		elementList->setCurrentRow(0);
	SelectElement();
}

TempestHUDComposer::Element *TempestHUDComposer::SelectedElement()
{
	if (!elementList || !elementList->currentItem())
		return nullptr;
	const int index = elementList->currentItem()->data(Qt::UserRole).toInt();
	return index >= 0 && index < elements.size() ? &elements[index] : nullptr;
}

const TempestHUDComposer::Element *TempestHUDComposer::SelectedElement() const
{
	if (!elementList || !elementList->currentItem())
		return nullptr;
	const int index = elementList->currentItem()->data(Qt::UserRole).toInt();
	return index >= 0 && index < elements.size() ? &elements[index] : nullptr;
}

void TempestHUDComposer::SelectElement()
{
	Element *element = SelectedElement();
	const bool available = element != nullptr;
	saveButton->setEnabled(available);
	addButton->setEnabled(available);
	if (element)
		LoadEditor(*element);
}

void TempestHUDComposer::LoadEditor(const Element &element)
{
	nameField->setText(element.name);
	typeSelector->setCurrentIndex(std::max(0, typeSelector->findData(element.type)));
	primaryField->setText(element.primary);
	secondaryField->setText(element.secondary);
	browserUrlField->setText(element.browserUrl);
	accentField->setText(element.accent);
	reactionSelector->setCurrentIndex(std::max(0, reactionSelector->findData(element.reaction)));
	signalSelector->setCurrentIndex(std::max(0, signalSelector->findData(element.signal)));
	strengthField->setValue(element.strength);
	thresholdField->setValue(element.threshold);
	attackField->setValue(element.attack);
	decayField->setValue(element.decay);
	idleField->setValue(element.idle);
	startingVisible->setChecked(element.starting);
	liveVisible->setChecked(element.live);
	brbVisible->setChecked(element.brb);
	endingVisible->setChecked(element.ending);
	UpdateBrowserUrlAvailability();
	UpdateReactionPreview();
}

void TempestHUDComposer::UpdateReactionPreview()
{
	if (!reactionPreview || !reactionSelector || !signalSelector || !strengthField || !thresholdField ||
	    !attackField || !decayField || !idleField)
		return;
	reactionPreview->SetState(primaryField ? primaryField->text().trimmed() : QString(),
				  accentField ? accentField->text().trimmed() : QStringLiteral("#45d9ff"),
				  reactionSelector->currentData().toString(), signalSelector->currentData().toString(),
				  strengthField->value(), thresholdField->value(), attackField->value(),
				  decayField->value(), idleField->value());
}

void TempestHUDComposer::TestReaction()
{
	UpdateReactionPreview();
	if (reactionPreview)
		reactionPreview->TriggerTest();
}

void TempestHUDComposer::UpdateBrowserUrlAvailability()
{
	if (!browserUrlField || !typeSelector)
		return;
	const QString type = typeSelector->currentData().toString();
	const bool chat = type == QStringLiteral("chat");
	const bool radio = type == QStringLiteral("radio");
	browserUrlField->setEnabled(chat || radio);
	browserUrlField->setPlaceholderText(chat ? QStringLiteral("https://www.twitch.tv/popout/CHANNEL/chat?popout=")
					    : radio ? QStringLiteral("https://radio.example.com/public/station/embed")
						    : QString());
	browserUrlField->setAccessibleDescription(
		chat    ? QStringLiteral("Optional Twitch popout chat or browser-overlay URL")
		: radio ? QStringLiteral("Optional AzuraCast public player or embed URL")
			: QStringLiteral("Available for Chat Terminal and Storm Horizon Radio elements"));
}

bool TempestHUDComposer::StoreEditor(Element &element)
{
	const QString name = nameField->text().trimmed();
	if (name.isEmpty()) {
		SetStatus(QStringLiteral("ELEMENT NAME IS REQUIRED"), true);
		return false;
	}
	const QString accent = accentField->text().trimmed();
	if (!QRegularExpression(QStringLiteral("^#[0-9A-Fa-f]{6}$")).match(accent).hasMatch()) {
		SetStatus(QStringLiteral("ACCENT MUST USE #RRGGBB FORMAT"), true);
		return false;
	}
	const QString browserUrl = browserUrlField->text().trimmed();
	if (!browserUrl.isEmpty()) {
		const QUrl parsedUrl(browserUrl, QUrl::StrictMode);
		if (!parsedUrl.isValid() || parsedUrl.host().isEmpty() ||
		    (parsedUrl.scheme() != QStringLiteral("https") && parsedUrl.scheme() != QStringLiteral("http"))) {
			SetStatus(QStringLiteral("BROWSER URL MUST USE HTTP OR HTTPS"), true);
			return false;
		}
	}
	const QString newSourceName = SuggestedSourceName(name);
	if (newSourceName != element.sourceName) {
		for (const Element &candidate : elements) {
			if (candidate.id != element.id &&
			    candidate.sourceName.compare(newSourceName, Qt::CaseInsensitive) == 0) {
				SetStatus(QStringLiteral("HUD ELEMENT NAME ALREADY EXISTS // %1").arg(name), true);
				return false;
			}
		}
		OBSSourceAutoRelease collision = obs_get_source_by_name(newSourceName.toUtf8().constData());
		if (collision) {
			SetStatus(QStringLiteral("SOURCE NAME ALREADY EXISTS // %1").arg(newSourceName), true);
			return false;
		}
		OBSSourceAutoRelease source = obs_get_source_by_name(element.sourceName.toUtf8().constData());
		if (source)
			obs_source_set_name(source, newSourceName.toUtf8().constData());
		element.sourceName = newSourceName;
	}
	element.name = name;
	element.type = typeSelector->currentData().toString();
	element.primary = primaryField->text().trimmed();
	element.secondary = secondaryField->text().trimmed();
	element.browserUrl = browserUrl;
	element.accent = accent.toUpper();
	element.reaction = reactionSelector->currentData().toString();
	element.signal = signalSelector->currentData().toString();
	element.strength = strengthField->value();
	element.threshold = thresholdField->value();
	element.attack = attackField->value();
	element.decay = decayField->value();
	element.idle = idleField->value();
	element.starting = startingVisible->isChecked();
	element.live = liveVisible->isChecked();
	element.brb = brbVisible->isChecked();
	element.ending = endingVisible->isChecked();
	return true;
}

void TempestHUDComposer::NewElement()
{
	Element element;
	element.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	int suffix = 1;
	do {
		element.name = QStringLiteral("New Signal Plate%1")
				       .arg(suffix == 1 ? QString() : QStringLiteral(" %1").arg(suffix));
		element.sourceName = SuggestedSourceName(element.name);
		++suffix;
	} while (std::any_of(elements.cbegin(), elements.cend(), [&element](const Element &candidate) {
		return candidate.sourceName.compare(element.sourceName, Qt::CaseInsensitive) == 0;
	}));
	element.primary = QStringLiteral("TEMPEST MAINFRAME");
	element.secondary = QStringLiteral("NEW SIGNAL ELEMENT");
	elements.push_back(element);
	SaveElements();
	RebuildElementList(element.id);
	SetStatus(QStringLiteral("NEW ELEMENT DEFINITION CREATED"));
}

void TempestHUDComposer::SaveElement()
{
	Element *element = SelectedElement();
	if (!element || !StoreEditor(*element))
		return;
	const QString selectedId = element->id;
	SaveElements();
	if (!RenderElement(*element))
		return;
	RefreshSelectedSource();
	RebuildElementList(selectedId);
	const bool remoteBrowser =
		(element->type == QStringLiteral("chat") || element->type == QStringLiteral("radio")) &&
		!element->browserUrl.isEmpty();
	SetStatus(remoteBrowser ? QStringLiteral("BROWSER ELEMENT LINKED // %1").arg(element->name.toUpper())
				: QStringLiteral("ELEMENT RENDERED // %1").arg(element->name.toUpper()));
}

bool TempestHUDComposer::EnsureOutputDirectory()
{
	if (!outputDirectory.isEmpty())
		return true;
	char path[1024];
	if (GetAppConfigPath(path, sizeof(path), "tempest-broadcast-system/control-deck") <= 0) {
		SetStatus(QStringLiteral("HUD OUTPUT DIRECTORY COULD NOT BE RESOLVED"), true);
		return false;
	}
	outputDirectory = QString::fromUtf8(path);
	if (!QDir().mkpath(outputDirectory)) {
		SetStatus(QStringLiteral("HUD OUTPUT DIRECTORY COULD NOT BE CREATED"), true);
		return false;
	}
	return true;
}

QString TempestHUDComposer::ElementPath(const Element &element) const
{
	return QDir(outputDirectory).filePath(QStringLiteral("hud-%1.html").arg(SafeFileId(element.id)));
}

bool TempestHUDComposer::RenderElement(const Element &element)
{
	if (!EnsureOutputDirectory())
		return false;
	QSaveFile file(ElementPath(element));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		SetStatus(QStringLiteral("HUD RENDER FAILED // %1").arg(file.errorString()), true);
		return false;
	}
	const QByteArray html = BuildElementHtml(element).toUtf8();
	if (file.write(html) != html.size() || !file.commit()) {
		SetStatus(QStringLiteral("HUD SAVE FAILED // %1").arg(file.errorString()), true);
		return false;
	}
	++renderRevision;
	return true;
}

QString TempestHUDComposer::BuildElementHtml(const Element &element) const
{
	QJsonObject state;
	state.insert(QStringLiteral("type"), element.type);
	state.insert(QStringLiteral("primary"), element.primary);
	state.insert(QStringLiteral("secondary"), element.secondary);
	state.insert(QStringLiteral("accent"), element.accent);
	state.insert(QStringLiteral("reaction"), element.reaction);
	state.insert(QStringLiteral("signal"), element.signal);
	state.insert(QStringLiteral("strength"), element.strength);
	state.insert(QStringLiteral("threshold"), element.threshold);
	state.insert(QStringLiteral("attack"), element.attack);
	state.insert(QStringLiteral("decay"), element.decay);
	state.insert(QStringLiteral("idle"), element.idle);
	QString json = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
	json.replace(QStringLiteral("</"), QStringLiteral("<\\/"));

	QString html = QString::fromUtf8(R"TEMPEST(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--accent:#45d9ff;--ice:#d8fbff;--muted:#7895a8;--panel:rgba(4,14,23,.88);--react:0;--glow:10px;--shift:0px}
*{box-sizing:border-box}html,body{width:100%;height:100%;margin:0;overflow:hidden;background:transparent;color:var(--ice);font-family:"Segoe UI",Arial,sans-serif}body{padding:8px}
.scan{position:absolute;inset:0;pointer-events:none;background:repeating-linear-gradient(0deg,rgba(69,217,255,.025) 0,rgba(69,217,255,.025) 1px,transparent 1px,transparent 5px);opacity:calc(.22 + var(--react)*.45)}
.frame{display:none;position:absolute;inset:20px;border:1px solid color-mix(in srgb,var(--accent) 45%,transparent);box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--accent) 42%,transparent),0 0 var(--glow) color-mix(in srgb,var(--accent) 24%,transparent)}
.frame:before,.frame:after{content:"";position:absolute;left:8%;right:8%;height:2px;background:linear-gradient(90deg,transparent,var(--accent),transparent);transform:translateX(var(--shift))}.frame:before{top:-1px}.frame:after{bottom:-1px;transform:translateX(calc(var(--shift)*-1))}
.corner{position:absolute;width:82px;height:82px;border-color:var(--accent);filter:drop-shadow(0 0 var(--glow) var(--accent))}.c1{left:-4px;top:-4px;border-left:4px solid;border-top:4px solid}.c2{right:-4px;top:-4px;border-right:4px solid;border-top:4px solid}.c3{left:-4px;bottom:-4px;border-left:4px solid;border-bottom:4px solid}.c4{right:-4px;bottom:-4px;border-right:4px solid;border-bottom:4px solid}
.frame-meta{position:absolute;left:110px;top:22px;color:var(--accent);font-size:18px;letter-spacing:.28em;font-weight:700}.frame-state{position:absolute;right:110px;bottom:24px;color:var(--muted);font-size:14px;letter-spacing:.2em}
.plate{position:absolute;inset:8px;display:flex;align-items:center;gap:18px;padding:22px 28px;background:linear-gradient(110deg,var(--panel),rgba(5,28,44,.72));border:1px solid color-mix(in srgb,var(--accent) 58%,transparent);clip-path:polygon(0 0,calc(100% - 28px) 0,100% 28px,100% 100%,28px 100%,0 calc(100% - 28px));box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--accent) 34%,transparent);transform:translateX(var(--shift))}
.core{flex:0 0 54px;width:54px;height:54px;border:2px solid var(--accent);transform:rotate(45deg) scale(calc(.82 + var(--react)*.25));box-shadow:0 0 var(--glow) var(--accent);position:relative}.core:after{content:"";position:absolute;inset:12px;background:var(--accent);opacity:calc(.25 + var(--react)*.65)}
.copy{min-width:0;flex:1}.kicker{color:var(--accent);font-size:12px;letter-spacing:.34em;margin-bottom:8px}.primary{font-size:28px;font-weight:800;letter-spacing:.12em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;text-shadow:0 0 var(--glow) color-mix(in srgb,var(--accent) 60%,transparent)}.secondary{margin-top:8px;color:var(--muted);font-size:13px;letter-spacing:.2em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.meter{height:76px;width:8px;display:flex;align-items:flex-end;background:rgba(69,217,255,.08)}.meter i{display:block;width:100%;height:calc(12% + var(--react)*88%);background:var(--accent);box-shadow:0 0 var(--glow) var(--accent)}
.chat-shell{display:none;position:absolute;inset:8px;background:linear-gradient(145deg,rgba(4,14,23,.93),rgba(4,27,43,.84));border:1px solid color-mix(in srgb,var(--accent) 58%,transparent);clip-path:polygon(0 0,calc(100% - 32px) 0,100% 32px,100% 100%,0 100%);box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--accent) 35%,transparent)}
.chat-head{height:92px;padding:20px 24px;border-bottom:1px solid color-mix(in srgb,var(--accent) 40%,transparent)}.chat-head b{display:block;color:var(--accent);font-size:20px;letter-spacing:.23em}.chat-head span{display:block;margin-top:9px;color:var(--muted);font-size:12px;letter-spacing:.16em}
.chat-body{position:absolute;left:18px;right:18px;top:112px;bottom:62px;display:flex;flex-direction:column;justify-content:flex-end;gap:14px}.msg{padding:13px 15px;border-left:2px solid var(--accent);background:rgba(69,217,255,.045);color:#9bb5c7;font-size:15px;line-height:1.35}.msg b{display:block;color:var(--accent);font-size:10px;letter-spacing:.18em;margin-bottom:5px}.chat-foot{position:absolute;left:22px;right:22px;bottom:20px;color:var(--accent);font-size:10px;letter-spacing:.2em;display:flex;justify-content:space-between}
body.frame-type .frame{display:block}body.frame-type .plate{display:none}body.chat-type .plate{display:none}body.chat-type .chat-shell{display:block}body.media-type .core{border-radius:50%;transform:rotate(0) scale(calc(.82 + var(--react)*.25))}body.media-type .core:after{border-radius:50%}body.lore-type .plate{align-items:flex-start;padding-top:30px}body.lore-type .primary{white-space:normal;font-size:24px}body.lore-type .secondary{white-space:normal;line-height:1.55}
@keyframes breathe{50%{opacity:.58}}body.pulse .core{animation:breathe 1.6s ease-in-out infinite}
</style></head>
<body><div class="scan"></div>
<div class="frame"><i class="corner c1"></i><i class="corner c2"></i><i class="corner c3"></i><i class="corner c4"></i><div class="frame-meta" id="framePrimary"></div><div class="frame-state" id="frameSecondary"></div></div>
<section class="plate"><div class="core"></div><div class="copy"><div class="kicker">TEMPEST MAINFRAME // SIGNAL ELEMENT</div><div class="primary" id="primary"></div><div class="secondary" id="secondary"></div></div><div class="meter"><i></i></div></section>
<section class="chat-shell"><header class="chat-head"><b id="chatPrimary"></b><span id="chatSecondary"></span></header><main class="chat-body"><div class="msg"><b>MAINFRAME</b>CHAT RELAY FOUNDATION ONLINE</div><div class="msg"><b>CHANNEL LINK</b>ADD A TWITCH POPOUT CHAT OR OVERLAY URL IN HUD COMPOSER</div><div class="msg"><b>OPERATOR NOTE</b>THE CHAT TERMINAL WILL SWITCH TO THE REMOTE BROWSER FEED</div></main><footer class="chat-foot"><span>RELAY STANDBY</span><span>SIGNAL REACTIVE</span></footer></section>
<script id="hud-state" type="application/json">{{STATE_JSON}}</script><script>
const s=JSON.parse(document.getElementById('hud-state').textContent),root=document.documentElement,body=document.body;root.style.setProperty('--accent',s.accent||'#45d9ff');body.classList.add((s.type||'plate')+'-type');body.classList.add(s.reaction||'signal');for(const id of ['primary','framePrimary','chatPrimary'])document.getElementById(id).textContent=s.primary||'TEMPEST MAINFRAME';for(const id of ['secondary','frameSecondary','chatSecondary'])document.getElementById(id).textContent=s.secondary||'SIGNAL ELEMENT ONLINE';let level=0,phase=0;
async function telemetry(){let raw=0;try{const r=await fetch('./telemetry.json?t='+Date.now(),{cache:'no-store'});if(r.ok){const d=await r.json(),channel=s.signal||'master',routed=channel==='desktop'?d.desktop:channel==='microphone'?d.microphone:channel==='beat'?d.beat:(d.master??d.level);raw=Math.max(Number(routed)||0,Number(d.pulse)||0)}}catch(_){}const threshold=Math.min(.95,Math.max(0,Number(s.threshold)||0)),target=Math.min(1.5,Math.max(0,(raw-threshold)/Math.max(.001,1-threshold))),attack=Math.min(1,Math.max(.05,Number(s.attack)||.55)),decay=Math.min(.98,Math.max(.5,Number(s.decay)||.82));if(target>level)level+=(target-level)*attack;else level*=decay;phase+=.12;let react=level*Math.max(0,Number(s.strength)||0),idle=Math.min(.5,Math.max(0,Number(s.idle)||0));if(s.reaction==='pulse')react=Math.max(react,idle+idle*.45*Math.sin(phase));if(s.reaction==='glow')react*=.68;react=Math.min(1.8,Math.max(0,react));root.style.setProperty('--react',react.toFixed(3));root.style.setProperty('--glow',(8+react*42)+'px');const glitch=s.reaction==='glitch'&&react>.62?(Math.random()-.5)*react*9:0;root.style.setProperty('--shift',glitch.toFixed(2)+'px')}telemetry();setInterval(telemetry,60);
</script></body></html>)TEMPEST");
	html.replace(QStringLiteral("{{STATE_JSON}}"), json);
	return html;
}

bool TempestHUDComposer::ApplySourceSettings(obs_source_t *source, const Element &element)
{
	if (!source || !EnsureOutputDirectory())
		return false;
	obs_video_info ovi{};
	QSize size = element.type == QStringLiteral("frame") ? QSize(1920, 1080) : ElementSize(element.type);
	if (element.type == QStringLiteral("frame") && obs_get_video_info(&ovi))
		size = QSize((int)ovi.base_width, (int)ovi.base_height);
	OBSDataAutoRelease settings = obs_data_create();
	const QByteArray path = QDir::toNativeSeparators(ElementPath(element)).toUtf8();
	const bool browserType = element.type == QStringLiteral("chat") || element.type == QStringLiteral("radio");
	const bool remoteBrowser = browserType && !element.browserUrl.trimmed().isEmpty();
	QString browserCss = QStringLiteral("body{background:rgba(0,0,0,0);margin:0;overflow:hidden;}");
	if (remoteBrowser) {
		browserCss =
			element.type == QStringLiteral("radio")
				? RemoteRadioCss(element.primary, element.secondary)
				: RemoteChatCss(element.browserUrl, element.primary, element.secondary, element.accent);
	}
	const QByteArray css = browserCss.append(QStringLiteral("/* hud-revision:%1 */").arg(renderRevision)).toUtf8();
	obs_data_set_bool(settings, "is_local_file", !remoteBrowser);
	if (remoteBrowser)
		obs_data_set_string(settings, "url", element.browserUrl.toUtf8().constData());
	else
		obs_data_set_string(settings, "local_file", path.constData());
	obs_data_set_int(settings, "width", size.width());
	obs_data_set_int(settings, "height", size.height());
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_data_set_bool(settings, "reroute_audio", false);
	obs_data_set_string(settings, "css", css.constData());
	obs_source_update(source, settings);
	return true;
}

bool TempestHUDComposer::EnsureElementInScene(Element &element, obs_scene_t *scene, bool applyDefaultTransform)
{
	if (!scene || !RenderElement(element))
		return false;
	OBSSourceAutoRelease source = obs_get_source_by_name(element.sourceName.toUtf8().constData());
	if (source && strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0) {
		SetStatus(QStringLiteral("NON-BROWSER SOURCE USES HUD NAME // %1").arg(element.sourceName), true);
		return false;
	}
	if (!source) {
		const char *sourceType = obs_get_latest_input_type_id("browser_source");
		if (!sourceType) {
			SetStatus(QStringLiteral("OBS BROWSER SOURCE MODULE IS UNAVAILABLE"), true);
			return false;
		}
		OBSDataAutoRelease settings = obs_data_create();
		source = obs_source_create(sourceType, element.sourceName.toUtf8().constData(), settings, nullptr);
		if (!source) {
			SetStatus(QStringLiteral("HUD SOURCE CREATION FAILED // %1").arg(element.name), true);
			return false;
		}
	}
	ApplySourceSettings(source, element);
	obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, element.sourceName.toUtf8().constData());
	if (!item) {
		item = obs_scene_add(scene, source);
		if (item && applyDefaultTransform)
			ApplyDefaultTransform(item, element);
	}
	return item != nullptr;
}

void TempestHUDComposer::ApplyDefaultTransform(obs_sceneitem_t *item, const Element &element) const
{
	if (!item)
		return;
	obs_video_info ovi{};
	const int canvasWidth = obs_get_video_info(&ovi) ? (int)ovi.base_width : 1920;
	const int canvasHeight = ovi.base_height ? (int)ovi.base_height : 1080;
	const QSize size = element.type == QStringLiteral("frame") ? QSize(canvasWidth, canvasHeight)
								   : ElementSize(element.type);
	vec2 position{};
	if (element.type == QStringLiteral("frame")) {
		position = {0.0f, 0.0f};
		obs_sceneitem_set_locked(item, true);
	} else if (element.type == QStringLiteral("chat")) {
		position = {(float)(canvasWidth - size.width() - 60), 120.0f};
	} else if (element.type == QStringLiteral("media")) {
		position = {60.0f, (float)(canvasHeight - size.height() - 60)};
	} else if (element.type == QStringLiteral("lore")) {
		position = {(float)((canvasWidth - size.width()) / 2), (float)(canvasHeight - size.height() - 90)};
	} else {
		position = {60.0f, 70.0f};
	}
	obs_sceneitem_set_pos(item, &position);
}

void TempestHUDComposer::AddSelectedToScene()
{
	Element *element = SelectedElement();
	OBSScene scene = main ? main->GetCurrentScene() : nullptr;
	if (!element || !scene) {
		SetStatus(QStringLiteral("NO ACTIVE SCENE FOR HUD ELEMENT"), true);
		return;
	}
	if (!StoreEditor(*element))
		return;
	SaveElements();
	if (!EnsureElementInScene(*element, scene, true))
		return;
	main->SaveProject();
	SetStatus(QStringLiteral("HUD ELEMENT LINKED // %1").arg(element->name.toUpper()));
}

void TempestHUDComposer::DeployStarterHud()
{
	OBSScene scene = main ? main->GetCurrentScene() : nullptr;
	if (!scene) {
		SetStatus(QStringLiteral("NO ACTIVE SCENE FOR HUD DEPLOYMENT"), true);
		return;
	}
	int deployed = 0;
	for (Element &element : elements) {
		if (EnsureElementInScene(element, scene, true))
			++deployed;
	}
	main->SaveProject();
	SetStatus(QStringLiteral("HUD DEPLOYED // %1 OF %2 ELEMENTS").arg(deployed).arg(elements.size()),
		  deployed != elements.size());
}

void TempestHUDComposer::RefreshSelectedSource()
{
	Element *element = SelectedElement();
	if (!element)
		return;
	OBSSourceAutoRelease source = obs_get_source_by_name(element->sourceName.toUtf8().constData());
	if (source && strcmp(obs_source_get_unversioned_id(source), "browser_source") == 0)
		ApplySourceSettings(source, *element);
}

bool TempestHUDComposer::VisibleForProtocol(const Element &element, const QString &protocolId) const
{
	if (protocolId == QStringLiteral("starting"))
		return element.starting;
	if (protocolId == QStringLiteral("live"))
		return element.live;
	if (protocolId == QStringLiteral("brb"))
		return element.brb;
	if (protocolId == QStringLiteral("ending"))
		return element.ending;
	return true;
}

void TempestHUDComposer::ApplyProtocolVisibility(obs_source_t *sceneSource, const QString &protocolId)
{
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!scene)
		return;
	for (const Element &element : elements) {
		obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, element.sourceName.toUtf8().constData());
		if (item)
			obs_sceneitem_set_visible(item, VisibleForProtocol(element, protocolId));
	}
}

QString TempestHUDComposer::TypeLabel(const QString &type)
{
	if (type == QStringLiteral("frame"))
		return QStringLiteral("FRAME");
	if (type == QStringLiteral("chat"))
		return QStringLiteral("CHAT");
	if (type == QStringLiteral("radio"))
		return QStringLiteral("RADIO");
	if (type == QStringLiteral("media"))
		return QStringLiteral("MEDIA");
	if (type == QStringLiteral("lore"))
		return QStringLiteral("LORE");
	return QStringLiteral("PLATE");
}

QString TempestHUDComposer::SafeFileId(const QString &id)
{
	QString safe = id;
	safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("-"));
	return safe;
}

QString TempestHUDComposer::SuggestedSourceName(const QString &name)
{
	return QStringLiteral("Tempest HUD // %1").arg(name.trimmed());
}

void TempestHUDComposer::SetStatus(const QString &message, bool error)
{
	statusLabel->setText(message);
	statusLabel->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
					   .arg(error ? QStringLiteral("#ff799c") : QStringLiteral("#45d9ff")));
}
