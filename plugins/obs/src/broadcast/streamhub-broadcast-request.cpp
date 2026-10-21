#include "streamhub-broadcast-request.h"

#include <QJsonArray>
#include <QStringList>

namespace {
QJsonArray BuildTags(const QString &value)
{
    QJsonArray tags;
    for (const QString &tag : value.split(',', Qt::SkipEmptyParts)) {
        const QString trimmed = tag.trimmed();
        if (!trimmed.isEmpty())
            tags.append(trimmed);
    }
    return tags;
}
}

QJsonObject StreamHubBroadcastRequest::BuildApplyPayload(const StreamHubBroadcastMetadata &metadata)
{
    const QJsonArray tags = BuildTags(metadata.tags);
    const QJsonObject globalMetadata{
        {"title", metadata.title},
    };
    const QJsonObject twitchMetadata{
        {"enabled", true},
        {"gameName", metadata.categoryName},
        {"gameId", metadata.categoryId},
        {"tags", tags},
        {"language", metadata.language},
        {"contentClassificationIds", metadata.classification.isEmpty()
             ? QJsonArray{} : QJsonArray{metadata.classification}},
    };
    const QJsonObject kickMetadata{
        {"enabled", true},
        {"categoryName", metadata.categoryName},
        {"categoryId", metadata.kickCategoryId},
    };
    const QJsonObject youtubeMetadata{
        {"enabled", true},
        {"broadcastId", metadata.youtubeBroadcastId},
        {"description", metadata.description},
        {"privacyStatus", metadata.privacyStatus},
        {"categoryName", metadata.youtubeCategoryName},
        {"categoryId", metadata.youtubeCategoryId},
        {"tags", tags},
        {"language", metadata.language},
        {"madeForKids", false},
        {"selfDeclaredMadeForKids", false},
    };

    return QJsonObject{
        {"global_metadata", globalMetadata},
        {"platforms", QJsonObject{
            {"twitch", twitchMetadata},
            {"kick", kickMetadata},
            {"youtube", youtubeMetadata},
        }},
        {"title", metadata.title},
        {"sourcePlatform", metadata.sourcePlatform},
    };
}
