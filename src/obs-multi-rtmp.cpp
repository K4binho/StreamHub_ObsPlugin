#include "pch.h"

#include <list>
#include <regex>
#include <filesystem>
#include <unordered_map>

#include "push-widget.h"
#include "plugin-support.h"

#include "output-config.h"

#include "streamhub-bundle.h"
#include "streamhub-chat-dock.h"
#include "streamhub-launcher.h"
#include "streamhub-paths.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#ifdef _WIN32
#include <Windows.h>
#endif

#define ConfigSection "obs-multi-rtmp"

static class GlobalServiceImpl : public GlobalService
{
public:
    bool RunInUIThread(std::function<void()> task) override {
        if (uiThread_ == nullptr)
            return false;
        QMetaObject::invokeMethod(uiThread_, [func = std::move(task)]() {
            func();
        });
        return true;
    }

    QThread* uiThread_ = nullptr;
} s_service;


GlobalService& GetGlobalService() {
    return s_service;
}


class OutputsListWidget : public QListWidget
{
public:
    using QListWidget::QListWidget;

    QSize sizeHint() const override
    {
        QSize hint = QListWidget::sizeHint();
        hint.setHeight(ContentHeight());
        return hint;
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    bool event(QEvent *event) override
    {
        const bool handled = QListWidget::event(event);

        switch (event->type()) {
        case QEvent::FontChange:
        case QEvent::LayoutRequest:
        case QEvent::PolishRequest:
        case QEvent::Show:
        case QEvent::StyleChange:
            updateGeometry();
            break;
        default:
            break;
        }

        return handled;
    }

private:
    int ContentHeight() const
    {
        auto *widget = const_cast<OutputsListWidget *>(this);
        widget->doItemsLayout();

        int totalHeight = frameWidth() * 2;
        const int itemCount = count();
        for (int i = 0; i < itemCount; ++i) {
            totalHeight += sizeHintForRow(i);
        }

        if (itemCount > 1) {
            totalHeight += (itemCount - 1) * spacing();
        }

        return (std::max)(totalHeight, frameWidth() * 2);
    }
};


class MultiOutputWidget : public QWidget
{
public:
    MultiOutputWidget(QWidget* parent = 0)
        : QWidget(parent)
    {
        setWindowTitle(obs_module_text("Title"));
        setObjectName("streamHubOutputs");

        container_ = new QWidget(&scroll_);
        container_->setObjectName("outputsPage");
        layout_ = new QVBoxLayout(container_);
        layout_->setAlignment(Qt::AlignmentFlag::AlignTop);
        layout_->setSizeConstraint(QLayout::SetMinAndMaxSize);
        layout_->setContentsMargins(12, 12, 12, 12);
        layout_->setSpacing(10);

        auto header = new QWidget(container_);
        auto headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        auto headerLabel = new QLabel(QString::fromUtf8(u8"◉  ") + obs_module_text("Title"), header);
        headerLabel->setObjectName("outputsTitle");
        headerLayout->addWidget(headerLabel);
        headerLayout->addStretch();

        auto addButton = new QPushButton(QString::fromUtf8(u8"＋  ") + obs_module_text("Btn.NewTarget"), header);
        addButton->setObjectName("addDestination");
        QObject::connect(addButton, &QPushButton::clicked, [this]() {
            auto& global = GlobalMultiOutputConfig();
            auto newId = GenerateId(global);
            auto target = std::make_shared<OutputTargetConfig>();
            target->id = newId;
            global.targets.emplace_back(target);
            auto pushWidget = AddPushWidget(newId);
            if (pushWidget->ShowEditDlg()) {
                SaveConfig();
            } else {
                DeletePushWidget(newId);
            }
        });
        headerLayout->addWidget(addButton);
        layout_->addWidget(header);

        // start all, stop all
        auto allBtnContainer = new QWidget(container_);
        auto allBtnLayout = new QHBoxLayout();
        allBtnLayout->setContentsMargins(0, 0, 0, 0);
        allBtnLayout->setSpacing(9);
        auto startAllButton = new QPushButton(QString::fromUtf8(u8"▶  ") + obs_module_text("Btn.StartAll"), allBtnContainer);
        startAllButton->setObjectName("startAll");
        startAllButton->setMinimumHeight(42);
        allBtnLayout->addWidget(startAllButton);
        auto stopAllButton = new QPushButton(QString::fromUtf8(u8"■  ") + obs_module_text("Btn.StopAll"), allBtnContainer);
        stopAllButton->setObjectName("stopAll");
        stopAllButton->setMinimumHeight(42);
        allBtnLayout->addWidget(stopAllButton);
        allBtnContainer->setLayout(allBtnLayout);
        layout_->addWidget(allBtnContainer);

        QObject::connect(startAllButton, &QPushButton::clicked, [this]() {
            for (auto x : GetAllPushWidgets())
                x->StartStreaming();
        });
        QObject::connect(stopAllButton, &QPushButton::clicked, [this]() {
            for (auto x : GetAllPushWidgets())
                x->StopStreaming();
        });
 
        // load and show outputs
        outputsContainer_ = new OutputsListWidget(container_);
        outputsContainer_->setDragDropMode(QAbstractItemView::InternalMove);
        outputsContainer_->setSelectionMode(QAbstractItemView::SingleSelection);
        outputsContainer_->setDropIndicatorShown(true);
        outputsContainer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        outputsContainer_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        outputsContainer_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        outputsContainer_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        outputsContainer_->setSpacing(8);
        outputsContainer_->setStyleSheet(
            "QListWidget {"
            "   padding: 0px;"
            "   margin: 0px;"
            "   border: none;"
            "   background: transparent;"
            "}"
            "QListWidget::item:selected, QListWidget::item:active {"
            "   border: none;"
            "   background: transparent;"
            "}"
            "QListWidget::item:hover, QListWidget::item:hover:selected, QListWidget::item:hover:active {"
            "   border: none;"
            "   background: rgba(127, 127, 127, 0.1);"
            "   cursor: move;"
            "}"
        );
        LoadConfig();
        connect(
            outputsContainer_->model(),
            &QAbstractItemModel::rowsMoved,
            this,
            &MultiOutputWidget::OnOutputMoved
        );
        layout_->addWidget(outputsContainer_);

        // donate
        if (std::string("\xe5\xa4\x9a\xe8\xb7\xaf\xe6\x8e\xa8\xe6\xb5\x81") == obs_module_text("Title"))
        {
            auto cr = new QWidget(container_);
            auto innerLayout = new QGridLayout(cr);
            innerLayout->setAlignment(Qt::AlignmentFlag::AlignLeft);

            auto label = new QLabel(u8"该插件免费提供，\r\n如您是付费取得，可向商家申请退款\r\n免费领红包或投喂支持插件作者。", cr);
            innerLayout->addWidget(label, 0, 0, 1, 2);
            innerLayout->setColumnStretch(0, 4);
            auto label2 = new QLabel(u8"作者：雷鸣", cr);
            innerLayout->addWidget(label2, 1, 0, 1, 1);
            auto btnFeed = new QPushButton(u8"支持", cr);
            innerLayout->addWidget(btnFeed, 1, 1, 1, 1);
            
            QObject::connect(btnFeed, &QPushButton::clicked, [this]() {
                const char redbagpng[] = 
                    "iVBORw0KGgoAAAANSUhEUgAAAJgAAACXAQMAAADTWgC3AAAABlBMVEUAAAD///+l2Z/dAAAAAWJLR0Q"
                    "AiAUdSAAAAAlwSFlzAAAuIwAALiMBeKU/dgAAAWtJREFUSMe1lk2OgzAMhY1YZJkj5CbkYkggcTG4SY"
                    "6QZRaonmcHqs7PYtTaVVWSLxJu7JfnEP/+0H9ZIaKRA0aZz4QJJXuGQFsJO9HU104H1ihuTENl4IS12"
                    "YmVcFSa4unJuE2xZV69mOav5XrX6nRgMi6Ii+3Nr9p4k8m7w5OtmOVbzw8ZrOmbxs0Y/ktENMlfnQnx"
                    "NweG2vB1ZrZCPoyPfmbwXWSPiz1DvIrHwOHgFQsQtTkrmG6McRvqUu4aGbM28Cm1wRM62HtOP2DwkFF"
                    "ypKVoU/dEa8Y9rtaFJLg5EzscoSfBKMWgZ8aY9bj4EQ1jo9GDIR68kKukMCF/6pPWTPW0R9XulVNzpj"
                    "6Z5ZzMpOZrzvRElPC49Awx2LOi3k7aP+akhnL1AEMmPYphvtqeGD032TPt5zB2kQBq5Mgo9hrl7lceT"
                    "MQsEkD80YH1O9xRw9Vzn/cSQ6Y6EK1JH3nVxvss/GCf3L3/YF97Nxv6vuoIAwAAAABJRU5ErkJggg=="
                    ;
                const char alipaypng[] = 
                    "iVBORw0KGgoAAAANSUhEUgAAALsAAAC4AQMAAACByg+HAAAABlBMVEUAAAD///+l2Z/dAAAAAWJLR0Q"
                    "AiAUdSAAAAAlwSFlzAAAuIwAALiMBeKU/dgAAAVtJREFUWMPNmEGugzAMRM0qx+CmIbkpx8gK1zM2/U"
                    "L66w4RogqvC5dhxqbm/6/TfgEuwzr8PNwnjhP7pgYNxR1r97XPhUvxjUsPztj0hkJ7O7c4m/V3gCg36"
                    "r5Q68uA7SHtaC8BlBbP2T4awFNzEaANOtSt4+EPEciEiKJn2eCZJSIQ5XbU5yFt+HNUfIgBNqFo6Grh"
                    "BDxttIQcoL5ICrsTbdAYWgBPIltxK7uVRaeLQToTFRMbb+JoYoAHjsG63UWXtFIQm2w/mV9x9vodUnC"
                    "vCWfuiA+oqwaLH7Al8qL/aS4GA9I6zkz/bbFBSsHVKi++Dftg89YC53DDq8iLTJCSVgcq6DlMZGRkzm"
                    "pBtW2jRRH3flU3UIIacWBLduv0gxzwltGTaUtOFS4HVSJHnBp35jvAsbJnV/RbewWgtLVyAlODkjZSd"
                    "eMrkNfsIwX3awYfuLLEaGKg/OvlAz+wXVruSNSgAAAAAElFTkSuQmCC";
                const char wechatpng[] = 
                    "iVBORw0KGgoAAAANSUhEUgAAAK8AAACtAQMAAAD8lL09AAAABlBMVEUAAAD///+l2Z/dAAAAAWJLR0Q"
                    "AiAUdSAAAAAlwSFlzAAAuIwAALiMBeKU/dgAAAbpJREFUSMe9l0GugzAMRI26yDJHyE3Si1WiEhcrN8"
                    "kRWLJA8Z9xaNUv/eUfUIXgqQtn4pkY87+ubv+Cd8O1T3g2y4unzveqxXf3BniUeLKa3XcxrnZr+32zg"
                    "iXPDvy0S/C04WYo4jpsKCK97FH2K3DovfqzoJzAX9sgwtFVNS/tvH03mwYPs6zb7PjDrf22lAZT6ugq"
                    "wwtaCzLYWLwO13yUUQl3N70yDSTFWOqCJbORsdloLYguxrSNgxyWhl3Z0l2LjWaZUE541qaRFUqM342"
                    "5sDTYdY5EZE1KjHU/z25+0UV3yi/GE4LeubHe0V9QYFZjEOhN70QOmp0JocSdGT82Nh+D7nrcQoHkDO"
                    "GOcmLnhXil3vAsy5nRWhS9SjGkhmMiddHIMO6580oMu5a0xpAC0aOSOHd0mC+jodjNnhyVqDH1Tj3Ts"
                    "0hEm3CGv7dYhDkdQGr0cOJxxquMM02GI44g+tKiqwwZVd4HugjHfOKxeEYvQ9jVOKawzrRnQkQSf4Yz"
                    "EeasiWHhwfYNF8WkIscc985pKCaGSzA9S6eGAmpMvRlFzonBLZ6qFp8nyhxSM/IP+3x2arDwc/kHxnM"
                    "tm62qBBUAAAAASUVORK5CYII=";
                auto donateWnd = new QDialog();
                donateWnd->setWindowTitle(u8"赞助");
                QTabWidget* tab = new QTabWidget(donateWnd);
                auto redbagQr = new QLabel(donateWnd);
                auto redbagQrBmp = QPixmap::fromImage(QImage::fromData(QByteArray::fromBase64(QByteArray::fromRawData(redbagpng, sizeof(redbagpng) - 1)), "png"));
                redbagQr->setPixmap(redbagQrBmp);
                tab->addTab(redbagQr, u8"支付宝领红包");
                auto aliQr = new QLabel(donateWnd);
                auto aliQrBmp = QPixmap::fromImage(QImage::fromData(QByteArray::fromBase64(QByteArray::fromRawData(alipaypng, sizeof(alipaypng) - 1)), "png"));
                aliQr->setPixmap(aliQrBmp);
                tab->addTab(aliQr, u8"支付宝打赏");
                auto weQr = new QLabel(donateWnd);
                auto weQrBmp = QPixmap::fromImage(QImage::fromData(QByteArray::fromBase64(QByteArray::fromRawData(wechatpng, sizeof(wechatpng) - 1)), "png"));
                weQr->setPixmap(weQrBmp);
                tab->addTab(weQr, u8"微信打赏");

                auto layout = new QGridLayout();
                layout->setRowStretch(0, 1);
                layout->setColumnStretch(0, 1);
                layout->addWidget(new QLabel(u8"打赏并非购买，不提供退款。", donateWnd), 0, 0);
                layout->addWidget(tab, 1, 0);
                donateWnd->setLayout(layout);
                donateWnd->setMinimumWidth(360);
                donateWnd->exec();
            });

            layout_->addWidget(cr);
        }
        else
        {
            auto label = new QLabel(
                u8"<p><b>Este plugin é fornecido gratuitamente.</b><br>"
                u8"Projeto original: SoraYuki — <a href=\"https://paypal.me/sorayuki0\">doar via PayPal</a><br>"
                u8"Melhorias StreamHub: K4binho — <a href=\"https://livepix.gg/k4binho\">apoiar via LivePix</a></p>",
                container_);
            label->setTextFormat(Qt::RichText);
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
            label->setOpenExternalLinks(true);
            label->setWordWrap(true);
            layout_->addWidget(label);
        }

        scroll_.setWidgetResizable(true);
        scroll_.setWidget(container_);
        scroll_.setObjectName("outputsScroll");
        scroll_.setFrameShape(QFrame::NoFrame);

        setStyleSheet(R"(
            QWidget#streamHubOutputs, QWidget#outputsPage { background: #101622; color: #edf1fb; }
            QScrollArea#outputsScroll { background: #101622; border: none; }
            QLabel#outputsTitle { color: #f5f2ff; font-size: 17px; font-weight: 700; }
            QPushButton#addDestination { background: #1a2436; border: 1px solid #3b4a66;
                border-radius: 8px; padding: 8px 14px; color: #edf1fb; font-weight: 600; }
            QPushButton#addDestination:hover { border-color: #8257ff; background: #243149; }
            QPushButton#startAll { background: #087c43; border: 1px solid #21d77c;
                border-radius: 8px; color: white; font-weight: 700; }
            QPushButton#startAll:hover { background: #0a9651; }
            QPushButton#stopAll { background: #50202c; border: 1px solid #dc3656;
                border-radius: 8px; color: #ffdce3; font-weight: 700; }
            QPushButton#stopAll:hover { background: #672637; }
            QWidget#outputCard { background: #151e2e; border: 1px solid #2b3952;
                border-radius: 9px; }
            QLabel#outputName { color: #f3f5fb; font-size: 14px; font-weight: 700; }
            QLabel#outputStatus { color: #8fa0ba; }
            QPushButton#outputStart { background: #263551; border: 1px solid #425474;
                border-radius: 7px; padding: 6px 12px; color: white; font-weight: 600; }
            QPushButton#outputStart:hover { background: #334667; border-color: #8257ff; }
            QPushButton#outputEdit, QPushButton#outputDelete { background: #1c273a;
                border: 1px solid #35455f; border-radius: 7px; padding: 6px 10px; color: #dbe4f5; }
            QPushButton#outputEdit:hover { border-color: #8257ff; }
            QPushButton#outputDelete:hover { border-color: #dc3656; color: #ff9caf; }
        )");

        auto fullLayout = new QGridLayout(this);
        fullLayout->setContentsMargins(0, 0, 0, 0);
        fullLayout->setRowStretch(0, 1);
        fullLayout->setColumnStretch(0, 1);
        fullLayout->addWidget(&scroll_, 0, 0);
    }

    std::list<PushWidget*> GetAllPushWidgets()
    {
        std::list<PushWidget*> result;
        for (int row = 0; row < outputsContainer_->count(); ++row) {
            auto item = outputsContainer_->item(row);
            if (!item) {
                continue;
            }

            auto widget = outputsContainer_->itemWidget(item);
            auto pushWidget = dynamic_cast<PushWidget*>(widget);
            if (pushWidget) {
                result.push_back(pushWidget);
            }
        }
        return result;
    }

    void SaveConfig()
    {
        SaveMultiOutputConfig();
    }

    void OnOutputMoved(
        const QModelIndex &parent,
        int start,
        int end,
        const QModelIndex &destination,
        int row
    )
    {
        // QListWidget uses a single root parent for internal move operations.
        if (parent != destination) {
            return;
        }

        const int count = outputsContainer_->count();
        if (count <= 0 || start < 0 || end < start || row < 0 || row > count) {
            return;
        }

        auto &targets = GlobalMultiOutputConfig().targets;
        std::unordered_map<std::string, OutputTargetConfigPtr> targetById;
        targetById.reserve(targets.size());
        for (auto &target : targets) {
            if (target) {
                targetById.emplace(target->id, target);
            }
        }

        std::remove_reference_t<decltype(targets)> reordered;
        for (int i = 0; i < count; ++i) {
            auto item = outputsContainer_->item(i);
            if (!item) {
                continue;
            }

            auto id = item->data(Qt::UserRole).toString().toStdString();
            auto it = targetById.find(id);
            if (it == targetById.end()) {
                continue;
            }

            reordered.emplace_back(it->second);
            targetById.erase(it);
        }

        // Keep unmatched items in their previous order to avoid accidental loss.
        if (!targetById.empty()) {
            for (auto &target : targets) {
                if (!target) {
                    continue;
                }
                auto it = targetById.find(target->id);
                if (it == targetById.end()) {
                    continue;
                }
                reordered.emplace_back(it->second);
                targetById.erase(it);
            }
        }

        targets.swap(reordered);

        SaveConfig();
        outputsContainer_->clearSelection();
    }

    void LoadConfig()
    {
        outputsContainer_->clear();

        GlobalMultiOutputConfig() = {};
        if (!LoadMultiOutputConfig()) {
            return;
        }

        for(auto x: GlobalMultiOutputConfig().targets)
        {
            AddPushWidget(x->id);
        }
    }

