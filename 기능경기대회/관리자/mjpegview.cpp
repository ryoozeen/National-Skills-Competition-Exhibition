#include "mjpegview.h"
#include <QVBoxLayout>      // 내부에 정중앙 라벨을 꽉 채워 넣기 위한 단순 레이아웃
#include <QNetworkRequest>  // HTTP 요청 헤더 조작(keep-alive 등)
#include <QMouseEvent>      // 라벨 클릭 → clicked() 시그널 방출

/**
 * @brief MJPEG 스트림 단순 뷰어 구현
 *
 * 기능 초점:
 *  - HTTP(MJPEG) 응답 바디를 바이트 스트림으로 받아, JPEG 프레임 경계(FFD8~FFD9)를
 *    직접 파싱해 순차 렌더링합니다.
 *  - 연결/에러/종료 시 자동 재시도(지정 지연)로 스트림 복구를 시도합니다.
 *  - 최근 프레임을 캐시(m_lastImg)하여 리사이즈 시 재샘플링 표시 품질을 유지합니다.
 *  - UI는 중앙의 QLabel 하나만 사용(간단/저비용), 배경은 검정으로 고정.
 *
 * 설계 포인트:
 *  - QNetworkAccessManager를 이용한 단일 GET 스트림(keep-alive) 사용.
 *  - ReadyRead에서 바이트 누적 → parseBuffer()에서 SOI/EOI로 프레임 분리.
 *  - 오류/종료 시 stop()으로 네트워크 객체 안전 종료 후 재시도 타이머 구동.
 *  - 대용량/손상 버퍼 방지: 경계가 안 보이면 일정 크기 초과 시 버퍼 drop.
 */

MjpegView::MjpegView(QWidget *parent): QWidget(parent) {
    // 페인트 최적화: OS 배경 지우기/불투명 페인트 플래그로 깜빡임 감소
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);

    // 내부 레이아웃: 여백 0, 중앙 라벨 1개
    auto lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);

    // 초기 상태 표시용 라벨(스트림 준비 전까지 텍스트 출력)
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setText(QString::fromUtf8("미리보기 로딩중…"));
    m_label->setStyleSheet("background:#000; color:#9aa; font-size:13px;"); // 검정 바탕에 흐린 회색 텍스트
    lay->addWidget(m_label);

    // 재시도 타이머: 연결 실패/종료 시 일정 시간 뒤 start() 재호출
    connect(&m_retryTimer, &QTimer::timeout, this, &MjpegView::retry);
    m_retryTimer.setSingleShot(true); // 1회성 타이머(수동 재시작)
}

MjpegView::~MjpegView() {
    // 네트워크 핸들/버퍼 정리(안전 종료)
    stop();
}

/**
 * @brief 재생 대상 URL 지정
 * @details 유효성은 start() 호출 시점에 검사합니다.
 */
void MjpegView::setUrl(const QUrl &u){ m_url=u; }

/**
 * @brief 스트림 시작(또는 재시작)
 *
 * 동작:
 *  - 기존 연결/타이머를 정리(stop)한 뒤, 유효한 URL이면 GET 수행.
 *  - readyRead/finished/errorOccurred 신호를 수신하여 파이프라인 구성.
 *  - 화면에는 "연결 중…" 상태 텍스트 출력.
 */
void MjpegView::start(){
    m_retryTimer.stop();    // ✅ 겹치는 재시도 예약 무효화
    stop();                 // 이전 연결/버퍼 완전 종료
    if(!m_url.isValid()) return;

    QNetworkRequest req(m_url);
    req.setRawHeader("Connection","keep-alive"); // 장시간 스트리밍 연결 힌트
    m_reply = m_nam.get(req);

    // 네트워크 수신 루프: 바디 수신 → 내부 버퍼 누적 → 프레임 분리
    connect(m_reply,&QNetworkReply::readyRead, this,&MjpegView::onReadyRead);
    // 서버/네트워크 종료: 재시도 타이머 가동
    connect(m_reply,&QNetworkReply::finished,  this,&MjpegView::onFinished);
    // 오류: 사용자 안내 → stop → 재시도 타이머
    connect(m_reply,&QNetworkReply::errorOccurred, this,&MjpegView::onError);

    m_label->setText(QString::fromUtf8("연결 중…"));
    m_buf.clear();
}

/**
 * @brief 스트림/연결 정리
 *
 * 안전성:
 *  - reply 시그널을 모두 해제(disconnect)하여 중복 콜백 방지.
 *  - 정상 상태(NoError)에서만 abort() 호출(에러 상태에서 abort는 추가 에러를 유발할 수 있음).
 *  - reply는 deleteLater로 이벤트 루프에서 파괴 예약.
 *  - 누적 버퍼도 함께 초기화.
 */
void MjpegView::stop(){
    if(m_reply){
        disconnect(m_reply,nullptr,this,nullptr);
        // 이미 에러 난 상태에서는 abort()를 호출하지 않음 (중복 에러 방지)
        if (!m_reply->isFinished()
            && m_reply->error() == QNetworkReply::NoError) {
            m_reply->abort();
        }
        m_reply->deleteLater();
        m_reply=nullptr;
    }
    m_buf.clear();
}

