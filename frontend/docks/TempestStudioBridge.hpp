#pragma once

#include "OBSDock.hpp"

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QStringList>

#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class OBSBasic;
class TempestSignalReactor;

class TempestStudioBridge : public OBSDock {
	Q_OBJECT

public:
	explicit TempestStudioBridge(OBSBasic *main, TempestSignalReactor *reactor, QWidget *parent = nullptr);
	~TempestStudioBridge() override;

	bool IsConnected() const;

private:
	struct ClientState;

	void BuildInterface();
	void LoadState();
	void SaveState();
	void ConnectToStudio();
	void DisconnectFromStudio(bool operatorRequested = true);
	void ScheduleReconnect();
	void SetConnectionState(const QString &state, const QString &detail, bool error = false);
	void HandleConnected();
	void HandleDisconnected(const QString &reason);
	void HandleIncoming(const QByteArray &message);
	void HandleCommand(const QJsonObject &message);
	void HandleReactionTrigger(const QJsonObject &message, const QJsonObject &payload);
	void HandleReactionClear(const QJsonObject &message, const QJsonObject &payload);
	void HandleAudioPlay(const QJsonObject &message, const QJsonObject &payload);
	void HandleVisualShow(const QJsonObject &message, const QJsonObject &payload);
	void HandleVisualHide(const QJsonObject &message, const QJsonObject &payload);
	void HandleStatusRequest(const QJsonObject &message);
	void ExpireLease(const QString &leaseKey);
	void RememberCommand(const QString &key);
	void SendDocument(const QJsonObject &document);
	void SendHello();
	void SendHeartbeat();
	void PublishHealth(const QString &reason = QString());
	void PublishEvent(const QString &topic, const QJsonObject &payload);
	void SendResponse(const QJsonObject &command, bool success, const QString &status, const QString &detail,
			  const QJsonObject &extra = {});
	QJsonObject CreateMessage(const QString &kind, const QString &topic = QString(),
				  const QJsonObject &payload = {}, const QString &target = QString(),
				  const QString &correlationId = QString()) const;
	QString ResolveToken(QString *error = nullptr) const;
	QString CommandKey(const QJsonObject &payload) const;
	QString LeaseKey(const QJsonObject &payload) const;
	bool SetCurrentSceneSourceVisible(const QString &sourceName, bool visible, bool restartMedia = false);

	QPointer<OBSBasic> main;
	QPointer<TempestSignalReactor> reactor;
	QPointer<QLineEdit> endpointEdit;
	QPointer<QLineEdit> tokenPathEdit;
	QPointer<QCheckBox> autoConnectCheck;
	QPointer<QPushButton> connectButton;
	QPointer<QLabel> connectionLabel;
	QPointer<QLabel> detailLabel;
	QPointer<QLabel> commandLabel;
	QPointer<QLabel> capabilityLabel;
	QPointer<QTimer> reconnectTimer;
	QPointer<QTimer> heartbeatTimer;
	std::unique_ptr<ClientState> client;
	QHash<QString, QPointer<QTimer>> activeLeases;
	QHash<QString, QString> activeVisualSources;
	QSet<QString> processedCommands;
	QStringList processedCommandOrder;
	QString currentLeaseKey;
	bool operatorDisconnect = false;
	int commandsHandled = 0;
};
