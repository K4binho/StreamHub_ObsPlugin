

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

void StreamHubVerticalWorkspace::UpdateSceneSyncButton()
{
    if (!sceneSyncButton_)
        return;
    const auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
    const bool connected = target && target->orientation == "vertical" && target->sceneMode == "follow";
    sceneSyncButton_->blockSignals(true);
    sceneSyncButton_->setChecked(connected);
    sceneSyncButton_->setIcon(QIcon(connected
        ? QStringLiteral(":/streamhub-ui/icons/scene-sync-locked.svg")
        : QStringLiteral(":/streamhub-ui/icons/scene-sync-broken.svg")));
    sceneSyncButton_->setToolTip(connected
        ? tr("Cenas sincronizadas com o OBS. Clique para deixar as cenas verticais independentes.")
        : tr("Cenas verticais independentes. Clique para sincronizar com as cenas do OBS."));
    sceneSyncButton_->setAccessibleName(connected ? tr("Sincronização de cenas ativada")
                                                   : tr("Sincronização de cenas desativada"));
    sceneSyncButton_->setEnabled(target && target->orientation == "vertical");
    sceneSyncButton_->blockSignals(false);
}

void StreamHubVerticalWorkspace::SetSceneSyncEnabled(bool enabled)
{
    const auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
    if (!target || target->orientation != "vertical") {
        UpdateSceneSyncButton();
        return;
    }
    const std::string previous = target->sceneMode;
    target->sceneMode = enabled ? "follow" : "independent";
    if (!SaveMultiOutputConfig()) {
        target->sceneMode = previous;
        UpdateSceneSyncButton();
        QMessageBox::warning(nullptr, tr("Sincronização de cenas"),
                             tr("Não foi possível salvar essa configuração no perfil do OBS."));
        return;
    }
    if (enabled)
        StreamHubFollowVertical(*target);
    else {
        obs_canvas_t *canvas = obs_get_canvas_by_uuid(target->canvasUuid.c_str());
        if (canvas) {
            obs_canvas_enum_scenes(canvas, [](void *, obs_source_t *source) {
                if (auto *scene = obs_scene_from_source(source))
                    StreamHubSetCompositeScene(scene, nullptr, false);
                return true;
            }, nullptr);
            obs_canvas_release(canvas);
            obs_frontend_save();
        }
    }
    UpdateSceneSyncButton();
    RefreshScenes();
}

obs_scene_t *StreamHubVerticalWorkspace::SelectedScene() const
{
    if (selectedSceneUuid_.empty())
        return nullptr;
    struct Lookup { const std::string &uuid; obs_scene_t *scene = nullptr; } lookup{selectedSceneUuid_};
    auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
    obs_canvas_t *canvas = target ? obs_get_canvas_by_uuid(target->canvasUuid.c_str()) : nullptr;
    if (!canvas)
        return nullptr;
    obs_canvas_enum_scenes(canvas, [](void *data, obs_source_t *source) {
        auto &state = *static_cast<Lookup *>(data);
        if (state.uuid == obs_source_get_uuid(source)) {
            auto *scene = obs_scene_from_source(source);
            state.scene = scene ? obs_scene_get_ref(scene) : nullptr;
            return false;
        }
        return true;
    }, &lookup);
    obs_canvas_release(canvas);
    return lookup.scene;
}

