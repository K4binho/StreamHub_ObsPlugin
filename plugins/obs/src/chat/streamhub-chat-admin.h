#pragma once

#include <functional>
#include <QDialog>
#include <QJsonObject>

class QLabel;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QNetworkAccessManager;

class StreamHubChatAdminDialog : public QDialog {
    Q_OBJECT
public:
    explicit StreamHubChatAdminDialog(int port, QWidget *parent = nullptr);

private:
    using ReplyHandler = std::function<void(const QJsonObject &, int)>;
    void Request(const QByteArray &method, const QString &path, const QJsonObject &body, ReplyHandler handler);
    void RunModeration();
    void LoadChatSettings();
    void ApplyChatSettings();
    void UpdateModerationPlatform();

    int port_;
    QNetworkAccessManager *network_ = nullptr;
    QLineEdit *moderationUser_ = nullptr;
    QComboBox *moderationPlatform_ = nullptr;
    QComboBox *moderationAction_ = nullptr;
    QSpinBox *timeoutSeconds_ = nullptr;
    QLabel *durationLabel_ = nullptr;
    QString durationPlatform_ = "twitch";
    QLabel *moderationStatus_ = nullptr;
    QLabel *moderationInfo_ = nullptr;
    QWidget *chatSettingsCard_ = nullptr;
    QCheckBox *slowMode_ = nullptr;
    QCheckBox *followerMode_ = nullptr;
    QCheckBox *subscriberMode_ = nullptr;
    QCheckBox *emoteMode_ = nullptr;
    QSpinBox *slowModeSeconds_ = nullptr;
};
