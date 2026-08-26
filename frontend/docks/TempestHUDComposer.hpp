#pragma once

#include "OBSDock.hpp"

#include <obs.h>

#include <QPointer>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class OBSBasic;
class TempestReactionPreview;

class TempestHUDComposer : public OBSDock {
	Q_OBJECT

public:
	explicit TempestHUDComposer(OBSBasic *main, QWidget *parent = nullptr);
	void ApplyProtocolVisibility(obs_source_t *sceneSource, const QString &protocolId);

private slots:
	void SelectElement();
	void NewElement();
	void SaveElement();
	void AddSelectedToScene();
	void DeployStarterHud();
	void RefreshSelectedSource();
	void UpdateReactionPreview();
	void TestReaction();

private:
	struct Element {
		QString id;
		QString sourceName;
		QString name;
		QString type = QStringLiteral("plate");
		QString primary;
		QString secondary;
		QString browserUrl;
		QString accent = QStringLiteral("#45d9ff");
		QString reaction = QStringLiteral("signal");
		QString signal = QStringLiteral("master");
		double strength = 1.0;
		double threshold = 0.08;
		double attack = 0.55;
		double decay = 0.82;
		double idle = 0.08;
		bool starting = true;
		bool live = true;
		bool brb = true;
		bool ending = true;
	};

	void BuildInterface();
	void LoadElements();
	void SaveElements();
	void SeedStarterElements();
	void RebuildElementList(const QString &selectedId = {});
	void LoadEditor(const Element &element);
	void UpdateBrowserUrlAvailability();
	bool StoreEditor(Element &element);
	bool EnsureOutputDirectory();
	bool RenderElement(const Element &element);
	QString BuildElementHtml(const Element &element) const;
	QString ElementPath(const Element &element) const;
	bool ApplySourceSettings(obs_source_t *source, const Element &element);
	bool EnsureElementInScene(Element &element, obs_scene_t *scene, bool applyDefaultTransform);
	void ApplyDefaultTransform(obs_sceneitem_t *item, const Element &element) const;
	bool VisibleForProtocol(const Element &element, const QString &protocolId) const;
	Element *SelectedElement();
	const Element *SelectedElement() const;
	static QString TypeLabel(const QString &type);
	static QString SafeFileId(const QString &id);
	static QString SuggestedSourceName(const QString &name);
	void SetStatus(const QString &message, bool error = false);

	QPointer<OBSBasic> main;
	QPointer<QListWidget> elementList;
	QPointer<QLineEdit> nameField;
	QPointer<QComboBox> typeSelector;
	QPointer<QLineEdit> primaryField;
	QPointer<QLineEdit> secondaryField;
	QPointer<QLineEdit> browserUrlField;
	QPointer<QLineEdit> accentField;
	QPointer<QComboBox> reactionSelector;
	QPointer<QComboBox> signalSelector;
	QPointer<QDoubleSpinBox> strengthField;
	QPointer<QDoubleSpinBox> thresholdField;
	QPointer<QDoubleSpinBox> attackField;
	QPointer<QDoubleSpinBox> decayField;
	QPointer<QDoubleSpinBox> idleField;
	QPointer<TempestReactionPreview> reactionPreview;
	QPointer<QCheckBox> startingVisible;
	QPointer<QCheckBox> liveVisible;
	QPointer<QCheckBox> brbVisible;
	QPointer<QCheckBox> endingVisible;
	QPointer<QPushButton> saveButton;
	QPointer<QPushButton> addButton;
	QPointer<QLabel> statusLabel;
	QVector<Element> elements;
	QString outputDirectory;
	quint64 renderRevision = 0;
};
