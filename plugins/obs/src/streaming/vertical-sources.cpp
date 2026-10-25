

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

void StreamHubVerticalWorkspace::ShowSourceContextMenu(const QPoint &position)
{
    if (!sources_) return;
    auto *row = sources_->itemAt(position);
    if (!row) return;
    sources_->setCurrentItem(row);
    obs_scene_t *scene = SelectedScene();
    auto *item = scene ? StreamHubFindEditorItem(scene, row->data(Qt::UserRole).toLongLong()) : nullptr;
    if (!item) {
        if (scene) obs_scene_release(scene);
        return;
    }
    const bool visible = obs_sceneitem_visible(item);
    const bool locked = obs_sceneitem_locked(item);
    QMenu menu(sources_);
    menu.addAction(tr("Propriedades"), this, [this]() {
        auto *scene=SelectedScene();
        auto *item=scene && sources_->currentItem() ? StreamHubFindEditorItem(scene,sources_->currentItem()->data(Qt::UserRole).toLongLong()) : nullptr;
        if(item) obs_frontend_open_source_properties(obs_sceneitem_get_source(item));
        if(scene) obs_scene_release(scene);
    });
    menu.addAction(tr("Filtros"), this, [this]() { OpenSelectedFilters(false); });
    menu.addAction(tr("Transformar"), this, [this]() { EditSource(); });
    if(obs_sceneitem_is_group(item)) {
        OBSDataAutoRelease settings=obs_sceneitem_get_private_settings(item);
        const bool collapsed=obs_data_get_bool(settings,"streamhub-group-collapsed");
        menu.addAction(collapsed?tr("Expandir grupo"):tr("Recolher grupo"),this,[this](){SourceCommand("collapse");});
    }
    menu.addSeparator();
    const std::pair<const char *, const char *> commands[] = {
        {"Renomear", "rename"}, {"Duplicar como fonte independente", "duplicate"},
        {"Copiar", "copy"}, {"Colar referência", "paste"}, {"Colar cópia independente", "pasteDuplicate"},
        {"Agrupar selecionadas", "group"}, {"Desagrupar", "ungroup"},
        {"Copiar filtros", "copyFilters"}, {"Colar filtros", "pasteFilters"}};
    for(const auto &[label,command]:commands)
        menu.addAction(tr(label),this,[this,command](){SourceCommand(QString::fromLatin1(command));});
    auto *transformMenu=menu.addMenu(tr("Transformação"));
    const std::pair<const char *, const char *> transforms[] = {
        {"Redefinir", "reset"}, {"Ajustar à tela", "fit"}, {"Esticar à tela", "stretch"},
        {"Centralizar", "center"}, {"Centralizar horizontalmente", "centerH"}, {"Centralizar verticalmente", "centerV"},
        {"Inverter horizontalmente", "flipH"}, {"Inverter verticalmente", "flipV"},
        {"Girar 90° à esquerda", "rotateLeft"}, {"Girar 90° à direita", "rotateRight"}};
    for(const auto &[label,command]:transforms)
        transformMenu->addAction(tr(label),this,[this,command](){SourceCommand(QString::fromLatin1(command));});
    menu.addAction(visible ? tr("Ocultar fonte") : tr("Exibir fonte"), this, [this, row, visible]() { SetSourceVisible(row, !visible); });
    menu.addAction(locked ? tr("Desbloquear fonte") : tr("Bloquear fonte"), this, [this, row, locked]() { SetSourceLocked(row, !locked); });
    auto *order = menu.addMenu(tr("Ordenar"));
    order->addAction(tr("Subir"), this, [this]() { ReorderSelectedSource(true); });
    order->addAction(tr("Descer"), this, [this]() { ReorderSelectedSource(false); });
    menu.addSeparator();
    menu.addAction(tr("Remover da cena vertical"), this, [this]() { RemoveSelectedSource(); });
    obs_scene_release(scene);
    menu.exec(sources_->mapToGlobal(position));
}

