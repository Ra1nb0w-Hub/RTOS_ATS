#include "QemuController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

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

QemuController::QemuController(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_qmpSocket(new QTcpSocket(this))
{
    connect(m_process, &QProcess::started, this, &QemuController::onProcessStarted);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &QemuController::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &QemuController::onReadyReadStandardError);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &QemuController::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &QemuController::onProcessErrorOccurred);

    connect(m_qmpSocket, &QTcpSocket::connected, this, &QemuController::onQmpConnected);
    connect(m_qmpSocket, &QTcpSocket::disconnected, this, &QemuController::onQmpDisconnected);
    connect(m_qmpSocket, &QTcpSocket::readyRead, this, &QemuController::onQmpReadyRead);
}

QemuController::~QemuController()
{
    if (m_qmpSocket->state() == QAbstractSocket::ConnectedState) {
        m_qmpSocket->disconnectFromHost();
    }
    stop();
}

void QemuController::setFirmwarePath(const QString &firmwarePath)
{
    m_firmwarePath = QDir::fromNativeSeparators(QFileInfo(firmwarePath).absoluteFilePath());
}


QString QemuController::firmwarePath() const
{
    return m_firmwarePath;
}

bool QemuController::hasFirmwarePath() const
{
    return !m_firmwarePath.isEmpty();
}

bool QemuController::start(quint16 serialPort)
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
    m_qmpBuffer.clear();
    m_serialPort = serialPort;
    m_qmpNegotiated = false;
    m_shuttingDown = false;

    m_qmpPort = findFreePort();

    const QString icountMode = QString::fromLatin1(kDefaultIcountMode);

    arguments << QStringLiteral("-M")
              << QString::fromLatin1(kDefaultMachineType)
              << QStringLiteral("-cpu")
              << QString::fromLatin1(kDefaultCpuType)
              << QStringLiteral("-nographic")
              << QStringLiteral("-qmp")
              << QStringLiteral("tcp:127.0.0.1:%1,server=on,wait=off").arg(m_qmpPort);

    /* 4 serial ports for multi-channel RPC */
    for (quint16 i = 0; i < 4; ++i) {
        arguments << QStringLiteral("-serial")
                  << QStringLiteral("tcp:127.0.0.1:%1").arg(serialPort + i);
    }

    arguments << QStringLiteral("-kernel")
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

    if (!m_process->waitForStarted(3000)) {
        return false;
    }

    connectToQmp();
    return true;
}

void QemuController::stop()
{
    if (!isRunning()) {
        return;
    }

    if (m_qmpSocket->state() == QAbstractSocket::ConnectedState && m_qmpNegotiated && !m_shuttingDown) {
        shutdownViaQmp();
        if (m_process->waitForFinished(3000)) {
            return;
        }
    }

    m_process->terminate();
    if (!m_process->waitForFinished(500)) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
}

bool QemuController::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void QemuController::onProcessStarted()
{
    emitLog(QStringLiteral("QEMU 已启动，端口信息：serial=%1~%2, qmp=%3")
                .arg(m_serialPort).arg(m_serialPort+3).arg(m_qmpPort),
            QStringLiteral("SYS"));
    emit started();
}

void QemuController::onProcessFinished(int exitCode, int exitStatus)
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

void QemuController::onProcessErrorOccurred(int error)
{
    emitLog(processErrorToText(static_cast<QProcess::ProcessError>(error)), QStringLiteral("ERROR"));
}

void QemuController::onReadyReadStandardOutput()
{
    processBufferedOutput(&m_stdoutBuffer, m_process->readAllStandardOutput(), QStringLiteral("INFO"));
}

void QemuController::onReadyReadStandardError()
{
    processBufferedOutput(&m_stderrBuffer, m_process->readAllStandardError(), QStringLiteral("ERROR"));
}

void QemuController::emitLog(const QString &message, const QString &level)
{
    emit logMessage(message, level);
}

void QemuController::processBufferedOutput(QByteArray *buffer, const QByteArray &chunk, const QString &level)
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

void QemuController::flushBufferedOutput(QByteArray *buffer, const QString &level)
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

QString QemuController::findQemuExecutablePath() const
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

quint16 QemuController::findFreePort() const
{
    QTcpServer server;
    if (server.listen(QHostAddress::LocalHost, 0)) {
        quint16 port = server.serverPort();
        server.close();
        return port;
    }
    return 0;
}

void QemuController::connectToQmp()
{
    if (m_qmpPort == 0) {
        emitLog(QStringLiteral("QMP 端口无效，无法连接"), QStringLiteral("ERROR"));
        return;
    }

    m_qmpSocket->connectToHost(QHostAddress::LocalHost, m_qmpPort);
}

void QemuController::onQmpConnected()
{

}

void QemuController::onQmpDisconnected()
{
    m_qmpNegotiated = false;
}

void QemuController::onQmpReadyRead()
{
    m_qmpBuffer.append(m_qmpSocket->readAll());

    while (true) {
        const int newlineIndex = m_qmpBuffer.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        QByteArray line = m_qmpBuffer.left(newlineIndex);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        m_qmpBuffer.remove(0, newlineIndex + 1);

        if (!line.trimmed().isEmpty()) {
            handleQmpMessage(line);
        }
    }
}

void QemuController::handleQmpMessage(const QByteArray &message)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(message, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        return;
    }

    const QJsonObject obj = doc.object();

    if (obj.contains(QStringLiteral("QMP"))) {
        sendQmpCommand(QByteArrayLiteral("{\"execute\":\"qmp_capabilities\"}"));
    } 
    else if (obj.contains(QStringLiteral("return"))) {
        if (!m_qmpNegotiated) {
            m_qmpNegotiated = true;
        }
    }
}

void QemuController::sendQmpCommand(const QByteArray &command)
{
    if (m_qmpSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QByteArray data = command + '\n';
    m_qmpSocket->write(data);
    m_qmpSocket->flush();
}

void QemuController::shutdownViaQmp()
{
    m_shuttingDown = true;
    emitLog(QStringLiteral("通过 QMP 发送关闭指令..."), QStringLiteral("SYS"));
    sendQmpCommand(QByteArrayLiteral("{\"execute\":\"quit\"}"));
}
