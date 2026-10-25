#pragma once

#include <obs.h>
#include <string>

inline bool StreamHubNormalizeVerticalCanvasName(obs_canvas_t *canvas, const std::string &targetId)
{
    if (!canvas || obs_canvas_removed(canvas)) return false;
    const char *name = obs_canvas_get_name(canvas);
    if (!name) return false;
    const std::string legacyName = "StreamHub Vertical " + targetId;
    if (std::string(name) == "StreamHub") return true;
    if (std::string(name).rfind(legacyName, 0) != 0) return false;
    obs_canvas_set_name(canvas, "StreamHub");
    return true;
}