void StreamHubVerticalWorkspace::OpenSelectedFilters(bool sceneFilters)
{
    obs_source_t *source = nullptr;
    if (sceneFilters) {
        obs_scene_t *scene = SelectedScene();
        if (scene) {
            source = obs_source_get_ref(obs_scene_get_source(scene));
            obs_scene_release(scene);
        }
    } else if (sources_ && sources_->currentItem()) {
        obs_scene_t *scene = SelectedScene();
        auto *item = scene ? StreamHubFindEditorItem(scene, sources_->currentItem()->data(Qt::UserRole).toLongLong()) : nullptr;
        if (item) source = obs_source_get_ref(obs_sceneitem_get_source(item));
        if (scene) obs_scene_release(scene);
    }
    if (source) {
        obs_frontend_open_source_filters(source);
        obs_source_release(source);
    }
}

void StreamHubVerticalWorkspace::RefreshSources()
{
    if (!sources_)
        return;
    const long long preserve = selectedSourceId_;
    sources_->blockSignals(true);
    sources_->clear();
    obs_scene_t *scene = SelectedScene();
    if (scene) {
        std::function<void(obs_scene_t *,int)> appendRows;
        appendRows=[&](obs_scene_t *current,int depth) {
        std::vector<obs_sceneitem_t *> items;
        obs_scene_enum_items(current,[](obs_scene_t *,obs_sceneitem_t *item,void *p){static_cast<std::vector<obs_sceneitem_t *> *>(p)->push_back(item);return true;},&items);
        for(auto it=items.rbegin();it!=items.rend();++it) {
            auto *item=*it;
            OBSDataAutoRelease sceneSettings = obs_source_get_private_settings(obs_scene_get_source(obs_sceneitem_get_scene(item)));
            OBSDataAutoRelease itemSettings = obs_sceneitem_get_private_settings(item);
            const bool composite=obs_data_get_bool(sceneSettings,"streamhub-follow-composite");
            const bool wrapper=obs_data_get_bool(itemSettings,"streamhub-follow-wrapper");
            const bool overlay=obs_data_get_bool(itemSettings,"streamhub-vertical-overlay") ||
                (!wrapper && !obs_data_has_user_value(itemSettings,"streamhub-independent-visible"));
            if(composite && !wrapper && !overlay) continue;
            if(!composite && wrapper) continue;
            if(overlay) obs_data_set_bool(itemSettings,"streamhub-vertical-overlay",true);
            obs_source_t *source = obs_sceneitem_get_source(item);
            auto *row = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(source)));
            sources_->addItem(row);
            row->setData(Qt::UserRole, qlonglong(StreamHubEditorKey(item)));
            row->setSizeHint(QSize(1, 26));
            const long long itemId = StreamHubEditorKey(item);
            const bool visible = obs_sceneitem_visible(item);
            const bool locked = obs_sceneitem_locked(item);
            auto *widget = new StreamHubVerticalSourceRow(
                sources_, row, StreamHubVerticalSourceIcon(source),
                QString(depth*3, QChar(' '))+(obs_sceneitem_is_group(item)?QString::fromUtf8(obs_data_get_bool(itemSettings,"streamhub-group-collapsed")?"▸ ":"▾ "):QString())+QString::fromUtf8(obs_source_get_name(source)), visible, locked,
                [this, row](bool checked) { SetSourceVisible(row, checked); },
                [this, row](bool checked) { SetSourceLocked(row, checked); });
            sources_->setItemWidget(row, widget);
            const bool selected=obs_sceneitem_selected(item)||preserve==itemId;
            obs_sceneitem_select(item,selected);
            row->setSelected(selected); widget->SetSelected(selected);
            if (preserve == itemId) sources_->setCurrentItem(row,QItemSelectionModel::NoUpdate);
            if(obs_sceneitem_is_group(item)&&!obs_data_get_bool(itemSettings,"streamhub-group-collapsed")) appendRows(obs_sceneitem_group_get_scene(item),depth+1);
        }
        };
        appendRows(scene,0);
        obs_scene_release(scene);
    }
    sources_->blockSignals(false);
    selectedSourceId_ = sources_->currentItem() && sources_->currentItem()->isSelected() ? sources_->currentItem()->data(Qt::UserRole).toLongLong() : -1;
    const bool selected = selectedSourceId_ >= 0;
    if (sourceFiltersButton_) sourceFiltersButton_->setEnabled(selected);
    if (editSourceButton_) editSourceButton_->setEnabled(selected);
    if (deleteSourceButton_) deleteSourceButton_->setEnabled(selected);
    if (moveUpButton_) moveUpButton_->setEnabled(selected && sources_->currentRow() > 0);
    if (moveDownButton_) moveDownButton_->setEnabled(selected && sources_->currentRow() + 1 < sources_->count());
    if (preview_) preview_->SetSelectedItemId(selectedSourceId_);
}

