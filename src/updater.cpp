#include "updater.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QVersionNumber>

namespace {
const QString repository = "mrelph/lectern-omarchy";
constexpr qint64 binaryLimit = 32 * 1024 * 1024;

QByteArray fileDigest(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (file.open(QIODevice::ReadOnly) && hash.addData(&file))
        return hash.result().toHex();
    return {};
}
}

Updater::Updater(const QString &version, const QString &executable) : m_version(version)
{
    const auto localBin = QDir::homePath() + "/.local/bin";
    const QFileInfo target(localBin + "/lectern-omarchy");
    if (!target.isSymLink() && target.isFile() && !target.canonicalFilePath().isEmpty() &&
        target.canonicalFilePath() == QFileInfo(executable).canonicalFilePath() &&
        (target.ownerId() == QFileInfo(QDir::homePath()).ownerId())) {
        m_executable = target.absoluteFilePath();
        m_installedDigest = fileDigest(m_executable);
        if (m_installedDigest.isEmpty())
            m_executable.clear();
    }
}

QNetworkReply *Updater::download(const QUrl &url, qint64 limit)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "lectern-omarchy/" + m_version.toUtf8());
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit](qint64 received, qint64 total) {
        if (received > limit || total > limit)
            reply->abort();
    });
    return reply;
}

void Updater::finish(const QString &status)
{
    m_busy = false;
    m_status = status;
    emit changed();
}

void Updater::check()
{
    if (m_busy)
        return;
    m_busy = true;
    m_available = false;
    m_asset.clear();
    m_release.clear();
    m_status = tr("Checking GitHub Releases…");
    emit changed();

    auto *reply = download(QUrl("https://api.github.com/repos/" + repository + "/releases/latest"), 1024 * 1024);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finish(tr("Could not check for updates. %1").arg(reply->errorString()));
            return;
        }
        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value("tag_name").toString();
        static const QRegularExpression stable(R"(^v[0-9]+\.[0-9]+\.[0-9]+$)");
        if (!stable.match(tag).hasMatch() || release.value("draft").toBool() || release.value("prerelease").toBool()) {
            finish(tr("GitHub returned an invalid release."));
            return;
        }
        if (QVersionNumber::fromString(tag.mid(1)) <= QVersionNumber::fromString(m_version)) {
            finish(tr("Lectern %1 is up to date.").arg(m_version));
            return;
        }

        m_release = QUrl("https://github.com/" + repository + "/releases/tag/" + tag);
        m_available = true;
        const QString name = "lectern-omarchy-linux-" + QSysInfo::buildCpuArchitecture();
        const QUrl assetUrl("https://github.com/" + repository + "/releases/download/" + tag + "/" + name);
        static const QRegularExpression sha256(R"(^sha256:([a-fA-F0-9]{64})$)");
        for (const auto &value : release.value("assets").toArray()) {
            const auto asset = value.toObject();
            const auto digest = sha256.match(asset.value("digest").toString());
            const auto size = asset.value("size").toInteger();
            if (asset.value("name").toString() == name && digest.hasMatch() && size > 0 &&
                size <= binaryLimit && QUrl(asset.value("browser_download_url").toString()) == assetUrl) {
                m_asset = assetUrl;
                m_digest = digest.captured(1).toLatin1().toLower();
                m_size = size;
                break;
            }
        }
        finish(tr("Lectern %1 is available.").arg(tag.mid(1)));
    });
}

void Updater::install()
{
    if (m_busy || !canInstall())
        return;
    m_busy = true;
    m_status = tr("Downloading update…");
    emit changed();

    auto *reply = download(m_asset, m_size);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finish(tr("Could not download the update. %1").arg(reply->errorString()));
            return;
        }
        const auto binary = reply->readAll();
        if (binary.size() != m_size || !binary.startsWith("\x7f" "ELF") ||
            QCryptographicHash::hash(binary, QCryptographicHash::Sha256).toHex() != m_digest) {
            finish(tr("The download failed its integrity check."));
            return;
        }

        QLockFile lock(m_executable + ".update.lock");
        if (!lock.tryLock() || QFileInfo(m_executable).isSymLink() || fileDigest(m_executable) != m_installedDigest) {
            finish(tr("The installed copy changed or another update is in progress. Restart Lectern and try again."));
            return;
        }
        QSaveFile file(m_executable);
        if (!file.open(QIODevice::WriteOnly) ||
            !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                 QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther) ||
            file.write(binary) != binary.size() || !file.commit()) {
            finish(tr("Could not install the update. %1").arg(file.errorString()));
            return;
        }
        m_executable.clear();
        m_available = false;
        finish(tr("Update installed. Restart Lectern when you are ready."));
    });
}

void Updater::openRelease()
{
    if (!m_release.isEmpty() && !QDesktopServices::openUrl(m_release))
        finish(tr("Could not open the release page."));
}
