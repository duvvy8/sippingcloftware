#include "instance.h"
#include <QLocalSocket>
namespace clip {
Instance::Instance(QObject *parent) : QObject(parent), lock_(stateDir() + "/application.lock") {
    name_ = "LocalClipper-" +
            QString::fromLatin1(
                QCryptographicHash::hash(QFileInfo(stateDir()).absoluteFilePath().toLower().toUtf8(),
                                         QCryptographicHash::Sha256)
                    .toHex()
                    .left(32));
    lock_.setStaleLockTime(0);
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = server_.nextPendingConnection()) {
            auto pending = std::make_shared<QByteArray>();
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, pending] {
                *pending += socket->readAll();
                if (pending->size() > 4096) {
                    socket->abort();
                    return;
                }
                if (!pending->contains('\n'))
                    return;
                auto document = QJsonDocument::fromJson(pending->left(pending->indexOf('\n')));
                if (document.isObject()) {
                    emit received(document.object());
                    socket->write("OK\n");
                    socket->flush();
                }
                socket->disconnectFromServer();
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(3000, socket, [socket] {
                socket->abort();
                socket->deleteLater();
            });
        }
    });
}
bool Instance::acquire(QString *error) {
    if (!lock_.tryLock()) {
        if (error)
            *error = lock_.error() == QLockFile::LockFailedError
                         ? "Application is already running"
                         : "Cannot lock application state; check folder access";
        return false;
    }
    QLocalServer::removeServer(name_);
    if (!server_.listen(name_)) {
        if (error)
            *error = server_.errorString();
        lock_.unlock();
        return false;
    }
    return true;
}
bool Instance::forward(const QJsonObject &message, QString *error, int acknowledgementMs) {
    QLocalSocket socket;
    for (int attempt = 0; attempt < 10; ++attempt) {
        socket.connectToServer(name_);
        if (socket.waitForConnected(200))
            break;
        socket.abort();
        QThread::msleep(100);
    }
    if (socket.state() != QLocalSocket::ConnectedState) {
        if (error)
            *error = "The existing application did not accept this request";
        return false;
    }
    auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > 4096 || socket.write(bytes) != bytes.size())
        return false;
    socket.flush();
    if (!socket.waitForReadyRead(acknowledgementMs) || socket.readLine() != "OK\n") {
        if (error)
            *error = "The existing application did not acknowledge this request";
        return false;
    }
    return true;
}
} // namespace clip