void StreamHubVerticalWorkspace::SetSourceVisible(QListWidgetItem *row, bool visible)
{
    if (!row)
        return;
    obs_scene_t *scene = SelectedScene();
    auto *item = scene ? StreamHubFindEditorItem(scene, row->data(Qt::UserRole).toLongLong()) : nullptr;
    if (item) {
        const auto before=StreamHubSceneState(scene);
        obs_sceneitem_set_visible(item, visible);
        StreamHubRecordEdit(tr("Visibilidade da fonte vertical"),before,scene);
    }
    if (scene) obs_scene_release(scene);
    if (preview_) preview_->update();
}

void StreamHubVerticalWorkspace::SetSourceLocked(QListWidgetItem *row, bool locked)
{
    if (!row)
        return;
    obs_scene_t *scene = SelectedScene();
    auto *item = scene ? StreamHubFindEditorItem(scene, row->data(Qt::UserRole).toLongLong()) : nullptr;
    if (item) {
        const auto before=StreamHubSceneState(scene);
        obs_sceneitem_set_locked(item, locked);
        StreamHubRecordEdit(tr("Bloqueio da fonte vertical"),before,scene);
    }
    if (scene) obs_scene_release(scene);
}

void StreamHubVerticalWorkspace::SourceCommand(const QString &command)
{
    auto *scene = SelectedScene();
    if (!scene) return;
    std::vector<obs_sceneitem_t *> selected;
    if (sources_) for (auto *row : sources_->selectedItems()) {
        auto *item = StreamHubFindEditorItem(scene, row->data(Qt::UserRole).toLongLong());
        if (item && !StreamHubSelectedAncestor(scene, item)) selected.push_back(item);
    }
    const auto before = StreamHubSceneState(scene);
    if (command == "copy" || command == "duplicate") {
        OBSDataAutoRelease data = obs_data_create();
        OBSDataArrayAutoRelease items = obs_data_array_create();
        for (auto *item : selected) obs_sceneitem_save(item, items);
        obs_data_set_array(data, "items", items); sourceClipboard_ = obs_data_get_json(data);
    }
    if (command == "paste" || command == "pasteDuplicate" || command == "duplicate") {
        OBSDataAutoRelease data = obs_data_create_from_json(sourceClipboard_.constData());
        OBSDataArrayAutoRelease items = obs_data_get_array(data, "items");
        StreamHubWalkItems(scene, [](obs_sceneitem_t *item, int) { obs_sceneitem_select(item, false); });
        for (size_t i=0; i<obs_data_array_count(items); ++i) {
            OBSDataAutoRelease saved = obs_data_array_item(items, i);
            if(obs_data_get_bool(saved,"group_item_backup"))continue;
            OBSSourceAutoRelease source = obs_get_source_by_uuid(obs_data_get_string(saved, "source_uuid"));
            if (!source) continue;
            if (command != "paste") {
                const auto name = StreamHubUniqueName(QString::fromUtf8(obs_source_get_name(source)) + tr(" (cópia)"));
                OBSSourceAutoRelease duplicate = StreamHubDuplicateSource(source, name.toUtf8().constData());
                if (!duplicate) continue;
                source = obs_source_get_ref(duplicate);
            }
            // Add a fresh item: saved item IDs belong to the original scene and cannot be reused.
            auto *added = obs_scene_add(scene, source);
            if (!added) continue;
            StreamHubApplySavedItemTransform(added,saved);
            OBSDataAutoRelease privateData=obs_sceneitem_get_private_settings(added);
            obs_data_set_bool(privateData,"streamhub-vertical-overlay",true);
            obs_sceneitem_select(added,true); selectedSourceId_=StreamHubEditorKey(added);
        }
    } else if (command == "group" && !selected.empty()) {
        auto *parent=obs_sceneitem_get_scene(selected.front());
        const bool sameParent=std::all_of(selected.begin(),selected.end(),[&](auto *item){return obs_sceneitem_get_scene(item)==parent;});
        if (sameParent) {
            const auto name=StreamHubUniqueName(tr("Grupo vertical"));
            if(auto *group=obs_scene_insert_group(parent,name.toUtf8().constData(),selected.data(),selected.size())) {
                OBSDataAutoRelease settings=obs_sceneitem_get_private_settings(group);
                obs_data_set_bool(settings,"streamhub-vertical-overlay",true);
                selectedSourceId_=StreamHubEditorKey(group); obs_sceneitem_select(group,true);
            }
        } else QMessageBox::information(sources_,tr("Agrupar"),tr("Selecione fontes que pertencem ao mesmo grupo."));
    } else if (command == "ungroup") {
        for(auto *item:selected) if(obs_sceneitem_is_group(item)&&!obs_sceneitem_locked(item)) obs_sceneitem_group_ungroup(item);
        selectedSourceId_=-1;
    } else if (command == "rename" && selected.size()==1) {
        auto *source=obs_sceneitem_get_source(selected.front()); bool ok=false;
        const auto name=QInputDialog::getText(sources_,tr("Renomear fonte"),tr("Nome (compartilhado por todas as referências)"),QLineEdit::Normal,QString::fromUtf8(obs_source_get_name(source)),&ok).trimmed();
        if(ok&&!name.isEmpty()) {
            OBSSourceAutoRelease existing=obs_get_source_by_name(name.toUtf8().constData());
            if(!existing||existing.Get()==source) obs_source_set_name(source,name.toUtf8().constData());
            else QMessageBox::warning(sources_,tr("Renomear"),tr("Já existe uma fonte com esse nome."));
        }
    } else if(command=="copyFilters" && selected.size()==1) {
        filterClipboard_=QString::fromUtf8(obs_source_get_uuid(obs_sceneitem_get_source(selected.front())));
    } else if(command=="pasteFilters") {
        OBSSourceAutoRelease source=obs_get_source_by_uuid(filterClipboard_.toUtf8().constData());
        if(source) for(auto *item:selected) if(obs_sceneitem_get_source(item)!=source.Get()) obs_source_copy_filters(obs_sceneitem_get_source(item),source);
    } else if(command!="copy" && command!="duplicate") {
        for(auto *item:selected) {
            if(command=="remove") { obs_sceneitem_remove(item); continue; }
            if(command=="collapse"&&obs_sceneitem_is_group(item)) {
                OBSDataAutoRelease settings=obs_sceneitem_get_private_settings(item);
                const bool collapsed=obs_data_get_bool(settings,"streamhub-group-collapsed");
                obs_data_set_bool(settings,"streamhub-group-collapsed",!collapsed);
                if(!collapsed)StreamHubWalkItems(obs_sceneitem_group_get_scene(item),[](obs_sceneitem_t *child,int){obs_sceneitem_select(child,false);});
                continue;
            }
            if(!StreamHubEditable(scene,item)) continue;
            obs_transform_info info{}; obs_sceneitem_get_info2(item,&info);
            if(command=="reset") {
                info={}; info.scale.x=info.scale.y=1; info.alignment=OBS_ALIGN_LEFT|OBS_ALIGN_TOP;
                obs_sceneitem_crop crop{}; obs_sceneitem_set_crop(item,&crop);
            } else if(command=="flipH") info.scale.x=-info.scale.x;
            else if(command=="flipV") info.scale.y=-info.scale.y;
            else if(command=="rotateLeft") info.rot-=90;
            else if(command=="rotateRight") info.rot+=90;
            else if(command=="fit"||command=="stretch") {
                info.pos={}; info.rot=0; info.alignment=OBS_ALIGN_LEFT|OBS_ALIGN_TOP;
                info.bounds_type=command=="fit"?OBS_BOUNDS_SCALE_INNER:OBS_BOUNDS_STRETCH;
                info.bounds.x=1080; info.bounds.y=1920; info.bounds_alignment=OBS_ALIGN_CENTER;
            } else if(command=="center"||command=="centerH"||command=="centerV") {
                auto box=StreamHubWorldBox(scene,item).mapRect(QRectF(0,0,1,1));
                const QPointF worldDelta(command=="centerV"?0:540-box.center().x(),command=="centerH"?0:960-box.center().y());
                const auto parent=StreamHubParentTransform(scene,item).inverted();
                const auto delta=parent.map(worldDelta)-parent.map(QPointF());
                info.pos.x+=float(delta.x()); info.pos.y+=float(delta.y());
            }
            obs_sceneitem_set_info2(item,&info);
        }
    }
    StreamHubRecordEdit(tr("Editar fontes verticais"),before,scene);
    obs_scene_release(scene); RefreshSources();
}

