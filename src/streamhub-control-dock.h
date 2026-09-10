#pragma once

#include <functional>
#include <QJsonObject>
#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class QTextEdit;
class QListWidget;
class QPushButton;
class QTimer;
class QNetworkAccessManager;

class StreamHubControlDock : public QWidget {
    Q_OBJECT
public:
    explicit StreamHubControlDock(QWidget *parent = nullptr);
    void ConnectTo(int port);
    void RefreshAccounts();
    void SetPrimaryPlatform(const QString &platform);
    void SetTwitchTransmission(const QJsonObject &transmission);
    void SetTwitchTransmissionError(const QString &error);
    void SetKickTransmission(const QJsonObject &transmission);
    void SetKickTransmissionError(const QString &error);
    void SetYoutubeTransmission(const QJsonObject &transmission);
    void SetYoutubeTransmissionError(const QString &error);

signals:
    void twitchConnected();
    void twitchTransmissionRequested();
    void twitchTransmissionReceived(const QJsonObject &transmission);
    void kickTransmissionRequested();
    void kickTransmissionReceived(const QJsonObject &transmission);
    void youtubeTransmissionRequested();
    void youtubeTransmissionReceived(const QJsonObject &transmission);

private:
    using ReplyHandler = std::function<void(const QJsonObject &, int)>;
    void Request(const QByteArray &method, const QString &path, const QJsonObject &body, ReplyHandler handler);
    void RefreshAccount();
    void StartTwitchLogin();
    void PollTwitchLogin(const QString &flowId, int intervalSeconds);
    void SyncTwitchTransmission();
    void StartKickLogin();
    void PollKickLogin(const QString &flowId);
    void RefreshKickAccount();
    void SyncKickTransmission();
    void StartYoutubeLogin();
    void PollYoutubeLogin(const QString &flowId);
    void RefreshYoutubeAccount();
    void SyncYoutubeTransmission();
    void ApplyTransmissionMetadata(const QJsonObject &transmission, const QString &platform);
    void SetTransmissionSyncNotice(const QString &platform, const QString &message);
    void SetBroadcastOperationStatus(const QString &message);
    void UpdateTransmissionSyncNotice();
    void LoadBroadcast();
    void ApplyBroadcast();
    void SearchCategories();
    void LoadCategoryCover(const QString &categoryId);
    void UpdatePreview();

    int port_ = 605;
    QNetworkAccessManager *network_ = nullptr;
    QTimer *loginTimer_ = nullptr;
    QTimer *kickLoginTimer_ = nullptr;
    QTimer *youtubeLoginTimer_ = nullptr;
    QTimer *categoryTimer_ = nullptr;
    QLabel *accountStatus_ = nullptr;
    QLabel *accountName_ = nullptr;
    QLabel *loginHelp_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QPushButton *twitchSyncButton_ = nullptr;
    QJsonObject twitchTransmission_;
    QLabel *kickAccountStatus_ = nullptr;
    QLabel *kickAccountName_ = nullptr;
    QLabel *kickLoginHelp_ = nullptr;
    QPushButton *kickConnectButton_ = nullptr;
    QPushButton *kickSyncButton_ = nullptr;
    QJsonObject kickTransmission_;
    QLabel *youtubeAccountStatus_ = nullptr;
    QLabel *youtubeAccountName_ = nullptr;
    QLabel *youtubeLoginHelp_ = nullptr;
    QPushButton *youtubeConnectButton_ = nullptr;
    QPushButton *youtubeSyncButton_ = nullptr;
    QJsonObject youtubeTransmission_;
    QTextEdit *title_ = nullptr;
    QLabel *titleCount_ = nullptr;
    QTextEdit *notification_ = nullptr;
    QLabel *notificationCount_ = nullptr;
    QLineEdit *category_ = nullptr;
    QListWidget *categoryResults_ = nullptr;
    QString categoryId_;
    QComboBox *visibility_ = nullptr;
    QLineEdit *tags_ = nullptr;
    QComboBox *language_ = nullptr;
    QComboBox *classification_ = nullptr;
    QLabel *broadcastStatus_ = nullptr;
    QLabel *twitchTransmissionStatus_ = nullptr;
    QLabel *kickTransmissionStatus_ = nullptr;
    QLabel *youtubeTransmissionStatus_ = nullptr;
    QString twitchSyncNotice_;
    QString kickSyncNotice_;
    QString youtubeSyncNotice_;
    QString broadcastOperationStatus_;
    QLabel *previewTitle_ = nullptr;
    QLabel *previewCategory_ = nullptr;
    QLabel *previewNotification_ = nullptr;
    QLabel *previewCover_ = nullptr;
    QString primaryPlatform_;
};
