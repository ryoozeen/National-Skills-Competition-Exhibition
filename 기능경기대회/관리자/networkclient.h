#pragma once
/**
 * @file networkclient.h
 * @brief 관리자 클라이언트의 공통 네트워크 모듈 (QTcpSocket 기반).
 *        서버와의 통신은 "한 줄에 하나의 JSON, 끝에 개행 '\n'" 프로토콜을 사용합니다.
 *        - 자동 줄바꿈/오프라인 큐(sendJson)
 *        - 연결/끊김/에러/수신 시그널 래핑
 *        - 연결 직후 HELLO(role) 핸드셰이크
 *
 * 사용 예시:
 *   auto* nc = new NetworkClient(this);
 *   nc->setRole("admin");                       // 기본값은 "admin"
 *   nc->connectToHost("127.0.0.1", 5050);       // 서버 접속
 *   connect(nc, &NetworkClient::messageReceived, this, [&](const QJsonObject& obj){
 *       // obj["cmd"]에 따라 분기 처리
 *   });
 *   // 로그인 요청
 *   nc->login("admin", "1234");
 */
#include <QObject>
#include <QTcpSocket>
#include <QJsonObject>
#include <QList>
#include <QByteArray>

class NetworkClient : public QObject {  // 서버와 TCP(JSON 라인 프로토콜)로 통신하는 경량 클라이언트

    Q_OBJECT
public:
    explicit NetworkClient(QObject* parent=nullptr);  // 소켓 시그널 연결 등 기본 세팅


    void setRole(const QString& role) { role_ = role; }  // 서버에 HELLO 시 자신의 역할을 알릴 때 사용("admin" 등)

    void connectToHost(const QString& host, quint16 port);  // 호스트/포트로 비동기 접속 시도

    void disconnectFromHost();  // 소켓 연결 종료


    void sendJson(const QJsonObject& obj);  // JSON을 한 줄(개행 포함)로 직렬화하여 송신. 미연결 시 pending_에 큐잉
// 자동 줄바꿈 + 오프라인 큐
    void login(const QString& adminId, const QString& pw);  // {"cmd":"LOGIN","admin_id":..,"pw":..} 전송 헬퍼


signals:
      // 외부로 전달하는 이벤트(수신 메시지/상태/에러)
void messageReceived(const QJsonObject& obj);  // 한 줄 수신 후 JSON 파싱 성공 시 방출

    void stateChanged(QAbstractSocket::SocketState);  // QTcpSocket 상태 변경 전달(Connecting/Connected 등)

    void errorOccurred(const QString& err);  // 소켓 에러 문자열 전달


private slots:
      // 내부 슬롯(소켓 시그널을 받아 로직 수행)
void onConnected();  // 접속 직후 HELLO(role) 송신 및 보류 큐 플러시

    void onReadyRead();  // 수신 버퍼에서 개행 단위로 라인을 분리 → JSON 파싱

    void onStateChanged(QAbstractSocket::SocketState s);  // 상태 변경 → 그대로 신호 재전달

    void onErrorOccurred(QAbstractSocket::SocketError e);  // 에러 발생 → 문자열로 변환해 신호 재전달


private:
      // 구현 디테일(헬퍼/멤버 변수)
static QByteArray toLine(const QJsonObject& o);  // QJsonObject → Compact JSON + 개행("\n")

    void flushPending();  // 연결되면 pending_ 큐에 있던 라인들을 한 번에 송신


    QTcpSocket sock_;
      // 실제 TCP 소켓(비동기)
QByteArray buffer_;
      // 수신 누적 버퍼(개행 찾을 때까지 축적)
QString host_;
      // 마지막 connectToHost 대상 호스트
quint16  port_{0};
      // 마지막 connectToHost 대상 포트
QString  role_{"admin"};

      // HELLO 시 서버에 알려줄 역할 문자열(기본값 "admin")
QList<QByteArray> pending_; // 연결 전 보낼 큐(한 줄당 1 JSON)  // 미연결 상태에서 sendJson 호출 시 여기 쌓임

};
