#include "streamhub-platforms.h"

#include <algorithm>
#include <QColor>

namespace {
const QList<StreamHubPlatformPreset> kPresets = {
    {"twitch", "Twitch", ":/streamhub-ui/icons/twitch.svg", "#9146FF", "rtmp://live.twitch.tv/app"},
    {"kick", "Kick", ":/streamhub-ui/icons/kick.svg", "#53FC18", ""},
    {"youtube", "YouTube", ":/streamhub-ui/icons/youtube.svg", "#FF0033", "rtmp://a.rtmp.youtube.com/live2"},
    {"tiktok", "TikTok", ":/streamhub-ui/icons/tiktok.svg", "#00F2EA", ""},
    {"facebook", "Facebook", ":/streamhub-ui/icons/facebook.svg", "#1877F2", "rtmps://live-api-s.facebook.com:443/rtmp/"},
    {"custom", "RTMP personalizado", ":/streamhub-ui/icons/settings.svg", "#00C8FF", ""},
};
}

const QList<StreamHubPlatformPreset> &StreamHubPlatformPresets()
{
    return kPresets;
}

StreamHubPlatformPreset StreamHubPlatformForTarget(const OutputTargetConfig &target)
{
    QString platform = QString::fromStdString(target.platform).toLower();
    const QString name = QString::fromStdString(target.name).toLower();
    const QString server = QString::fromStdString(
        target.serviceParam.value("server", std::string{})).toLower();

    if (platform.isEmpty() || platform == "custom") {
        for (const auto &preset : kPresets) {
            if (preset.id == "custom")
                continue;
            if (name.contains(preset.id) || server.contains(preset.id) ||
                (preset.id == "youtube" && server.contains("youtu")) ||
                (preset.id == "facebook" && server.contains("facebook"))) {
                platform = preset.id;
                break;
            }
        }
    }

    for (const auto &preset : kPresets) {
        if (preset.id == platform) {
            if (platform != "custom")
                return preset;
            StreamHubPlatformPreset customized = preset;
            const QString icon = QString::fromStdString(target.customIcon);
            const QString accent = QString::fromStdString(target.customAccent);
            if (!icon.isEmpty())
                customized.iconPath = QString(":/streamhub-ui/icons/%1.svg").arg(icon);
            if (QColor::isValidColorName(accent))
                customized.accent = accent;
            customized.name = QString::fromStdString(target.name).trimmed();
            if (customized.name.isEmpty()) customized.name = preset.name;
            return customized;
        }
    }
    return kPresets.back();
}

void StreamHubApplyPlatformPreset(OutputTargetConfig &target, const QString &platformId)
{
    const auto it = std::find_if(kPresets.cbegin(), kPresets.cend(), [&](const auto &preset) {
        return preset.id == platformId;
    });
    const auto &preset = it == kPresets.cend() ? kPresets.back() : *it;

    target.platform = preset.id.toStdString();
    target.customIcon = "settings";
    target.customAccent = preset.accent.toStdString();
    target.name = preset.name.toStdString();
    target.protocol = "RTMP";
    target.serviceParam = nlohmann::json::object();
    target.serviceParam["server"] = preset.server.toStdString();
    target.serviceParam["key"] = "";
    target.outputParam = nlohmann::json::object();
}
