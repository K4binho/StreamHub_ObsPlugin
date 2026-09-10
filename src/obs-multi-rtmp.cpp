#include "pch.h"

#include <list>
#include <regex>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <chrono>

#include "push-widget.h"
#include "plugin-support.h"

#include "output-config.h"

#include "streamhub-bundle.h"
#include "streamhub-chat-dock.h"
#include "streamhub-control-dock.h"
#include "streamhub-launcher.h"
#include "streamhub-paths.h"
#include "streamhub-platforms.h"
#include "streamhub-theme-installer.h"
#include "streamhub-native-theme.h"
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QApplication>
#include <QDialog>
#include <QDockWidget>
#include <QFile>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>

#ifdef _WIN32
#include <Windows.h>
#endif

#define ConfigSection "obs-multi-rtmp"

const char *ModuleText(const char *key, const char *fallback);
static void InitializeStreamHubResources();

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

class StreamHubBackdrop : public QWidget
{
public:
    explicit StreamHubBackdrop(QWidget *parent = nullptr)
        : QWidget(parent), background_(":/streamhub-ui/branding/streamhub-background.png"),
          crown_(":/streamhub-ui/branding/k4-crown.png")
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        if (!background_.isNull()) {
            const QPixmap scaled = background_.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                                      Qt::SmoothTransformation);
            painter.setOpacity(0.52);
            painter.drawPixmap((width() - scaled.width()) / 2,
                               (height() - scaled.height()) / 2, scaled);
        }

        if (!crown_.isNull()) {
            const QPixmap crown = crown_.scaled(150, 150, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation);
            painter.setOpacity(0.16);
            painter.drawPixmap(width() - crown.width() - 18, 62, crown);
        }
    }

private:
    QPixmap background_;
    QPixmap crown_;
};

class StreamHubPlatformDialog : public QDialog
{
public:
    explicit StreamHubPlatformDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(tr("Adicionar destino"));
        setMinimumWidth(520);
        auto *layout = new QVBoxLayout(this);
        auto *title = new QLabel(tr("Escolha a plataforma"), this);
        title->setObjectName("presetTitle");
        layout->addWidget(title);
        auto *hint = new QLabel(tr("O destino será criado com nome, ícone, protocolo e servidor conhecidos."), this);
        hint->setObjectName("presetHint");
        hint->setWordWrap(true);
        layout->addWidget(hint);

        auto *grid = new QGridLayout();
        int index = 0;
        for (const auto &preset : StreamHubPlatformPresets()) {
            auto *button = new QPushButton(preset.name, this);
            button->setObjectName("presetButton");
            button->setIcon(QIcon(preset.iconPath));
            button->setIconSize(QSize(28, 28));
            button->setMinimumSize(150, 52);
            connect(button, &QPushButton::clicked, this, [this, id = preset.id]() {
                selected_ = id;
                accept();
            });
            grid->addWidget(button, index / 3, index % 3);
            ++index;
        }
        layout->addLayout(grid);
        auto *cancel = new QPushButton(tr("Cancelar"), this);
        cancel->setObjectName("presetCancel");
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        layout->addWidget(cancel, 0, Qt::AlignRight);

        setStyleSheet(R"(
            QDialog { background:#080c14; color:#f2f7ff; }
            QLabel#presetTitle { font-size:20px; font-weight:700; color:#f2f7ff; }
            QLabel#presetHint { color:#9eb2cb; margin-bottom:8px; }
            QPushButton#presetButton { background:#101a2a; border:1px solid #29496f;
                border-radius:9px; padding:9px 14px; color:#f2f7ff; text-align:left; font-weight:600; }
            QPushButton#presetButton:hover { background:#15233a; border:1px solid #00c8ff; }
            QPushButton#presetCancel { background:#101a2a; border:1px solid #29496f;
                border-radius:7px; padding:7px 18px; color:#9eb2cb; }
        )");
    }

    QString selected() const { return selected_; }

private:
    QString selected_;
};

