#include "networkclient.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QCoreApplication>
#include <QDebug>
/*
 * @file networkclient.cpp
 * @brief 네트워크 송수신 구현부.
 *        - toLine(): QJsonObject → 개행 포함 바이트 배열
 *        - 오프라인 상태에서의 송신 요청은 내부 큐에 보관 후 연결 시 flushPending()으로 전송
 *        - onReadyRead(): '\n' 단위로 라인을 잘라 JSON 파싱 → messageReceived 신호 방출
 *        - onConnected(): 접속되면 HELLO(role) 송신 후 보류 큐 플러시
 */
/**
 * @brief QJsonObject를 Compact JSON 문자열로 직렬화하고 끝에 개행('
')을 붙입니다.
 *        서버/클라 모두 '한 줄 = 한 메시지' 규약을 사용하므로 개행은 필수입니다.
 */

QByteArray NetworkClient::toLine(const QJsonObject& o) {
    QByteArray b = QJsonDocument(o).toJson(QJsonDocument::Compact);
    b.append('\n');
    return b;
}
/**
 * @brief 생성자: 소켓 시그널을 내부 슬롯과 연결합니다.
 *  - connected → onConnected()
 *  - readyRead → onReadyRead()
 *  - stateChanged → onStateChanged()
 *  - errorOccurred → onErrorOccurred()
 */

NetworkClient::NetworkClient(QObject* parent) : QObject(parent) {
    // 소켓 연결 완료 시그널 → 내부 onConnected()로 연결
connect(&sock_, &QTcpSocket::connected,    this, &NetworkClient::onConnected);
    // 수신 버퍼에 읽을 데이터가 생기면 → onReadyRead()에서 라인 파싱
connect(&sock_, &QTcpSocket::readyRead,    this, &NetworkClient::onReadyRead);
    // 상태 변화(Connecting/Connected/Closing 등) → onStateChanged()
connect(&sock_, &QTcpSocket::stateChanged, this, &NetworkClient::onStateChanged);
    // 에러 발생 → onErrorOccurred()
connect(&sock_, &QTcpSocket::errorOccurred,this, &NetworkClient::onErrorOccurred);
}
/**
 * @brief 지정한 호스트/포트로 비동기 연결을 시도합니다.
 *        실패하면 errorOccurred 신호로 에러 문자열이 전달됩니다.
 */

void NetworkClient::connectToHost(const QString& host, quint16 port) {
    host_ = host; port_ = port;
    qInfo() << "[NET] connecting to" << host_ << port_;
    sock_.connectToHost(host_, port_);
}
/**
 * @brief 현재 연결을 종료합니다. (서버가 정상이면 FIN 흐름)
 */

void NetworkClient::disconnectFromHost() {
    sock_.disconnectFromHost();
}
/**
 * @brief JSON 한 건을 전송합니다.
 *        - 연결(Connected) 상태: 즉시 write()
 *        - 미연결(Unconnected) 상태: pending_ 큐에 저장 후 필요 시 재연결 시도
 */

void NetworkClient::sendJson(const QJsonObject& obj) {
    const QByteArray line = toLine(obj);
    const QString cmd = obj.value("cmd").toString();
    if (sock_.state() == QAbstractSocket::ConnectedState) {
        qInfo() << "[NET] send" << cmd << line;
        sock_.write(line);
        return;
    }
    // 연결 전이면 큐에 쌓고, 아직 연결 안돼 있으면 연결 시도 유지
    pending_.push_back(line);
    qInfo() << "[NET] queued (offline)" << cmd << "queue_size=" << pending_.size();
    if (sock_.state() == QAbstractSocket::UnconnectedState && !host_.isEmpty())
        sock_.connectToHost(host_, port_);
}
/**
 * @brief 로그인 요청을 도와주는 헬퍼.
 *        {"cmd":"LOGIN","admin_id":..., "pw":...} 형태로 요청을 구성해 sendJson() 호출.
 */

