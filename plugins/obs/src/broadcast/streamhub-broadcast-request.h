#pragma once

#include <QJsonObject>
#include <QString>

struct StreamHubBroadcastMetadata {
    QString title;
    QString description;
    QString categoryName;
    QString categoryId;
    QString kickCategoryId;
    QString youtubeCategoryName;
    QString youtubeCategoryId;
    QString tags;
    QString language;
    QString youtubeBroadcastId;
    QString classification;
    QString privacyStatus;
    QString sourcePlatform;
};

class StreamHubBroadcastRequest {
public:
    static QJsonObject BuildApplyPayload(const StreamHubBroadcastMetadata &metadata);
};