class StreamHubInlineSettings : public QWidget
{
public:
    StreamHubInlineSettings(const std::string &targetId, PushWidget *pushWidget,
                            std::function<void()> onSaved, std::function<void()> onDelete,
                            std::function<void()> onClose,
                            QWidget *parent = nullptr)
        : QWidget(parent), targetId_(targetId), pushWidget_(pushWidget), onSaved_(std::move(onSaved)),
          onDelete_(std::move(onDelete)), onClose_(std::move(onClose))
    {
        setObjectName("inlineSettings");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(9);

        auto *header = new QHBoxLayout();
        icon_ = new QLabel(this);
        icon_->setFixedSize(26, 26);
        icon_->setAlignment(Qt::AlignCenter);
        header->addWidget(icon_);
        title_ = new QLabel(this);
        title_->setObjectName("inlineTitle");
        title_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        header->addWidget(title_);
        auto *close = new QPushButton(QString::fromUtf8(u8"×"), this);
        close->setObjectName("inlineClose");
        close->setFixedSize(28, 28);
        close->setToolTip(tr("Fechar configurações"));
        connect(close, &QPushButton::clicked, this, [this]() {
            if (onClose_)
                onClose_();
            else
                hide();
        });
        header->addWidget(close);
        layout->addLayout(header);

        auto *form = new QFormLayout();
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(8);
        form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        name_ = new QLineEdit(this);
        server_ = new QLineEdit(this);
        key_ = new QLineEdit(this);
        server_->setEchoMode(QLineEdit::Password);
        key_->setEchoMode(QLineEdit::Password);
        server_->setPlaceholderText(tr("Servidor RTMP da plataforma"));
        key_->setPlaceholderText(tr("Cole a chave de transmissão"));
        form->addRow(tr("Nome"), name_);
        form->addRow(tr("Servidor RTMP"), SecretField(server_, serverVisible_, this));
        form->addRow(tr("Stream key"), SecretField(key_, keyVisible_, this));
        customIcon_ = new QComboBox(this);
        customIcon_->addItem(QIcon(":/streamhub-ui/icons/settings.svg"), tr("RTMP"), "settings");
        customIcon_->addItem(QIcon(":/streamhub-ui/icons/camera.svg"), tr("Câmera"), "camera");
        customIcon_->addItem(QIcon(":/streamhub-ui/icons/twitch.svg"), tr("Twitch"), "twitch");
        customIcon_->addItem(QIcon(":/streamhub-ui/icons/kick.svg"), tr("Kick"), "kick");
        customIcon_->addItem(QIcon(":/streamhub-ui/icons/youtube.svg"), tr("YouTube"), "youtube");
        customIcon_->addItem(QIcon(":/streamhub-ui/icons/tiktok.svg"), tr("TikTok"), "tiktok");
        customColor_ = new QPushButton(tr("Escolher cor"), this);
        connect(customColor_, &QPushButton::clicked, this, [this]() {
            const QColor selected = QColorDialog::getColor(customAccent_, this, tr("Cor do destino RTMP"));
            if (selected.isValid()) {
                customAccent_ = selected;
                UpdateCustomColorButton();
            }
        });
        customIconLabel_ = new QLabel(tr("Ícone personalizado"), this);
        customColorLabel_ = new QLabel(tr("Cor personalizada"), this);
        form->addRow(customIconLabel_, customIcon_);
        form->addRow(customColorLabel_, customColor_);
        layout->addLayout(form);

        syncStart_ = new QCheckBox(tr("Iniciar junto com a transmissão principal do OBS"), this);
        syncStop_ = new QCheckBox(tr("Parar junto com a transmissão principal do OBS"), this);
        layout->addWidget(syncStart_);
        layout->addWidget(syncStop_);

        auto *actions = new QGridLayout();
        auto *advanced = new QPushButton(tr("Configurações avançadas"), this);
        advanced->setObjectName("inlineSecondary");
        connect(advanced, &QPushButton::clicked, this, [this]() {
            if (pushWidget_->ShowEditDlg()) {
                Reload();
                if (onSaved_)
                    onSaved_();
            }
        });
        actions->addWidget(advanced, 0, 0);
        auto *remove = new QPushButton(tr("Excluir destino"), this);
        remove->setObjectName("inlineDanger");
        connect(remove, &QPushButton::clicked, this, [this]() { if (onDelete_) onDelete_(); });
        actions->addWidget(remove, 0, 1);
        auto *save = new QPushButton(tr("Salvar alterações"), this);
        save->setObjectName("inlinePrimary");
        connect(save, &QPushButton::clicked, this, [this]() { Save(); });
        actions->addWidget(save, 1, 0, 1, 2);
        actions->setColumnStretch(0, 1);
        actions->setColumnStretch(1, 1);
        layout->addLayout(actions);

        savedNotice_ = new QLabel(tr("✓ Alterações salvas"), this);
        savedNotice_->setObjectName("savedNotice");
        savedNotice_->setAlignment(Qt::AlignCenter);
        savedNotice_->hide();
        layout->addWidget(savedNotice_);
        saveNoticeTimer_ = new QTimer(this);
        saveNoticeTimer_->setSingleShot(true);
        connect(saveNoticeTimer_, &QTimer::timeout, savedNotice_, &QWidget::hide);
        Reload();
    }

    void Reload()
    {
        auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
        if (!target)
            return;
        const auto &platform = StreamHubPlatformForTarget(*target);
        icon_->setPixmap(QIcon(platform.iconPath).pixmap(24, 24));
        title_->setText(tr("Configurações — %1").arg(platform.name));
        name_->setText(QString::fromStdString(target->name));
        server_->setText(QString::fromStdString(target->serviceParam.value("server", std::string{})));
        key_->setText(QString::fromStdString(target->serviceParam.value("key", std::string{})));
        HideSecret(server_, serverVisible_);
        HideSecret(key_, keyVisible_);
        syncStart_->setChecked(target->syncStart);
        syncStop_->setChecked(target->syncStop);
        const bool custom = QString::fromStdString(target->platform).toLower() == "custom";
        customIconLabel_->setVisible(custom);
        customIcon_->setVisible(custom);
        customColorLabel_->setVisible(custom);
        customColor_->setVisible(custom);
        const int iconIndex = customIcon_->findData(QString::fromStdString(target->customIcon));
        customIcon_->setCurrentIndex(iconIndex >= 0 ? iconIndex : 0);
        customAccent_ = QColor(QString::fromStdString(target->customAccent));
        if (!customAccent_.isValid()) customAccent_ = QColor("#00C8FF");
        UpdateCustomColorButton();
    }

private:
    void Save()
    {
        auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
        if (!target)
            return;
        target->name = name_->text().trimmed().toStdString();
        target->serviceParam["server"] = server_->text().trimmed().toStdString();
        target->serviceParam["key"] = key_->text().trimmed().toStdString();
        target->syncStart = syncStart_->isChecked();
        target->syncStop = syncStop_->isChecked();
        if (QString::fromStdString(target->platform).toLower() == "custom") {
            target->customIcon = customIcon_->currentData().toString().toStdString();
            target->customAccent = customAccent_.name(QColor::HexRgb).toStdString();
        }
        if (target->serviceParam.value("server", std::string{}).empty() ||
            target->serviceParam.value("key", std::string{}).empty()) {
            target->syncStart = false;
            target->syncStop = false;
            syncStart_->setChecked(false);
            syncStop_->setChecked(false);
        }
        SaveMultiOutputConfig();
        pushWidget_->ReloadConfig();
        if (onSaved_)
            onSaved_();
        savedNotice_->show();
        saveNoticeTimer_->start(2800);
    }

