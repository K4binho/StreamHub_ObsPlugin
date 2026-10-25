#pragma once
#include "obs.hpp"
#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <QTransform>
#include <QByteArray>
#include <QString>
#include <atomic>
#include <functional>
#include <vector>
#include <algorithm>
#include <string>

inline std::atomic<unsigned long> StreamHubEditorRevision{0};
inline long long StreamHubEditorKey(obs_sceneitem_t *item) { return static_cast<long long>(reinterpret_cast<uintptr_t>(item)); }
inline void StreamHubWalkItems(obs_scene_t *scene, const std::function<void(obs_sceneitem_t *, int)> &visit, int depth=0)
{
    struct Walk { const std::function<void(obs_sceneitem_t *,int)> &visit; int depth; } walk{visit,depth};
    obs_scene_enum_items(scene,[](obs_scene_t *,obs_sceneitem_t *item,void *data){
        auto &walk=*static_cast<Walk *>(data); walk.visit(item,walk.depth);
        if(obs_sceneitem_is_group(item)) StreamHubWalkItems(obs_sceneitem_group_get_scene(item),walk.visit,walk.depth+1);
        return true;
    },&walk);
}
inline obs_sceneitem_t *StreamHubFindEditorItem(obs_scene_t *scene,long long key)
{
    obs_sceneitem_t *result=nullptr;
    StreamHubWalkItems(scene,[&](obs_sceneitem_t *item,int){if(StreamHubEditorKey(item)==key) result=item;});
    return result;
}
inline QTransform StreamHubMatrix(const matrix4 &m) { return QTransform(m.x.x,m.x.y,m.y.x,m.y.y,m.t.x,m.t.y); }
inline QTransform StreamHubParentTransform(obs_scene_t *root,obs_sceneitem_t *item)
{
    auto *parent=obs_sceneitem_get_scene(item);
    if(parent==root) return {};
    obs_sceneitem_t *group=nullptr;
    StreamHubWalkItems(root,[&](obs_sceneitem_t *candidate,int){
        if(obs_sceneitem_is_group(candidate)&&obs_sceneitem_group_get_scene(candidate)==parent) group=candidate;
    });
    if(!group) return {};
    matrix4 matrix; obs_sceneitem_get_draw_transform(group,&matrix);
    return StreamHubMatrix(matrix)*StreamHubParentTransform(root,group);
}
inline QTransform StreamHubWorldBox(obs_scene_t *root,obs_sceneitem_t *item)
{
    matrix4 matrix; obs_sceneitem_get_box_transform(item,&matrix);
    return StreamHubMatrix(matrix)*StreamHubParentTransform(root,item);
}
inline bool StreamHubEditable(obs_scene_t *root,obs_sceneitem_t *item)
{
    if(!item||obs_sceneitem_locked(item)||!obs_sceneitem_visible(item)) return false;
    auto *parent=obs_sceneitem_get_scene(item);
    if(parent==root) return true;
    obs_sceneitem_t *group=nullptr;
    StreamHubWalkItems(root,[&](obs_sceneitem_t *candidate,int){
        if(obs_sceneitem_is_group(candidate)&&obs_sceneitem_group_get_scene(candidate)==parent) group=candidate;
    });
    return !group||StreamHubEditable(root,group);
}
inline bool StreamHubSelectedAncestor(obs_scene_t *root,obs_sceneitem_t *item)
{
    const auto *parent=obs_sceneitem_get_scene(item);
    bool selected=false;
    StreamHubWalkItems(root,[&](obs_sceneitem_t *candidate,int){
        if(obs_sceneitem_is_group(candidate)&&obs_sceneitem_group_get_scene(candidate)==parent)
            selected=obs_sceneitem_selected(candidate)||StreamHubSelectedAncestor(root,candidate);
    });
    return selected;
}
inline bool StreamHubNudgeItem(obs_scene_t *root,obs_sceneitem_t *item,const QPointF &delta)
{
    if(!StreamHubEditable(root,item)||StreamHubSelectedAncestor(root,item))return false;
    bool invertible=false;
    const auto parent=StreamHubParentTransform(root,item).inverted(&invertible);
    if(!invertible)return false;
    const auto local=parent.map(delta)-parent.map(QPointF());
    vec2 position;obs_sceneitem_get_pos(item,&position);
    position.x+=float(local.x());position.y+=float(local.y());obs_sceneitem_set_pos(item,&position);
    return true;
}
inline QByteArray StreamHubSceneState(obs_scene_t *scene)
{
    OBSDataAutoRelease state=obs_data_create();
    OBSDataArrayAutoRelease sources=obs_data_array_create();
    StreamHubWalkItems(scene,[&](obs_sceneitem_t *item,int){
        OBSDataAutoRelease saved=obs_save_source(obs_sceneitem_get_source(item));
        obs_data_set_bool(saved,"streamhub-history-group",obs_sceneitem_is_group(item));
        obs_data_array_push_back(sources,saved);
    });
    OBSDataAutoRelease root=obs_save_source(obs_scene_get_source(scene));
    obs_data_set_obj(state,"root",root); obs_data_set_array(state,"sources",sources);
    return QByteArray(obs_data_get_json(state));
}
inline void StreamHubRestoreSceneState(const char *json)
{
    OBSDataAutoRelease state=obs_data_create_from_json(json);
    OBSDataAutoRelease root=obs_data_get_obj(state,"root");
    OBSSourceAutoRelease source=obs_get_source_by_uuid(obs_data_get_string(root,"uuid"));
    if(!source) source=obs_load_source(root);
    if(!source) return;
    std::vector<OBSSource> keepAlive;
    auto *scene=obs_group_or_scene_from_source(source);
    if(!scene) return;
    StreamHubWalkItems(scene,[&](obs_sceneitem_t *item,int){keepAlive.emplace_back(obs_sceneitem_get_source(item));});
    OBSDataArrayAutoRelease array=obs_data_get_array(state,"sources");
    std::vector<OBSSource> groups;
    for(size_t i=0;i<obs_data_array_count(array);++i){
        OBSDataAutoRelease data=obs_data_array_item(array,i);
        OBSSourceAutoRelease existing=obs_get_source_by_uuid(obs_data_get_string(data,"uuid"));
        if(!existing) existing=obs_load_source(data);
        if(!existing) continue;
        keepAlive.emplace_back(existing.Get());
        const char *savedName=obs_data_get_string(data,"name");
        if(savedName && *savedName) obs_source_set_name(existing,savedName);
        OBSDataArrayAutoRelease filters=obs_data_get_array(data,"filters");
        if(!filters) filters=obs_data_array_create();
        obs_source_restore_filters(existing,filters);
        if(obs_data_get_bool(data,"streamhub-history-group")) {
            OBSDataAutoRelease settings=obs_data_get_obj(data,"settings"); obs_source_update(existing,settings); groups.emplace_back(existing.Get());
        }
    }
    for(auto it=groups.rbegin();it!=groups.rend();++it) obs_source_load2(*it);
    OBSDataAutoRelease settings=obs_data_get_obj(root,"settings"); obs_source_update(source,settings); obs_source_load2(source);
    obs_source_set_name(source,obs_data_get_string(root,"name"));
    OBSDataArrayAutoRelease filters=obs_data_get_array(root,"filters");
    if(!filters)filters=obs_data_array_create();
    obs_source_restore_filters(source,filters);
    OBSDataAutoRelease savedPrivate=obs_data_get_obj(root,"private_settings");
    OBSDataAutoRelease currentPrivate=obs_source_get_private_settings(source); obs_data_clear(currentPrivate);obs_data_apply(currentPrivate,savedPrivate);
    obs_frontend_save(); ++StreamHubEditorRevision;
}
inline QByteArray StreamHubCanvasState(obs_canvas_t *canvas)
{
    OBSDataAutoRelease state=obs_data_create();
    OBSDataArrayAutoRelease scenes=obs_data_array_create();
    obs_canvas_enum_scenes(canvas,[](void *p,obs_source_t *source){
        if(auto *scene=obs_scene_from_source(source)) {
            const auto saved=StreamHubSceneState(scene);
            OBSDataAutoRelease data=obs_data_create_from_json(saved.constData());
            obs_data_array_push_back(static_cast<obs_data_array_t *>(p),data);
        }
        return true;
    },scenes.Get());
    obs_data_set_string(state,"canvas",obs_canvas_get_uuid(canvas)); obs_data_set_array(state,"scenes",scenes);
    OBSSourceAutoRelease active=obs_canvas_get_channel(canvas,0);
    if(active) obs_data_set_string(state,"active",obs_source_get_uuid(active));
    return obs_data_get_json(state);
}
inline void StreamHubRestoreCanvasState(const char *json)
{
    OBSDataAutoRelease state=obs_data_create_from_json(json);
    auto *canvas=obs_get_canvas_by_uuid(obs_data_get_string(state,"canvas")); if(!canvas)return;
    OBSDataArrayAutoRelease saved=obs_data_get_array(state,"scenes");
    std::vector<std::string> uuids;
    for(size_t i=0;i<obs_data_array_count(saved);++i){
        OBSDataAutoRelease data=obs_data_array_item(saved,i); OBSDataAutoRelease root=obs_data_get_obj(data,"root");
        uuids.emplace_back(obs_data_get_string(root,"uuid")); StreamHubRestoreSceneState(obs_data_get_json(data));
    }
    std::vector<OBSSource> remove;
    struct Removed {std::vector<std::string> &uuids;std::vector<OBSSource> &remove;} removed{uuids,remove};
    obs_canvas_enum_scenes(canvas,[](void *p,obs_source_t *source){
        auto &r=*static_cast<Removed *>(p);
        if(std::find(r.uuids.begin(),r.uuids.end(),obs_source_get_uuid(source))==r.uuids.end())r.remove.emplace_back(source);
        return true;
    },&removed);
    for(auto &source:remove)obs_canvas_scene_remove(obs_scene_from_source(source));
    OBSSourceAutoRelease active=obs_get_source_by_uuid(obs_data_get_string(state,"active"));
    if(active)obs_canvas_set_channel(canvas,0,active);
    obs_canvas_release(canvas);obs_frontend_save();++StreamHubEditorRevision;
}
inline void StreamHubRecordCanvasEdit(const QString &label,const QByteArray &before,obs_canvas_t *canvas)
{
    const auto after=StreamHubCanvasState(canvas);
    if(before!=after)obs_frontend_add_undo_redo_action(label.toUtf8().constData(),StreamHubRestoreCanvasState,StreamHubRestoreCanvasState,before.constData(),after.constData(),false);
    obs_frontend_save();
}
inline void StreamHubRecordEdit(const QString &label,const QByteArray &before,obs_scene_t *scene)
{
    const auto after=StreamHubSceneState(scene);
    if(!before.isEmpty()&&before!=after)
        obs_frontend_add_undo_redo_action(label.toUtf8().constData(),StreamHubRestoreSceneState,StreamHubRestoreSceneState,before.constData(),after.constData(),false);
    obs_frontend_save();
}
inline QString StreamHubUniqueName(const QString &base)
{
    QString name=base;
    for(int i=2;;++i){OBSSourceAutoRelease source=obs_get_source_by_name(name.toUtf8().constData());if(!source)return name;name=base+" "+QString::number(i);}
}
// libobs deliberately returns the original reference when duplicating a public scene.
// Explicit scene duplication is required for a genuinely independent copy.
inline obs_source_t *StreamHubDuplicateSource(obs_source_t *source,const char *name)
{
    if(auto *scene=obs_group_or_scene_from_source(source)) {
        OBSSceneAutoRelease copy=obs_scene_duplicate(scene,name,OBS_SCENE_DUP_COPY);
        return copy?obs_source_get_ref(obs_scene_get_source(copy)):nullptr;
    }
    auto *copy=obs_source_duplicate(source,name,false);
    if(copy==source){obs_source_release(copy);return nullptr;}
    return copy;
}
inline void StreamHubApplySavedItemTransform(obs_sceneitem_t *item,obs_data_t *saved)
{
    obs_transform_info info{};
    obs_data_get_vec2(saved,"pos",&info.pos);obs_data_get_vec2(saved,"scale",&info.scale);
    obs_data_get_vec2(saved,"bounds",&info.bounds);info.rot=float(obs_data_get_double(saved,"rot"));
    info.alignment=uint32_t(obs_data_get_int(saved,"align"));info.bounds_type=obs_bounds_type(obs_data_get_int(saved,"bounds_type"));
    info.bounds_alignment=uint32_t(obs_data_get_int(saved,"bounds_align"));info.crop_to_bounds=obs_data_get_bool(saved,"bounds_crop");
    obs_sceneitem_crop crop{int(obs_data_get_int(saved,"crop_left")),int(obs_data_get_int(saved,"crop_top")),int(obs_data_get_int(saved,"crop_right")),int(obs_data_get_int(saved,"crop_bottom"))};
    obs_sceneitem_defer_update_begin(item);obs_sceneitem_set_info2(item,&info);obs_sceneitem_set_crop(item,&crop);obs_sceneitem_defer_update_end(item);
    obs_sceneitem_set_visible(item,obs_data_get_bool(saved,"visible"));obs_sceneitem_set_locked(item,obs_data_get_bool(saved,"locked"));
}
