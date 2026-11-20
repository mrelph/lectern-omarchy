#pragma once
#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace MediaImport {
inline bool sameFileContents(const QString &left, const QString &right)
{
    QFile a(left), b(right);
    if (a.size() != b.size() || !a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly)) return false;
    while (!a.atEnd()) {
        auto block = a.read(65536);
        if (block.isEmpty() || b.read(block.size()) != block) return false;
    }
    return a.error() == QFile::NoError && b.error() == QFile::NoError;
}

inline QString importMedia(const QString &path, QString input, QString &created, QString &kind)
{
    QFileInfo source(input.startsWith("~/") ? QDir::home().filePath(input.mid(2)) : input);
    if (input.contains("://") || input.contains(QChar::Null) || !source.isFile())
        throw QString("Expected a local image or video file");
    source = QFileInfo(source.canonicalFilePath());
    const QString ext = source.suffix().toLower();
    if (QStringList{"mp4","mov","m4v"}.contains(ext)) kind = "video";
    else if (QString("png jpg jpeg gif heic heif tif tiff bmp webp svg pdf").split(' ').contains(ext)) kind = "image";
    else throw QString("Unsupported media type");
    const QDir dir(QFileInfo(path).absolutePath());
    if (!dir.mkpath("assets")) throw QString("Could not create assets directory");
    QString stem = source.completeBaseName().replace(QRegularExpression("[^a-zA-Z0-9._-]|-{2,}"), "-");
    stem.replace(QRegularExpression("-+"), "-").remove(QRegularExpression("^[-.]+|[-.]+$"));
    stem = stem.isEmpty() ? "media" : stem.left(80);
    for (int n = 1; ; ++n) {
        const QString relative = "assets/" + stem + (n == 1 ? QString() : "-" + QString::number(n)) + "." + ext;
        const QString target = dir.filePath(relative);
        if (QFileInfo::exists(target)) {
            if (QFileInfo(target).isFile() && sameFileContents(source.filePath(),target)) return relative;
            continue;
        }
        QFile file(source.filePath());
        if (!file.copy(target)) throw file.errorString();
        created = target;
        return relative;
    }
}
}