    QWidget *SecretField(QLineEdit *field, QPushButton *&visibleButton, QWidget *parent)
    {
        auto *holder = new QWidget(parent);
        holder->setObjectName("secretField");
        holder->setMinimumHeight(42);
        auto *row = new QHBoxLayout(holder);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        row->addWidget(field, 1);

        visibleButton = new QPushButton(tr("Ver"), holder);
        visibleButton->setObjectName("secretAction");
        visibleButton->setToolTip(tr("Mostrar ou ocultar"));
        visibleButton->setMinimumWidth(48);
        visibleButton->setFixedHeight(40);
        connect(visibleButton, &QPushButton::clicked, this, [field, visibleButton]() {
            const bool show = field->echoMode() == QLineEdit::Password;
            field->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
            visibleButton->setText(show ? QObject::tr("Ocultar") : QObject::tr("Ver"));
        });
        row->addWidget(visibleButton);

        auto *copy = new QPushButton(tr("Copiar"), holder);
        copy->setObjectName("secretAction");
        copy->setToolTip(tr("Copiar sem exibir"));
        copy->setMinimumWidth(58);
        copy->setFixedHeight(40);
        connect(copy, &QPushButton::clicked, this, [field]() {
            QApplication::clipboard()->setText(field->text());
        });
        row->addWidget(copy);
        field->setMinimumHeight(40);
        return holder;
    }

    void HideSecret(QLineEdit *field, QPushButton *visibleButton)
    {
        field->setEchoMode(QLineEdit::Password);
        if (visibleButton)
            visibleButton->setText(tr("Ver"));
    }

    void UpdateCustomColorButton()
    {
        if (!customColor_) return;
        customColor_->setStyleSheet(QString("background:%1; color:%2; border:1px solid #f1faff; "
                                            "border-radius:6px; padding:6px 10px; font-weight:700;")
                                        .arg(customAccent_.name(), customAccent_.lightness() > 150 ? "#080d14" : "#f1faff"));
        customColor_->setText(customAccent_.name(QColor::HexRgb).toUpper());
    }

