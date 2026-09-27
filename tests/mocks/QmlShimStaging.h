#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

namespace strmqt::test {

// Copies the build's tier shims (src/ui/shims/<tier>, spec 2026-09-27 §4.3)
// into a staged StrmQt module and returns their qmldir lines, so a test that
// stages StrmIcon, StrmPanel and friends by copying files resolves StrmTint and
// the rest exactly as the app does. Returns an empty array on failure.
inline QByteArray stageShims(const QString &modulePath)
{
    const QDir shims(QStringLiteral(STRMQT_SHIMS_DIR));
    const QStringList files = shims.entryList({QStringLiteral("*.qml")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    QByteArray lines;
    for (const QString &file : files) {
        const QString target = modulePath + QLatin1Char('/') + file;
        QFile::remove(target);
        if (!QFile::copy(shims.filePath(file), target))
            return {};
        lines += file.chopped(4).toUtf8() + " 1.0 " + file.toUtf8() + '\n';
    }
    return lines;
}

} // namespace strmqt::test
