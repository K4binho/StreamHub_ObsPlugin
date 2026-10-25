#include "pch.h"

#include <list>
#include <regex>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <chrono>

#include "push-widget.h"
#include "output-lifecycle.h"
#include "plugin-support.h"

#include "output-config.h"
#include "streamhub-output-settings.h"
#include "vertical-workspace.h"
#include "vertical-output.h"
#include "obs.hpp"

#include "streamhub-bundle.h"
#include "streamhub-chat-dock.h"
#include "streamhub-control-dock.h"
#include "streamhub-launcher.h"
#include "streamhub-paths.h"
#include "streamhub-platforms.h"
#include "streamhub-theme-installer.h"
#include "streamhub-native-theme.h"
#include "streamhub-updater.h"
#include "../audio/noise-suppressor-registration.h"
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDockWidget>
#include <QFile>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMainWindow>
#include <QListWidget>
#include <QStackedWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QUrl>

#ifdef _WIN32
#include <Windows.h>
#endif

static StreamHubVerticalWorkspace *s_verticalWorkspaceDock = nullptr;

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


class OutputsListWidget : public QListWidget
{
public:
    using QListWidget::QListWidget;

    QSize sizeHint() const override
    {
        QSize hint = QListWidget::sizeHint();
        // A lista é criada antes dos destinos serem carregados. Sem uma
        // largura base, o QDockWidget nasce estreito e aumenta quando os
        // cards aparecem, deslocando todo o layout do OBS.
        hint.setWidth((std::max)(hint.width(), 380));
        hint.setHeight(ContentHeight());
        return hint;
    }

    QSize minimumSizeHint() const override
    {
        // A largura da dock é do usuário; o hint mínimo da lista não deve
        // forçar a área rolável a ficar maior que o viewport.
        return QSize(0, ContentHeight());
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
            totalHeight += rowHeight > 0 ? rowHeight : 68;
        }

        if (itemCount > 1) {
            totalHeight += (itemCount - 1) * spacing();
        }

        return (std::max)(totalHeight, frameWidth() * 2);
    }
};

class MainStreamToggleButton : public QPushButton
{
public:
    explicit MainStreamToggleButton(QWidget *parent = nullptr) : QPushButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(48, 26);
    }

    void SetAccentColor(const QColor &color)
    {
        accent_ = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF track = QRectF(rect()).adjusted(1, 2, -1, -2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isChecked() ? accent_ : QColor("#304257"));
        painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
        const qreal diameter = track.height() - 6;
        const qreal x = isChecked() ? track.right() - diameter - 3 : track.left() + 3;
        painter.setBrush(QColor("#f2f7ff"));
        painter.drawEllipse(QRectF(x, track.top() + 3, diameter, diameter));
    }

private:
    QColor accent_ = QColor("#16d86a");
};

class SummaryMetricIcon : public QWidget
{
public:
    enum class Kind { Bandwidth, Live };
    SummaryMetricIcon(Kind kind, QWidget *parent = nullptr) : QWidget(parent), kind_(kind)
    {
        setFixedSize(28, 28);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#00c8ff"), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        if (kind_ == Kind::Bandwidth) {
            p.drawArc(QRectF(3, 4, 22, 22), 30 * 16, 120 * 16);
            p.drawLine(QPointF(14, 16), QPointF(20, 10));
            p.setBrush(QColor("#00c8ff"));
            p.drawEllipse(QPointF(14, 16), 2.0, 2.0);
        } else {
            p.drawArc(QRectF(2, 3, 24, 24), 315 * 16, 90 * 16);
            p.drawArc(QRectF(2, 3, 24, 24), 135 * 16, 90 * 16);
            p.drawArc(QRectF(7, 8, 14, 14), 315 * 16, 90 * 16);
            p.drawArc(QRectF(7, 8, 14, 14), 135 * 16, 90 * 16);
            p.setBrush(QColor("#00c8ff"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(14, 15), 2.0, 2.0);
        }
    }

private:
    Kind kind_;
};

