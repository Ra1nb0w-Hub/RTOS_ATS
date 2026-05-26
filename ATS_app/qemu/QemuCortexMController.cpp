#include "QemuCortexMController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

namespace
{
constexpr int kSearchDepth = 8;
const char kDefaultIcountMode[] = "shift=auto,align=off,sleep=on";
const char kDefaultMachineType[] = "mps2-an385";
const char kDefaultCpuType[] = "cortex-m3";

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

QStringList candidateQemuExecutablePaths()
{
    QStringList baseDirectories;
    QStringList candidates;
    QSet<QString> seenCandidates;

    appendParentDirectories(&baseDirectories, QCoreApplication::applicationDirPath());
    appendParentDirectories(&baseDirectories, QDir::currentPath());
    appendParentDirectories(&baseDirectories, QFileInfo(QStringLiteral(__FILE__)).absolutePath());

    for (const QString &baseDirectory : std::as_const(baseDirectories)) {
        const QDir baseDir(baseDirectory);
        const QStringList relativePaths = {
            QStringLiteral("tools/qemu/qemu-system-arm.exe"),
            QStringLiteral("ATS_app/tools/qemu/qemu-system-arm.exe"),
            QStringLiteral("qemu/qemu-system-arm.exe")
        };

        for (const QString &relativePath : relativePaths) {
            const QString candidate = QDir::cleanPath(baseDir.filePath(relativePath));
            if (!seenCandidates.contains(candidate)) {
                candidates.append(candidate);
                seenCandidates.insert(candidate);
            }
        }
    }

    return candidates;
}

QString processErrorToText(QProcess::ProcessError error)
{
    switch (error) {
    case QProcess::FailedToStart:
        return QStringLiteral("QEMU 进程启动失败");
    case QProcess::Crashed:
        return QStringLiteral("QEMU 进程异常退出");
    case QProcess::Timedout:
        return QStringLiteral("QEMU 进程操作超时");
    case QProcess::WriteError:
        return QStringLiteral("QEMU 进程写入失败");
    case QProcess::ReadError:
        return QStringLiteral("QEMU 进程读取失败");
    case QProcess::UnknownError:
    default:
        return QStringLiteral("QEMU 进程出现未知错误");
    }
}
}

QemuCortexMController::QemuCortexMController(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::started, this, &QemuCortexMController::onProcessStarted);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &QemuCortexMController::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &QemuCortexMController::onReadyReadStandardError);
    connect(m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &QemuCortexMController::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &QemuCortexMController::onProcessErrorOccurred);
}

QemuCortexMController::~QemuCortexMController()
{
    stop();
}

void QemuCortexMController::setFirmwarePath(const QString &firmwarePath)
{
    m_firmwarePath = QDir::fromNativeSeparators(QFileInfo(firmwarePath).absoluteFilePath());
}

QString QemuCortexMController::firmwarePath() const
{
    return m_firmwarePath;
}

bool QemuCortexMController::hasFirmwarePath() const
{
    return !m_firmwarePath.isEmpty();
}

bool QemuCortexMController::start(quint16 serialPort)
{
    const QFileInfo firmwareInfo(m_firmwarePath);
    const QString qemuExecutablePath = findQemuExecutablePath();
    QStringList arguments;

    if (isRunning()) {
        emitLog(QStringLiteral("QEMU 已在运行中"), QStringLiteral("WARN"));
        return false;
    }

    if (!firmwareInfo.exists() || !firmwareInfo.isFile()) {
        emitLog(QStringLiteral("未找到已导入的 ELF 文件"), QStringLiteral("ERROR"));
        return false;
    }

    if (qemuExecutablePath.isEmpty()) {
        emitLog(QStringLiteral("未找到可用的 qemu-system-arm.exe"), QStringLiteral("ERROR"));
        return false;
    }

    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_serialPort = serialPort;
    const QString icountMode = QString::fromLatin1(kDefaultIcountMode);

    arguments << QStringLiteral("-M")
              << QString::fromLatin1(kDefaultMachineType)
              << QStringLiteral("-cpu")
              << QString::fromLatin1(kDefaultCpuType)
              << QStringLiteral("-nographic")
              << QStringLiteral("-monitor")
              << QStringLiteral("none")
              << QStringLiteral("-serial")
              << QStringLiteral("tcp:127.0.0.1:%1").arg(serialPort)
              << QStringLiteral("-kernel")
              << QDir::toNativeSeparators(firmwareInfo.absoluteFilePath())
              << QStringLiteral("-icount")
              << icountMode;

    const QFileInfo qemuInfo(qemuExecutablePath);
    QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
    const QString qemuDirectory = qemuInfo.absolutePath();
    const QString originalPath = processEnvironment.value(QStringLiteral("PATH"));
    processEnvironment.insert(QStringLiteral("PATH"),
                              qemuDirectory + QLatin1Char(';') + originalPath);

    m_process->setProcessEnvironment(processEnvironment);
    m_process->setWorkingDirectory(qemuDirectory);
    m_process->start(QDir::toNativeSeparators(qemuExecutablePath), arguments);
    return m_process->waitForStarted(3000);
}