void StreamHubVerticalWorkspace::RefreshScenes()
{
    if (!scenes_)
        return;
    const std::string preserve = selectedSceneUuid_;
    scenes_->blockSignals(true);
    scenes_->clear();
    auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
    obs_canvas_t *canvas = target ? obs_get_canvas_by_uuid(target->canvasUuid.c_str()) : nullptr;
    if (canvas) {
        struct SceneRow {
            obs_source_t *source;
            std::string uuid;
            std::string mainUuid;
            int canvasOrder;
            int savedOrder;
            int mainOrder;
            bool verticalOnly;
            bool hidden;
        };
        std::vector<SceneRow> sceneRows;
        obs_frontend_source_list mainScenes{};
        obs_frontend_get_scenes(&mainScenes);
        std::map<std::string, int> mainOrder;
        for (size_t i = 0; i < mainScenes.sources.num; ++i) {
            auto *source = mainScenes.sources.array[i];
            if (source && obs_scene_from_source(source))
                mainOrder.emplace(obs_source_get_uuid(source), static_cast<int>(i));
        }
        struct Rows { std::vector<SceneRow> *rows; const std::map<std::string, int> *mainOrder; } rows{&sceneRows, &mainOrder};
        obs_canvas_enum_scenes(canvas, [](void *data, obs_source_t *source) {
            auto &state = *static_cast<Rows *>(data);
            OBSDataAutoRelease settings = obs_source_get_private_settings(source);
            const bool verticalOnly = obs_data_get_bool(settings, "streamhub-vertical-only");
            const char *linkedUuid = obs_data_get_string(settings, "streamhub-main-scene");
            const auto linked = linkedUuid ? std::string(linkedUuid) : std::string{};
            const auto rank = state.mainOrder->find(linked);
            const bool hasSavedOrder = obs_data_has_user_value(settings, "streamhub-vertical-order");
            state.rows->push_back({obs_source_get_ref(source), obs_source_get_uuid(source), linked,
                                   static_cast<int>(state.rows->size()),
                                   hasSavedOrder ? static_cast<int>(obs_data_get_int(settings, "streamhub-vertical-order")) : -1,
                                   rank == state.mainOrder->end() ? -1 : rank->second,
                                   verticalOnly, obs_data_get_bool(settings, "streamhub-vertical-hidden")});
            return true;
        }, &rows);
        const bool follow = target && target->sceneMode == "follow";
        std::stable_sort(sceneRows.begin(), sceneRows.end(), [follow](const SceneRow &a, const SceneRow &b) {
            if (follow && a.verticalOnly != b.verticalOnly)
                return !a.verticalOnly;
            if (follow && !a.verticalOnly && a.mainOrder != b.mainOrder) {
                if (a.mainOrder < 0) return false;
                if (b.mainOrder < 0) return true;
                return a.mainOrder < b.mainOrder;
            }
            if (a.savedOrder >= 0 && b.savedOrder >= 0 && a.savedOrder != b.savedOrder)
                return a.savedOrder < b.savedOrder;
            if (a.savedOrder >= 0 && b.savedOrder < 0) return true;
            if (a.savedOrder < 0 && b.savedOrder >= 0) return false;
            if (!follow && a.mainOrder >= 0 && b.mainOrder >= 0 && a.mainOrder != b.mainOrder)
                return a.mainOrder < b.mainOrder;
            return a.canvasOrder < b.canvasOrder;
        });
        bool savedOrderChanged = false;
        for (size_t i = 0; i < sceneRows.size(); ++i) {
            auto &entry = sceneRows[i];
            OBSDataAutoRelease settings = obs_source_get_private_settings(entry.source);
            if (entry.savedOrder != static_cast<int>(i)) {
                obs_data_set_int(settings, "streamhub-vertical-order", static_cast<long long>(i));
                savedOrderChanged = true;
            }
            QString name = QString::fromUtf8(obs_source_get_name(entry.source));
            if (entry.verticalOnly) name += tr("  ·  só vertical");
            if (entry.hidden) name += tr("  ·  oculta");
            auto *row = new QListWidgetItem(name, scenes_);
            row->setData(Qt::UserRole, QString::fromStdString(entry.uuid));
            row->setData(Qt::UserRole + 1, entry.verticalOnly);
            row->setData(Qt::UserRole + 2, entry.hidden);
            row->setHidden(entry.hidden && !(showHiddenScenes_ && showHiddenScenes_->isChecked()));
            if (preserve == entry.uuid)
                scenes_->setCurrentItem(row);
            obs_source_release(entry.source);
        }
        obs_frontend_source_list_free(&mainScenes);
        if (savedOrderChanged)
            obs_frontend_save();
        obs_canvas_release(canvas);
    }
    if (scenes_->count() && !scenes_->currentItem())
        scenes_->setCurrentRow(0);
    scenes_->blockSignals(false);
    if (scenes_->currentItem()) {
        selectedSceneUuid_ = scenes_->currentItem()->data(Qt::UserRole).toString().toStdString();
        if (deleteSceneButton_)
            deleteSceneButton_->setEnabled(!target || target->sceneMode != "follow" || scenes_->currentItem()->data(Qt::UserRole + 1).toBool());
        SelectScene();
    } else {
        selectedSceneUuid_.clear();
        if (preview_) preview_->SetCanvasUuid(target ? target->canvasUuid : std::string{});
        if (sources_) sources_->clear();
        if (deleteSceneButton_) deleteSceneButton_->setEnabled(false);
        if (sceneFiltersButton_) sceneFiltersButton_->setEnabled(false);
        if (sceneUpButton_) sceneUpButton_->setEnabled(false);
        if (sceneDownButton_) sceneDownButton_->setEnabled(false);
    }
}