private:
    // Main widget of this module's dock
    QWidget* container_ = 0;
    // The layout of the root widget
    QVBoxLayout* layout_ = 0;
    // Scrollable area in case of overflows of content
    QScrollArea scroll_;
    // Widget, that contains output source widgets
    QListWidget* outputsContainer_ = 0;

    void DeletePushWidget(const std::string& targetId)
    {
        // Delete from model
        auto outputTargets = &(GlobalMultiOutputConfig().targets);
        auto currentTarget = std::find_if(outputTargets->begin(), outputTargets->end(), [&targetId](auto& x) { return x->id == targetId; });
        if (currentTarget == outputTargets->end()) {
            return;
        }
        outputTargets->erase(currentTarget);

        // Delete from List View
        const QString id = QString::fromStdString(targetId);
        for(auto listItem: outputsContainer_->findItems("", Qt::MatchContains)) {
            if (listItem->data(Qt::UserRole).toString() != id) {
                continue;
            }
            int row = outputsContainer_->row(listItem);
            auto removedItem = outputsContainer_->takeItem(row);
            auto pushWidget = outputsContainer_->itemWidget(removedItem);
            delete removedItem;
            if (pushWidget) {
                pushWidget->deleteLater();
            }
        }
    }

    PushWidget* AddPushWidget(const std::string& targetId)
    {
        auto pushWidget = createPushWidget(targetId, outputsContainer_->viewport());

        QListWidgetItem* listItem = new QListWidgetItem();
        listItem->setData(Qt::UserRole, QString::fromStdString(targetId));
        listItem->setSizeHint(pushWidget->sizeHint());
        outputsContainer_->addItem(listItem);
        outputsContainer_->setItemWidget(listItem, pushWidget);

        QObject::connect(pushWidget->GetDeleteButton(), &QPushButton::clicked, [this, targetId]() {
            auto msgbox = new QMessageBox(
                QMessageBox::Icon::Question,
                obs_module_text("Question.Title"),
                obs_module_text("Question.Delete"),
                QMessageBox::Yes | QMessageBox::No,
                this
            );
            if (msgbox->exec() != QMessageBox::Yes) {
                return;
            }
            DeletePushWidget(targetId);
            SaveConfig();
        });

        return pushWidget;
    }
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-multi-rtmp", "en-US")
OBS_MODULE_AUTHOR("SoraYuki (@sorayukinoyume); melhorias StreamHub por K4binho")

