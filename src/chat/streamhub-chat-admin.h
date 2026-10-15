#pragma once

#include <functional>
#include <QDialog>
#include <QJsonObject>

class QLabel;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QListWidget;
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
    void RefreshRewards();
    void CreateReward();
    void RefreshRedemptions();
    void ResolveRedemption(const QString &status);

    int port_;
    QNetworkAccessManager *network_ = nullptr;
    QLineEdit *moderationUser_ = nullptr;
    QComboBox *moderationAction_ = nullptr;
    QSpinBox *timeoutSeconds_ = nullptr;
    QLabel *moderationStatus_ = nullptr;
    QCheckBox *slowMode_ = nullptr;
    QCheckBox *followerMode_ = nullptr;
    QCheckBox *subscriberMode_ = nullptr;
    QCheckBox *emoteMode_ = nullptr;
    QSpinBox *slowModeSeconds_ = nullptr;
    QLineEdit *rewardTitle_ = nullptr;
    QSpinBox *rewardCost_ = nullptr;
    QLineEdit *rewardPrompt_ = nullptr;
    QComboBox *rewardSelector_ = nullptr;
    QListWidget *redemptionsList_ = nullptr;
    QLabel *rewardStatus_ = nullptr;
};