void NetworkClient::login(const QString& adminId, const QString& pw) {
    QJsonObject req;
    req["cmd"] = "LOGIN";
    req["admin_id"] = adminId;
    req["pw"] = pw;
    qInfo() << "[UI->NET] login request" << adminId;
    sendJson(req);
}
/**
 * @brief 연결 직후 호출.
 *        - 우선 HELLO(role) 전송으로 클라이언트 역할을 서버에 알립니다.
 *        - 그 다음 pending_ 큐에 쌓여 있던 요청들을 flushPending()으로 모두 전송합니다.
 */

void NetworkClient::onConnected() {
    qInfo() << "[NET] connected";
    // 먼저 HELLO 전송 (역할 알림)
    QJsonObject hello;                       // 서버에 역할을 알리는 최초 인사 메시지
hello["cmd"] = "HELLO";
hello["role"] = role_;
qInfo() << "[NET] send HELLO role=" << role_;
    sock_.write(toLine(hello));             // 규약에 맞게 개행 포함 전송
// 보류 중이던 요청 전송
    flushPending();                         // 연결되었으니 보류 중이던 요청 모두 전송
}
/**
 * @brief 보류 중인 라인들을 소켓에 순차 write 후 큐를 비웁니다.
 *        연결이 끊겼다면 다시 쌓일 수 있으니 호출 시점은 onConnected()가 주가 됩니다.
 */

void NetworkClient::flushPending() {
    if (pending_.isEmpty()) return;
    qInfo() << "[NET] flush pending count=" << pending_.size();
    for (const QByteArray& line : pending_) {
        sock_.write(line);
    }
    pending_.clear();
}
/**
 * @brief 수신 처리 루프.
 *        - readAll()로 수신 버퍼를 누적 → '
'을 찾아 한 줄씩 분리 → JSON 파싱
 *        - 파싱 성공 시 messageReceived(obj) 신호 방출
 *        - 로그 스팸 방지를 위해 LOGIN_OK는 콘솔 출력에서 제외(필요 시 추가 제외 가능)
 */

void NetworkClient::onReadyRead() {
    buffer_ += sock_.readAll();            // 누적 버퍼에 추가(개행이 나올 때까지 쌓는다)
for (;;) {                              // 버퍼에서 개행("\n")을 찾는 한 무한 루프

        int idx = buffer_.indexOf(\'\n\');     // 라인 종료 지점 탐색
if (idx < 0) break;
        QByteArray line = buffer_.left(idx).trimmed();  // 한 줄 추출(양끝 공백 제거)
buffer_.remove(0, idx + 1);             // 처리한 라인(+개행)만큼 버퍼에서 제거
if (line.isEmpty()) continue;           // 빈 줄은 무시
QJsonParseError perr{};                 // 파싱 에러 정보를 받기 위한 구조체
QJsonDocument doc = QJsonDocument::fromJson(line, &perr);  // JSON 파싱 시도
if (perr.error != QJsonParseError::NoError || !doc.isObject()) {  // JSON 형식 오류면 버림

            qWarning() << "[NET] bad json:" << line;
            continue;
        }
        const QString cmd = doc.object().value("cmd").toString();  // 명령어 필드만 먼저 뽑아 로그/분기에 활용
// LOGIN_OK는 콘솔에 출력하지 않음 (필요하면 목록을 늘리세요)
        // 과도한 로그를 막기 위해 LOGIN_OK만 콘솔 출력에서 제외(필요 시 목록 확장)
if (cmd.compare("LOGIN_OK", Qt::CaseInsensitive) != 0) {
            qInfo() << "[NET] recv" << cmd << line;
        }
        emit messageReceived(doc.object());
    }
}
/**
 * @brief 소켓 상태 변화를 그대로 외부 signal로 재전달합니다.
 */

void NetworkClient::onStateChanged(QAbstractSocket::SocketState s) {
    emit stateChanged(s);
}
/**
 * @brief 소켓 에러가 발생했을 때, 사람이 읽을 수 있는 문자열로 변환하여 알립니다.
 */

void NetworkClient::onErrorOccurred(QAbstractSocket::SocketError) {
    emit errorOccurred(sock_.errorString());
}
