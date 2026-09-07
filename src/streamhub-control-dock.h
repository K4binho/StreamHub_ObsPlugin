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

private:
    using ReplyHandler = std::function<void(const QJsonObject &, int)>;
    void Request(const QByteArray &method, const QString &path, const QJsonObject &body, ReplyHandler handler);
    void RefreshAccount();
    void StartTwitchLogin();
    void PollTwitchLogin(const QString &flowId, int intervalSeconds);
    void LoadBroadcast();
    void ApplyBroadcast();
    void SearchCategories();
    void UpdatePreview();

    int port_ = 3000;
    QNetworkAccessManager *network_ = nullptr;
    QTimer *loginTimer_ = nullptr;
    QTimer *categoryTimer_ = nullptr;
    QLabel *accountStatus_ = nullptr;
    QLabel *accountName_ = nullptr;
    QLabel *loginHelp_ = nullptr;
    QPushButton *connectButton_ = nullptr;
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
    QLabel *previewTitle_ = nullptr;
    QLabel *previewCategory_ = nullptr;
    QLabel *previewNotification_ = nullptr;
    QLabel *previewCover_ = nullptr;
};