void StreamHubVerticalWorkspace::SelectScene()
{
    if (!scenes_ || !scenes_->currentItem())
        return;
    selectedSceneUuid_ = scenes_->currentItem()->data(Qt::UserRole).toString().toStdString();
    selectedSourceId_ = -1;
    auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
    if (preview_) {
        preview_->SetCanvasUuid(target ? target->canvasUuid : std::string{});
        preview_->SetSelectedItemId(-1);
    }
    obs_canvas_t *canvas = target ? obs_get_canvas_by_uuid(target->canvasUuid.c_str()) : nullptr;
    if (canvas) {
        OBSSourceAutoRelease scene;
        struct Lookup { const std::string &uuid; obs_source_t *source = nullptr; } lookup{selectedSceneUuid_};
        obs_canvas_enum_scenes(canvas, [](void *data, obs_source_t *candidate) {
            auto &state = *static_cast<Lookup *>(data);
            if (state.uuid == obs_source_get_uuid(candidate)) {
                state.source = obs_source_get_ref(candidate);
                return false;
            }
            return true;
        }, &lookup);
        scene = lookup.source;
        if (scene) {
            obs_canvas_set_channel(canvas, 0, scene);
            obs_frontend_save();
        }
        obs_canvas_release(canvas);
    }
    const bool verticalOnly = scenes_->currentItem()->data(Qt::UserRole + 1).toBool();
    const bool follow = target && target->sceneMode == "follow";
    if (deleteSceneButton_) deleteSceneButton_->setEnabled(!follow || verticalOnly);
    if (sceneFiltersButton_) sceneFiltersButton_->setEnabled(true);
    const int row = scenes_->currentRow();
    const bool canMoveUp = row > 0 && (!follow || (verticalOnly && scenes_->item(row - 1)->data(Qt::UserRole + 1).toBool()));
    const bool canMoveDown = row >= 0 && row + 1 < scenes_->count() &&
        (!follow || (verticalOnly && scenes_->item(row + 1)->data(Qt::UserRole + 1).toBool()));
    if (sceneUpButton_) sceneUpButton_->setEnabled(canMoveUp);
    if (sceneDownButton_) sceneDownButton_->setEnabled(canMoveDown);
    RefreshSources();
}

void StreamHubVerticalWorkspace::CreateScene(bool copyMain)
{
    const auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
    obs_canvas_t *canvas = target ? obs_get_canvas_by_uuid(target->canvasUuid.c_str()) : nullptr;
    if (!canvas)
        return;
    OBSSourceAutoRelease main = obs_frontend_get_current_scene();
    const QString defaultName = copyMain && main
        ? QString::fromUtf8("Vertical · ") + QString::fromUtf8(obs_source_get_name(main)) : QString();
    bool ok = false;
    const QString name = QInputDialog::getText(nullptr, tr("Cena vertical"), tr("Nome da cena"),
                                               QLineEdit::Normal, defaultName, &ok).trimmed();
    if (!ok || name.isEmpty()) {
        obs_canvas_release(canvas);
        return;
    }
    const auto before=StreamHubCanvasState(canvas);
    obs_scene_t *scene = copyMain && main && obs_scene_from_source(main)
        ? StreamHubCopyVerticalScene(obs_scene_from_source(main), canvas, name.toUtf8().constData())
        : obs_canvas_scene_create(canvas, name.toUtf8().constData());
    if (scene) {
        obs_source_t *source = obs_scene_get_source(scene);
        OBSDataAutoRelease settings = obs_source_get_private_settings(source);
        obs_data_set_bool(settings, "streamhub-vertical-only", true);
        obs_canvas_set_channel(canvas, 0, source);
        selectedSceneUuid_ = obs_source_get_uuid(source);
        obs_scene_release(scene);
        StreamHubRecordCanvasEdit(tr("Criar cena vertical"),before,canvas);
    }
    obs_canvas_release(canvas);
    RefreshScenes();
}

void StreamHubVerticalWorkspace::DeleteVerticalScene()
{
    if (!scenes_ || !scenes_->currentItem())
        return;
    const auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
    const bool verticalOnly = scenes_->currentItem()->data(Qt::UserRole + 1).toBool();
    if (target && target->sceneMode == "follow" && !verticalOnly)
        return;
    if (QMessageBox::question(nullptr, tr("Remover cena vertical"),
                              tr("Esta cena será removida somente do canvas vertical. As fontes originais e as cenas horizontais não serão apagadas. Continuar?")) != QMessageBox::Yes)
        return;
    obs_canvas_t *canvas = target ? obs_get_canvas_by_uuid(target->canvasUuid.c_str()) : nullptr;
    obs_scene_t *scene = SelectedScene();
    if (canvas && scene) {
        const auto before=StreamHubCanvasState(canvas);
        obs_canvas_scene_remove(scene);
        selectedSceneUuid_.clear();
        StreamHubRecordCanvasEdit(tr("Remover cena vertical"),before,canvas);
    }
    if (scene) obs_scene_release(scene);
    if (canvas) obs_canvas_release(canvas);
    RefreshScenes();
}

