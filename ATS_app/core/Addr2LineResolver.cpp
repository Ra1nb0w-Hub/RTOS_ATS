#include "Addr2LineResolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStringList>

namespace
{
constexpr int kSearchDepth = 8;

void appendParentDirectories(QStringList *directories, const QString &startPath)
{
    QDir current(startPath);

    if (startPath.isEmpty()) {
        return;
    }

    for (int depth = 0; depth < kSearchDepth; ++depth) {
        const QString absolutePath = current.absolutePath();
        if (!directories->contains(absolutePath)) {
            directories->append(absolutePath);
        }

        if (!current.cdUp()) {
            break;
        }
    }
}
}

Addr2LineResolver::Addr2LineResolver(const QString &elfPath)
    : m_elfPath(elfPath)
    , m_toolPath(findToolPath())
{
}

bool Addr2LineResolver::isValid() const
{
    return !m_toolPath.isEmpty()
           && QFileInfo::exists(m_elfPath)
           && QFileInfo(m_elfPath).isFile();
}

QString Addr2LineResolver::resolve(quint32 addr) const
{
    if (!isValid()) {
        return QString();
    }

    const QString addrStr = QString("0x%1").arg(addr, 8, 16, QChar('0'));

    QProcess process;
    process.setProgram(m_toolPath);
    process.setArguments({
        QStringLiteral("-e"), m_elfPath,
        QStringLiteral("-f"),
        QStringLiteral("-p"),
        QStringLiteral("-a"),
        addrStr
    });

    process.start();
    if (!process.waitForFinished(5000)) {
        process.kill();
        return QString();
    }

    const QByteArray output = process.readAllStandardOutput().trimmed();
    if (output.isEmpty()) {
        return QString("??\n??:?");
    }

    return QDir::toNativeSeparators(QString::fromUtf8(output));
}

QString Addr2LineResolver::findToolPath() const
{
    const QString environmentOverride =
        qEnvironmentVariable("ATS_ADDR2LINE_TOOL_PATH");

    if (!environmentOverride.isEmpty()) {
        const QFileInfo overrideInfo(environmentOverride);
        if (overrideInfo.exists() && overrideInfo.isFile()) {
            return overrideInfo.absoluteFilePath();
        }
    }

    QStringList baseDirectories;
    QSet<QString> seenCandidates;

    appendParentDirectories(&baseDirectories, QCoreApplication::applicationDirPath());
    appendParentDirectories(&baseDirectories, QDir::currentPath());
    appendParentDirectories(&baseDirectories, QFileInfo(QStringLiteral(__FILE__)).absolutePath());

    const QStringList relativePaths = {
        QStringLiteral("tools/arm-none-eabi-addr2line.exe"),
        QStringLiteral("ATS_app/tools/arm-none-eabi-addr2line.exe"),
    };

    for (const QString &baseDirectory : std::as_const(baseDirectories)) {
        const QDir baseDir(baseDirectory);
        for (const QString &relativePath : relativePaths) {
            const QString candidate = QDir::cleanPath(baseDir.filePath(relativePath));
            if (!seenCandidates.contains(candidate)) {
                if (QFileInfo::exists(candidate) && QFileInfo(candidate).isFile()) {
                    return QFileInfo(candidate).absoluteFilePath();
                }
                seenCandidates.insert(candidate);
            }
        }
    }

    return QString();
}