void StreamHubVerticalWorkspace::AddSource()
{
    obs_scene_t *scene = SelectedScene();
    if (!scene)
        return;
    QStringList names;
    obs_enum_sources([](void *data, obs_source_t *source) {
        if (obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT)
            static_cast<QStringList *>(data)->append(QString::fromUtf8(obs_source_get_name(source)));
        return true;
    }, &names);
    names.sort();names.prepend(tr("Criar nova fonte…"));
    bool ok = false;
    const QString name = QInputDialog::getItem(nullptr, tr("Adicionar fonte vertical"), tr("Nova fonte ou referência existente"), names, 0, false, &ok);
    if (ok) {
        const auto before=StreamHubSceneState(scene);
        OBSSourceAutoRelease source;
        const bool createNew=name==names.first();
        if(createNew) {
            QStringList types;std::map<QString,std::string> identifiers;const char *id=nullptr;
            for(size_t i=0;obs_enum_input_types(i,&id);++i) {
                if(obs_get_source_output_flags(id)&OBS_SOURCE_DEPRECATED)continue;
                const auto label=QString::fromUtf8(obs_source_get_display_name(id));
                types.append(label);identifiers[label]=id;
            }
            types.sort();
            const auto type=QInputDialog::getItem(sources_,tr("Nova fonte vertical"),tr("Tipo"),types,0,false,&ok);
            if(!ok){obs_scene_release(scene);return;}
            const auto newName=QInputDialog::getText(sources_,tr("Nova fonte vertical"),tr("Nome"),QLineEdit::Normal,StreamHubUniqueName(type+tr(" vertical")),&ok).trimmed();
            if(!ok||newName.isEmpty()){obs_scene_release(scene);return;}
            OBSSourceAutoRelease existing=obs_get_source_by_name(newName.toUtf8().constData());
            if(existing){QMessageBox::warning(sources_,tr("Nova fonte"),tr("Já existe uma fonte com esse nome."));obs_scene_release(scene);return;}
            source=obs_source_create(identifiers[type].c_str(),newName.toUtf8().constData(),nullptr,nullptr);
        } else {
            source=obs_get_source_by_name(name.toUtf8().constData());
            const auto choice=QMessageBox::question(sources_,tr("Fonte independente"),tr("Criar uma cópia independente?\nSim: propriedades separadas. Não: compartilhar a fonte existente."),QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
            if(choice==QMessageBox::Cancel) { obs_scene_release(scene); return; }
            if(source && choice==QMessageBox::Yes) {
                const auto copyName=StreamHubUniqueName(name+tr(" (vertical)"));
                source=StreamHubDuplicateSource(source,copyName.toUtf8().constData());
            }
        }
        auto *added=source ? obs_scene_add(scene,source) : nullptr;
        if (added) {
            OBSDataAutoRelease settings=obs_sceneitem_get_private_settings(added);
            obs_data_set_bool(settings,"streamhub-vertical-overlay",true);
            obs_sceneitem_set_locked(added,false);
            obs_sceneitem_set_visible(added,true);
            selectedSourceId_=StreamHubEditorKey(added);
            obs_sceneitem_select(added,true);
            blog(LOG_INFO,"[streamhub-editor] source added: target=%s scene=%s source=%s",targetId_.c_str(),obs_source_get_uuid(obs_scene_get_source(scene)),obs_source_get_uuid(source));
            StreamHubRecordEdit(tr("Adicionar fonte vertical"),before,scene);
            if(createNew)obs_frontend_open_source_properties(source);
        }
        else QMessageBox::warning(sources_,tr("Adicionar fonte"),tr("Esta fonte não pôde ser criada ou duplicada. Tente adicionar uma referência."));
    }
    obs_scene_release(scene);
    RefreshSources();
}

void StreamHubVerticalWorkspace::RemoveSelectedSource()
{
    if (!sources_ || !sources_->currentItem()) return;
    const QString sourceName = sources_->currentItem()->text();
    if (QMessageBox::question(nullptr, tr("Remover fonte vertical"),
                              tr("Remover '%1' somente desta cena vertical? A fonte e as outras cenas não serão apagadas.").arg(sourceName)) != QMessageBox::Yes)
        return;
    SourceCommand("remove");
}

void StreamHubVerticalWorkspace::EditSource()
{
    obs_scene_t *scene = SelectedScene();
    if (!scene || !sources_ || !sources_->currentItem()) {
        if (scene) obs_scene_release(scene);
        return;
    }
    auto *item = StreamHubFindEditorItem(scene, sources_->currentItem()->data(Qt::UserRole).toLongLong());
    if (item)
        obs_frontend_open_sceneitem_edit_transform(item);
    obs_scene_release(scene);
}

void StreamHubVerticalWorkspace::ReorderSelectedSource(bool up)
{
    if (!sources_ || !sources_->currentItem())
        return;
    obs_scene_t *scene = SelectedScene();
    auto *item = scene ? StreamHubFindEditorItem(scene, sources_->currentItem()->data(Qt::UserRole).toLongLong()) : nullptr;
    if (item) {
        const auto before=StreamHubSceneState(scene);
        obs_sceneitem_set_order(item, up ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
        StreamHubRecordEdit(tr("Ordenar fonte vertical"),before,scene);
    }
    if (scene) obs_scene_release(scene);
    RefreshSources();
}

void StreamHubVerticalWorkspace::SelectSourceItem(long long id)
{
    selectedSourceId_ = id;
    if (preview_) {
        preview_->SetSelectedItemId(id);
        preview_->update();
    }
}

void StreamHubVerticalWorkspace::NudgeSelectedSources(const QPointF &delta)
{
    auto *scene=SelectedScene();if(!scene)return;
    const auto before=StreamHubSceneState(scene);
    if(sources_)for(auto *row:sources_->selectedItems()){
        auto *item=StreamHubFindEditorItem(scene,row->data(Qt::UserRole).toLongLong());
        if(item)StreamHubNudgeItem(scene,item,delta);
    }
    StreamHubRecordEdit(tr("Mover fontes verticais"),before,scene);obs_scene_release(scene);
}

bool StreamHubVerticalWorkspace::MoveItem(long long id, const QPointF &position)
{
    obs_scene_t *scene = SelectedScene();
    auto *item = scene ? StreamHubFindEditorItem(scene, id) : nullptr;
    if (!item) {
        if (scene) obs_scene_release(scene);
        return false;
    }
    if (!StreamHubEditable(scene,item)) {
        obs_scene_release(scene);
        return false;
    }
    obs_transform_info info{};
    obs_sceneitem_get_info2(item, &info);
    info.pos.x = static_cast<float>(position.x());
    info.pos.y = static_cast<float>(position.y());
    obs_sceneitem_set_info2(item, &info);
    obs_scene_release(scene);
    return true;
}

bool StreamHubVerticalWorkspace::ResizeItem(long long id, const QPointF &scale)
{
    obs_scene_t *scene = SelectedScene();
    auto *item = scene ? StreamHubFindEditorItem(scene, id) : nullptr;
    if (!item) {
        if (scene) obs_scene_release(scene);
        return false;
    }
    if (obs_sceneitem_locked(item)) {
        obs_scene_release(scene);
        return false;
    }
    obs_transform_info info{};
    obs_sceneitem_get_info2(item, &info);
    if (info.bounds_type != OBS_BOUNDS_NONE) {
        const float factorX = info.scale.x == 0.0f ? 1.0f : static_cast<float>(scale.x()) / info.scale.x;
        const float factorY = info.scale.y == 0.0f ? 1.0f : static_cast<float>(scale.y()) / info.scale.y;
        info.bounds.x = static_cast<float>((std::max)(1.0, static_cast<double>(info.bounds.x * factorX)));
        info.bounds.y = static_cast<float>((std::max)(1.0, static_cast<double>(info.bounds.y * factorY)));
    } else {
        info.scale.x = static_cast<float>((std::max)(0.01, scale.x()));
        info.scale.y = static_cast<float>((std::max)(0.01, scale.y()));
    }
    obs_sceneitem_set_info2(item, &info);
    obs_scene_release(scene);
    return true;
}

QRectF StreamHubVerticalWorkspace::ItemBounds(long long id) const
{
    auto *scene = const_cast<StreamHubVerticalWorkspace *>(this)->SelectedScene();
    auto *item = scene ? StreamHubFindEditorItem(scene, id) : nullptr;
    QRectF bounds;
    if (item) {
        obs_transform_info info{};
        obs_sceneitem_get_info2(item, &info);
        obs_source_t *source = obs_sceneitem_get_source(item);
        obs_sceneitem_crop crop{};
        obs_sceneitem_get_crop(item, &crop);
        const float w = info.bounds_type == OBS_BOUNDS_NONE
            ? (std::max)(0.0f, float(obs_source_get_width(source)) - crop.left - crop.right) * std::abs(info.scale.x) : info.bounds.x;
        const float h = info.bounds_type == OBS_BOUNDS_NONE
            ? (std::max)(0.0f, float(obs_source_get_height(source)) - crop.top - crop.bottom) * std::abs(info.scale.y) : info.bounds.y;
        const float ax = (info.alignment & OBS_ALIGN_RIGHT) ? 1.0f : (info.alignment & OBS_ALIGN_LEFT) ? 0.0f : 0.5f;
        const float ay = (info.alignment & OBS_ALIGN_BOTTOM) ? 1.0f : (info.alignment & OBS_ALIGN_TOP) ? 0.0f : 0.5f;
        QTransform transform;
        transform.translate(info.pos.x, info.pos.y);
        transform.rotate(info.rot);
        transform.translate(-ax * w, -ay * h);
        bounds = StreamHubWorldBox(scene,item).mapRect(QRectF(0,0,1,1));
    }
    if (scene) obs_scene_release(scene);
    return bounds;
}