class FooterHeartIcon : public QWidget
{
public:
    explicit FooterHeartIcon(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(42, 42);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath heart;
        heart.moveTo(21, 37);
        heart.cubicTo(17, 32, 4, 25, 4, 16);
        heart.cubicTo(4, 7, 15, 4, 21, 12);
        heart.cubicTo(27, 4, 38, 7, 38, 16);
        heart.cubicTo(38, 25, 25, 32, 21, 37);
        QLinearGradient gradient(3, 3, 39, 39);
        gradient.setColorAt(0.0, QColor("#00d8ff"));
        gradient.setColorAt(0.58, QColor("#1688ff"));
        gradient.setColorAt(1.0, QColor("#983cff"));
        p.setPen(QPen(QBrush(gradient), 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(heart);
        QPainterPath accent;
        accent.moveTo(29, 27);
        accent.cubicTo(34, 26, 38, 29, 38, 33);
        accent.cubicTo(38, 36, 35, 38, 32, 38);
        p.drawPath(accent);
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
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        container_ = new StreamHubBackdrop(&scroll_);
        container_->setObjectName("outputsPage");
        layout_ = new QVBoxLayout(container_);
        layout_->setAlignment(Qt::AlignmentFlag::AlignTop);
        // O dock deve conservar o tamanho salvo pelo OBS. SetMinAndMaxSize
        // fazia a geometria acompanhar mudanças no conteúdo (targets/status),
        // reorganizando o layout do usuário de forma inesperada.
        layout_->setSizeConstraint(QLayout::SetDefaultConstraint);
        layout_->setContentsMargins(10, 10, 10, 8);
        layout_->setSpacing(7);

        // Mantém os blocos superiores na mesma coluna interna dos cards da lista.
        auto addCardColumnInset = [this](QWidget *content) {
            auto *inset = new QWidget(container_);
            auto *insetLayout = new QHBoxLayout(inset);
            insetLayout->setContentsMargins(10, 0, 10, 0);
            insetLayout->setSpacing(0);
            insetLayout->addWidget(content);
            return inset;
        };

        auto header = new QWidget(container_);
        auto headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(10, 0, 10, 0);
        auto headerIcon = new QLabel(header);
        headerIcon->setObjectName("outputsTitleIcon");
        headerIcon->setFixedSize(26, 26);
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
        layout_->addWidget(addCardColumnInset(header));

        // start all, stop all
        auto allBtnContainer = new QWidget(container_);
        allBtnContainer->setFixedHeight(42);
        auto allBtnLayout = new QHBoxLayout();
        allBtnLayout->setContentsMargins(0, 0, 0, 0);
        allBtnLayout->setSpacing(7);
        auto startAllButton = new QPushButton(ModuleText("Btn.StartAll", "Iniciar tudo"), allBtnContainer);
        startAllButton->setObjectName("startAll");
        startAllButton->setIcon(QIcon(":/streamhub-ui/icons/play.svg"));
        startAllButton->setIconSize(QSize(17, 17));
        startAllButton->setFixedHeight(36);
        allBtnLayout->addWidget(startAllButton, 1);
        auto stopAllButton = new QPushButton(ModuleText("Btn.StopAll", "Parar tudo"), allBtnContainer);
        stopAllButton->setObjectName("stopAll");
        stopAllButton->setIcon(QIcon(":/streamhub-ui/icons/stop.svg"));
        stopAllButton->setIconSize(QSize(15, 15));
        stopAllButton->setFixedHeight(36);
        allBtnLayout->addWidget(stopAllButton, 1);
        allBtnContainer->setLayout(allBtnLayout);
        layout_->addWidget(addCardColumnInset(allBtnContainer));

        auto *outputSummary = new QWidget(container_);
        outputSummary->setObjectName("outputSummary");
        outputSummary->setFixedHeight(52);
        auto *summaryLayout = new QHBoxLayout(outputSummary);
        summaryLayout->setContentsMargins(10, 5, 10, 5);
        summaryLayout->setSpacing(8);
        auto *bandwidthIcon = new SummaryMetricIcon(SummaryMetricIcon::Kind::Bandwidth, outputSummary);
        summaryLayout->addWidget(bandwidthIcon);
        auto *bandwidthDetails = new QWidget(outputSummary);
        auto *bandwidthLayout = new QVBoxLayout(bandwidthDetails);
        bandwidthLayout->setContentsMargins(0, 0, 0, 0);
        bandwidthLayout->setSpacing(0);
        auto *bandwidthTitle = new QLabel(tr("Banda das saídas"), bandwidthDetails);
        bandwidthTitle->setObjectName("summaryTitle");
        bandwidthLayout->addWidget(bandwidthTitle);
        aggregateBitrate_ = new QLabel(tr("0,00 Mbps"), bandwidthDetails);
        aggregateBitrate_->setObjectName("aggregateBitrate");
        bandwidthLayout->addWidget(aggregateBitrate_);
        summaryLayout->addWidget(bandwidthDetails, 1);
        auto *summaryDivider = new QFrame(outputSummary);
        summaryDivider->setObjectName("summaryDivider");
        summaryDivider->setFixedSize(1, 32);
        summaryLayout->addWidget(summaryDivider);
        auto *liveIcon = new SummaryMetricIcon(SummaryMetricIcon::Kind::Live, outputSummary);
        summaryLayout->addWidget(liveIcon);
        auto *liveDetails = new QWidget(outputSummary);
        auto *liveLayout = new QVBoxLayout(liveDetails);
        liveLayout->setContentsMargins(0, 0, 0, 0);
        liveLayout->setSpacing(0);
        activeOutputs_ = new QLabel(tr("0 ativa(s)"), liveDetails);
        activeOutputs_->setObjectName("activeOutputs");
        liveLayout->addWidget(activeOutputs_);
        auto *liveDescription = new QLabel(tr("Transmissões ativas"), liveDetails);
        liveDescription->setObjectName("summaryTitle");
        liveLayout->addWidget(liveDescription);
        summaryLayout->addWidget(liveDetails, 1);
        layout_->addWidget(addCardColumnInset(outputSummary));
        aggregateTimer_ = new QTimer(this);
        aggregateTimer_->setInterval(1000);
        QObject::connect(aggregateTimer_, &QTimer::timeout, this, [this]() {
            RefreshLinkedOutputStatuses();
            double totalBps = 0.0;
            int active = 0;
            for (auto *output : GetAllPushWidgets()) {
                if (output->IsRunningForAggregate())
                    ++active;
                totalBps += output->CurrentBitrateBps();
            }

            if (frontendReady_) {
                obs_output_t *mainOutput = obs_frontend_get_streaming_output();
                if (mainOutput && obs_output_active(mainOutput) && obs_frontend_streaming_active()) {
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
            } else {
                mainTotalBytes_ = 0;
                mainCurrentBps_ = 0.0;
                mainLastInfoTime_ = {};
            }
            totalBps += mainCurrentBps_;

            aggregateBitrate_->setText(
                tr("%1 Mbps").arg(QLocale().toString(totalBps / 1000000.0, 'f', 2)));
            activeOutputs_->setText(tr("%1 ativa(s)").arg(active));
        });
        aggregateTimer_->start();

        auto *mainCard = new QWidget(container_);
        mainCard->setObjectName("outputCard");
        auto *mainCardLayout = new QGridLayout(mainCard);
        mainCardLayout->setContentsMargins(10, 7, 10, 7);
        mainCardLayout->setHorizontalSpacing(8);
        auto *mainName = new QLabel(mainCard);
        mainName->setObjectName("outputName");
        mainCardLayout->addWidget(mainName, 0, 1);
        auto *mainIcon = new QLabel(mainCard);
        mainIcon->setFixedSize(36, 36);
        mainIcon->setAlignment(Qt::AlignCenter);
        mainCardLayout->addWidget(mainIcon, 0, 0, 2, 1);
        auto *mainStatus = new QLabel(mainCard);
        mainStatus->setObjectName("outputStatus");
        mainCardLayout->addWidget(mainStatus, 1, 1);
        mainCardLayout->setColumnStretch(1, 1);
        auto *mainToggle = new MainStreamToggleButton(mainCard);
        mainToggle->setObjectName("mainOutputToggle");
        mainToggle->setToolTip(tr("Iniciar ou parar a transmissão principal do OBS"));
        mainToggle->setAccessibleName(tr("Transmissão principal"));
        mainCardLayout->addWidget(mainToggle, 0, 2, 2, 1);
        mainToggle->setCursor(Qt::PointingHandCursor);
        mainToggle->setToolTip(tr("Incluir saída principal em Iniciar tudo (preferência salva)"));
        QObject::connect(mainToggle, &QPushButton::clicked, this, [mainToggle](bool checked) {
            auto &config = GlobalMultiOutputConfig();
            const bool previous = config.mainEnabled;
            config.mainEnabled = checked;
            if (!SaveMultiOutputConfig()) {
                config.mainEnabled = previous;
                mainToggle->setChecked(previous);
                QMessageBox::warning(mainToggle, QObject::tr("Falha ao salvar"), QObject::tr("Não foi possível salvar a seleção da saída principal."));
            }
        });
        auto *mainPlay = new QPushButton(mainCard);
        mainPlay->setObjectName("outputStreamButton");
        mainPlay->setCursor(Qt::PointingHandCursor);
        mainPlay->setFixedSize(32, 30);
        mainPlay->setIconSize(QSize(17, 17));
        mainCardLayout->addWidget(mainPlay, 0, 3, 2, 1);
        QObject::connect(mainPlay, &QPushButton::clicked, this, [this]() {
            if (mainStopping_) return;
            if (obs_frontend_streaming_active() || mainStarting_) {
                StopHorizontalOnly();
            } else {
                StartHorizontalOnly();
            }
        });
        auto *mainSettingsButton = new QPushButton(mainCard);
        mainSettingsButton->setObjectName("outputEdit");
        mainSettingsButton->setFixedSize(32, 30);
        mainSettingsButton->setIcon(QIcon(":/streamhub-ui/icons/settings.svg"));
        mainSettingsButton->setIconSize(QSize(17, 17));
        mainSettingsButton->setToolTip(tr("Configurações de transmissão do OBS"));
        mainSettingsButton->setAccessibleName(tr("Configurações de transmissão do OBS"));
        mainCardLayout->addWidget(mainSettingsButton, 0, 4, 2, 1);
        QObject::connect(mainSettingsButton, &QPushButton::clicked, this, []() {
            auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
            if (!mainWindow)
                return;
            auto *settingsAction = mainWindow->findChild<QAction *>(QStringLiteral("action_Settings"));
            if (!settingsAction)
                return;
            settingsAction->trigger();
            QTimer::singleShot(0, mainWindow, [mainWindow]() {
                QDialog *dialog = mainWindow->findChild<QDialog *>(QStringLiteral("OBSBasicSettings"));
                if (!dialog) {
                    for (QWidget *window : QApplication::topLevelWidgets()) {
                        if (auto *candidate = qobject_cast<QDialog *>(window)) {
                            if (candidate->objectName() == QStringLiteral("OBSBasicSettings")) {
                                dialog = candidate;
                                break;
                            }
                        }
                    }
                }
                if (!dialog)
                    return;
                if (auto *categories = dialog->findChild<QListWidget *>(QStringLiteral("listWidget")))
                    categories->setCurrentRow(2);
                else if (auto *pages = dialog->findChild<QStackedWidget *>(QStringLiteral("settingsPages")))
                    pages->setCurrentIndex(2);
                dialog->raise();
                dialog->activateWindow();
            });
        });
        const auto updateMainCard = [this, mainStatus, mainToggle, mainName, mainIcon, mainCard, mainPlay, mainSettingsButton]() {
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
            QString mainAccent = "#00c8ff";
            if (mainPlatform == "twitch") mainAccent = "#9146ff";
            else if (mainPlatform == "kick") mainAccent = "#53fc18";
            else if (mainPlatform == "youtube") mainAccent = "#ff0033";
            if (mainCard->property("platform").toString() != mainPlatform) {
                mainCard->setProperty("platform", mainPlatform);
                mainCard->setStyleSheet(QString("QWidget#outputCard { border-left: 3px solid %1; }")
                                            .arg(mainAccent));
                mainToggle->SetAccentColor(QColor(mainAccent));
            }
            const bool active = obs_frontend_streaming_active();
            mainToggle->setChecked(GlobalMultiOutputConfig().mainEnabled);
            mainPlay->setIcon(QIcon(active || mainStarting_ || mainStopping_ ? ":/streamhub-ui/icons/stop.svg" : ":/streamhub-ui/icons/play.svg"));
            mainPlay->setEnabled(!mainStopping_);
            mainPlay->setToolTip(mainStopping_ ? tr("Encerrando saída principal...") : active || mainStarting_ ? tr("Parar saída principal") : tr("Iniciar somente a saída principal"));
            mainPlay->setAccessibleName(mainPlay->toolTip());
            mainSettingsButton->setEnabled(!active && !mainStarting_ && !mainStopping_);
            mainStatus->setText(mainStopping_ ? tr("● Encerrando...") : mainStarting_ ? tr("● Conectando...") : active ? tr("● Ao vivo") : tr("● Pronta"));
            mainStatus->setProperty("ready", true);
            mainStatus->style()->unpolish(mainStatus); mainStatus->style()->polish(mainStatus);
        };
        auto *mainTimer = new QTimer(mainCard);
        mainTimer->setInterval(500);
        QObject::connect(mainTimer, &QTimer::timeout, mainCard, updateMainCard);
        mainTimer->start();
        updateMainCard();
        layout_->addWidget(addCardColumnInset(mainCard));

        QObject::connect(startAllButton, &QPushButton::clicked, [this]() {
            horizontalOnlyRequested_ = false;
            horizontalOnlyMode_ = false;
            if (GlobalMultiOutputConfig().mainEnabled && !obs_frontend_streaming_active() && !mainStarting_ && !mainStopping_) {
                mainStarting_ = true;
                obs_frontend_streaming_start();
            }

            for (auto x : GetAllPushWidgets()) {
                if (x->IsEnabledForAll())
                    x->StartStreaming();
            }
        });
        QObject::connect(stopAllButton, &QPushButton::clicked, [this]() {
            horizontalOnlyRequested_ = false;
            horizontalOnlyMode_ = false;
            for (auto x : GetAllPushWidgets())
                x->StopStreaming();
            if (obs_frontend_streaming_active() || mainStarting_)
                obs_frontend_streaming_stop();
        });
 
        // load and show outputs
        outputsContainer_ = new OutputsListWidget(container_);
        outputsContainer_->setDragDropMode(QAbstractItemView::InternalMove);
        outputsContainer_->setSelectionMode(QAbstractItemView::SingleSelection);
        outputsContainer_->setDropIndicatorShown(true);
        outputsContainer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        // O conteúdo deve se adaptar à largura do dock; não o contrário.
        // AdjustToContents fazia a largura depender do momento em que os
        // destinos eram carregados e causava o efeito de abrir estreito e
        // expandir alguns instantes depois.
        outputsContainer_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        outputsContainer_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        outputsContainer_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        outputsContainer_->setSpacing(6);
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
            auto *footerCard = new QWidget(this);
            footerCard->setObjectName("outputsFooter");
            auto *footerLayout = new QHBoxLayout(footerCard);
            footerLayout->setContentsMargins(14, 10, 14, 10);
            footerLayout->setSpacing(12);
            auto *heartIcon = new FooterHeartIcon(footerCard);
            footerLayout->addWidget(heartIcon, 0, Qt::AlignVCenter);
            auto *label = new QLabel(
                u8"<p><b>Este plugin é fornecido gratuitamente.</b><br>"
                u8"Melhorias StreamHub: K4binho — <a href=\"https://livepix.gg/k4binho\">apoiar via LivePix</a></p>",
                footerCard);
            label->setObjectName("outputsFooterText");
            label->setTextFormat(Qt::RichText);
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
            label->setOpenExternalLinks(true);
            label->setWordWrap(true);
            footerLayout->addWidget(label, 1);
            footer = footerCard;
        }

        scroll_.setWidgetResizable(true);
        scroll_.setWidget(container_);
        scroll_.setObjectName("outputsScroll");
        scroll_.setFrameShape(QFrame::NoFrame);
        scroll_.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        setStyleSheet(R"(
            QWidget#streamHubOutputs { background: #080c14; color: #f2f7ff; }
            QWidget#outputsPage { background: transparent; color: #f2f7ff; }
            QScrollArea#outputsScroll, QScrollArea#outputsScroll > QWidget > QWidget { background: transparent; border: none; }
            QLabel#outputsTitleIcon { background:transparent; }
            QLabel#outputsTitle { color: #f2f7ff; font-size: 18px; font-weight: 700; }
            QPushButton#addDestination { background: rgba(16,26,42,235); border: 1px solid #0077ff;
                border-radius: 7px; padding: 6px 11px; color: #f2f7ff; font-weight: 600; }
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
                border-radius: 8px; }
            QLabel#outputName { color: #f2f7ff; font-size: 14px; font-weight: 700; }
            QLabel#outputStatus { color: #9eb2cb; }
            QLabel#outputStatus[ready="true"] { color: #16d86a; }
            QLabel#outputStatus[error="true"] { color: #ff647c; }
            QPushButton#outputStreamButton { background:#00a6e8; border:1px solid #37cfff; border-radius:6px; }
            QPushButton#outputStreamButton:hover { background:#56d6ff; }
            QPushButton#outputStreamButton:disabled { background:#24374b; border-color:#38536a; }
            QWidget#outputSummary { background:rgba(8,18,32,220); border:1px solid #29496f;
                border-radius:8px; }
            QLabel#summaryTitle { color:#9eb2cb; font-size:10px; }
            QLabel#aggregateBitrate { color:#f2f7ff; font-size:12px; font-weight:700; }
            QLabel#activeOutputs { color:#f2f7ff; font-size:12px; font-weight:700; }
            QFrame#summaryDivider { color:#29496f; }
            QPushButton#outputEdit { background: #101a2a; border: 1px solid #29496f;
                border-radius: 7px; padding: 6px; color: #dbe4f5; }
            QPushButton#outputEdit:hover { border-color: #00c8ff; background:#15233a; }
            QPushButton#mainOutputToggle { background:transparent; border:none; }
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
            QWidget#outputsFooter { background:rgba(8,18,32,232); border:1px solid #29496f;
                border-radius:12px; color:#f2f7ff; }
            QLabel#outputsFooterText { background:transparent; border:none; color:#cbd7e8; }
            QLabel#outputsFooterText b { color:#f2f7ff; }
            QLabel#outputsFooterText a { color:#00d8ff; }
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

    PushWidget *FindOutputWidget(const std::string &id)
    {
        for(int row=0;row<outputsContainer_->count();++row){
            auto *item=outputsContainer_->item(row);
            if(item->data(Qt::UserRole).toString().toStdString()==id)
                return dynamic_cast<PushWidget *>(outputsContainer_->itemWidget(item));
        }
        return nullptr;
    }

    std::vector<PushWidget *> LinkedOutputWidgets(const std::string &id)
    {
        std::vector<PushWidget *> outputs;
        if(auto *root=FindOutputWidget(id))outputs.push_back(root);
        for(const auto &target:GlobalMultiOutputConfig().targets)
            if(target&&target->platform=="youtube"&&target->orientation=="vertical"&&target->parentTargetId==id)
                if(auto *child=FindOutputWidget(target->id))outputs.push_back(child);
        return outputs;
    }

    void ToggleLinkedOutput(const std::string &id)
    {
        auto *root=FindOutputWidget(id);if(!root)return;
        const auto outputs=LinkedOutputWidgets(id);
        StreamHubToggleLinkedOutputs(outputs,[root](auto *output){return output==root||output->IsEnabledForLinkedStart();});
        RefreshLinkedOutputStatuses();
    }

    void RefreshLinkedOutputStatuses()
    {
        for(const auto &target:GlobalMultiOutputConfig().targets) {
            if(!target||target->platform!="youtube"||target->orientation=="vertical")continue;
            auto *root=FindOutputWidget(target->id);if(!root)continue;
            const auto outputs=LinkedOutputWidgets(target->id);
            QStringList status;bool busy=false;
            if(outputs.size()>1) {
                status.append(tr("Horizontal: %1").arg(root->StreamStatus()));
                for(auto *output:outputs) {
                    busy=busy||output->IsBusy();
                    if(output!=root)status.append(tr("Vertical: %1%2").arg(output->StreamStatus(),
                        output->IsEnabledForLinkedStart()?QString():tr(" (início separado)")));
                }
            }
            root->SetLinkedStatus(status.join('\n'),busy);
            for(int row=0;row<outputsContainer_->count();++row){
                auto *item=outputsContainer_->item(row);
                if(outputsContainer_->itemWidget(item)==root)
                    item->setSizeHint(QSize(1,(std::max)(root->sizeHint().height(),68)));
            }
        }
    }

    void SaveConfig()
    {
        SaveMultiOutputConfig();
        if (outputPlatformsChanged_)
            outputPlatformsChanged_(ConfiguredPlatforms());
    }

    QStringList ConfiguredPlatforms() const
    {
        QStringList platforms;
        const QString primaryPlatform = PrimaryPlatform();
        if (!primaryPlatform.isEmpty())
            platforms.append(primaryPlatform);
        for (const auto &target : GlobalMultiOutputConfig().targets) {
            if (!target)
                continue;
            const QString platform = StreamHubPlatformForTarget(*target).id;
            if (!platform.isEmpty() && !platforms.contains(platform))
                platforms.append(platform);
        }
        return platforms;
    }

    void SetOutputPlatformsChangedCallback(std::function<void(const QStringList &)> callback)
    {
        outputPlatformsChanged_ = std::move(callback);
    }

    void MarkFrontendReady()
    {
        frontendReady_ = true;
    }

    void ObserveMainOutputEvent(obs_frontend_event event)
    {
        if (event == OBS_FRONTEND_EVENT_STREAMING_STARTING) {
            mainStarting_ = true;
            horizontalOnlyMode_ = horizontalOnlyRequested_;
            horizontalOnlyRequested_ = false;
        }
        if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPING) mainStopping_ = true;
        if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED) mainStarting_ = false;
        if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
            mainStarting_ = false;
            mainStopping_ = false;
        }
    }

    void StartHorizontalOnly()
    {
        if (obs_frontend_streaming_active() || mainStarting_ || mainStopping_)
            return;
        horizontalOnlyRequested_ = true;
        mainStarting_ = true;
        obs_frontend_streaming_start();
    }

    void StopHorizontalOnly()
    {
        if ((!obs_frontend_streaming_active() && !mainStarting_) || mainStopping_)
            return;
        horizontalOnlyMode_ = true;
        mainStopping_ = true;
        obs_frontend_streaming_stop();
    }

    bool HorizontalOnlyMode() const { return horizontalOnlyMode_; }

    bool SuppressAuxiliarySync(obs_frontend_event event) const
    {
        return horizontalOnlyMode_ && event >= OBS_FRONTEND_EVENT_STREAMING_STARTING &&
               event <= OBS_FRONTEND_EVENT_STREAMING_STOPPED;
    }

    void FinishHorizontalOnlyStop(obs_frontend_event event)
    {
        if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
            horizontalOnlyMode_ = false;
            horizontalOnlyRequested_ = false;
        }
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
        if (!server.startsWith("rtmp://", Qt::CaseInsensitive) &&
            !server.startsWith("rtmps://", Qt::CaseInsensitive)) {
            blog(LOG_WARNING, TAG "Rejected non-RTMP server for %s output sync.",
                 platform.toUtf8().constData());
            return;
        }

        auto &global = GlobalMultiOutputConfig();
        OutputTargetConfigPtr target;
        for (const auto &candidate : global.targets) {
            if (candidate && candidate->orientation != "vertical" && QString::fromStdString(candidate->platform).compare(platform, Qt::CaseInsensitive) == 0) {
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

        if (!target->manualCredentials) {
            OBSOutputAutoRelease currentOutput = obs_get_output_by_name(("streamhub-output-" + target->id).c_str());
            if (currentOutput && obs_output_active(currentOutput)) {
                QMessageBox::warning(this, tr("Saída em uso"), tr("Pare esta saída antes de sincronizar seu servidor e sua chave."));
                return;
            }
            target->serviceParam["server"] = server.toStdString();
            target->serviceParam["key"] = key.toStdString();
        }
        if (platform == "youtube")
            target->youtubeBroadcastId = transmission.value("broadcastId").toString().toStdString();
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
            if (outputPlatformsChanged_)
                outputPlatformsChanged_(ConfiguredPlatforms());
            return;
        }

        for(auto x: GlobalMultiOutputConfig().targets)
        {
            AddPushWidget(x->id);
        }
        if (outputPlatformsChanged_)
            outputPlatformsChanged_(ConfiguredPlatforms());
    }

    QSize sizeHint() const override
    {
        constexpr int kInitialDockWidth = 410;
        const QSize calculated = QWidget::sizeHint();
        return QSize((std::max)(calculated.width(), kInitialDockWidth), calculated.height());
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
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
    QDialog* settingsDialog_ = nullptr;
    StreamHubInlineSettings* settingsPanel_ = nullptr;
    QLabel *aggregateBitrate_ = nullptr;
    QLabel *activeOutputs_ = nullptr;
    QTimer *aggregateTimer_ = nullptr;
    bool frontendReady_ = false;
    bool mainStarting_ = false;
    bool mainStopping_ = false;
    bool horizontalOnlyRequested_ = false;
    bool horizontalOnlyMode_ = false;
    std::chrono::steady_clock::time_point mainLastInfoTime_{};
    uint64_t mainTotalBytes_ = 0;
    double mainCurrentBps_ = 0.0;
    std::string settingsTargetId_;
    std::function<void(const QStringList &)> outputPlatformsChanged_;

    void ShowSettingsFor(const std::string &targetId, PushWidget *pushWidget)
    {
        if (settingsDialog_ && settingsTargetId_ == targetId) {
            settingsDialog_->raise();
            settingsDialog_->activateWindow();
            return;
        }
        if (settingsDialog_) {
            settingsDialog_->close();
            settingsDialog_ = nullptr;
            settingsPanel_ = nullptr;
        }

        auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
        if (!mainWindow)
            return;
        auto *dialog = new QDialog(mainWindow);
        dialog->setObjectName("streamHubSettingsDialog");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(tr("Configurações do destino"));
        dialog->setWindowFlags(dialog->windowFlags() & ~Qt::WindowContextHelpButtonHint);
        dialog->setWindowModality(Qt::WindowModal);
        auto *dialogLayout = new QVBoxLayout(dialog);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        dialogLayout->setSpacing(0);

        settingsTargetId_ = targetId;
        settingsPanel_ = new StreamHubInlineSettings(
            targetId, pushWidget,
            [this, pushWidget]() {
                pushWidget->ReloadConfig();
                if (s_verticalWorkspaceDock)
                    s_verticalWorkspaceDock->RefreshFromConfig();
                for (const auto &target : GlobalMultiOutputConfig().targets) {
                    bool found = false;
                    for (int row = 0; row < outputsContainer_->count(); ++row) {
                        auto *item = outputsContainer_->item(row);
                        if (item->data(Qt::UserRole).toString().toStdString() == target->id) {
                            if (auto *widget = dynamic_cast<PushWidget *>(outputsContainer_->itemWidget(item))) widget->ReloadConfig();
                            found = true;
                            break;
                        }
                    }
                    if (!found) AddPushWidget(target->id);
                }
                outputsContainer_->doItemsLayout();
                if (outputPlatformsChanged_)
                    outputPlatformsChanged_(ConfiguredPlatforms());
            },
            [pushWidget]() { pushWidget->GetDeleteButton()->click(); },
            [dialog]() { dialog->reject(); }, dialog);
        auto *settingsScroll = new QScrollArea(dialog);
        const auto selectedTarget = FindById(GlobalMultiOutputConfig().targets, targetId);
        if (selectedTarget && selectedTarget->platform == "youtube" && selectedTarget->orientation != "vertical") {
            auto *verticalControl = new QPushButton(tr("Iniciar / parar saída vertical"), dialog);
            verticalControl->setObjectName("youtubeVerticalPlayback");
            connect(verticalControl, &QPushButton::clicked, dialog, [this, targetId]() {
                for (const auto &target : GlobalMultiOutputConfig().targets) {
                    if (target->parentTargetId != targetId || target->orientation != "vertical") continue;
                    for (int row = 0; row < outputsContainer_->count(); ++row) {
                        auto *item = outputsContainer_->item(row);
                        if (item->data(Qt::UserRole).toString().toStdString() != target->id) continue;
                        if (auto *widget = dynamic_cast<PushWidget *>(outputsContainer_->itemWidget(item)))
                            widget->GetStreamButton()->click();
                        return;
                    }
                }
                QMessageBox::information(this, tr("Saída vertical"), tr("Configure e salve a saída vertical antes de iniciar."));
            });
            dialogLayout->addWidget(verticalControl);

        }
        settingsScroll->setWidgetResizable(true);
        settingsScroll->setFrameShape(QFrame::NoFrame);
        settingsScroll->setWidget(settingsPanel_);
        dialogLayout->addWidget(settingsScroll);
        dialog->setStyleSheet(styleSheet());
        dialog->adjustSize();
        const int dialogWidth = (std::min)(520, (std::max)(420, mainWindow->width() / 3));
        dialog->resize((std::max)(dialogWidth, 540), (std::min)(mainWindow->height() - 80, settingsPanel_->sizeHint().height() + 24));
        dialog->move(mainWindow->frameGeometry().center() -
                     QPoint(dialog->width() / 2, dialog->height() / 2));
        settingsDialog_ = dialog;
        QObject::connect(dialog, &QDialog::finished, this, [this, dialog]() {
            if (settingsDialog_ == dialog) {
                settingsDialog_ = nullptr;
                settingsPanel_ = nullptr;
                settingsTargetId_.clear();
            }
        });
        dialog->open();
    }

    void DeletePushWidget(const std::string& targetId)
    {
        if (settingsPanel_ && settingsTargetId_ == targetId) {
            if (settingsDialog_)
                settingsDialog_->close();
            settingsDialog_ = nullptr;
            settingsPanel_ = nullptr;
            settingsTargetId_.clear();
        }
        // Delete from model
        auto outputTargets = &(GlobalMultiOutputConfig().targets);
        auto currentTarget = std::find_if(outputTargets->begin(), outputTargets->end(), [&targetId](auto& x) { return x->id == targetId; });
        if (currentTarget == outputTargets->end()) {
            return;
        }
        outputTargets->erase(currentTarget);
        for (int row = 0; row < outputsContainer_->count(); ++row) {
            auto *item = outputsContainer_->item(row);
            auto target = FindById(*outputTargets, item->data(Qt::UserRole).toString().toStdString());
            if (target && target->parentTargetId == targetId) {
                target->parentTargetId.clear();
                item->setHidden(false);
            }
        }

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
        QSize cardSize(1, (std::max)(pushWidget->sizeHint().height(), 68));
        listItem->setSizeHint(cardSize);
        outputsContainer_->addItem(listItem);
        outputsContainer_->setItemWidget(listItem, pushWidget);
        const auto target = FindById(GlobalMultiOutputConfig().targets, targetId);
        if(target&&target->platform=="youtube"&&target->orientation!="vertical")
            pushWidget->SetStreamAction([this,targetId](){ToggleLinkedOutput(targetId);});
        if (target && target->orientation == "vertical" && !target->parentTargetId.empty() &&
            FindById(GlobalMultiOutputConfig().targets, target->parentTargetId))
            listItem->setHidden(true);

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

    const QString legacyDataPath = StreamHubModuleDataPath();
    const QString dataPath = StreamHubWritableDataPath(false);
    if (!dataPath.isEmpty()) {
        if (!legacyDataPath.isEmpty() && legacyDataPath != dataPath)
            StreamHub_MigrateBundledData(legacyDataPath, dataPath);
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
    streamhub_register_noise_suppressor();
    // --- StreamHub: recria a pasta data/ a partir do que está embutido na
    // própria DLL (qrc/streamhub-data.qrc). Precisa rodar ANTES do primeiro
    // obs_module_text() logo abaixo, que já lê locale/*.ini do disco — por
    // isso vem antes de qualquer outra coisa em obs_module_load(). É isso
    // que permite copiar só a .dll para uma instalação nova do OBS.
    // OBS paths are relative to its current working directory, not to the
    // plugin DLL. Resolve once before extraction and before Node changes cwd.
    const QString legacyDataPath = StreamHubModuleDataPath();
    const QString dataPath = StreamHubWritableDataPath();
    if (!dataPath.isEmpty() && !legacyDataPath.isEmpty() && legacyDataPath != dataPath)
        StreamHub_MigrateBundledData(legacyDataPath, dataPath);
    StreamHub_EnsureBundledData(dataPath);
    QString serverDir = QDir(dataPath).filePath("streamhub-server");
    StreamHubInstallBundledTheme();
    StreamHubInstallNativeThemeHook();

    auto mainwin = (QMainWindow*)obs_frontend_get_main_window();
    if (mainwin == nullptr)
        return false;

#if defined(_WIN32)
    static StreamHubUpdater *s_updater = new StreamHubUpdater(mainwin);
    s_updater->Start();
#endif

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

    s_verticalWorkspaceDock = new StreamHubVerticalWorkspace(
        [dock](bool start) {
            if (start) dock->StartHorizontalOnly();
            else dock->StopHorizontalOnly();
        },
        [dock]() {
            for (auto *output : dock->GetAllPushWidgets())
                output->StopStreaming();
        });
    QWidget *verticalCanvasPanel = s_verticalWorkspaceDock->CreateCanvasPanel();
    QWidget *verticalScenesPanel = s_verticalWorkspaceDock->CreateScenesPanel();
    QWidget *verticalSourcesPanel = s_verticalWorkspaceDock->CreateSourcesPanel();
    const bool canvasDockAdded = obs_frontend_add_dock_by_id(
        "streamhub-vertical-canvas", "Canvas vertical", verticalCanvasPanel);
    const bool scenesDockAdded = obs_frontend_add_dock_by_id(
        "streamhub-vertical-scenes", "Cenas verticais", verticalScenesPanel);
    const bool sourcesDockAdded = obs_frontend_add_dock_by_id(
        "streamhub-vertical-sources", "Fontes verticais", verticalSourcesPanel);
    if (canvasDockAdded) brandDock(verticalCanvasPanel);
    if (scenesDockAdded) brandDock(verticalScenesPanel);
    if (sourcesDockAdded) brandDock(verticalSourcesPanel);
    QTimer::singleShot(0, mainwin, [mainwin, canvasDockAdded, scenesDockAdded, sourcesDockAdded]() {
        const auto prepareResize = [mainwin](const char *name, bool added) {
            if (!added)
                return;
            if (auto *dockWidget = mainwin->findChild<QDockWidget *>(name)) {
                dockWidget->setMinimumSize(0, 0);
                dockWidget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                dockWidget->setFeatures(dockWidget->features() | QDockWidget::DockWidgetMovable |
                                        QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
            }
        };
        prepareResize("streamhub-vertical-canvas", canvasDockAdded);
        prepareResize("streamhub-vertical-scenes", scenesDockAdded);
        prepareResize("streamhub-vertical-sources", sourcesDockAdded);
    });
    s_verticalWorkspaceDock->RefreshFromConfig();

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
    chatDock->SetOutputPlatforms(dock->ConfiguredPlatforms());
    dock->SetOutputPlatformsChangedCallback([chatDock](const QStringList &platforms) {
        chatDock->SetOutputPlatforms(platforms);
    });
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
        // Aplica estado disponível agora e reconsulta até serviço ficar pronto.
        const auto updatePrimaryPlatform = [dock, controlDock]() {
            const QString platform = dock->PrimaryPlatform();
            if (platform.isEmpty())
                return;
            controlDock->SetPrimaryPlatform(platform);
            dock->EnsurePlatformTarget(platform);
        };
        updatePrimaryPlatform();
        auto *primaryTimer = new QTimer(controlDock);
        primaryTimer->setInterval(500);
        QObject::connect(primaryTimer, &QTimer::timeout, controlDock, updatePrimaryPlatform);
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
            dock->ObserveMainOutputEvent(event);
            if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED || event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED ||
                event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED || event == OBS_FRONTEND_EVENT_FINISHED_LOADING ||
                event == OBS_FRONTEND_EVENT_PROFILE_CHANGED) {
                const bool activateMainScene = event != OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED;
                for (const auto &target : GlobalMultiOutputConfig().targets)
                    if (target) StreamHubFollowVertical(*target, activateMainScene);
            }
            if (s_verticalWorkspaceDock &&
                (event == OBS_FRONTEND_EVENT_SCENE_CHANGED || event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED ||
                 event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED || event == OBS_FRONTEND_EVENT_FINISHED_LOADING ||
                 event == OBS_FRONTEND_EVENT_PROFILE_CHANGED))
                s_verticalWorkspaceDock->RefreshFromConfig();

            if (s_verticalWorkspaceDock)
                s_verticalWorkspaceDock->SetHorizontalOnlyMode(dock->HorizontalOnlyMode());
            if (!dock->SuppressAuxiliarySync(event))
                for(auto x: dock->GetAllPushWidgets())
                    x->OnOBSEvent(event);
            dock->FinishHorizontalOnlyStop(event);
            if (s_verticalWorkspaceDock)
                s_verticalWorkspaceDock->SetHorizontalOnlyMode(dock->HorizontalOnlyMode());

            if (event == obs_frontend_event::OBS_FRONTEND_EVENT_EXIT)
            {
                dock->SaveConfig();
                s_launcher->Stop();
            }
            else if (event == obs_frontend_event::OBS_FRONTEND_EVENT_FINISHED_LOADING)
            {
                dock->MarkFrontendReady();
            }
            else if (event == obs_frontend_event::OBS_FRONTEND_EVENT_PROFILE_CHANGED)
            {
                dock->LoadConfig();
                if (s_verticalWorkspaceDock)
                    s_verticalWorkspaceDock->RefreshFromConfig();
            }
        }, dock
    );

    return true;
}

const char *obs_module_description(void)
{
    return "Multiple RTMP Output Plugin";
}
