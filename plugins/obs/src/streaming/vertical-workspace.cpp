

#include "output-config.h"
#include "obs.hpp"
#include "vertical-output.h"
#include "center-snap.h"
#include "vertical-editor-core.h"

#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>

#include <QHBoxLayout>
#include <QCheckBox>
#include <QCursor>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QPushButton>
#include <QStyleOption>
#include <QToolBar>
#include <QToolButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLineF>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QWindow>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QRubberBand>
#include <QShortcut>
#include <QWheelEvent>
#include <QDropEvent>
#include <QDrag>
#include <QMimeData>
#include <util/config-file.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "vertical-workspace.h"
#include "vertical-preview.h"
#include "vertical-editor-widgets.h"

StreamHubVerticalWorkspace::StreamHubVerticalWorkspace(std::function<void(bool)> horizontalStreamAction,
                                                               std::function<void()> stopAuxiliaryOutputs, QObject *parent)
    : QObject(parent), horizontalStreamAction_(std::move(horizontalStreamAction)),
      stopAuxiliaryOutputs_(std::move(stopAuxiliaryOutputs))
{
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        const auto revision = StreamHubEditorRevision.load();
        if (editorRevision_ != revision) { editorRevision_ = revision; selectedSourceId_ = -1; RefreshScenes(); }
    });
    timer->start(100);
}

void StreamHubVerticalWorkspace::RefreshFromConfig()
{
    OutputTargetConfigPtr target, fallback;
    for (const auto &candidate : GlobalMultiOutputConfig().targets) {
        if (!candidate || candidate->orientation != "vertical" || candidate->canvasUuid.empty())
            continue;
        if (!fallback)
            fallback = candidate;
        const auto parent = FindById(GlobalMultiOutputConfig().targets, candidate->parentTargetId);
        if (parent && parent->platform == "youtube") {
            target = candidate;
            break;
        }
    }
    if (!target)
        target = fallback;
    targetId_ = target ? target->id : std::string{};
    obs_canvas_t *canvas = target ? obs_get_canvas_by_uuid(target->canvasUuid.c_str()) : nullptr;
    if (canvas && target) StreamHubNormalizeVerticalCanvasName(canvas, target->id);
    const bool ready = canvas && !obs_canvas_removed(canvas);
    if (ready && canvas) {
        OBSSourceAutoRelease active = obs_canvas_get_channel(canvas, 0);
        if (active)
            selectedSceneUuid_ = obs_source_get_uuid(active);
    }
    if (canvas) obs_canvas_release(canvas);
    if (status_)
        status_->setText(ready ? tr("Canvas pronto") : target ? tr("Canvas não carregado") : tr("Sem saída vertical"));
    if (sceneStatus_)
        sceneStatus_->setText(ready
            ? tr("A lista principal é sincronizada. Cenas criadas aqui permanecem somente no vertical.")
            : tr("Adicione e configure uma saída 9:16 em Múltiplas saídas para ativar este painel."));
    if (sourceStatus_)
        sourceStatus_->setText(ready ? tr("Arraste fontes na prévia para posicionar; use Transformar para ajustes precisos.")
                                     : tr("As fontes aparecem quando o canvas vertical estiver pronto."));
    if (scenes_) scenes_->setEnabled(ready);
    if (sources_) sources_->setEnabled(ready);
    UpdateSceneSyncButton();
    if (preview_) {
        preview_->SetCanvasUuid(target ? target->canvasUuid : std::string{});
        preview_->setEnabled(ready);
    }
    RefreshScenes();
}

void StreamHubVerticalWorkspace::StartHorizontalOnly()
{
    if (!horizontalStreamAction_ || obs_frontend_streaming_active())
        return;
    if (stopAuxiliaryOutputs_)
        stopAuxiliaryOutputs_();
    horizontalOnlyMode_ = true;
    horizontalStreamAction_(true);
}

void StreamHubVerticalWorkspace::StopHorizontalOnly()
{
    if (!horizontalStreamAction_ || !obs_frontend_streaming_active())
        return;
    horizontalOnlyMode_ = true;
    horizontalStreamAction_(false);
}

void StreamHubVerticalWorkspace::ToggleRecording()
{
    if (obs_frontend_recording_active())
        obs_frontend_recording_stop();
    else
        obs_frontend_recording_start();
}
