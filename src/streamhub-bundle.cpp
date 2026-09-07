#include "streamhub-bundle.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "obs.h"

#include "plugin-support.h"

namespace {

// Suba este número sempre que qrc/streamhub-data.qrc mudar de conteúdo
// (novo arquivo, JS corrigido, etc.) para forçar reextração na próxima
// abertura do OBS. Não precisa acompanhar PLUGIN_VERSION.
constexpr const char *kBundleVersion = "10";

constexpr const char *kResourcePrefix = ":/streamhub-data";
constexpr const char *kVersionMarkerName = ".streamhub-bundle-version";

// Caminho, relativo à raiz do bundle, que nunca deve ser sobrescrito
// automaticamente — é o arquivo com os dados/canais do próprio usuário.
constexpr const char *kUserConfigRelPath = "streamhub-server/config.json";

bool ExtractResourceTree(const QString &destRoot)
{
    const QString prefix = QString::fromLatin1(kResourcePrefix);
    QDirIterator it(prefix, QDir::Files, QDirIterator::Subdirectories);

    bool ok = true;
    int extracted = 0;

    while (it.hasNext()) {
        const QString srcPath = it.next();
        const QString relPath = srcPath.mid(prefix.length() + 1); // sem a barra inicial

        if (relPath == QString::fromLatin1(kUserConfigRelPath)) {
            continue; // nunca mexe na configuração do usuário
        }

        const QString destPath = destRoot + QLatin1Char('/') + relPath;

        if (!QDir().mkpath(QFileInfo(destPath).absolutePath())) {
            blog(LOG_WARNING, "[streamhub] não consegui criar pasta para %s", relPath.toUtf8().constData());
            ok = false;
            continue;
        }

        // QFile::copy() recusa sobrescrever; como este arquivo vem de um
        // recurso somente-leitura embutido na DLL, remover o antigo antes
        // é seguro (não é o config.json do usuário, esse já foi pulado).
        QFile::remove(destPath);

        if (!QFile::copy(srcPath, destPath)) {
            blog(LOG_WARNING, "[streamhub] falha ao extrair %s", relPath.toUtf8().constData());
            ok = false;
            continue;
        }

        // Recursos Qt são somente-leitura; o servidor Node precisa poder
        // regravar alguns desses arquivos (ex.: dashboard antigo) e o
        // usuário precisa poder apagá-los manualmente se quiser.
        QFile::setPermissions(destPath, QFile::permissions(destPath) | QFileDevice::WriteOwner);
        ++extracted;
    }

    blog(LOG_INFO, "[streamhub] %d arquivo(s) extraído(s) para %s", extracted, destRoot.toUtf8().constData());
    return ok;
}

bool EnsureUserConfig(const QString &dataPath)
{
    const QString serverDir = dataPath + "/streamhub-server";
    const QString configPath = serverDir + "/config.json";
    if (QFileInfo::exists(configPath))
        return true;

    const QString examplePath = serverDir + "/config.example.json";
    if (!QFileInfo::exists(examplePath)) {
        blog(LOG_WARNING, "[streamhub] não consegui criar config.json: config.example.json não existe");
        return false;
    }
    if (!QFile::copy(examplePath, configPath)) {
        blog(LOG_WARNING, "[streamhub] não consegui criar a configuração inicial em %s",
             configPath.toUtf8().constData());
        return false;
    }
    QFile::setPermissions(configPath, QFile::permissions(configPath) | QFileDevice::WriteOwner);
    blog(LOG_INFO, "[streamhub] configuração inicial criada em %s", configPath.toUtf8().constData());
    return true;
}

} // namespace

bool StreamHub_EnsureBundledData(const QString &dataPath)
{
    if (!QDir().mkpath(dataPath)) {
        blog(LOG_WARNING, "[streamhub] não consegui criar a pasta de dados do plugin: %s",
             dataPath.toUtf8().constData());
        return false;
    }

    const QString versionFilePath = dataPath + QLatin1Char('/') + QString::fromLatin1(kVersionMarkerName);
    QFile versionFile(versionFilePath);

    QString onDiskVersion;
    if (versionFile.open(QIODevice::ReadOnly)) {
        onDiskVersion = QString::fromUtf8(versionFile.readAll()).trimmed();
        versionFile.close();
    }

    if (onDiskVersion == QString::fromLatin1(kBundleVersion)) {
        return EnsureUserConfig(dataPath);
    }

    blog(LOG_INFO, "[streamhub] preparando dados embutidos (versão do bundle: %s -> %s) em %s",
         onDiskVersion.isEmpty() ? "nenhuma" : onDiskVersion.toUtf8().constData(), kBundleVersion,
         dataPath.toUtf8().constData());

    const bool ok = ExtractResourceTree(dataPath);

    // Só grava o marcador se a extração foi (pelo menos majoritariamente)
    // bem-sucedida, senão tentamos de novo na próxima abertura do OBS.
    if (ok && versionFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        versionFile.write(kBundleVersion);
        versionFile.close();
    }

    return ok && EnsureUserConfig(dataPath);
}
