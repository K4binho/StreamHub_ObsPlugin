#include "output-target-config.h"
#include <stdexcept>

static void check(bool condition) { if (!condition) throw std::runtime_error("Output configuration persistence failed"); }

int main()
{
    OutputTargetConfig target;
    target.id = "vertical-test";
    target.enabled = false;
    target.syncStart = true;
    target.startWithParent = false;
    target.syncStop = false;
    target.manualCredentials = true;
    target.orientation = "vertical";
    target.sceneMode = "follow";
    target.videoBitrateKbps = 6000;
    target.copiedEncoding = {{"videoId", "fixture-encoder"}, {"video", {{"bitrate", 6000}}},
        {"audio", {{"bitrate", 160}}}, {"width", 1920}, {"height", 1080}};
    target.canvasUuid = "canvas-test";
    target.parentTargetId = "horizontal-test";
    target.youtubeBroadcastId = "selected-event";
    target.serviceParam = {{"server", "rtmps://ingest.example/live2"}, {"key", "fixture-key"}};
    auto encoded = SaveTarget(target);
    auto decoded = LoadTargetConfig(encoded);
    check(decoded && !decoded->enabled && decoded->syncStart && !decoded->syncStop);
    check(!decoded->startWithParent);
    check(decoded->orientation == "vertical" && decoded->canvasUuid == "canvas-test");
    check(decoded->sceneMode == "follow");
    check(decoded->parentTargetId == "horizontal-test" && decoded->youtubeBroadcastId == "selected-event");
    check(decoded->manualCredentials && decoded->serviceParam == target.serviceParam);
    check(decoded->videoBitrateKbps == 6000);
    check(decoded->copiedEncoding == target.copiedEncoding);
    target.enabled = true;
    encoded = SaveTarget(target);
    check(LoadTargetConfig(encoded)->enabled);
    nlohmann::json legacy = {{"id", "old"}, {"sync-start", true}};
    check(LoadTargetConfig(legacy)->enabled);
    check(LoadTargetConfig(legacy)->startWithParent);
    legacy["orientation"] = "vertical";
    check(LoadTargetConfig(legacy)->sceneMode == "follow");
    legacy["scene-mode"] = "independent";
    check(LoadTargetConfig(legacy)->sceneMode == "independent");
    legacy.erase("orientation");
    legacy.erase("scene-mode");
    check(LoadTargetConfig(legacy)->videoBitrateKbps == 0);
    legacy["video-bitrate-kbps"] = -1;
    check(LoadTargetConfig(legacy)->videoBitrateKbps == 0);
    legacy["sync-start"] = false;
    check(!LoadTargetConfig(legacy)->enabled);
    legacy["enabled"] = false;
    legacy["sync-start"] = true;
    check(!LoadTargetConfig(legacy)->enabled);
}
