#pragma once
// 헤더 중복 포함을 한 번으로 제한하는 지시자(헤더가 여러 번 include되어도 단일 컴파일로 보장)

#include <QWidget>       // AdminWindow의 기반 클래스(시각적 컨테이너/이벤트 루프 통합)
#include <QHash>         // 키-값 해시 컨테이너(이벤트 중복 방지 타임스탬프 캐시)
#include <QJsonObject>   // 서버/클라이언트 간 JSON 메시지 표현(키-값 맵)
#include <QQueue>        // 메시지 큐(네트워크 수신을 단일 처리 파이프라인으로 직렬화)
#include <QTimer>        // 배치 처리·쿨다운·주기 작업 트리거(0ms single-shot 포함)
#include <QJsonObject>   // [중복 포함] 기능은 동일(위와 동일 역할) — 정리 시 하나만 남겨도 무방
#include <QPointer>      // QObject 안전 포인터(파괴 시 nullptr 자동화로 UAF 방지)

#include "notification.h" // 알림 UI/매니저 컴포넌트(배지, 팝업, 리스트 등과 연동)

// ===== 전방 선언(상호 참조/빌드 시간 최적화) =====
class NetworkClient;         // 서버와의 비동기 메시지 송수신 담당
class QStackedWidget;        // 중앙 콘텐츠 페이지 스택(화면 전환 컨테이너)
class QPushButton;           // 사이드바·로그아웃 등 상호작용 버튼
class QLabel;                // 회사/사용자/시계/아이콘 등 텍스트·픽토그램 표시
class QButtonGroup;          // 좌측 메뉴 단일 선택 보장(라디오 그룹 기능)

// ===== 페이지/팝업 전방 선언 =====
class MonitoringPage;        // 모니터링 대시보드(카메라 썸네일, 상태 타일 등)
class AttendancePage;        // 근태·사원 목록(지연 생성)
class AlertsPage;            // 알림/이벤트 로그(테이블/스트림)
class RobotPage;             // 로봇 콘솔(이벤트 로그, 에러, 미디어 재생)
class SettingsPage;          // 설정/권한(초기 권한/유저 목록 요청 포함 가능)
class ManualControlPage;     // 설비 수동 제어(ESTOP/문/가동 상태 표시·명령)
class CameraViewerPage;      // 단일 카메라 확대 뷰(뒤로가기 내장)
class NotificationListPopup; // 알림 목록 팝업(배지 클릭으로 토글)

// ===== 메인 윈도우(좌측 사이드바 + 우측 스택) =====
class AdminWindow : public QWidget {
    Q_OBJECT
public:
    explicit AdminWindow(QWidget* parent = nullptr);
    // 로그인 성공 후, 외부 로그인 창에서 주입되는 네트워크 핸들(수명은 AdminWindow 소유권 하에 관리)
    void setNetwork(NetworkClient* net);

    // 사이드바 표시용 사용자/회사명 텍스트 갱신(표시 전용)
    void setUserName(const QString& name);
    void setCompanyName(const QString& company);

signals:
    // 상위(로그인 창 등)로 로그아웃 의사를 알리는 신호(세션 종료/화면 전환 트리거)
    void logoutRequested();

private slots:
    // 토스트/팝업 형태의 즉시 알림 표시(제목/본문 전달)
    void showNotificationPopup(const QString& title, const QString& message);

    // 페이지 전환 단축 슬롯(사이드바 버튼/팝업 인터랙션과 연결)
    void goMonitoring();
    void goAttendance();
    void goAlerts();
    void goRobot();
    void goManual();
    void goSettings();

    // 모니터링 → 카메라 단일 뷰 전환(선택된 카메라 이름/URL 전달)
    void openCameraViewer(const QString& name, const QString& url);

private:
    // ===================== 중앙 메시지 파이프라인 =====================
    // - 네트워크 수신을 UI 스레드에서 순차 처리하기 위한 버퍼
    // - 폭주 방지, 프레임 안정성, 순서 보장에 초점
    QQueue<QJsonObject> msgQueue_; // 서버에서 들어온 JSON 메시지 대기열(선입선출)
    bool   processingMsg_ = false; // 재진입 방지 플래그(동시 처리 차단)
    QTimer* msgTimer_     = nullptr; // 0ms single-shot 배치 타이머(틱마다 일정량 처리)

    // 네트워크 송수신 컴포넌트(연결 상태/에러/수신 이벤트를 신호로 제공)
    NetworkClient* net_{};

    // JSON 메시지 라우팅 엔진(명령별 분기/중복 억제/캐시·UI 반영)
    void handleServerMessage(const QJsonObject& msg);

