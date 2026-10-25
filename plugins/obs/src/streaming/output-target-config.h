#pragma once
#include "output-config.h"
#include "json-util.hpp"

inline nlohmann::json SaveTarget(OutputTargetConfig& config) {
    nlohmann::json json;
    json["id"] = config.id;
    json["name"] = config.name;
    json["platform"] = config.platform;
    json["custom-icon"] = config.customIcon;
    json["custom-accent"] = config.customAccent;
    json["protocol"] = config.protocol;
    json["service-param"] = config.serviceParam;
    json["output-param"] = config.outputParam;
    json["sync-start"] = config.syncStart;
    json["start-with-parent"] = config.startWithParent;
    json["sync-stop"] = config.syncStop;
    json["enabled"] = config.enabled;
    json["manual-credentials"] = config.manualCredentials;
    json["orientation"] = config.orientation;
    json["video-bitrate-kbps"] = config.videoBitrateKbps;
    json["copied-encoding"] = config.copiedEncoding;
    json["canvas-uuid"] = config.canvasUuid;
    json["scene-mode"] = config.sceneMode;
    json["parent-target-id"] = config.parentTargetId;
    json["youtube-broadcast-id"] = config.youtubeBroadcastId;
    if (config.videoConfig.has_value())
        json["video-config"] = *config.videoConfig;
    if (config.audioConfig.has_value())
        json["audio-config"] = *config.audioConfig;
    return json;
}

inline OutputTargetConfigPtr LoadTargetConfig(nlohmann::json& json) {
    auto id = GetJsonField<std::string>(json, "id");
    if (!id.has_value())
        return {};

    auto config = std::make_shared<OutputTargetConfig>();
    config->id = *id;
    config->name = GetJsonField<std::string>(json, "name").value_or("");
    config->platform = GetJsonField<std::string>(json, "platform").value_or("custom");
    config->customIcon = GetJsonField<std::string>(json, "custom-icon").value_or("settings");
    config->customAccent = GetJsonField<std::string>(json, "custom-accent").value_or("#00C8FF");
    config->protocol = GetJsonField<std::string>(json, "protocol").value_or("RTMP"); // for compatibility
    config->syncStart = GetJsonField<bool>(json, "sync-start").value_or(false);
    config->startWithParent = GetJsonField<bool>(json, "start-with-parent").value_or(true);
    config->syncStop = GetJsonField<bool>(json, "sync-stop").value_or(config->syncStart);
    config->enabled = GetJsonField<bool>(json, "enabled").value_or(config->syncStart);
    config->manualCredentials = GetJsonField<bool>(json, "manual-credentials").value_or(false);
    config->orientation = GetJsonField<std::string>(json, "orientation").value_or("horizontal");
    config->videoBitrateKbps = GetJsonField<int>(json, "video-bitrate-kbps").value_or(0);
    config->copiedEncoding = GetJsonField<nlohmann::json>(json, "copied-encoding").value_or(nlohmann::json{});
    if (config->videoBitrateKbps < 0 || config->videoBitrateKbps > 51000)
        config->videoBitrateKbps = 0;
    config->canvasUuid = GetJsonField<std::string>(json, "canvas-uuid").value_or("");
    config->sceneMode = GetJsonField<std::string>(json, "scene-mode")
        .value_or(config->orientation == "vertical" ? "follow" : "independent");
    config->parentTargetId = GetJsonField<std::string>(json, "parent-target-id").value_or("");
    config->youtubeBroadcastId = GetJsonField<std::string>(json, "youtube-broadcast-id").value_or("");
    config->serviceParam = GetJsonField<nlohmann::json>(json, "service-param").value_or(nlohmann::json{});
    config->outputParam = GetJsonField<nlohmann::json>(json, "output-param").value_or(nlohmann::json{});
    config->videoConfig = GetJsonField<std::string>(json, "video-config");
    config->audioConfig = GetJsonField<std::string>(json, "audio-config");

    return config;
}
