#ifndef MJPEGVIEW_H
#define MJPEGVIEW_H

#include <QWidget>                  // 위젯 기반 커스텀 뷰
#include <QLabel>                   // 프레임 표시(픽스맵)용 단일 라벨
#include <QNetworkAccessManager>    // HTTP 스트리밍(MJPEG) 수신
#include <QNetworkReply>            // 비동기 응답/에러/종료 신호
#include <QTimer>                   // 자동 재시도(백오프) 타이머
#include <QUrl>                     // 대상 스트림 URL 표현
#include <QImage>                   // 디코드된 JPEG 프레임 보관

/**
 * @brief 단일 HTTP MJPEG 스트림 뷰어
 *
 * 기능 요약(무엇을 하는가):
 *  - HTTP 응답 바이트 스트림에서 JPEG 프레임 경계(0xFFD8~0xFFD9)를 직접 찾아 분리/디코딩.
 *  - 최신 프레임을 QLabel 픽스맵으로 그려 간단하고 저비용으로 렌더링.
 *  - 네트워크 종료/에러 시 stop()으로 안전 정리 후 지연 재시도(retry)로 연결 복구 시도.
 *  - 리사이즈 시 마지막 프레임을 고품질 리샘플링(SmoothTransformation)하여 즉시 반영.
 *
 * 설계 포인트:
 *  - 네트워크는 QNetworkAccessManager 하나와 단일 QNetworkReply로 관리(keep-alive).
 *  - 내부 누적 버퍼에 조각 단위로 적재 → SOI/EOI 스캔으로 프레임 분리(parseBuffer).
 *  - 과도한/손상 데이터 대비 버퍼 상한 체크로 누수/메모리 팽창 방지.
 *  - UI 스레드에서만 라벨 갱신(스레드 전환 없음).
 */
class MjpegView : public QWidget {
    Q_OBJECT
public:
    explicit MjpegView(QWidget *parent=nullptr);
    ~MjpegView();

    /// 대상 MJPEG 엔드포인트 설정(유효성은 start() 시점 검사)
    void setUrl(const QUrl &url);
    /// 현재 설정된 URL 반환(상태 조회용)
    QUrl url() const { return m_url; }

    /**
     * @brief 스트림 수신 파이프라인 시작
     * 동작: 기존 연결 정리 → 유효 URL이면 GET 시작 → readyRead/finished/error 신호 연결 →
     *       라벨에 상태 텍스트 출력 및 버퍼 초기화.
     */
    void start();

    /**
     * @brief 스트림/네트워크 핸들 정리
     * 동작: reply 신호 해제 → 에러 없는 진행중일 때만 abort → deleteLater → 버퍼 초기화.
     * 목적: 중복 콜백/에러를 막고 안전한 재시작/종료 보장.
     */
    void stop();

signals:
    /// 라벨 좌클릭 신호(예: 전체 화면 전환/컨트롤 표시 등 상위 제어 트리거)
    void clicked();  // 미리보기 클릭(확대용)

protected:
    /// 리사이즈 시 마지막 프레임을 현재 크기에 맞춰 비율 유지 스케일링하여 즉시 재그리기
    void resizeEvent(QResizeEvent *e) override;
    /// 좌클릭을 clicked()로 래핑해 상위에서 제스처로 활용 가능하도록 노출
    void mousePressEvent(QMouseEvent *e) override;

private slots:
    /// 바디 수신: 조각 데이터를 내부 버퍼에 누적 후 프레임 분리(parseBuffer) 시도
    void onReadyRead();
    /// 서버가 연결을 닫거나 종료된 경우: stop() 후 재시도 타이머 기동
    void onFinished();
    /// 네트워크 오류: 상태 반영 → stop() → 재시도 타이머 기동
    void onError(QNetworkReply::NetworkError);
    /// 재시도 타이머 만료: 기존 reply가 없을 때에만 start() 호출(중복 연결 방지)
    void retry();

private:
    /**
     * @brief 디코드된 한 프레임을 QLabel 픽스맵으로 적용
     *  - m_lastImg에 캐시해 리사이즈 시 즉시 재표시 품질 확보
     *  - 스케일링 품질은 SmoothTransformation 사용
     */
    void drawFrame(const QImage &img);

    /**
     * @brief 내부 누적 버퍼에서 JPEG SOI/EOI(FFD8/FFD9) 경계를 찾아 프레임을 추출/디코드
     * 안정성:
     *  - SOI 미발견 상태에서 버퍼가 비정상적으로 커지면 드롭(스팸/손상 대비).
     *  - SOI만 있고 EOI가 없으면 다음 데이터까지 보류(부분 프레임).
     */
    void parseBuffer(); // SOI/EOI 기준으로 JPEG 분리

    /**
     * @brief 화면 맞춤 모드(미사용 옵션 토글)
     *  - true: Fill(꽉 채우기, 크롭 가능)
     *  - false: Fit(비율 유지, 레터박스 허용) — 현재 구현은 Fit 기준으로 drawFrame/resize에서 처리
     */
    bool m_fillMode = false; // ← true면 Fill(꽉 채우기), false면 Fit(여백 허용)

    // ── 표시부/상태 ─────────────────────────────────────────────
    QLabel *m_label{nullptr};        ///< 중앙 표시 라벨(픽스맵 적용 대상)
    QNetworkAccessManager m_nam;     ///< 단일 HTTP 세션 관리자(앱 스레드에서 사용)
    QNetworkReply *m_reply{nullptr}; ///< 진행중 응답 핸들(수신/종료/에러 신호 소스)
    QUrl m_url;                      ///< 대상 스트림 URL
    QByteArray m_buf;                ///< 조각 수신 누적 버퍼(프레임 경계 탐색용)
    QImage m_lastImg;                ///< 최근 프레임 캐시(리사이즈 즉시 반영)
    QTimer m_retryTimer;             ///< 자동 재시도 타이머(중복 시도 방지 로직 포함)
};

#endif // MJPEGVIEW_H