    // 초기 UI 트리 구성(사이드바/스택/버튼/라벨/알림·메뉴 연동)
    void buildUi();
    // 전역 스타일시트 적용(사이드바 톤, 버튼 룩앤필, 폰트 등)
    void applyStyle();
    // 사이드바 공통 버튼 빌더(체크 가능, 좌측 정렬, 호버/체크 스타일)
    QPushButton* makeSidebarButton(const QString& text);

    // ===================== 중복 억제/쿨다운 메커니즘 =====================
    // - 동일 성격의 이벤트를 짧은 간격으로 반복 기록하지 않도록 차단
    QHash<QString, qint64> dupGuard_;     // (cmd|id|event 등) → 최근 처리 시각(ms)
    qint64  lastFireConfirmedMs_ = 0;     // 화재 확정 이벤트의 마지막 처리 시각
    const qint64 dupWindowMs_     = 3000; // 일반 이벤트 중복 억제 창(3초)
    const qint64 fireCooldownMs_  = 20*1000; // 화재 확정 쿨다운(20초)

private:
    // ===================== 페이지 스택/인덱스 =====================
    QStackedWidget* stack{};  // 중앙 콘텐츠 컨테이너(페이지 전환의 단일 진입점)
    int idxMonitoring{-1};    // 모니터링 탭 인덱스
    int idxAttendance{-1};    // 근태 탭 인덱스(지연 생성)
    int idxAlerts{-1};        // 알림 로그 탭 인덱스
    int idxRobot{-1};         // 로봇 콘솔 탭 인덱스
    int idxManual{-1};        // 수동 조작 탭 인덱스
    int idxCamViewer{-1};     // 카메라 단일 뷰 인덱스
    int idxSettings{-1};      // 설정/권한 탭 인덱스

    // ===================== 설비 상태 캐시 =====================
    // - FACTORY_* 푸시 메시지로부터 추출된 최근 상태(표시·변화 감지용)
    int lastRun_      = -1;   // 설비 가동 상태 캐시(-1: 미정)
    int lastDoor_     = -1;   // 설비 문 상태 캐시(-1: 미정)
    int lastHelmetOk_ = -1;   // 헬멧 착용 정상 여부 캐시(-1: 미정)
    int lastError_    = -1;   // 설비 에러/결함 코드 캐시(-1: 미정)

    // ===================== 페이지 포인터 =====================
    // 지연 생성: 최초 진입 시 실체화하여 초기 로딩 시간 최적화
    AttendancePage*    attendancePage{}; // 근태 페이지(지연 생성 대상)
    RobotPage*         robotPage{};      // 로봇 콘솔(상태/로그/미디어 재생)
    AlertsPage*        alertsPage{};     // 알림 로그(appendJson/브릿지 이벤트 소스)
    ManualControlPage* manualPage{};     // 설비 수동 조작(ESTOP/문/가동)
    CameraViewerPage*  camViewer{};      // 단일 카메라 뷰(뒤로 전환 포함)

    // ===================== 사이드바(메뉴/버튼) =====================
    QButtonGroup* menuGroup{}; // 메뉴 단일 선택 보장(라디오 그룹 역할)
    QPushButton* btnMon{};     // 모니터링
    QPushButton* btnAtt{};     // 근태
    QPushButton* btnAlm{};     // 알림/이벤트
    QPushButton* btnRbt{};     // 로봇 콘솔
    QPushButton* btnMan{};     // 수동 조작
    QPushButton* btnSet{};     // 설정/권한

    // ===================== 상단 정보/알림 위젯 =====================
    NotificationButton*             notiBtn{};         // 알림 배지/버튼(카운트 표기)
    QPointer<NotificationPopup>     notiPopup{};       // 토스트 팝업(안전 포인터로 생명주기 보호)
    QPointer<NotificationListPopup> notiListPopup{};   // 알림 목록 팝업(토글 표시)
    QPushButton* btnLogout{};                          // 로그아웃 트리거 버튼
    QLabel*      profileIcon{};                        // 프로필 아이콘(원형 배경)
    QLabel*      companyLabel{};                       // 회사명 표시
    QLabel*      userLabel{};                          // 사용자명 표시
    QLabel*      dateLabel{};                          // 날짜 표시 라벨(YYYY-MM-DD)
    QLabel*      timeLabel{};                          // 시각 표시 라벨(HH:mm:ss)

protected:
    ~AdminWindow() override; // 소멸 시 리소스/타이머/큐 정리(객체 트리 기반 자동 해제 연계)
    // ※ protected 선언은 외부에서 임의 delete를 제한하고, Qt 부모-자식 소멸 규칙에 따르는 의도를 반영
};
