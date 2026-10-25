#pragma once
#include "pch.h"
#include "push-widget.h"
#include "output-config.h"
#include "streamhub-platforms.h"
#include "vertical-output.h"
#include "obs.hpp"
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QFormLayout>
#include <QUrl>
#include <QSpinBox>

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
        name_->setObjectName("outputNameInput");
        server_->setObjectName("outputServerInput");
        key_->setObjectName("outputKeyInput");
        server_->setEchoMode(QLineEdit::Password);
        key_->setEchoMode(QLineEdit::Password);
        server_->setPlaceholderText(tr("Servidor RTMP da plataforma"));
        key_->setPlaceholderText(tr("Cole a chave de transmissão"));
        form->addRow(tr("Nome"), name_);
        serverLabel_ = new QLabel(tr("Servidor RTMP"), this);
        serverLabel_->setObjectName("outputServerLabel");
        keyLabel_ = new QLabel(tr("Chave de transmissão"), this);
        form->addRow(serverLabel_, SecretField(server_, serverVisible_, this));
        form->addRow(keyLabel_, SecretField(key_, keyVisible_, this));
        auto *copyMain = new QPushButton(tr("Copiar configurações da transmissão principal"), this);
        copyMain->setObjectName("copyMainEncoding");
        form->addRow(copyMain);
        connect(copyMain, &QPushButton::clicked, this, [this]() { CopyMainEncoding(false); });
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

        orientation_ = new QComboBox(this);
        orientation_->setObjectName("outputOrientation");
        orientation_->addItem(tr("Horizontal — vídeo principal"), "horizontal");
        orientation_->addItem(tr("Vertical — área própria 9:16 do StreamHub"), "vertical");
        form->addRow(tr("Formato de vídeo"), orientation_);
        bitrate_ = new QSpinBox(this);
        bitrate_->setObjectName("outputVideoBitrate");
        bitrate_->setRange(0, 51000);
        bitrate_->setSingleStep(500);
        bitrate_->setSpecialValueText(tr("Herdar do OBS"));
        bitrate_->setSuffix(tr(" kbps"));
        form->addRow(tr("Bitrate de vídeo"), bitrate_);
        canvasInfo_ = new QLabel(tr("O StreamHub cria e mantém uma área 9:16 própria (1080 × 1920)."), this);
        canvasInfo_->setWordWrap(true);
        form->addRow(canvasInfo_);
        manual_ = new QCheckBox(tr("Preservar servidor e chave manuais ao sincronizar a conta"), this);
        manual_->setObjectName("outputManualCredentials");
        layout->addWidget(manual_);
        connect(orientation_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
            canvasInfo_->setVisible(orientation_->currentData().toString() == "vertical");
            if (verticalGroup_) {
                const auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
                verticalGroup_->setVisible(target && target->platform == "youtube" && orientation_->currentData().toString() != "vertical");
            }
        });

        verticalGroup_ = new QGroupBox(tr("YouTube vertical — servidor e chave separados"), this);
        auto *verticalForm = new QFormLayout(verticalGroup_);
        verticalForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
        verticalEnabled_ = new QCheckBox(tr("Incluir saída vertical em Iniciar tudo"), verticalGroup_);
        verticalEnabled_->setObjectName("youtubeVerticalEnabled");
        verticalForm->addRow(verticalEnabled_);
        verticalServer_ = new QLineEdit(verticalGroup_);
        verticalKey_ = new QLineEdit(verticalGroup_);
        verticalServer_->setObjectName("youtubeVerticalServer");
        verticalKey_->setObjectName("youtubeVerticalKey");
        verticalKey_->setEchoMode(QLineEdit::Password);
        verticalServer_->setPlaceholderText("rtmps://...");
        verticalKey_->setPlaceholderText(tr("Chave Vertical exibida no YouTube Studio"));
        verticalForm->addRow(tr("Servidor RTMP vertical"), verticalServer_);
        verticalForm->addRow(tr("Chave vertical"), SecretField(verticalKey_, verticalKeyVisible_, verticalGroup_));
        auto *copyVertical = new QPushButton(tr("Copiar configurações da transmissão principal"), verticalGroup_);
        copyVertical->setObjectName("copyVerticalEncoding");
        verticalForm->addRow(copyVertical);
        connect(copyVertical, &QPushButton::clicked, this, [this]() { CopyMainEncoding(true); });
        auto *verticalCanvasInfo = new QLabel(tr("Área vertical própria do StreamHub: 1080 × 1920 (9:16)."), verticalGroup_);
        verticalCanvasInfo->setWordWrap(true);
        verticalForm->addRow(verticalCanvasInfo);
        verticalBitrate_ = new QSpinBox(verticalGroup_);
        verticalBitrate_->setObjectName("youtubeVerticalBitrate");
        verticalBitrate_->setRange(0, 51000);
        verticalBitrate_->setSingleStep(500);
        verticalBitrate_->setSpecialValueText(tr("Herdar do OBS"));
        verticalBitrate_->setSuffix(tr(" kbps"));
        verticalForm->addRow(tr("Bitrate vertical"), verticalBitrate_);
        auto *verticalHint = new QLabel(tr("A chave horizontal fica nos campos acima. A saída vertical terá seu próprio botão de iniciar/parar."), verticalGroup_);
        verticalHint->setWordWrap(true);
        verticalForm->addRow(verticalHint);
        layout->addWidget(verticalGroup_);

        linkedStartGroup_=new QGroupBox(tr("Vínculo de início — horizontal e vertical"),this);
        linkedStartGroup_->setObjectName("youtubeLinkedStartCard");
        auto *linkedLayout=new QVBoxLayout(linkedStartGroup_);
        startWithParent_=new QCheckBox(tr("Iniciar a vertical junto com a horizontal"),linkedStartGroup_);
        startWithParent_->setObjectName("youtubeVerticalStartWithHorizontal");
        linkedLayout->addWidget(startWithParent_);
        auto *linkedHint=new QLabel(tr("Ligado: o play da horizontal inicia também a vertical habilitada. Desligado: inicie a vertical pelo controle individual nas configurações. O botão de parar do cartão continua encerrando ambas."),linkedStartGroup_);
        linkedHint->setWordWrap(true);linkedLayout->addWidget(linkedHint);
        layout->addWidget(linkedStartGroup_);

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
        manual_->setChecked(target->manualCredentials);
        bitrate_->setValue(target->videoBitrateKbps);
        orientation_->setCurrentIndex(target->orientation == "vertical" ? 1 : 0);
        canvasInfo_->setVisible(target->orientation == "vertical");
        const bool dual = target->platform == "youtube" && target->orientation != "vertical";
        serverLabel_->setText(dual ? tr("Servidor RTMP horizontal") : target->orientation == "vertical" ? tr("Servidor RTMP vertical") : tr("Servidor RTMP"));
        keyLabel_->setText(dual ? tr("Chave horizontal") : target->orientation == "vertical" ? tr("Chave vertical") : tr("Chave de transmissão"));
        verticalGroup_->setVisible(dual);
        OutputTargetConfigPtr vertical;
        for (const auto &candidate : GlobalMultiOutputConfig().targets)
            if (candidate->parentTargetId == targetId_ && candidate->orientation == "vertical") vertical = candidate;
        verticalEnabled_->setChecked(vertical && vertical->enabled);
        linkedStartGroup_->setVisible(dual || (target->orientation=="vertical"&&!target->parentTargetId.empty()));
        startWithParent_->setChecked(target->orientation=="vertical"?target->startWithParent:!vertical||vertical->startWithParent);
        verticalServer_->setText(QString::fromStdString(vertical ? vertical->serviceParam.value("server", std::string{}) : ""));
        verticalKey_->setText(QString::fromStdString(vertical ? vertical->serviceParam.value("key", std::string{}) : ""));
        verticalBitrate_->setValue(vertical ? vertical->videoBitrateKbps : 0);
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
        savedNotice_->hide();
        auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
        if (!target)
            return;
        if (pushWidget_->IsBusy()) {
            QMessageBox::warning(this, tr("Saída em uso"), tr("Pare esta saída antes de alterar servidor, chave ou formato."));
            return;
        }
        const auto validServer = [](const QString &text) {
            const QUrl url(text.trimmed());
            return url.isValid() && !url.host().isEmpty() &&
                (url.scheme().compare("rtmp", Qt::CaseInsensitive) == 0 || url.scheme().compare("rtmps", Qt::CaseInsensitive) == 0);
        };
        const bool hasServer = !server_->text().trimmed().isEmpty();
        const bool hasKey = !key_->text().trimmed().isEmpty();
        if (hasServer != hasKey || (hasServer && !validServer(server_->text()))) {
            QMessageBox::warning(this, tr("Configuração incompleta"), tr("Informe o servidor RTMP/RTMPS e a chave correspondente, ou deixe ambos vazios."));
            return;
        }
        const bool isVertical = orientation_->currentData().toString() == "vertical";
        const bool configurePair = verticalGroup_->isVisible() &&
            (!verticalServer_->text().trimmed().isEmpty() || !verticalKey_->text().trimmed().isEmpty() || verticalEnabled_->isChecked());
        if (configurePair && (!validServer(verticalServer_->text()) || verticalKey_->text().trimmed().isEmpty() ||
            verticalKey_->text().trimmed() == key_->text().trimmed())) {
            QMessageBox::warning(this, tr("Saída vertical incompleta"), tr("Informe servidor RTMP/RTMPS e uma chave vertical diferente da horizontal."));
            return;
        }
        auto &global = GlobalMultiOutputConfig();
        OutputTargetConfigPtr pair;
        for (const auto &candidate : global.targets)
            if (candidate->parentTargetId == targetId_ && candidate->orientation == "vertical") pair = candidate;
        if (pair) {
            OBSOutputAutoRelease activePair = obs_get_output_by_name(("streamhub-output-" + pair->id).c_str());
            if (activePair && obs_output_active(activePair)) {
                QMessageBox::warning(this, tr("Saída vertical em uso"), tr("Pare a saída vertical antes de alterar suas configurações."));
                return;
            }
        }
        const OutputTargetConfig originalTarget = *target;
        const std::optional<OutputTargetConfig> originalPair = pair ? std::optional<OutputTargetConfig>(*pair) : std::nullopt;
        bool createdTargetCanvas = false;
        bool createdPairCanvas = false;
        obs_canvas_t *targetCanvas = nullptr;
        obs_canvas_t *pairCanvas = nullptr;
        const bool credentialsEdited = target->serviceParam.value("server", std::string{}) != server_->text().trimmed().toStdString() ||
            target->serviceParam.value("key", std::string{}) != key_->text().trimmed().toStdString();
        target->manualCredentials = manual_->isChecked() || credentialsEdited;
        manual_->setChecked(target->manualCredentials);
        target->orientation = isVertical ? "vertical" : "horizontal";
        target->videoBitrateKbps = bitrate_->value();
        if (pendingEncoding_.is_object()) {
            target->copiedEncoding = pendingEncoding_;
            target->videoConfig.reset();
            target->audioConfig.reset();
        }
        if (!isVertical)
            target->canvasUuid.clear();
        if (configurePair) {
            if (!pair) {
                pair = std::make_shared<OutputTargetConfig>();
                pair->id = GenerateId(global);
                StreamHubApplyPlatformPreset(*pair, "youtube");
                pair->name = "YouTube · Vertical";
                pair->parentTargetId = targetId_;
                pair->orientation = "vertical";
                global.targets.emplace_back(pair);
            }
            pair->serviceParam["server"] = verticalServer_->text().trimmed().toStdString();
            pair->serviceParam["key"] = verticalKey_->text().trimmed().toStdString();
            pair->manualCredentials = true;
            pair->videoBitrateKbps = verticalBitrate_->value();
            if (pendingVerticalEncoding_.is_object()) {
                pair->copiedEncoding = pendingVerticalEncoding_;
                pair->videoConfig.reset();
                pair->audioConfig.reset();
            }
            pair->enabled = verticalEnabled_->isChecked();
            pair->startWithParent = startWithParent_->isChecked();
            pair->syncStart = syncStart_->isChecked();
            pair->syncStop = syncStop_->isChecked();
        } else if (pair) {
            pair->enabled = false;
        }
        target->name = name_->text().trimmed().toStdString();
        target->serviceParam["server"] = server_->text().trimmed().toStdString();
        target->serviceParam["key"] = key_->text().trimmed().toStdString();
        target->syncStart = syncStart_->isChecked();
        if(isVertical)target->startWithParent=startWithParent_->isChecked();
        target->syncStop = syncStop_->isChecked();
        if (QString::fromStdString(target->platform).toLower() == "custom") {
            target->customIcon = customIcon_->currentData().toString().toStdString();
            target->customAccent = customAccent_.name(QColor::HexRgb).toStdString();
        }
        if (isVertical) {
            targetCanvas = StreamHubEnsureVerticalCanvas(*target, createdTargetCanvas);
            if (!targetCanvas) {
                *target = originalTarget;
                if (pair && originalPair) *pair = *originalPair;
                else if (pair) global.targets.remove_if([&](const auto &item) { return item->id == pair->id; });
                QMessageBox::warning(this, tr("Área vertical"), tr("Não foi possível criar a área vertical 9:16 no OBS."));
                return;
            }
            target->canvasUuid = obs_canvas_get_uuid(targetCanvas);
            target->sceneMode = "follow";
        }
        if (configurePair) {
            pairCanvas = StreamHubEnsureVerticalCanvas(*pair, createdPairCanvas);
            if (!pairCanvas) {
                if (createdTargetCanvas) StreamHubRemoveCreatedVerticalCanvas(targetCanvas);
                else if (targetCanvas) obs_canvas_release(targetCanvas);
                *target = originalTarget;
                if (pair && originalPair) *pair = *originalPair;
                else if (pair) global.targets.remove_if([&](const auto &item) { return item->id == pair->id; });
                QMessageBox::warning(this, tr("Área vertical"), tr("Não foi possível criar a área vertical 9:16 no OBS."));
                return;
            }
            pair->canvasUuid = obs_canvas_get_uuid(pairCanvas);
            pair->sceneMode = "follow";
        }
        if (target->serviceParam.value("server", std::string{}).empty() ||
            target->serviceParam.value("key", std::string{}).empty()) {
            target->syncStart = false;
            target->syncStop = false;
            target->enabled = false;
            syncStart_->setChecked(false);
            syncStop_->setChecked(false);
        }
        if (!SaveMultiOutputConfig()) {
            *target = originalTarget;
            if (pair && originalPair) *pair = *originalPair;
            else if (pair) global.targets.remove_if([&](const auto &item) { return item->id == pair->id; });
            if (createdTargetCanvas) StreamHubRemoveCreatedVerticalCanvas(targetCanvas);
            else if (targetCanvas) obs_canvas_release(targetCanvas);
            if (createdPairCanvas) StreamHubRemoveCreatedVerticalCanvas(pairCanvas);
            else if (pairCanvas) obs_canvas_release(pairCanvas);
            QMessageBox::warning(this, tr("Falha ao salvar"), tr("Não foi possível gravar as configurações no perfil do OBS. Verifique a permissão da pasta e tente novamente."));
            return;
        }
        if (targetCanvas) {
            StreamHubFollowVertical(*target);
            obs_canvas_release(targetCanvas);
        }
        if (pairCanvas) {
            StreamHubFollowVertical(*pair);
            obs_canvas_release(pairCanvas);
        }
        if (createdTargetCanvas || createdPairCanvas)
            obs_frontend_save();
        pushWidget_->ReloadConfig();
        if (onSaved_)
            onSaved_();
        savedNotice_->show();
        saveNoticeTimer_->start(2800);
    }

    void CopyMainEncoding(bool vertical) {
        const auto settings = pushWidget_->CaptureMainEncoding();
        if (!settings.is_object()) {
            QMessageBox::warning(this, tr("Copiar configurações"), tr("Pare a saída e confira se o perfil principal usa o modo de saída Avançado."));
            return;
        }
        (vertical ? pendingVerticalEncoding_ : pendingEncoding_) = settings;
        (vertical ? verticalBitrate_ : bitrate_)->setValue(settings["video"].value("bitrate", 0));
        QMessageBox::information(this, tr("Configurações copiadas"), tr("Vídeo e áudio copiados. Clique em Salvar alterações para aplicar. A saída vertical mantém sua área 9:16."));
    }

    nlohmann::json pendingEncoding_;
    nlohmann::json pendingVerticalEncoding_;

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
    QLabel *serverLabel_ = nullptr;
    QLabel *keyLabel_ = nullptr;
    QComboBox *orientation_ = nullptr;
    QSpinBox *bitrate_ = nullptr;
    QSpinBox *verticalBitrate_ = nullptr;
    QLabel *canvasInfo_ = nullptr;
    QCheckBox *manual_ = nullptr;
    QGroupBox *verticalGroup_ = nullptr;
    QGroupBox *linkedStartGroup_ = nullptr;
    QCheckBox *startWithParent_ = nullptr;
    QCheckBox *verticalEnabled_ = nullptr;
    QLineEdit *verticalServer_ = nullptr;
    QLineEdit *verticalKey_ = nullptr;
    QPushButton *verticalKeyVisible_ = nullptr;
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