    std::string targetId_;
    PushWidget *pushWidget_;
    std::function<void()> onSaved_;
    std::function<void()> onDelete_;
    std::function<void()> onClose_;
    QLabel *icon_ = nullptr;
    QLabel *title_ = nullptr;
    QLineEdit *name_ = nullptr;
    QLineEdit *server_ = nullptr;
    QLineEdit *key_ = nullptr;
    QPushButton *serverVisible_ = nullptr;
    QPushButton *keyVisible_ = nullptr;
    QLabel *savedNotice_ = nullptr;
    QTimer *saveNoticeTimer_ = nullptr;
    QCheckBox *syncStart_ = nullptr;
    QCheckBox *syncStop_ = nullptr;
    QLabel *customIconLabel_ = nullptr;
    QLabel *customColorLabel_ = nullptr;
    QComboBox *customIcon_ = nullptr;
    QPushButton *customColor_ = nullptr;
    QColor customAccent_ = QColor("#00C8FF");
};


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

        // Reserva espaço real para a borda/sombra do último cartão. Sem
        // essa folga, o QListWidget arredonda a altura e corta a base.
        int totalHeight = frameWidth() * 2 + 28;
        const int itemCount = count();
        for (int i = 0; i < itemCount; ++i) {
            const auto *item = widget->item(i);
            const int itemHeight = item ? item->sizeHint().height() : 0;
            const int rowHeight = (std::max)(itemHeight, widget->sizeHintForRow(i));
            totalHeight += rowHeight > 0 ? rowHeight : 82;
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
        setWindowTitle(ModuleText("Title", "Múltiplas saídas"));
        setObjectName("streamHubOutputs");

        container_ = new StreamHubBackdrop(&scroll_);
        container_->setObjectName("outputsPage");
        layout_ = new QVBoxLayout(container_);
        layout_->setAlignment(Qt::AlignmentFlag::AlignTop);
        layout_->setSizeConstraint(QLayout::SetMinAndMaxSize);
        layout_->setContentsMargins(12, 12, 12, 12);
        layout_->setSpacing(10);

        auto header = new QWidget(container_);
        auto headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        auto headerIcon = new QLabel(header);
        headerIcon->setObjectName("outputsTitleIcon");
        headerIcon->setFixedSize(28, 28);
        headerIcon->setAlignment(Qt::AlignCenter);
        headerIcon->setPixmap(QIcon(":/streamhub-ui/icons/camera.svg").pixmap(24, 24));
        headerLayout->addWidget(headerIcon);
        auto headerLabel = new QLabel(ModuleText("Title", "Múltiplas saídas"), header);
        headerLabel->setObjectName("outputsTitle");
        headerLayout->addWidget(headerLabel);
        headerLayout->addStretch();

        auto addButton = new QPushButton(QString::fromUtf8(u8"＋  ") + ModuleText("Btn.NewTarget", "Adicionar novo destino"), header);
        addButton->setObjectName("addDestination");
        QObject::connect(addButton, &QPushButton::clicked, [this]() {
            StreamHubPlatformDialog chooser(this);
            if (chooser.exec() != QDialog::Accepted || chooser.selected().isEmpty())
                return;
            auto& global = GlobalMultiOutputConfig();
            auto newId = GenerateId(global);
            auto target = std::make_shared<OutputTargetConfig>();
            target->id = newId;
            StreamHubApplyPlatformPreset(*target, chooser.selected());
            global.targets.emplace_back(target);
            auto pushWidget = AddPushWidget(newId);
            SaveConfig();
            ShowSettingsFor(newId, pushWidget);
        });
        headerLayout->addWidget(addButton);
        layout_->addWidget(header);

        // start all, stop all
        auto allBtnContainer = new QWidget(container_);
        allBtnContainer->setMinimumHeight(58);
        auto allBtnLayout = new QHBoxLayout();
        allBtnLayout->setContentsMargins(0, 2, 0, 8);
        allBtnLayout->setSpacing(9);
        auto startAllButton = new QPushButton(ModuleText("Btn.StartAll", "Iniciar tudo"), allBtnContainer);
        startAllButton->setObjectName("startAll");
        startAllButton->setIcon(QIcon(":/streamhub-ui/icons/play.svg"));
        startAllButton->setIconSize(QSize(17, 17));
        startAllButton->setFixedHeight(48);
        allBtnLayout->addWidget(startAllButton);
        auto stopAllButton = new QPushButton(ModuleText("Btn.StopAll", "Parar tudo"), allBtnContainer);
        stopAllButton->setObjectName("stopAll");
        stopAllButton->setIcon(QIcon(":/streamhub-ui/icons/stop.svg"));
        stopAllButton->setIconSize(QSize(15, 15));
        stopAllButton->setFixedHeight(48);
        allBtnLayout->addWidget(stopAllButton);
        allBtnContainer->setLayout(allBtnLayout);
        layout_->addWidget(allBtnContainer);

        aggregateBitrate_ = new QLabel(tr("Banda das saídas: 0,00 Mbps"), container_);
        aggregateBitrate_->setObjectName("aggregateBitrate");
        layout_->addWidget(aggregateBitrate_, 0, Qt::AlignRight);
        aggregateTimer_ = new QTimer(this);
        aggregateTimer_->setInterval(1000);
        QObject::connect(aggregateTimer_, &QTimer::timeout, this, [this]() {
            double totalBps = 0.0;
            int active = 0;
            for (auto *output : GetAllPushWidgets()) {
                if (output->IsRunningForAggregate())
                    ++active;
                totalBps += output->CurrentBitrateBps();
            }

            obs_output_t *mainOutput = obs_frontend_get_streaming_output();
            if (mainOutput && obs_output_active(mainOutput)) {
                ++active;
                const auto now = std::chrono::steady_clock::now();
                const uint64_t bytes = obs_output_get_total_bytes(mainOutput);
                if (mainLastInfoTime_.time_since_epoch().count() != 0) {
                    const double interval = std::chrono::duration_cast<std::chrono::duration<double>>(
                        now - mainLastInfoTime_).count();
                    if (interval > 0.0 && bytes >= mainTotalBytes_)
                        mainCurrentBps_ = (bytes - mainTotalBytes_) * 8.0 / interval;
                }
                mainTotalBytes_ = bytes;
                mainLastInfoTime_ = now;
            } else {
                mainTotalBytes_ = 0;
                mainCurrentBps_ = 0.0;
                mainLastInfoTime_ = {};
            }
            if (mainOutput)
                obs_output_release(mainOutput);
            totalBps += mainCurrentBps_;

            aggregateBitrate_->setText(
                tr("Banda das saídas: %1 Mbps · %2 ativa(s)")
                    .arg(QLocale().toString(totalBps / 1000000.0, 'f', 2))
                    .arg(active));
        });
        aggregateTimer_->start();

        auto *mainCard = new QWidget(container_);
        mainCard->setObjectName("outputCard");
        auto *mainCardLayout = new QGridLayout(mainCard);
        mainCardLayout->setContentsMargins(13, 10, 13, 10);
        mainCardLayout->setHorizontalSpacing(10);
        auto *mainName = new QLabel(mainCard);
        mainName->setObjectName("outputName");
        mainCardLayout->addWidget(mainName, 0, 1);
        auto *mainIcon = new QLabel(mainCard);
        mainIcon->setFixedSize(42, 42);
        mainIcon->setAlignment(Qt::AlignCenter);
        mainCardLayout->addWidget(mainIcon, 0, 0, 2, 1);
        auto *mainStatus = new QLabel(mainCard);
        mainStatus->setObjectName("outputStatus");
        mainCardLayout->addWidget(mainStatus, 1, 1);
        mainCardLayout->setColumnStretch(1, 1);
        auto *mainQuality = new QLabel(tr("OBS"), mainCard);
        mainQuality->setObjectName("outputQuality");
        mainQuality->setAlignment(Qt::AlignCenter);
        mainCardLayout->addWidget(mainQuality, 0, 2, 2, 1);
        auto *mainToggle = new QPushButton(mainCard);
        mainToggle->setObjectName("mainOutputToggle");
        mainToggle->setCheckable(true);
        mainToggle->setFixedSize(66, 34);
        mainCardLayout->addWidget(mainToggle, 0, 3, 2, 1);
        const auto updateMainCard = [mainStatus, mainToggle, mainName, mainIcon]() {
            QString mainServiceName = QObject::tr("Transmissão principal");
            QString mainPlatform = "custom";
            if (obs_service_t *service = obs_frontend_get_streaming_service()) {
                obs_data_t *settings = obs_service_get_settings(service);
                const QString configuredService = QString::fromUtf8(obs_data_get_string(settings, "service")).trimmed();
                if (!configuredService.isEmpty()) mainServiceName = configuredService;
                const QString lowered = mainServiceName.toLower();
                if (lowered.contains("twitch")) mainPlatform = "twitch";
                else if (lowered.contains("youtube")) mainPlatform = "youtube";
                else if (lowered.contains("kick")) mainPlatform = "kick";
                else if (lowered.contains("tiktok")) mainPlatform = "tiktok";
                obs_data_release(settings);
            }
            mainName->setText(QString("%1 · %2").arg(mainServiceName, QObject::tr("Principal")));
            mainIcon->setPixmap(QIcon(mainPlatform == "custom" ? ":/streamhub-ui/icons/camera.svg"
                                                                  : QString(":/streamhub-ui/icons/%1.svg").arg(mainPlatform)).pixmap(38, 38));
            const bool active = obs_frontend_streaming_active();
            mainToggle->blockSignals(true);
            mainToggle->setChecked(active);
            mainToggle->setText(active ? QObject::tr("Parar") : QObject::tr("Iniciar"));
            mainToggle->blockSignals(false);
            mainStatus->setText(active ? QObject::tr("● Ao vivo") : QObject::tr("● Pronta"));
            mainStatus->setProperty("ready", true);
            mainStatus->style()->unpolish(mainStatus); mainStatus->style()->polish(mainStatus);
        };
        QObject::connect(mainToggle, &QPushButton::clicked, this, [](bool checked) {
            if (checked) obs_frontend_streaming_start(); else obs_frontend_streaming_stop();
        });
        auto *mainTimer = new QTimer(mainCard);
        mainTimer->setInterval(500);
        QObject::connect(mainTimer, &QTimer::timeout, mainCard, updateMainCard);
        mainTimer->start();
        updateMainCard();
        layout_->addWidget(mainCard);

        QObject::connect(startAllButton, &QPushButton::clicked, [this]() {
            if (!obs_frontend_streaming_active()) {
                obs_frontend_streaming_start();
                return;
            }

            for (auto x : GetAllPushWidgets()) {
                if (x->IsEnabledForAll())
                    x->StartStreaming();
            }
        });
        QObject::connect(stopAllButton, &QPushButton::clicked, [this]() {
            for (auto x : GetAllPushWidgets())
                x->StopStreaming();
            if (obs_frontend_streaming_active())
                obs_frontend_streaming_stop();
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
        const QString primaryPlatform = PrimaryPlatform();
        if (!primaryPlatform.isEmpty())
            EnsurePlatformTarget(primaryPlatform);
        QTimer::singleShot(1500, this, [this]() {
            const QString delayedPrimaryPlatform = PrimaryPlatform();
            if (!delayedPrimaryPlatform.isEmpty())
                EnsurePlatformTarget(delayedPrimaryPlatform);
        });
        connect(
            outputsContainer_->model(),
            &QAbstractItemModel::rowsMoved,
            this,
            &MultiOutputWidget::OnOutputMoved
        );
        layout_->addWidget(outputsContainer_);

        settingsPanelHost_ = new QWidget(container_);
        settingsPanelHost_->setObjectName("settingsPanelHost");
        settingsPanelLayout_ = new QVBoxLayout(settingsPanelHost_);
        settingsPanelLayout_->setContentsMargins(0, 0, 0, 0);
        settingsPanelHost_->hide();
        layout_->addWidget(settingsPanelHost_);

        QWidget *footer = nullptr;

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
                this);
            label->setObjectName("outputsFooter");
            label->setTextFormat(Qt::RichText);
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
            label->setOpenExternalLinks(true);
            label->setWordWrap(true);
            footer = label;
        }

        scroll_.setWidgetResizable(true);
        scroll_.setWidget(container_);
        scroll_.setObjectName("outputsScroll");
        scroll_.setFrameShape(QFrame::NoFrame);

        setStyleSheet(R"(
            QWidget#streamHubOutputs { background: #080c14; color: #f2f7ff; }
            QWidget#outputsPage { background: transparent; color: #f2f7ff; }
            QScrollArea#outputsScroll, QScrollArea#outputsScroll > QWidget > QWidget { background: transparent; border: none; }
            QLabel#outputsTitleIcon { background:transparent; }
            QLabel#outputsTitle { color: #f2f7ff; font-size: 18px; font-weight: 700; }
            QPushButton#addDestination { background: rgba(16,26,42,235); border: 1px solid #0077ff;
                border-radius: 8px; padding: 8px 14px; color: #f2f7ff; font-weight: 600; }
            QPushButton#addDestination:hover { border-color: #00c8ff; background: #15233a; }
            QPushButton#startAll { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #00c8ff,stop:1 #0077ff);
                border: 1px solid #41dcff; border-radius: 8px; padding:0 10px;
                color:#080c14; font-weight:800; }
            QPushButton#startAll:hover { background:#3bd7ff; border-color:#f2f7ff; }
            QPushButton#stopAll { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #d94155,stop:1 #8f1833);
                border: 1px solid #ff5268; border-radius: 8px; padding:0 10px;
                color:#f2f7ff; font-weight:800; }
            QPushButton#stopAll:hover { background:#ef4a60; border-color:#ff9aaa; }
            QWidget#outputCard { background: rgba(8,18,32,238); border: 1px solid #29496f;
                border-radius: 9px; }
            QLabel#outputName { color: #f2f7ff; font-size: 15px; font-weight: 700; }
            QLabel#outputStatus { color: #9eb2cb; }
            QLabel#outputStatus[ready="true"] { color: #16d86a; }
            QLabel#outputQuality { background:#101a2a; border:1px solid #29496f;
                border-radius:6px; padding:5px 8px; color:#dbe8f8; }
            QLabel#aggregateBitrate { color:#91b3c7; padding:0 4px 2px 4px; font-weight:600; }
            QPushButton#outputEdit { background: #101a2a; border: 1px solid #29496f;
                border-radius: 7px; padding: 6px; color: #dbe4f5; }
            QPushButton#outputEdit:hover { border-color: #00c8ff; background:#15233a; }
            QPushButton#mainOutputToggle { background:#101a2a; border:1px solid #29496f; border-radius:14px; color:#dbe8f8; font-size:10px; font-weight:700; }
            QPushButton#mainOutputToggle:checked { background:#16d86a; border-color:#53fc18; color:#06130b; }
            QWidget#inlineSettings { background:rgba(16,26,42,242); border:1px solid #00c8ff;
                border-radius:10px; color:#f2f7ff; }
            QLabel#inlineTitle { font-size:16px; font-weight:700; color:#f2f7ff; }
            QPushButton#inlineClose { background:transparent; border:none; color:#9eb2cb; font-size:20px; }
            QWidget#secretField { background:transparent; border:none; }
            QPushButton#secretAction { background:#101a2a; border:1px solid #29496f; border-radius:6px;
                padding:3px 7px; color:#dbe8f8; }
            QPushButton#secretAction:hover { border-color:#00c8ff; background:#15233a; }
            QLineEdit { background:#080c14; border:1px solid #29496f; border-radius:7px;
                min-height:30px; padding:3px 8px; color:#f2f7ff; }
            QLineEdit:focus { border-color:#00c8ff; }
            QPushButton#inlinePrimary { background:#0077ff; border:1px solid #00c8ff; border-radius:7px;
                padding:7px 13px; color:white; font-weight:700; }
            QPushButton#inlineSecondary { background:#101a2a; border:1px solid #29496f; border-radius:7px;
                padding:7px 11px; color:#f2f7ff; }
            QPushButton#inlineDanger { background:#351924; border:1px solid #d94155; border-radius:7px;
                padding:7px 11px; color:#ffb9c2; }
            QLabel#savedNotice { background:rgba(22,216,106,28); border:1px solid #16d86a;
                border-radius:7px; padding:7px 10px; color:#16d86a; font-weight:700; }
            QLabel#outputsFooter { background:rgba(8,12,20,245); border-top:1px solid #29496f;
                padding:7px 12px 9px 12px; color:#f2f7ff; }
        )");

        auto fullLayout = new QGridLayout(this);
        fullLayout->setContentsMargins(0, 0, 0, 0);
        fullLayout->setRowStretch(0, 1);
        fullLayout->setColumnStretch(0, 1);
        fullLayout->addWidget(&scroll_, 0, 0);
        if (footer)
            fullLayout->addWidget(footer, 1, 0);
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

    QString PrimaryPlatform() const
    {
        auto matchPlatform = [](const QString &name) {
            const QString value = name.trimmed().toLower();
            if (value.contains("twitch"))
                return QStringLiteral("twitch");
            if (value.contains("youtube") || value.contains("google"))
                return QStringLiteral("youtube");
            if (value.contains("kick"))
                return QStringLiteral("kick");
            return QString();
        };

        if (obs_service_t *service = obs_frontend_get_streaming_service()) {
            obs_data_t *settings = obs_service_get_settings(service);
            if (settings) {
                const QString serviceName = QString::fromUtf8(
                    obs_data_get_string(settings, "service"));
                const QString matched = matchPlatform(serviceName);
                obs_data_release(settings);
                if (!matched.isEmpty())
                    return matched;
            }
        }

        char *profilePath = obs_frontend_get_current_profile_path();
        if (!profilePath)
            return {};

        const QString servicePath = StreamHubAbsolutePath(
            QString::fromUtf8(profilePath) + "/service.json");
        bfree(profilePath);
        QFile serviceFile(servicePath);
        if (!serviceFile.open(QIODevice::ReadOnly))
            return {};

        const QJsonDocument document = QJsonDocument::fromJson(serviceFile.readAll());
        const QJsonObject settings = document.object().value("settings").toObject();
        return matchPlatform(settings.value("service").toString());
    }

    void SyncPlatformTransmission(const QString &platform, const QJsonObject &transmission)
    {
        const QString server = transmission.value("server").toString().trimmed();
        const QString key = transmission.value("streamKey").toString().trimmed();
        if (server.isEmpty() || key.isEmpty())
            return;

        auto &global = GlobalMultiOutputConfig();
        OutputTargetConfigPtr target;
        for (const auto &candidate : global.targets) {
            if (candidate && QString::fromStdString(candidate->platform).compare(platform, Qt::CaseInsensitive) == 0) {
                target = candidate;
                break;
            }
        }

        bool primaryIsPlatform = false;
        if (obs_service_t *service = obs_frontend_get_streaming_service()) {
            obs_data_t *settings = obs_service_get_settings(service);
            const QString serviceName = QString::fromUtf8(obs_data_get_string(settings, "service"));
            primaryIsPlatform = serviceName.toLower().contains(platform.toLower());
            obs_data_release(settings);
        }
        if (!target && primaryIsPlatform)
            return;

        bool created = false;
        if (!target) {
            target = std::make_shared<OutputTargetConfig>();
            target->id = GenerateId(global);
            StreamHubApplyPlatformPreset(*target, platform);
            global.targets.emplace_back(target);
            created = true;
        }

        target->serviceParam["server"] = server.toStdString();
        target->serviceParam["key"] = key.toStdString();
        SaveConfig();

        PushWidget *pushWidget = nullptr;
        for (int row = 0; row < outputsContainer_->count(); ++row) {
            auto item = outputsContainer_->item(row);
            if (!item || item->data(Qt::UserRole).toString().toStdString() != target->id)
                continue;
            pushWidget = dynamic_cast<PushWidget *>(outputsContainer_->itemWidget(item));
            break;
        }
        if (created)
            pushWidget = AddPushWidget(target->id);
        if (pushWidget)
            pushWidget->ReloadConfig();
        outputsContainer_->doItemsLayout();
    }

    void EnsurePlatformTarget(const QString &platform)
    {
        auto &global = GlobalMultiOutputConfig();
        const auto preset = [&platform]() {
            for (const auto &candidate : StreamHubPlatformPresets()) {
                if (candidate.id.compare(platform, Qt::CaseInsensitive) == 0)
                    return candidate;
            }
            return StreamHubPlatformPreset{};
        }();

        bool primaryIsPlatform = false;
        if (obs_service_t *service = obs_frontend_get_streaming_service()) {
            obs_data_t *settings = obs_service_get_settings(service);
            const QString serviceName = QString::fromUtf8(obs_data_get_string(settings, "service"));
            primaryIsPlatform = serviceName.compare(platform, Qt::CaseInsensitive) == 0 ||
                                serviceName.toLower().contains(platform.toLower());
            obs_data_release(settings);
        }

        if (primaryIsPlatform) {
            std::vector<std::string> automaticDuplicateIds;
            for (const auto &candidate : global.targets) {
                if (!candidate || StreamHubPlatformForTarget(*candidate).id.compare(platform, Qt::CaseInsensitive) != 0)
                    continue;

                const auto server = QString::fromStdString(
                    candidate->serviceParam.value("server", std::string{}));
                const auto key = QString::fromStdString(
                    candidate->serviceParam.value("key", std::string{}));
                const bool automaticIncomplete =
                    !preset.id.isEmpty() &&
                    QString::fromStdString(candidate->name) == preset.name &&
                    server == preset.server && key.isEmpty() &&
                    QString::fromStdString(candidate->customIcon) == "settings" &&
                    QString::fromStdString(candidate->customAccent).compare(preset.accent, Qt::CaseInsensitive) == 0 &&
                    candidate->outputParam.is_object() && candidate->outputParam.empty() &&
                    !candidate->videoConfig.has_value() && !candidate->audioConfig.has_value() &&
                    !candidate->syncStart && !candidate->syncStop;
                if (automaticIncomplete)
                    automaticDuplicateIds.push_back(candidate->id);
            }

            for (const auto &id : automaticDuplicateIds)
                DeletePushWidget(id);
            if (!automaticDuplicateIds.empty()) {
                SaveConfig();
                outputsContainer_->doItemsLayout();
                blog(LOG_INFO, TAG "removed %zu auxiliary %s target(s); platform is OBS primary service",
                     automaticDuplicateIds.size(), platform.toUtf8().constData());
            }
            return;
        }

        OutputTargetConfigPtr target;
        for (const auto &candidate : global.targets) {
            if (candidate && StreamHubPlatformForTarget(*candidate).id.compare(platform, Qt::CaseInsensitive) == 0) {
                target = candidate;
                break;
            }
        }

        if (!target) {
            target = std::make_shared<OutputTargetConfig>();
            target->id = GenerateId(global);
            StreamHubApplyPlatformPreset(*target, platform);
            global.targets.emplace_back(target);
            SaveConfig();
            AddPushWidget(target->id);
            blog(LOG_INFO, TAG "created %s output target after account connection", platform.toUtf8().constData());
        }
        outputsContainer_->doItemsLayout();
    }

    void SyncTwitchTransmission(const QJsonObject &transmission)
    {
        SyncPlatformTransmission("twitch", transmission);
    }

    void SyncKickTransmission(const QJsonObject &transmission)
    {
        SyncPlatformTransmission("kick", transmission);
    }

    void SyncYoutubeTransmission(const QJsonObject &transmission)
    {
        SyncPlatformTransmission("youtube", transmission);
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
    QWidget* settingsPanelHost_ = nullptr;
    QVBoxLayout* settingsPanelLayout_ = nullptr;
    StreamHubInlineSettings* settingsPanel_ = nullptr;
    QLabel *aggregateBitrate_ = nullptr;
    QTimer *aggregateTimer_ = nullptr;
    std::chrono::steady_clock::time_point mainLastInfoTime_{};
    uint64_t mainTotalBytes_ = 0;
    double mainCurrentBps_ = 0.0;
    std::string settingsTargetId_;

    void ShowSettingsFor(const std::string &targetId, PushWidget *pushWidget)
    {
        if (settingsPanel_ && settingsTargetId_ == targetId) {
            settingsPanelHost_->setVisible(!settingsPanelHost_->isVisible());
            return;
        }
        if (settingsPanel_) {
            settingsPanelLayout_->removeWidget(settingsPanel_);
            settingsPanel_->deleteLater();
        }
        settingsTargetId_ = targetId;
        settingsPanel_ = new StreamHubInlineSettings(
            targetId, pushWidget,
            [this, pushWidget]() {
                pushWidget->ReloadConfig();
                outputsContainer_->doItemsLayout();
            },
            [pushWidget]() { pushWidget->GetDeleteButton()->click(); },
            [this]() { settingsPanelHost_->hide(); }, settingsPanelHost_);
        settingsPanelLayout_->addWidget(settingsPanel_);
        settingsPanelHost_->show();
    }

    void DeletePushWidget(const std::string& targetId)
    {
        if (settingsPanel_ && settingsTargetId_ == targetId) {
            settingsPanelLayout_->removeWidget(settingsPanel_);
            settingsPanel_->deleteLater();
            settingsPanel_ = nullptr;
            settingsTargetId_.clear();
            settingsPanelHost_->hide();
        }
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
        QSize cardSize = pushWidget->sizeHint();
        cardSize.setHeight((std::max)(cardSize.height(), 82));
        listItem->setSizeHint(cardSize);
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

        QObject::connect(pushWidget->GetEditButton(), &QPushButton::clicked,
                         [this, targetId, pushWidget]() { ShowSettingsFor(targetId, pushWidget); });

        return pushWidget;
    }
};

OBS_DECLARE_MODULE()

// OBS calls obs_module_set_locale() during obs_open_module(), before
// obs_module_load(). Extract bundled locale first so DLL-only installs have
// translations available when OBS initializes the module.
lookup_t *obs_module_lookup = nullptr;

const char *obs_module_text(const char *val)
{
    const char *out = val;
    text_lookup_getstr(obs_module_lookup, val, &out);
    if (out && std::strcmp(out, val) != 0)
        return out;

    if (std::strcmp(val, "Title") == 0)
        return "Múltiplas saídas";
    if (std::strcmp(val, "Btn.NewTarget") == 0)
        return "Adicionar novo destino";
    if (std::strcmp(val, "Btn.StartAll") == 0)
        return "Iniciar tudo";
    if (std::strcmp(val, "Btn.StopAll") == 0)
        return "Parar tudo";
    if (std::strcmp(val, "Question.Title") == 0)
        return "Pergunta";
    if (std::strcmp(val, "Question.Delete") == 0)
        return "Tem certeza de que deseja excluir?";
    return out ? out : val;
}

const char *ModuleText(const char *key, const char *fallback)
{
    const char *text = obs_module_text(key);
    return text && *text ? text : fallback;
}

static void InitializeStreamHubResources()
{
    static bool initialized = false;
    if (!initialized) {
        Q_INIT_RESOURCE(streamhub_data);
        initialized = true;
    }
}

bool obs_module_get_string(const char *val, const char **out)
{
    return text_lookup_getstr(obs_module_lookup, val, out);
}

void obs_module_set_locale(const char *locale)
{
    // OBS solicita o locale antes de obs_module_load(). Assim, uma instalação
    // apenas com a DLL já encontra os recursos embutidos nesta primeira etapa.
    InitializeStreamHubResources();

    if (obs_module_lookup)
        text_lookup_destroy(obs_module_lookup);

    const char *rawDataPath = obs_get_module_data_path(obs_current_module());
    if (rawDataPath && *rawDataPath) {
        const QString dataPath = StreamHubAbsolutePath(QString::fromUtf8(rawDataPath));
        StreamHub_EnsureBundledData(dataPath);
    }

    obs_module_lookup = obs_module_load_locale(obs_current_module(), "en-US",
                                                locale ? locale : "en-US");
}

void obs_module_free_locale(void)
{
    if (obs_module_lookup)
        text_lookup_destroy(obs_module_lookup);
    obs_module_lookup = nullptr;
}

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
    StreamHubInstallBundledTheme();
    StreamHubInstallNativeThemeHook();

    auto mainwin = (QMainWindow*)obs_frontend_get_main_window();
    if (mainwin == nullptr)
        return false;
    QMetaObject::invokeMethod(mainwin, []() {
        s_service.uiThread_ = QThread::currentThread();
    });

    const auto brandDock = [mainwin](QWidget *content) {
        QTimer::singleShot(0, mainwin, [content]() {
            QWidget *parent = content ? content->parentWidget() : nullptr;
            while (parent && !qobject_cast<QDockWidget *>(parent)) parent = parent->parentWidget();
            if (auto *dockWidget = qobject_cast<QDockWidget *>(parent)) {
                const QIcon k4(":/streamhub-ui/branding/k4-logo.png");
                dockWidget->setWindowIcon(k4);
                if (dockWidget->toggleViewAction()) dockWidget->toggleViewAction()->setIcon(k4);
            }
        });
    };

    InitializeStreamHubResources();
    auto dock = new MultiOutputWidget();
    dock->setObjectName("obs-multi-rtmp-dock");
    const QByteArray outputsDockTitle =
        QString("%1  · K4").arg(QString::fromUtf8(obs_module_text("Title"))).toUtf8();
    if (!obs_frontend_add_dock_by_id("obs-multi-rtmp-dock", outputsDockTitle.constData(), dock))
    {
        delete dock;
        return false;
    }
    brandDock(dock);

    blog(LOG_INFO, TAG "version: %s by SoraYuki https://github.com/sorayuki/obs-multi-rtmp/", PLUGIN_VERSION);

    // --- StreamHub: sobe o servidor de chat + o dock nativo que mostra ele ---

    int chatPort = 605;
    {
        QFile cfgFile(serverDir + "/config.json");
        if (!cfgFile.exists()) {
            // primeira execução: ainda não existe config.json, só o exemplo
            cfgFile.setFileName(serverDir + "/config.example.json");
        }
        if (cfgFile.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(cfgFile.readAll());
            chatPort = doc.object().value("server").toObject().value("port").toInt(605);
        }
    }

    static StreamHubLauncher *s_launcher = new StreamHubLauncher();

    auto *chatDock = new StreamHubChatDock();
    chatDock->setObjectName("streamhub-chat-dock");
    chatDock->SetConfigPath(serverDir + "/config.json");
    if (obs_frontend_add_dock_by_id("streamhub-chat-dock", "StreamHub Chat  · K4", chatDock)) {
        brandDock(chatDock);
        // Conecta o status do launcher (baixando Node, instalando deps,
        // iniciando servidor...) na label da dock, pra o usuário ver
        // progresso em vez de uma dock em branco na primeira execução.
        QObject::connect(s_launcher, &StreamHubLauncher::statusChanged, chatDock, &StreamHubChatDock::SetStatus);
        QObject::connect(chatDock, &StreamHubChatDock::SettingsSaved, s_launcher, &StreamHubLauncher::Restart);
        chatDock->ConnectTo(chatPort);
    } else {
        delete chatDock;
    }

    auto *controlDock = new StreamHubControlDock();
    controlDock->setObjectName("streamhub-control-dock");
    QObject::connect(controlDock, &StreamHubControlDock::twitchConnected,
                     dock, [dock]() { dock->EnsurePlatformTarget("twitch"); });
    QObject::connect(controlDock, &StreamHubControlDock::twitchTransmissionRequested,
                     s_launcher, &StreamHubLauncher::SyncTwitchTransmission);
    QObject::connect(s_launcher, &StreamHubLauncher::twitchTransmissionReady,
                     controlDock, &StreamHubControlDock::SetTwitchTransmission);
    QObject::connect(s_launcher, &StreamHubLauncher::twitchTransmissionFailed,
                     controlDock, &StreamHubControlDock::SetTwitchTransmissionError);
    QObject::connect(controlDock, &StreamHubControlDock::twitchTransmissionReceived,
                     dock, &MultiOutputWidget::SyncTwitchTransmission);
    QObject::connect(controlDock, &StreamHubControlDock::kickTransmissionRequested,
                     s_launcher, &StreamHubLauncher::SyncKickTransmission);
    QObject::connect(s_launcher, &StreamHubLauncher::kickTransmissionReady,
                     controlDock, &StreamHubControlDock::SetKickTransmission);
    QObject::connect(s_launcher, &StreamHubLauncher::kickTransmissionFailed,
                     controlDock, &StreamHubControlDock::SetKickTransmissionError);
    QObject::connect(controlDock, &StreamHubControlDock::kickTransmissionReceived,
                     dock, &MultiOutputWidget::SyncKickTransmission);
    QObject::connect(controlDock, &StreamHubControlDock::youtubeTransmissionRequested,
                     s_launcher, &StreamHubLauncher::SyncYoutubeTransmission);
    QObject::connect(s_launcher, &StreamHubLauncher::youtubeTransmissionReady,
                     controlDock, &StreamHubControlDock::SetYoutubeTransmission);
    QObject::connect(s_launcher, &StreamHubLauncher::youtubeTransmissionFailed,
                     controlDock, &StreamHubControlDock::SetYoutubeTransmissionError);
    QObject::connect(controlDock, &StreamHubControlDock::youtubeTransmissionReceived,
                     dock, &MultiOutputWidget::SyncYoutubeTransmission);
    controlDock->ConnectTo(chatPort);
    if (obs_frontend_add_dock_by_id("streamhub-control-dock", "Informações de transmissão K4", controlDock)) {
        brandDock(controlDock);

        // OBS pode expor serviço principal somente após carregar perfil.
        // Reconsulta evita congelar dock em "serviço não identificado".
        auto *primaryTimer = new QTimer(controlDock);
        primaryTimer->setInterval(500);
        QObject::connect(primaryTimer, &QTimer::timeout, controlDock,
                         [dock, controlDock]() {
                             const QString platform = dock->PrimaryPlatform();
                             if (platform.isEmpty())
                                 return;
                             controlDock->SetPrimaryPlatform(platform);
                             dock->EnsurePlatformTarget(platform);
                         });
        primaryTimer->start();
    } else {
        delete controlDock;
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
