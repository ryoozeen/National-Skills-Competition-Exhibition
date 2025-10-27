#pragma once
#include <QWidget>   // 기본 UI 컨테이너(페이지 베이스)
#include <QString>   // 카메라 이름/URL 문자열 처리

// 전방 선언(헤더 간 의존도/빌드 시간 최소화)
class QLabel;
class QPushButton;
class MjpegView;

/**
 * @brief 단일 카메라 전체화면(확대) 재생 페이지.
 *
 * 역할/기능
 * - 모니터링 화면에서 특정 카메라를 선택했을 때, 단독으로 크게 재생하는 뷰.
 * - 상단 바: [← 뒤로] + "카메라 보기 — {이름}" + 재생 상태 텍스트.
 * - 본문: 검정 배경의 videoBox 안에 MJPEG 전용 뷰어(MjpegView) 삽입/교체.
 *
 * 데이터 흐름
 * - loadCamera(name, url) 호출 시:
 *   1) 타이틀/상태 텍스트 즉시 갱신("연결 중…").
 *   2) 전달받은 URL에서 /mjpeg 경로 자동 보정(베이스 URL만 온 경우).
 *   3) 기 보유한 뷰어가 있으면 안전 정리(stop + deleteLater).
 *   4) 새 MjpegView를 videoBox에 부착하고 start()로 스트림 수신 시작.
 *   5) 상태 텍스트를 "스트림 재생 중"으로 갱신.
 *
 * 내비게이션
 * - 사용자가 [← 뒤로] 버튼을 누르면 backRequested() 시그널 방출.
 *   → 상위(AdminWindow)가 이를 받아 기존 페이지(모니터링)로 전환.
 *
 * 라이프사이클/소유권
 * - viewer_는 이 페이지가 소유하며, 새 카메라 로딩마다 교체 가능.
 * - 소멸자(암묵적) 시 부모-자식 관계에 의해 자식 위젯들은 함께 정리.
 */
class CameraViewerPage : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief 페이지 인스턴스 생성.
     * - buildUi()로 UI 트리를 구성하고 applyStyle()로 룩앤필을 적용.
     */
    explicit CameraViewerPage(QWidget* parent = nullptr);

public slots:
    /**
     * @brief 선택된 카메라 정보(이름/URL)로 재생 화면을 갱신/시작.
     *
     * 동작 요약
     * - 타이틀을 "카메라 보기 — {name}"로 변경.
     * - 상태 라벨을 "연결 중…" → 준비/오류/재생 중으로 단계 갱신.
     * - url이 베이스(예: http://host:port)만 온 경우 /mjpeg 경로를 자동 부착.
     * - 기존 뷰어가 있으면 stop() 후 안전 파기, 새 MjpegView를 생성/부착/start().
     *
     * 파라미터
     * @param name UI에 표시할 카메라 식별용 이름(좌상단 타이틀에 반영).
     * @param url  MJPEG 엔드포인트(또는 베이스 URL). 비어 있으면 "URL 없음" 상태 표시.
     */
    void loadCamera(const QString& name, const QString& url);

signals:
    /**
     * @brief 사용자 요청: 뒤로가기.
     * - 상위 컨테이너(AdminWindow 등)가 이 시그널을 받아 이전 페이지로 전환.
     */
    void backRequested();

private:
    /**
     * @brief 페이지 UI 트리 구성.
     * - 상단 바(뒤로 버튼/타이틀/상태) + 비디오 카드(videoBox) 배치.
     * - 초기에는 videoBox에 안내용 플레이스홀더 라벨을 중앙 배치.
     */
    void buildUi();

    /**
     * @brief 페이지 전용 스타일(QSS) 적용.
     * - 타이틀/상태/뒤로 버튼, 비디오 카드/박스의 룩앤필을 통일.
     * - videoBox는 검정 배경으로 지정하여 스트림 대비를 높임.
     */
    void applyStyle();

private:
    // ── 상단 바 구성 요소 ─────────────────────────────────────────
    QLabel*      titleLabel{};   ///< "카메라 보기 — {name}" 형태로 동적 갱신되는 제목
    QLabel*      statusLabel{};  ///< "연결 중…/재생 중/URL 없음" 등 스트림 상태 표시
    QPushButton* btnBack{};      ///< 뒤로가기 버튼(클릭 시 backRequested() 방출)

    // ── 비디오 영역 ───────────────────────────────────────────────
    QWidget*     videoBox{};     ///< 실제 스트림 위젯(MjpegView) 삽입 대상 컨테이너(검정 배경)
    MjpegView*   viewer_{nullptr}; ///< 현재 활성화된 MJPEG 뷰어(로드마다 교체/소유)

    // ── 상태 값 ──────────────────────────────────────────────────
    QString      currentUrl_;    ///< 마지막으로 요청된 원본 URL(디버그/재시도 힌트)
};
