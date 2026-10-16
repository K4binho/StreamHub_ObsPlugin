#pragma once

#include <QList>
#include <QString>

#include "output-config.h"

struct StreamHubPlatformPreset {
    QString id;
    QString name;
    QString iconPath;
    QString accent;
    QString server;
};

const QList<StreamHubPlatformPreset> &StreamHubPlatformPresets();
StreamHubPlatformPreset StreamHubPlatformForTarget(const OutputTargetConfig &target);
void StreamHubApplyPlatformPreset(OutputTargetConfig &target, const QString &platformId);