void StreamHubVerticalWorkspace::MoveSelectedScene(bool up)
{
    if (!scenes_ || !scenes_->currentItem()) return;
    const int from = scenes_->currentRow();
    const int to = from + (up ? -1 : 1);
    if (to < 0 || to >= scenes_->count()) return;
    const auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
    const bool follow = target && target->sceneMode == "follow";
    const bool verticalOnly = scenes_->item(from)->data(Qt::UserRole + 1).toBool();
    if (follow && (!verticalOnly || !scenes_->item(to)->data(Qt::UserRole + 1).toBool())) return;

    QListWidgetItem *item = scenes_->takeItem(from);
    scenes_->insertItem(to, item);
    scenes_->setCurrentItem(item);
    obs_canvas_t *canvas = target ? obs_get_canvas_by_uuid(target->canvasUuid.c_str()) : nullptr;
    if (canvas) {
        const auto before=StreamHubCanvasState(canvas);
        for (int i = 0; i < scenes_->count(); ++i) {
            const std::string uuid = scenes_->item(i)->data(Qt::UserRole).toString().toStdString();
            struct Lookup { const std::string &uuid; obs_source_t *source = nullptr; } lookup{uuid};
            obs_canvas_enum_scenes(canvas, [](void *data, obs_source_t *source) {
                auto &state = *static_cast<Lookup *>(data);
                if (state.uuid == obs_source_get_uuid(source)) {
                    state.source = obs_source_get_ref(source);
                    return false;
                }
                return true;
            }, &lookup);
            if (lookup.source) {
                OBSDataAutoRelease settings = obs_source_get_private_settings(lookup.source);
                obs_data_set_int(settings, "streamhub-vertical-order", i);
                obs_source_release(lookup.source);
            }
        }
        StreamHubRecordCanvasEdit(tr("Ordenar cenas verticais"),before,canvas);
        obs_canvas_release(canvas);
    }
    RefreshScenes();
}

void StreamHubVerticalWorkspace::SetSelectedSceneHidden(bool hidden)
{
    obs_scene_t *scene = SelectedScene();
    if (!scene) return;
    const auto before=StreamHubSceneState(scene);
    OBSDataAutoRelease settings = obs_source_get_private_settings(obs_scene_get_source(scene));
    obs_data_set_bool(settings, "streamhub-vertical-hidden", hidden);
    StreamHubRecordEdit(tr("Ocultar cena vertical"),before,scene);
    obs_scene_release(scene);
    obs_frontend_save();
    RefreshScenes();
}

void StreamHubVerticalWorkspace::ShowSceneContextMenu(const QPoint &position)
{
    if (!scenes_) return;
    if (auto *item = scenes_->itemAt(position))
        scenes_->setCurrentItem(item);
    QMenu menu(scenes_);
    menu.addAction(tr("Criar cena somente vertical…"), this, [this]() { CreateScene(false); });
    menu.addAction(tr("Copiar cena principal para o vertical…"), this, [this]() { CreateScene(true); });
    auto *item = scenes_->itemAt(position);
    if (!item) {
        menu.exec(scenes_->mapToGlobal(position));
        return;
    }
    menu.addSeparator();
    auto *renameAction=menu.addAction(tr("Renomear"),this,[this](){SceneCommand("rename");});
    const auto renameTarget=FindById(GlobalMultiOutputConfig().targets,targetId_);
    renameAction->setEnabled(!renameTarget||renameTarget->sceneMode!="follow"||item->data(Qt::UserRole+1).toBool());
    menu.addAction(tr("Duplicar cena vertical"),this,[this](){SceneCommand("duplicate");});
    menu.addAction(tr("Copiar filtros"),this,[this](){SceneCommand("copyFilters");});
    menu.addAction(tr("Colar filtros"),this,[this](){SceneCommand("pasteFilters");});
    menu.addAction(tr("Filtros"), this, [this]() { OpenSelectedFilters(true); });
    auto *hiddenAction = menu.addAction(item->data(Qt::UserRole + 2).toBool()
        ? tr("Exibir cena nesta lista") : tr("Ocultar cena desta lista"));
    connect(hiddenAction, &QAction::triggered, this, [this, item]() {
        SetSelectedSceneHidden(!item->data(Qt::UserRole + 2).toBool());
    });
    const auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
    const bool canRemove = !target || target->sceneMode != "follow" || item->data(Qt::UserRole + 1).toBool();
    auto *removeAction = menu.addAction(tr("Remover"), this, [this]() { DeleteVerticalScene(); });
    removeAction->setEnabled(canRemove);
    menu.addSeparator();
    auto *orderMenu = menu.addMenu(tr("Ordenar"));
    const int row = scenes_->row(item);
    const bool only = item->data(Qt::UserRole + 1).toBool();
    const bool follow = target && target->sceneMode == "follow";
    auto *up = orderMenu->addAction(tr("Mover para cima"), this, [this]() { MoveSelectedScene(true); });
    auto *down = orderMenu->addAction(tr("Mover para baixo"), this, [this]() { MoveSelectedScene(false); });
    up->setEnabled(row > 0 && (!follow || (only && scenes_->item(row - 1)->data(Qt::UserRole + 1).toBool())));
    down->setEnabled(row + 1 < scenes_->count() && (!follow || (only && scenes_->item(row + 1)->data(Qt::UserRole + 1).toBool())));
    menu.exec(scenes_->mapToGlobal(position));
}