/**
 * @brief 재시도 타이머 만료 콜백
 * @details 이미 연결 시도 중(reply 존재)이면 재시작하지 않아 중복 연결을 막습니다.
 */
void MjpegView::retry(){
    if (!m_reply) start();  // ✅ 이미 시도 중이면 중복 start() 방지
}

/**
 * @brief 리사이즈 시 최근 프레임을 현재 위젯 크기에 맞춰 리샘플링
 * @details m_lastImg 캐시를 사용해 즉각 품질 유지(새 프레임 대기 없음).
 */
void MjpegView::resizeEvent(QResizeEvent *){
    if(!m_lastImg.isNull()){
        QPixmap px = QPixmap::fromImage(m_lastImg).scaled(
            size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_label->setPixmap(px);
    }
}

/**
 * @brief 좌클릭을 clicked() 시그널로 노출
 * @details 상위에서 전체 화면 보기/컨트롤 토글 등으로 활용 가능.
 */
void MjpegView::mousePressEvent(QMouseEvent *e){
    if(e->button()==Qt::LeftButton) emit clicked();
    QWidget::mousePressEvent(e);
}

/**
 * @brief 네트워크로부터 데이터 수신 시(조각 단위) 버퍼 누적 → 파싱 트리거
 */
void MjpegView::onReadyRead(){
    if(!m_reply) return;
    m_buf += m_reply->readAll();
    parseBuffer();
}

/**
 * @brief 스트림 종료(서버 닫힘/정상 종료 포함)
 * @details 먼저 stop()으로 정리 후, 일정 지연(1.5s) 뒤 자동 재시도.
 */
// void MjpegView::onFinished(){ m_retryTimer.start(1500); }
void MjpegView::onFinished(){
    stop();                 // ✅ 먼저 정상 정리
    m_retryTimer.start(1500);
}

/**
 * @brief 네트워크 오류 처리
 * @details 사용자에게 상태 텍스트를 보여주고, stop() 후 재시도 예약.
 */
void MjpegView::onError(QNetworkReply::NetworkError){
    m_label->setText(QString::fromUtf8("연결 오류, 재시도 중…"));
    stop();
    m_retryTimer.start(1500);
}

/**
 * @brief 내부 누적 버퍼에서 JPEG 프레임을 분리/디코드/렌더링
 *
 * 파싱 전략:
 *  - JPEG SOI(0xFF 0xD8)부터 EOI(0xFF 0xD9)까지를 한 프레임으로 간주.
 *  - SOI가 없으면 다음 데이터 대기(버퍼가 비정상적으로 커지면 드랍).
 *  - SOI만 있고 EOI가 없으면 부분 프레임 → 다음 수신때까지 보관.
 *
 * 안정성:
 *  - 경계가 안 보이는 손상/이상 스트림 대비: 버퍼가 1MB를 넘으면 초기화.
 *  - 프레임을 성공 디코드하면 drawFrame()에서 리샘플링 및 화면 갱신.
 */
void MjpegView::parseBuffer(){
    // JPEG SOI(FFD8)/EOI(FFD9) 기준으로 프레임 분리
    while(true){
        int soi = m_buf.indexOf("\xFF\xD8", 0);
        if(soi < 0) {
            // SOI 자체가 없다면 프레임 시작점 대기.
            // 너무 커지면 스팸/손상 데이터로 보고 리셋.
            if(m_buf.size() > (1<<20)) m_buf.clear();
            break;
        }
        int eoi = m_buf.indexOf("\xFF\xD9", soi+2);
        if(eoi < 0) {
            // SOI는 있지만 EOI가 아직 없음 → 다음 수신까지 보류.
            // 앞부분 잡음 제거: SOI 이전은 삭제하여 버퍼 누수 방지.
            if(soi > 0) m_buf.remove(0, soi);
            break;
        }

        // [SOI..EOI] 구간을 하나의 JPEG로 추출
        int len = eoi - soi + 2;
        QByteArray jpg = m_buf.mid(soi, len);
        m_buf.remove(0, soi + len); // 사용한 구간 제거(다음 프레임 탐색 대비)

        // JPEG 디코드 → 성공 시 프레임 그리기
        QImage img = QImage::fromData(jpg, "JPG");
        if(!img.isNull()) drawFrame(img);
        // 실패한 경우(깨진 프레임)는 조용히 건너뛰고 다음 루프 진행
    }
}

/**
 * @brief 한 프레임을 현재 위젯 크기에 맞춰 비율 유지로 표시
 *
 *  - m_lastImg 캐시에 저장해 리사이즈 시 재활용.
 *  - QLabel 픽스맵으로 그려 flicker 없이 즉시 반영.
 */
void MjpegView::drawFrame(const QImage &img){
    m_lastImg = img;
    // Fit: 비율 유지 + 프레임 전체 보이기 (여백 허용)
    QPixmap px = QPixmap::fromImage(img).scaled(
        size(), Qt::KeepAspectRatio, Qt::SmoothTransformation
        );
    m_label->setPixmap(px);
}
