#pragma once

#include "output-config.h"
#include "obs.hpp"
#include "vertical-canvas-name.h"
#include <obs-frontend-api.h>
#include <vector>
#include <cmath>

inline bool StreamHubInitializeVerticalVideo(obs_canvas_t *canvas)
{
    obs_video_info info{};
    if (obs_canvas_get_video_info(canvas, &info) && info.base_width && info.base_height)
        return true;
    if (!obs_get_video_info(&info)) return false;
    info.base_width = info.output_width = 1080;
    info.base_height = info.output_height = 1920;
    return obs_canvas_reset_video(canvas, &info);
}

inline obs_canvas_t *StreamHubEnsureVerticalCanvas(const OutputTargetConfig &target, bool &created)
{
    created = false;
    const std::string managedName = "StreamHub";

    if (!target.canvasUuid.empty()) {
        obs_canvas_t *canvas = obs_get_canvas_by_uuid(target.canvasUuid.c_str());
        if (canvas) {
            obs_video_info info{};
            const bool managed = StreamHubNormalizeVerticalCanvasName(canvas, target.id);
            if (!obs_canvas_removed(canvas) && managed && obs_canvas_get_video_info(canvas, &info) &&
                info.output_width == 1080 && info.output_height == 1920)
                return canvas;
            obs_canvas_release(canvas);
        }
    }

    obs_video_info info{};
    if (!obs_get_video_info(&info))
        return nullptr;

    info.base_width = 1080;
    info.base_height = 1920;
    info.output_width = 1080;
    info.output_height = 1920;
    obs_canvas_t *canvas = obs_frontend_add_canvas(managedName.c_str(), &info, PROGRAM);
    created = canvas != nullptr;
    return canvas;
}

inline void StreamHubRemoveCreatedVerticalCanvas(obs_canvas_t *canvas)
{
    if (!canvas)
        return;
    obs_frontend_remove_canvas(canvas);
    obs_canvas_release(canvas);
}

inline obs_scene_t *StreamHubCopyVerticalScene(obs_scene_t *original, obs_canvas_t *canvas, const char *name)
{
    obs_scene_t *copy = obs_canvas_scene_create(canvas, name);
    if (!copy)
        return nullptr;

    obs_scene_enum_items(original, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
        auto *created = obs_scene_add(static_cast<obs_scene_t *>(data), obs_sceneitem_get_source(item));
        if (!created)
            return true;
        obs_transform_info transform{};
        obs_sceneitem_crop crop{};
        obs_sceneitem_get_info2(item, &transform);
        obs_sceneitem_get_crop(item, &crop);
        obs_sceneitem_set_info2(created, &transform);
        obs_sceneitem_set_crop(created, &crop);
        obs_sceneitem_set_visible(created, obs_sceneitem_visible(item));
        return true;
    }, copy);
    return copy;
}

inline bool StreamHubSetCompositeScene(obs_scene_t *scene, obs_source_t *main, bool enabled)
{
    OBSDataAutoRelease state = obs_source_get_private_settings(obs_scene_get_source(scene));
    if (obs_data_get_bool(state, "streamhub-follow-composite") == enabled) return false;
    std::vector<obs_sceneitem_t *> items;
    obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
        obs_sceneitem_addref(item);
        static_cast<std::vector<obs_sceneitem_t *> *>(data)->push_back(item);
        return true;
    }, &items);
    bool hasWrapper = false;
    for (auto *item : items) {
        OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
        const bool wrapper = obs_data_get_bool(settings, "streamhub-follow-wrapper");
        hasWrapper = hasWrapper || wrapper;
        if (wrapper) {
            obs_sceneitem_set_visible(item, enabled);
        } else if (enabled && !obs_data_get_bool(settings,"streamhub-vertical-overlay")) {
            obs_data_set_bool(settings, "streamhub-independent-visible", obs_sceneitem_visible(item));
            obs_sceneitem_set_visible(item, false);
        } else if (obs_data_has_user_value(settings, "streamhub-independent-visible")) {
            obs_sceneitem_set_visible(item, obs_data_get_bool(settings, "streamhub-independent-visible"));
        }
        obs_sceneitem_release(item);
    }
    if (enabled && !hasWrapper && main) {
        auto *wrapper = obs_scene_add(scene, main);
        if (!wrapper) return false;
        OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(wrapper);
        obs_data_set_bool(settings, "streamhub-follow-wrapper", true);
        obs_transform_info info{};
        info.alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
        const float width = static_cast<float>((std::max)(obs_source_get_width(main), 1u));
        info.scale.x = info.scale.y = 1080.0f / width;
        info.pos.y = (1920.0f - obs_source_get_height(main) * info.scale.y) / 2.0f;
        obs_sceneitem_set_info2(wrapper, &info);
        obs_sceneitem_set_locked(wrapper, false);
    }
    obs_data_set_bool(state, "streamhub-follow-composite", enabled);
    return true;
}