void QemuCortexMController::stop()
{
    if (!isRunning()) {
        return;
    }

    m_process->terminate();
    if (!m_process->waitForFinished(500)) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
}

bool QemuCortexMController::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void QemuCortexMController::onProcessStarted()
{
    emitLog(QStringLiteral("QEMU 已启动，串口桥端口 serial=%1")
                .arg(m_serialPort),
            QStringLiteral("SYS"));
    emit started();
}

void QemuCortexMController::onProcessFinished(int exitCode, int exitStatus)
{
    const QProcess::ExitStatus processExitStatus = static_cast<QProcess::ExitStatus>(exitStatus);

    flushBufferedOutput(&m_stdoutBuffer, QStringLiteral("INFO"));
    flushBufferedOutput(&m_stderrBuffer, QStringLiteral("ERROR"));

    if (processExitStatus == QProcess::NormalExit) {
        emitLog(QStringLiteral("QEMU 已退出，退出码: %1").arg(exitCode), QStringLiteral("SYS"));
    } else {
        emitLog(QStringLiteral("QEMU 异常退出，退出码: %1").arg(exitCode), QStringLiteral("ERROR"));
    }

    emit stopped();
}

void QemuCortexMController::onProcessErrorOccurred(int error)
{
    emitLog(processErrorToText(static_cast<QProcess::ProcessError>(error)), QStringLiteral("ERROR"));
}

void QemuCortexMController::onReadyReadStandardOutput()
{
    processBufferedOutput(&m_stdoutBuffer, m_process->readAllStandardOutput(), QStringLiteral("INFO"));
}

void QemuCortexMController::onReadyReadStandardError()
{
    processBufferedOutput(&m_stderrBuffer, m_process->readAllStandardError(), QStringLiteral("ERROR"));
}

void QemuCortexMController::emitLog(const QString &message, const QString &level)
{
    emit logMessage(message, level);
}

void QemuCortexMController::processBufferedOutput(QByteArray *buffer, const QByteArray &chunk, const QString &level)
{
    int newlineIndex = -1;

    if (!buffer) {
        return;
    }

    buffer->append(chunk);
    while ((newlineIndex = buffer->indexOf('\n')) >= 0) {
        QByteArray line = buffer->left(newlineIndex);
        if (!line.isEmpty() && line.endsWith('\r')) {
            line.chop(1);
        }

        if (!line.trimmed().isEmpty()) {
            emitLog(QString::fromLocal8Bit(line), level);
        }

        buffer->remove(0, newlineIndex + 1);
    }
}

void QemuCortexMController::flushBufferedOutput(QByteArray *buffer, const QString &level)
{
    if (!buffer || buffer->trimmed().isEmpty()) {
        if (buffer) {
            buffer->clear();
        }
        return;
    }

    emitLog(QString::fromLocal8Bit(*buffer), level);
    buffer->clear();
}

QString QemuCortexMController::findQemuExecutablePath() const
{
    const QString environmentOverride =
        qEnvironmentVariable("ATS_QEMU_SYSTEM_ARM");

    if (!environmentOverride.isEmpty()) {
        const QFileInfo overrideInfo(environmentOverride);
        if (overrideInfo.exists() && overrideInfo.isFile()) {
            return overrideInfo.absoluteFilePath();
        }
    }

    const QStringList candidates = candidateQemuExecutablePaths();

    for (const QString &candidate : candidates) {
        const QFileInfo candidateInfo(candidate);
        if (candidateInfo.exists() && candidateInfo.isFile()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("qemu-system-arm"));
    if (!fromPath.isEmpty()) {
        return QDir::fromNativeSeparators(fromPath);
    }

    return QString();
}