void StreamHubVerticalWorkspace::SceneCommand(const QString &command)
{
    if(command=="remove"){DeleteVerticalScene();return;}
    auto *scene=SelectedScene();if(!scene)return;
    const auto target=FindById(GlobalMultiOutputConfig().targets,targetId_);
    auto *canvas=target?obs_get_canvas_by_uuid(target->canvasUuid.c_str()):nullptr;
    if(!canvas){obs_scene_release(scene);return;}
    auto *source=obs_scene_get_source(scene);
    const auto before=StreamHubCanvasState(canvas);
    if(command=="copyFilters") filterClipboard_=QString::fromUtf8(obs_source_get_uuid(source));
    else if(command=="pasteFilters") {
        OBSSourceAutoRelease copied=obs_get_source_by_uuid(filterClipboard_.toUtf8().constData());
        if(copied&&copied.Get()!=source)obs_source_copy_filters(source,copied);
    } else if(command=="rename") {
        OBSDataAutoRelease settings=obs_source_get_private_settings(source);
        if(!target||target->sceneMode!="follow"||obs_data_get_bool(settings,"streamhub-vertical-only")) {
            bool ok=false;const auto name=QInputDialog::getText(scenes_,tr("Renomear cena vertical"),tr("Nome"),QLineEdit::Normal,QString::fromUtf8(obs_source_get_name(source)),&ok).trimmed();
            if(ok&&!name.isEmpty()) {
                OBSSourceAutoRelease existing=obs_canvas_get_source_by_name(canvas,name.toUtf8().constData());
                if(!existing||existing.Get()==source)obs_source_set_name(source,name.toUtf8().constData());
                else QMessageBox::warning(scenes_,tr("Renomear"),tr("Já existe uma cena com esse nome."));
            }
        }
    } else if(command=="duplicate") {
        auto name=QString::fromUtf8(obs_source_get_name(source))+tr(" (cópia)");
        for(int n=2;;++n){OBSSourceAutoRelease existing=obs_canvas_get_source_by_name(canvas,name.toUtf8().constData());if(!existing)break;name=QString::fromUtf8(obs_source_get_name(source))+tr(" (cópia %1)").arg(n);}
        OBSSceneAutoRelease copy=obs_scene_duplicate(scene,name.toUtf8().constData(),OBS_SCENE_DUP_REFS);
        if(copy) {
            OBSDataAutoRelease previous=obs_source_get_private_settings(source);
            OBSDataAutoRelease privateData=obs_source_get_private_settings(obs_scene_get_source(copy));
            obs_data_apply(privateData,previous);obs_data_set_bool(privateData,"streamhub-vertical-only",true);
            obs_data_erase(privateData,"streamhub-main-scene");
            obs_source_copy_filters(obs_scene_get_source(copy),source);
            selectedSceneUuid_=obs_source_get_uuid(obs_scene_get_source(copy));
            obs_canvas_set_channel(canvas,0,obs_scene_get_source(copy));
        }
    }
    StreamHubRecordCanvasEdit(tr("Editar cena vertical"),before,canvas);
    obs_scene_release(scene);obs_canvas_release(canvas);RefreshScenes();
}