inline void StreamHubFollowVertical(const OutputTargetConfig &target, bool activateMainScene = true)
{
    if (target.orientation != "vertical" || target.sceneMode != "follow")
        return;
    obs_canvas_t *canvas = obs_get_canvas_by_uuid(target.canvasUuid.c_str());
    if (!canvas)
        return;
    if (!StreamHubInitializeVerticalVideo(canvas)) {
        obs_canvas_release(canvas);
        return;
    }
    struct SceneEntry { obs_source_t *source; std::string uuid; std::string name; };
    obs_frontend_source_list mainScenes{};
    obs_frontend_get_scenes(&mainScenes);
    std::vector<SceneEntry> mainEntries;
    mainEntries.reserve(mainScenes.sources.num);
    for (size_t i = 0; i < mainScenes.sources.num; ++i) {
        auto *source = mainScenes.sources.array[i];
        if (source && obs_scene_from_source(source))
            mainEntries.push_back({source, obs_source_get_uuid(source), obs_source_get_name(source)});
    }
    if (mainEntries.empty()) {
        obs_frontend_source_list_free(&mainScenes);
        obs_canvas_release(canvas);
        return;
    }

    const std::string activeUuid = [&]() {
        OBSSourceAutoRelease active = obs_frontend_get_current_scene();
        return active ? std::string(obs_source_get_uuid(active)) : std::string{};
    }();
    bool changed = false;
    std::string verticalActiveUuid;
    for (const auto &entry : mainEntries) {
        struct Match { const std::string &mainUuid; obs_source_t *source = nullptr; } match{entry.uuid};
        obs_canvas_enum_scenes(canvas, [](void *data, obs_source_t *source) {
            auto &state = *static_cast<Match *>(data);
            OBSDataAutoRelease settings = obs_source_get_private_settings(source);
            if (state.mainUuid == obs_data_get_string(settings, "streamhub-main-scene")) {
                state.source = obs_source_get_ref(source);
                return false;
            }
            return true;
        }, &match);

        obs_scene_t *created = nullptr;
        if (!match.source) {
            created = StreamHubCopyVerticalScene(obs_scene_from_source(entry.source), canvas, entry.name.c_str());
            if (created) {
                match.source = obs_source_get_ref(obs_scene_get_source(created));
                OBSDataAutoRelease settings = obs_source_get_private_settings(match.source);
                obs_data_set_string(settings, "streamhub-main-scene", entry.uuid.c_str());
                obs_data_set_bool(settings, "streamhub-vertical-only", false);
                changed = true;
            }
        }
        if (match.source) {
            // Items created before the canvas had dimensions retain a zero scale reference.
            auto *verticalScene = obs_scene_from_source(match.source);
            struct Repair { obs_scene_t *main; std::vector<obs_sceneitem_t *> broken; } repair{obs_scene_from_source(entry.source)};
            obs_scene_enum_items(verticalScene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
                obs_transform_info info{};
                obs_sceneitem_get_info2(item, &info);
                if (!std::isfinite(info.scale.x) || !std::isfinite(info.scale.y) ||
                    (info.scale.x == 0 && info.scale.y == 0)) {
                    obs_sceneitem_addref(item);
                    static_cast<Repair *>(data)->broken.push_back(item);
                }
                return true;
            }, &repair);
            for (auto *broken : repair.broken) {
                struct Original { obs_source_t *source; obs_sceneitem_t *item = nullptr; } original{obs_sceneitem_get_source(broken)};
                obs_scene_enum_items(repair.main, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
                    auto &state = *static_cast<Original *>(data);
                    if (obs_sceneitem_get_source(item) == state.source) { state.item = item; return false; }
                    return true;
                }, &original);
                if (original.item) {
                    auto *replacement = obs_scene_add(verticalScene, original.source);
                    if (replacement) {
                        obs_transform_info info{};
                        obs_sceneitem_get_info2(original.item, &info);
                        obs_sceneitem_set_info2(replacement, &info);
                        obs_sceneitem_crop crop{};
                        obs_sceneitem_get_crop(broken, &crop);
                        obs_sceneitem_set_crop(replacement, &crop);
                        obs_sceneitem_set_visible(replacement, obs_sceneitem_visible(broken));
                        obs_sceneitem_set_locked(replacement, obs_sceneitem_locked(broken));
                        obs_sceneitem_set_order_position(replacement, obs_sceneitem_get_order_position(broken));
                        obs_sceneitem_remove(broken);
                        changed = true;
                    }
                }
                obs_sceneitem_release(broken);
            }
            changed = StreamHubSetCompositeScene(verticalScene, entry.source, true) || changed;
            if (std::string(obs_source_get_name(match.source)) != entry.name) {
                obs_source_set_name(match.source, entry.name.c_str());
                changed = true;
            }
            if (entry.uuid == activeUuid)
                verticalActiveUuid = obs_source_get_uuid(match.source);
            obs_source_release(match.source);
        }
        if (created)
            obs_scene_release(created);
    }

    struct StaleScenes { const std::vector<SceneEntry> &main; std::vector<obs_scene_t *> stale; bool changed = false; } stale{mainEntries};
    obs_canvas_enum_scenes(canvas, [](void *data, obs_source_t *source) {
        auto &state = *static_cast<StaleScenes *>(data);
        OBSDataAutoRelease settings = obs_source_get_private_settings(source);
        const char *linkedUuid = obs_data_get_string(settings, "streamhub-main-scene");
        if (!linkedUuid || !*linkedUuid) {
            if (!obs_data_get_bool(settings, "streamhub-vertical-only")) {
                obs_data_set_bool(settings, "streamhub-vertical-only", true);
                state.changed = true;
            }
            return true;
        }
        const bool exists = std::any_of(state.main.begin(), state.main.end(), [linkedUuid](const SceneEntry &entry) {
            return entry.uuid == linkedUuid;
        });
        if (!exists) {
            if (auto *scene = obs_scene_from_source(source))
                state.stale.push_back(obs_scene_get_ref(scene));
        }
        return true;
    }, &stale);
    for (auto *scene : stale.stale) {
        obs_canvas_scene_remove(scene);
        obs_scene_release(scene);
        changed = true;
    }
    changed = changed || stale.changed;
    OBSSourceAutoRelease canvasActive = obs_canvas_get_channel(canvas, 0);
    bool preserveVerticalOnly = false;
    if (canvasActive) {
        OBSDataAutoRelease settings = obs_source_get_private_settings(canvasActive);
        preserveVerticalOnly = obs_data_get_bool(settings, "streamhub-vertical-only");
    }
    if (!verticalActiveUuid.empty() && (activateMainScene || !preserveVerticalOnly)) {
        struct Lookup { const std::string &uuid; obs_source_t *source = nullptr; } lookup{verticalActiveUuid};
        obs_canvas_enum_scenes(canvas, [](void *data, obs_source_t *source) {
            auto &state = *static_cast<Lookup *>(data);
            if (state.uuid == obs_source_get_uuid(source)) {
                state.source = obs_source_get_ref(source);
                return false;
            }
            return true;
        }, &lookup);
        if (lookup.source) {
            obs_canvas_set_channel(canvas, 0, lookup.source);
            obs_source_release(lookup.source);
        }
    }
    obs_frontend_source_list_free(&mainScenes);
    if (changed)
        obs_frontend_save();
    obs_canvas_release(canvas);
}