bool obs_module_load()
{
    // --- StreamHub: recria a pasta data/ a partir do que está embutido na
    // própria DLL (qrc/streamhub-data.qrc). Precisa rodar ANTES do primeiro
    // obs_module_text() logo abaixo, que já lê locale/*.ini do disco — por
    // isso vem antes de qualquer outra coisa em obs_module_load(). É isso
    // que permite copiar só a .dll para uma instalação nova do OBS.
    // OBS paths are relative to its current working directory, not to the
    // plugin DLL. Resolve once before extraction and before Node changes cwd.
    const QString dataPath = StreamHubAbsolutePath(
        QString::fromUtf8(obs_get_module_data_path(obs_current_module())));
    QString serverDir = dataPath + "/streamhub-server";
    StreamHub_EnsureBundledData(dataPath);

    auto mainwin = (QMainWindow*)obs_frontend_get_main_window();
    if (mainwin == nullptr)
        return false;
    QMetaObject::invokeMethod(mainwin, []() {
        s_service.uiThread_ = QThread::currentThread();
    });

    auto dock = new MultiOutputWidget();
    dock->setObjectName("obs-multi-rtmp-dock");
    if (!obs_frontend_add_dock_by_id("obs-multi-rtmp-dock", obs_module_text("Title"), dock))
    {
        delete dock;
        return false;
    }

    blog(LOG_INFO, TAG "version: %s by SoraYuki https://github.com/sorayuki/obs-multi-rtmp/", PLUGIN_VERSION);

    // --- StreamHub: sobe o servidor de chat + o dock nativo que mostra ele ---

    int chatPort = 3000;
    {
        QFile cfgFile(serverDir + "/config.json");
        if (!cfgFile.exists()) {
            // primeira execução: ainda não existe config.json, só o exemplo
            cfgFile.setFileName(serverDir + "/config.example.json");
        }
        if (cfgFile.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(cfgFile.readAll());
            chatPort = doc.object().value("server").toObject().value("port").toInt(3000);
        }
    }

    static StreamHubLauncher *s_launcher = new StreamHubLauncher();

    auto *chatDock = new StreamHubChatDock();
    chatDock->setObjectName("streamhub-chat-dock");
    chatDock->SetConfigPath(serverDir + "/config.json");
    if (obs_frontend_add_dock_by_id("streamhub-chat-dock", "StreamHub Chat", chatDock)) {
        // Conecta o status do launcher (baixando Node, instalando deps,
        // iniciando servidor...) na label da dock, pra o usuário ver
        // progresso em vez de uma dock em branco na primeira execução.
        QObject::connect(s_launcher, &StreamHubLauncher::statusChanged, chatDock, &StreamHubChatDock::SetStatus);
        QObject::connect(chatDock, &StreamHubChatDock::SettingsSaved, s_launcher, &StreamHubLauncher::Restart);
        chatDock->ConnectTo(chatPort);
    } else {
        delete chatDock;
    }

    // dataPath já é a pasta de dados do plugin — usada tanto pro servidor
    // bundled quanto (se precisar) pra guardar o runtime portátil do Node.
    s_launcher->Start(dataPath, serverDir, chatPort);
    // --- fim StreamHub ---

    obs_frontend_add_event_callback(
        [](enum obs_frontend_event event, void *private_data) {
            auto dock = static_cast<MultiOutputWidget*>(private_data);

            for(auto x: dock->GetAllPushWidgets())
                x->OnOBSEvent(event);

            if (event == obs_frontend_event::OBS_FRONTEND_EVENT_EXIT)
            {   
                dock->SaveConfig();
                s_launcher->Stop();
            }
            else if (event == obs_frontend_event::OBS_FRONTEND_EVENT_PROFILE_CHANGED)
            {
                dock->LoadConfig();
            }
        }, dock
    );

    return true;
}

const char *obs_module_description(void)
{
    return "Multiple RTMP Output Plugin";
}
