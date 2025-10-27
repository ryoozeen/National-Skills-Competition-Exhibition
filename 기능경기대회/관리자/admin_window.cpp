// AdminWindow: 안전관리 클라이언트의 메인 컨테이너(좌측 사이드바 + 우측 페이지 스택)
// - 좌측: 사용자/회사/시계/알림/메뉴(모니터링, 근태, 알림, 로봇, 수동, 설정)
// - 우측: QStackedWidget 기반 페이지 전환
// - 서버에서 오는 JSON 메시지는 중앙 큐에서 단일 흐름으로 분기 처리
#include "admin_window.h"

// ===== Qt 위젯/레이아웃/유틸 =====
#include <QStackedWidget>                // 다중 페이지 관리(중앙 콘텐츠 교체)
#include <QHBoxLayout>                   // 수평 레이아웃(루트: 좌측/우측 분할)
#include <QVBoxLayout>                   // 수직 레이아웃(사이드바 내부 구성)
#include <QFrame>                        // 사이드바 컨테이너(스타일 식별용)
#include <QLabel>                        // 텍스트/아이콘 표시(회사명/사용자/시계 등)
#include <QPushButton>                   // 메뉴 버튼/로그아웃 버튼
#include <QTimer>                        // 주기 작업(시계, 메시지 큐 배치 처리)
#include <QDateTime>                     // 날짜/시간(시계, 로그 타임스탬프)
#include <QSizePolicy>                   // 위젯 크기 정책(버튼 높이 고정 등)
#include <QButtonGroup>                  // 메뉴 라디오 그룹(단일 선택 보장)
#include <QCoreApplication>              // 실행 파일 경로(ini 로딩)
#include <QSettings>                     // INI 설정 읽기(카메라 URL)
#include <QJsonValue>                    // JSON 값 타입 유틸
#include <QJsonObject>                   // JSON 오브젝트(서버 메시지)
#include <QJsonDocument>                 // JSON 직렬화(로그/표시)
#include <QQueue>                        // 메시지 큐(서버 → 중앙 분배)
#include <QDebug>                        // 디버그 출력

#include "networkclient.h"
#include "monitoring_page.h"
#include "attendance_page.h"
#include "alerts_page.h"
#include "robot_page.h"
#include "settings_page.h"
#include "manual_control_page.h"
#include "camera_viewer_page.h"
#include "notification.h"

AdminWindow::AdminWindow(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyStyle();

    // 메인 윈도우 자체 배경도 하늘색으로 고정
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("QMainWindow, QWidget{background:#eaf0ff;}");

}

// 로그인 성공 후 호출: 네트워크 핸들을 주입하고
// - 중앙 메시지 파이프라인(큐)로 수신 경로를 단일화
// - 각 페이지와 상태/이벤트 신호를 기능별로 연결
void AdminWindow::setNetwork(NetworkClient* net)
{
    net_ = net;                 // 주입된 네트워크 객체를 내부 보관(이후 모든 통신의 단일 진입점)
    if (!net_) return;          // 네트워크가 없으면 어떤 연결도 만들지 않음(안전 종료)

    // 소유권 이전: AdminWindow가 파괴되면 net_도 함께 파괴되도록 Qt 객체 트리 편입
    // - 로그인 창이 먼저 닫혀도 net_가 살아 있어야 하는 상황을 방지
    net_->setParent(this);

    // 재주입/재연결 대비: 이전에 걸어둔 모든 연결 해제(중복 신호 방지)
    // - (sender=net_, receiver=this|alertsPage|manualPage|robotPage, signal/slot=모두)
    QObject::disconnect(net_, nullptr, this,        nullptr);
    QObject::disconnect(net_, nullptr, alertsPage,  nullptr);
    QObject::disconnect(net_, nullptr, manualPage,  nullptr);
    QObject::disconnect(net_, nullptr, robotPage,   nullptr);

    // [수신 경로 표준화]
    // NetworkClient::messageReceived → 중앙 큐(msgQueue_)에 적재하고
    // 0ms single-shot 타이머(msgTimer_)로 UI 스레드에서 순차 처리(프레임 드랍/경합 방지)
    // - Qt::QueuedConnection: 다른 스레드에서 올라온 신호를 안전하게 큐잉
    connect(net_, &NetworkClient::messageReceived, this,
            [this](const QJsonObject& m){
                msgQueue_.enqueue(m);     // 선입선출 보장(메시지 폭주 시에도 순서 유지)
                if (msgTimer_) msgTimer_->start();  // buildUi()에서 구성된 0ms 타이머 트리거
            },
            Qt::QueuedConnection);

    // [설정/권한 페이지 초기화]
    // - 설정 탭이 존재하면 동일 핸들을 전달(내부에서 유저/권한 목록 요청 등 자체 로직 수행)
    if (auto* sp = qobject_cast<SettingsPage*>(stack->widget(idxSettings))) {
        sp->setNetwork(net_);
    }

    // [UI → 서버] 수동 제어 요청 라우팅
    // - ESTOP 토글을 서버로 전송(서버 → UI 반영은 handleServerMessage() 경로에서 일원화)
    if (manualPage) {
        connect(manualPage, &ManualControlPage::requestEmergencyStop,
                this, [this](bool engage){
                    if (!net_) return; // 주입 해제/종료 순간 대비
                    net_->sendJson(QJsonObject{
                        {"cmd","ESTOP_SET"},         // 명령 식별자
                        {"engaged",engage}           // 활성/해제 상태
                    });
                });
        // ※ DOOR_SET / RUN_SET 등 다른 제어 신호도 동일 패턴으로 추가 가능
    }

    // [연결 상태 표시]
    // - 네트워크 연결 상태/에러를 로봇 콘솔 페이지에 반영(표시 전용, 로직 비개입)
    if (robotPage) {
        connect(net_, &NetworkClient::stateChanged,
                robotPage, &RobotPage::setConnectionState,
                Qt::QueuedConnection); // 스레드 경계 안전 반영

        connect(net_, &NetworkClient::errorOccurred,
                robotPage, &RobotPage::setNetworkError,
                Qt::QueuedConnection);
    }

    // [페이지 간 브릿지] AlertsPage → RobotPage
    // - 증거 업로드/로봇 이벤트/에러를 로봇 콘솔 로그와 플레이어에 전달
    if (alertsPage && robotPage) {
        // 중복 연결 방지: 이전에 걸린 Alerts→Robot 연결을 모두 제거
        QObject::disconnect(alertsPage, nullptr, robotPage, nullptr);

        // 람다 슬롯은 UniqueConnection 비교가 애매할 수 있어(람다 개체 식별 문제)
        // 명시적으로 disconnect 후, 큐잉만 적용(CTQ2)
        constexpr auto CTQ2 = Qt::QueuedConnection;

        // [업로드 완료 알림] → 로봇 콘솔 로그에 기록 + 파일 경로 있으면 재생
        connect(alertsPage, &AlertsPage::robotUploadDone,
                robotPage,  [rp = robotPage](const QString& path, const QJsonObject& full){
                    const QString now = QDateTime::currentDateTime().toString("HH:mm:ss"); // 표시용 시각
                    rp->appendRobotEvent(
                        now,
                        u8"UPLOAD_DONE",                                                // 이벤트 태그
                        QString::fromUtf8(QJsonDocument(full).toJson(QJsonDocument::Compact)) // 원문 JSON
                        );
                    if (!path.isEmpty()) rp->playEvidenceFile(path); // 증거 파일 미디어 재생
                }, CTQ2);

        // [일반 로봇 이벤트] → 로그에 기록, error 레벨은 에러 상태로도 반영
        connect(alertsPage, &AlertsPage::robotEvent,
                robotPage,  [rp = robotPage](const QString& level, const QString& message, const QJsonObject& full){
                    const QString now = QDateTime::currentDateTime().toString("HH:mm:ss");
                    rp->appendRobotEvent(
                        now,
                        level.isEmpty()? u8"ROBOT_EVENT" : level,                         // 레벨 기본값
                        message.isEmpty()
                            ? QString::fromUtf8(QJsonDocument(full).toJson(QJsonDocument::Compact))
                            : message                                                     // 커스텀 메시지 우선
                        );
                    if (level.compare("error", Qt::CaseInsensitive) == 0)                 // 에러 레벨이면
                        rp->setNetworkError(message.isEmpty()? u8"로봇 오류" : message);   // 상태 라벨에도 반영
                }, CTQ2);

        // [로봇 에러 전용] → 로그 기록 + 에러 상태 갱신(문구 없으면 원문 JSON 압축 표시)
        connect(alertsPage, &AlertsPage::robotError, robotPage,
                [this](const QString& message, const QJsonObject& full){
                    const QString now = QDateTime::currentDateTime().toString("HH:mm:ss");
                    robotPage->appendRobotEvent(
                        now,
                        u8"ROBOT_ERROR",
                        message.isEmpty()
                            ? QString::fromUtf8(QJsonDocument(full).toJson(QJsonDocument::Compact))
                            : message
                        );
                    robotPage->setNetworkError(message.isEmpty()? u8"로봇 오류" : message);
                }, CTQ2);
    }
}

// 좌측 사이드바에 들어갈 공통 스타일의 메뉴 버튼을 생성
QPushButton* AdminWindow::makeSidebarButton(const QString& text) {
    auto* b = new QPushButton(text, this);           // 부모를 AdminWindow로 지정(수명 함께 관리)
    b->setCheckable(true);                           // 토글 가능(선택 상태 유지 → 메뉴 탭처럼 사용)
    b->setMinimumHeight(44);                         // 접근성/가독성 확보를 위한 최소 높이
    b->setCursor(Qt::PointingHandCursor);            // 마우스 오버 시 손가락 커서로 피드백 제공
    b->setStyleSheet(R"(
        QPushButton {
            text-align:left; padding:10px 12px;      /* 텍스트 좌측 정렬, 내부 여백 */
            margin-bottom:8px;                       /* 버튼 간 세로 간격 */
            border-radius:10px; border:1px solid transparent; /* 라운드+기본 테두리 투명 */
            background: transparent; color:#111827; font-weight:600; /* 평상시 색/굵기 */
        }
        QPushButton:hover {
            background:#eef3ff; border-color:#dbe3ff; /* 호버 시 배경/보더로 초점 강조 */
        }
        QPushButton:checked {
            background:#dfe9ff; border:1px solid #c7d2fe; color:#0b0f19; /* 선택 상태 표현 */
            // box-shadow: 0 0 0 2px rgba(99,102,241,0.15) inset;         /* (옵션) 인셋 그림자 */
        }
    )");
    return b;                                        // 호출 측에서 레이아웃에 배치
}

void AdminWindow::buildUi() {
    // 루트 레이아웃: 좌(사이드바), 우(콘텐츠 스택) 2분할
    auto* root = new QHBoxLayout(this);              // AdminWindow를 레이아웃 소유자로 설정
    root->setContentsMargins(0,0,0,0);               // 외곽 여백 제거(풀블리드 느낌)
    root->setSpacing(0);                             // 영역 간 간격 제거(절단선 없이 자연스럽게)

    // ===== 사이드바 =====
    auto* side = new QFrame(this);                   // 좌측 고정 패널 컨테이너
    side->setObjectName("sidebar");                  // 스타일시트 선택자( applyStyle() 에서 사용 )
    side->setFixedWidth(280);                        // 고정 폭으로 레이아웃 안정화
    auto* sv = new QVBoxLayout(side);                // 사이드바 내부 수직 스택
    sv->setContentsMargins(16,16,16,16);             // 내부 여백(좌우/상하 패딩)
    sv->setSpacing(12);                              // 위젯 간 기본 간격

    auto* brand = new QLabel(u8"안전관리 시스템");   // 좌측 상단 브랜드/타이틀
    brand->setStyleSheet("font-size:18px; font-weight:800; color:#0b0f19;"); // 시각적 앵커 역할

    profileIcon = new QLabel;                        // 프로필 아이콘 placeholder(원형 배경 블록)
    profileIcon->setFixedSize(40,40);                // 정사각형 크기
    profileIcon->setStyleSheet("background:#c7d2fe; border-radius:20px;"); // 원형 처리

    companyLabel = new QLabel(u8"회사명(데모)");     // 회사명 표시
    companyLabel->setStyleSheet("font-weight:700;"); // 굵게
    companyLabel->setStyleSheet("color:#111827;");   // (참고) 앞선 스타일을 이 줄이 덮어씀

    userLabel = new QLabel(u8"사용자(데모)");        // 사용자명 표시
    userLabel->setStyleSheet("color:#374151;");      // 중간 톤으로 위계 부여

    dateLabel = new QLabel;                          // 날짜(YYYY-MM-DD)
    dateLabel->setStyleSheet("color:#6b7280; font-size:12px;");              // 보조 정보 톤/크기

    timeLabel = new QLabel;                          // 시각(HH:mm:ss)
    timeLabel->setStyleSheet("color:#6b7280; font-size:14px; font-weight:600;"); // 약간 강조

    auto* clockTimer = new QTimer(this);             // 1초 주기의 시계 갱신 타이머
    connect(clockTimer, &QTimer::timeout, this, [this]{
        const QDateTime now = QDateTime::currentDateTime(); // 현재 시각
        dateLabel->setText(now.toString("yyyy-MM-dd"));     // 날짜 라벨 업데이트
        timeLabel->setText(now.toString("HH:mm:ss"));       // 시각 라벨 업데이트
    });
    clockTimer->start(1000);                         // 1000ms 주기

    auto* idBox = new QVBoxLayout;                   // 회사/사용자/날짜/시간 묶음
    idBox->setSpacing(2);                            // 항목 간 촘촘한 간격
    idBox->addWidget(companyLabel);
    idBox->addWidget(userLabel);
    idBox->addWidget(dateLabel);
    idBox->addWidget(timeLabel);

    // 알림 버튼(배지/팝업 트리거)과 팝업 리스트 초기화
    notiBtn = new NotificationButton(this);          // 상단 알림 아이콘/카운트
    notiBtn->setFixedHeight(36);                     // 버튼 높이 통일
    notiBtn->setStyleSheet("color:#111827;");        // 다크 텍스트로 좌측 패널 톤 유지
    notiListPopup = new NotificationListPopup(this); // 부모를 지정해 위치/수명 관리

    auto* topRow = new QHBoxLayout;                  // 프로필/정보/알림을 가로 정렬
    topRow->addWidget(profileIcon, 0, Qt::AlignTop); // 아이콘: 상단 정렬
    topRow->addLayout(idBox, 1);                     // 정보 묶음: 스트레치 1(가변)
    topRow->addWidget(notiBtn, 0, Qt::AlignTop);     // 알림 버튼: 상단 정렬

    // 로그아웃 버튼(상단 영역 바로 아래 배치)
    btnLogout = new QPushButton(u8"로그아웃");
    btnLogout->setObjectName("logoutBtn");           // 스타일시트에서 개별 스타일 적용용 ID
    btnLogout->setFixedHeight(36);                   // 높이 통일
    btnLogout->setStyleSheet("color:#111827;");      // 텍스트 컬러 고정
    btnLogout->setSizePolicy(QSizePolicy::Expanding, // 가로폭은 남는 공간을 채움
                             QSizePolicy::Fixed);    // 세로는 고정

    // 사이드바 화면 구성 요소를 상단부터 순서대로 추가
    sv->addWidget(brand);
    sv->addLayout(topRow);
    sv->addWidget(btnLogout);
    sv->addSpacing(8);                               // 상단 영역과 메뉴 간 간격

    // 메뉴 버튼 생성(공통 빌더로 동일 룩앤필)
    btnMon = makeSidebarButton(u8"모니터링");        // 대시보드
    btnAtt = makeSidebarButton(u8"사원 목록/근태 관리");
    btnAlm = makeSidebarButton(u8"알람/이벤트 로그");
    btnRbt = makeSidebarButton(u8"로봇 콘솔");
    btnMan = makeSidebarButton(u8"수동 조작");
    btnSet = makeSidebarButton(u8"설정/권한");

    // 메뉴 버튼을 세로로 배치하고, 아래는 가변 공간으로 밀어 하단 붙음 방지
    sv->addWidget(btnMon);
    sv->addWidget(btnAtt);
    sv->addWidget(btnAlm);
    sv->addWidget(btnRbt);
    sv->addWidget(btnMan);
    sv->addWidget(btnSet);
    sv->addStretch();                                // 남은 공간 소모(상단 그룹 유지)

    root->addWidget(side);                           // 루트 레이아웃 왼쪽에 사이드바 추가

    // ===== 스택 영역 =====
    stack         = new QStackedWidget(this);        // 우측 콘텐츠 컨테이너(페이지 전환)
    auto* monPage = new MonitoringPage(this);        // 모니터링 페이지 인스턴스
    idxMonitoring = stack->addWidget(monPage);       // 인덱스 저장(전환 시 사용)

    // 근태 페이지는 초기 로딩 비용이 크다고 가정 → 지연 생성용 placeholder 삽입
    idxAttendance = stack->addWidget(new QWidget(this));  // 첫 진입 때 실체로 교체

    alertsPage    = new AlertsPage(this);            // 알림/이벤트 로그 페이지
    idxAlerts     = stack->addWidget(alertsPage);

    robotPage     = new RobotPage(this);             // 로봇 콘솔(로그/재생 등) — 멤버 보관
    idxRobot      = stack->addWidget(robotPage);

    manualPage    = new ManualControlPage(this);     // 수동 조작(설비 제어)
    idxManual     = stack->addWidget(manualPage);

    camViewer     = new CameraViewerPage(this);      // 카메라 단일 뷰(확대)
    idxCamViewer  = stack->addWidget(camViewer);

    idxSettings   = stack->addWidget(new SettingsPage(this)); // 설정/권한

    root->addWidget(stack, 1);                       // 우측 스택: stretch 1(남는 너비 차지)

    // ✅ 초기 카메라 URL을 INI에서 읽어 주입 (없으면 빈 문자열 유지)
    {
        QSettings ini(QCoreApplication::applicationDirPath() + "/admin_client.ini",
                      QSettings::IniFormat);         // 실행 파일 옆의 INI 로딩
        ini.beginGroup("camera");                    // [camera] 섹션
        const QString entrance = ini.value("entrance_url").toString(); // 출입구 카메라 URL
        const QString fire     = ini.value("fire_url").toString();     // 화재 카메라 URL
        ini.endGroup();

        monPage->setEntranceCamUrl(entrance);        // 모니터링 타일에 초기 URL 주입
        monPage->setFireCamUrl(fire);
    }

    // 모니터링에서 특정 카메라 선택 시 단일 뷰로 전환
    connect(monPage, &MonitoringPage::cameraSelected,
            this, &AdminWindow::openCameraViewer);

    // 단일 뷰에서 "뒤로" 액션 시 모니터링 탭으로 복귀
    connect(camViewer, &CameraViewerPage::backRequested,
            this, &AdminWindow::goMonitoring);

    // ===== 메뉴 버튼 그룹 =====
    menuGroup = new QButtonGroup(this);              // 라디오처럼 단일 선택 보장
    menuGroup->setExclusive(true);                   // 동시 다중 체크 금지
    menuGroup->addButton(btnMon, 0);                 // 버튼 ↔ 페이지 인덱스 맵핑
    menuGroup->addButton(btnAtt, 1);
    menuGroup->addButton(btnAlm, 2);
    menuGroup->addButton(btnRbt, 3);
    menuGroup->addButton(btnMan, 4);
    menuGroup->addButton(btnSet, 5);

    // 버튼 클릭 시 ID로 분기하여 페이지 전환
    connect(menuGroup, &QButtonGroup::idClicked, this, [this](int id){
        switch (id) {
        case 0: goMonitoring(); break;
        case 1: goAttendance(); break;   // 지연 생성: 첫 진입 시 실제 페이지로 교체됨
        case 2: goAlerts();     break;
        case 3: goRobot();      break;
        case 4: goManual();     break;
        case 5: goSettings();   break;
        }
    });

    btnMon->setChecked(true);                          // 초기 선택 상태(모니터링)
    stack->setCurrentIndex(idxMonitoring);             // 초기 표시 페이지 지정

    // ===== Notification 연동 =====

    // 알림 목록에서 항목 활성화 시 알림 탭으로 이동(관련 로그/상세 확인 흐름)
    connect(notiListPopup, &NotificationListPopup::itemActivated,
            this,
            [this](const QString&, const QString&, const QDateTime&){
                goAlerts();
            });

    // NotificationManager 이벤트를 팝업/리스트/배지에 반영
    auto CTQ = Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection); // 스레드 안전+중복 방지
    connect(&NotificationManager::instance(),
            &NotificationManager::notificationAdded,
            this,
            &AdminWindow::showNotificationPopup,
            CTQ);                                      // 새 알림 → 토스트 팝업
    connect(&NotificationManager::instance(),
            &NotificationManager::notificationAdded,
            alertsPage,
            &AlertsPage::appendNotification,
            CTQ);                                      // 새 알림 → 알림 리스트 추가
    connect(&NotificationManager::instance(),
            &NotificationManager::notificationCountChanged,
            notiBtn,
            &NotificationButton::setNotificationCount,
            CTQ);                                      // 카운트 변화 → 배지 업데이트

    // 알림 버튼 클릭 시 팝업 토글(열림/닫힘)
    connect(notiBtn, &QPushButton::clicked, this, [this]{
        if (notiListPopup) notiListPopup->toggleFor(notiBtn);
    });

    // 로그아웃 버튼 클릭 시 상위로 신호 전파(세션 정리/화면 전환)
    connect(btnLogout, &QPushButton::clicked, this, [this]{ emit logoutRequested(); });

    // 중앙 메시지 큐 타이머 (0ms, 싱글샷)
    if (!msgTimer_) {
        msgTimer_ = new QTimer(this);                 // UI 스레드에서만 실행되는 타이머
        msgTimer_->setInterval(0);                    // 다음 이벤트 루프 사이클에 곧바로 실행
        msgTimer_->setSingleShot(true);               // 1회성 실행(루프에서 필요 시 재가동)

        // 큐 적재된 서버 메시지를 일정량(budget)만큼씩 배치 처리
        connect(msgTimer_, &QTimer::timeout, this, [this]{
            if (processingMsg_) return;               // 재진입 방지(동시 실행 차단)
            processingMsg_ = true;                    // 처리중 플래그 세팅
            int budget = 200;                         // 1틱당 처리 상한(폭주/프리즈 방지)
            while (!msgQueue_.isEmpty() && budget-- > 0) {
                const QJsonObject m = msgQueue_.dequeue(); // FIFO: 순서 보장
                handleServerMessage(m);                // 실제 분기 처리(알림 라우팅/캐시 반영 등)
            }
            processingMsg_ = false;                   // 처리 종료

            // 아직 남아있다면 타이머 재시작(다음 틱에서 이어서 처리)
            if (!msgQueue_.isEmpty()) msgTimer_->start();
        });
    }
}

// 전역 스타일을 한 번에 적용하는 함수
void AdminWindow::applyStyle() {
    // setStyleSheet: 이 위젯 하위 트리에 CSS 유사 규칙을 일괄 적용
    setStyleSheet(R"(
        /* 기본 폰트 패밀리 지정(한국어 가독성 우선, 폴백 포함) */
        QWidget { font-family:'Malgun Gothic','Noto Sans KR',sans-serif; }

        /* ID가 sidebar인 컨테이너(좌측 패널)의 배경/분리선 지정 */
        #sidebar {
            background:#f5f7ff;
            border-right:1px solid #e5e7eb;
        }

        /* 사이드바 영역 내부의 텍스트/버튼 색상을 어둡게 고정(상태/테마 무관) */
        #sidebar QLabel { color:#111827; }
        #sidebar QPushButton { color:#111827; }

        /* 로그아웃 버튼의 룩앤필(hover/disabled 상태 포함) */
        #logoutBtn {
            background:#ffffff;               /* 평상시 흰 배경 */
            border:1px solid #dbe3ff;         /* 옅은 파랑 테두리 */
            border-radius:8px;                /* 라운드 모서리 */
            padding:10px 12px;                /* 터치/클릭 여유 */
            color:#111827;                    /* 글자색 고정 */
            font-weight:700;                  /* 굵게 */
        }
        #logoutBtn:hover   { background:#eef3ff; }   /* 포커스 피드백 */
        #logoutBtn:disabled{ color:#111827; }        /* 비활성이어도 글자색 유지 */
    )");
}

// 간단 토스트/팝업 알림 출력 함수
void AdminWindow::showNotificationPopup(const QString& title, const QString& message) {
    // notiPopup이 아직 없으면 생성.
    // 부모를 nullptr로 두어 최상위(top-level) 윈도우로 띄움 → 클리핑 방지/항상 위 표시 용이
    if (!notiPopup) notiPopup = new NotificationPopup(nullptr);
    // 주어진 제목/본문으로 즉시 표시
    notiPopup->showNotification(title, message);
}

/* =========================
 *       페이지 전환
 * ========================= */

// 모니터링 탭으로 전환(사이드바 버튼 상태 동기화)
void AdminWindow::goMonitoring() {
    stack->setCurrentIndex(idxMonitoring);   // 중앙 스택의 현재 페이지를 모니터링으로
    if (btnMon) btnMon->setChecked(true);    // 좌측 메뉴 버튼의 선택 상태를 일치
}

// 근태 탭으로 전환(지연 생성된 실제 페이지로 교체)
void AdminWindow::goAttendance() {
    if (!attendancePage) {
        // placeholder를 실제 페이지로 교체(초기 로딩 지연 전략)
        QWidget* old = stack->widget(idxAttendance);  // 기존 자리의 플레이스홀더
        if (old) {                                    // 스택에서 제거하고
            stack->removeWidget(old);
            old->deleteLater();                       // 안전한 시점에 파괴(시그널 레퍼런스 대비)
        }
        attendancePage = new AttendancePage(this);    // 실제 페이지 생성(부모=AdminWindow)
        stack->insertWidget(idxAttendance, attendancePage); // 동일 인덱스 위치에 삽입
    }
    stack->setCurrentIndex(idxAttendance);            // 근태 페이지로 전환
    if (btnAtt) btnAtt->setChecked(true);             // 사이드바 버튼 상태 동기화
}

// 알림/이벤트 로그 탭으로 전환
void AdminWindow::goAlerts() {
    stack->setCurrentIndex(idxAlerts);
    if (btnAlm) btnAlm->setChecked(true);
}

// 로봇 콘솔 탭으로 전환
void AdminWindow::goRobot() {
    stack->setCurrentIndex(idxRobot);
    if (btnRbt) btnRbt->setChecked(true);
}

// 수동 조작 탭으로 전환
void AdminWindow::goManual() {
    stack->setCurrentIndex(idxManual);
    if (btnMan) btnMan->setChecked(true);
}

// 설정/권한 탭으로 전환
void AdminWindow::goSettings() {
    stack->setCurrentIndex(idxSettings);
    if (btnSet) btnSet->setChecked(true);
}

// 모니터링 타일에서 카메라를 선택했을 때 단일 뷰(확대)로 전환
void AdminWindow::openCameraViewer(const QString& name, const QString& url) {
    if (camViewer) camViewer->loadCamera(name, url);  // 단일 뷰 위젯에 카메라 메타/스트림 주입
    stack->setCurrentIndex(idxCamViewer);             // 중앙 스택을 카메라 뷰어 페이지로 전환
    if (btnMon) btnMon->setChecked(true);             // 사이드바는 "모니터링"을 계속 강조(정보 구조 유지)
}

// 사이드바의 사용자명 표시 갱신(표시 목적, 빈 값이면 데모 텍스트 유지)
void AdminWindow::setUserName(const QString& name) {
    if (userLabel) userLabel->setText(name.isEmpty() ? u8"사용자(데모)" : name);
}

// 사이드바의 회사명 표시 갱신(표시 목적, 빈 값이면 데모 텍스트 유지)
void AdminWindow::setCompanyName(const QString& company) {
    if (companyLabel) companyLabel->setText(company.isEmpty() ? u8"회사명(데모)" : company);
}

/*
 * JSON 객체에서 "사람이 읽을 대표 문자열"을 뽑아내는 헬퍼
 * - keys 리스트 순서대로 우선 탐색하며 첫 성공값을 즉시 반환
 * - 타입 허용: string / number / bool / object
 *   - string  : 그대로 반환
 *   - number  : 소수 포함 숫자를 QString로 변환
 *   - bool    : true/false 문자열로 변환
 *   - object  : 흔한 중첩 구조(payload.event, error.message 등)에서
 *               내부 대표 키(event, id, incident_id, message, reason, zone, area) 재귀 탐색
 * - 실패 시 빈 문자열 반환 (호출부에서 비어있음 판단)
 */
static QString pickStr(const QJsonObject& obj, std::initializer_list<const char*> keys) {
    for (auto k: keys) {
        const auto v = obj.value(k);                  // 현재 키의 값 조회
        if (v.isString()) return v.toString();        // 문자열: 즉시 반환
        if (v.isDouble()) return QString::number(v.toDouble()); // 숫자: 문자열화
        if (v.isBool())   return v.toBool() ? "true" : "false"; // 불리언: "true"/"false"

        // 객체 타입: 대표 필드(event/id/message/reason/zone/area 등) 재귀 탐색
        if (v.isObject()) {
            // 흔한 중첩: payload.event / error.message / payload.{id,zone,area} …
            const auto o = v.toObject();
            auto inner = pickStr(o, {"event","id","incident_id","message","reason","zone","area"});
            if (!inner.isEmpty()) return inner;       // 내부에서 대표 문자열을 찾으면 반환
        }
    }
    return {};                                        // 어떤 키에서도 값을 못 찾으면 빈 문자열
}

// 서버에서 수신한 단일 JSON 메시지를 명령(cmd) 단위로 분기 처리
// - 중복 억제(dupGuard_)와 쿨다운(fireCooldownMs_)로 스팸/폭주 방지
// - 상태 반영과 알림/로그 포워딩을 역할 분리
void AdminWindow::handleServerMessage(const QJsonObject& msg) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();         // 중복/쿨다운 판단용 현재 시각(ms)
    const QString cmd = msg.value("cmd").toString().toUpper();      // 명령 식별자(대문자 비교)

    // 공통 파라미터 추출(존재하면 사용)
    // - id: 사건/요청을 구분하는 식별자(중복 키 구성에 사용)
    // - eventStr: 하위 이벤트명(payload.event 포함)
    // - reason: 실패/오류 사유(문구)
    // - savedPath: 업로드/저장 경로(성공 케이스 구분/로그 표시)
    // - ok: 성공/실패 플래그(UPLOAD_DONE 분기 등)
    const QString id        = pickStr(msg, {"incident_id","id","task_id","request_id"});
    const QString eventStr  = pickStr(msg, {"event","payload"});         // payload.event까지 커버
    const QString reason    = pickStr(msg, {"reason","error","message"});
    const QString savedPath = pickStr(msg, {"saved_path","path","url"});
    const bool    ok        = msg.value("ok").toBool(false);

    // 단순 시간창 기반 중복 억제 함수
    // - key: 이벤트 성격을 대표하는 문자열("CMD|subtype|id|...")
    // - dupWindowMs_ 내 재도착 시 false(차단), 통과 시 처리 후 타임스탬프 갱신
    auto dedup = [&](const QString& key)->bool {
        const qint64 last = dupGuard_.value(key, 0);
        if (now - last < dupWindowMs_) return false; // 너무 최근 → 차단
        dupGuard_[key] = now;
        return true;
    };

    // -------------------- 명령 분기 시작 --------------------

    // [즉시 UI 반영] 비상정지 상태 갱신(표시 우선)
    // - ESTOP 상태는 수동 제어 패널의 토글에 바로 반영하여 일관성 유지
    if (cmd == "ESTOP_STATE") {
        if (manualPage) manualPage->setEmergencyStop(msg.value("engaged").toBool());
        // Alerts 테이블로의 별도 기록은 생략(필요 시 아래 "기본 포워딩" 경로로도 내려갈 수 있음)
        // return;  // 여기서 반환하면 포워딩 차단, 주석 해제 시 UI 반영만 수행
    }

    // [특수 이벤트] 화재 감지 흐름
    if (cmd == "FIRE_EVENT") {
        // (1) 확정 이벤트: 노이즈 억제를 위해 쿨다운 적용
        // - 동일 사건 ID에 대해 일정 시간 내 중복 알림/로그 방지
        if (eventStr.compare("fire_confirmed", Qt::CaseInsensitive) == 0) {
            if (now - lastFireConfirmedMs_ >= fireCooldownMs_) {      // 쿨다운 초과 여부
                const QString key = "FIRE_EVENT|confirmed|" + id;     // 중복 억제 키
                if (dedup(key)) {
                    // 필요 시 NotificationManager로 토스트/배지 증가 트리거 가능 지점
                    lastFireConfirmedMs_ = now;                        // 마지막 확정 시각 갱신
                }
            }
            return;      // 확정 이벤트는 이중 기록 방지를 위해 Alerts 포워딩 차단
        }
        // (2) 세션 종료: 후속 처리(업로드/정리) 전환 시점 알림
        if (eventStr.compare("session_ended", Qt::CaseInsensitive) == 0) {
            const QString key = "FIRE_EVENT|ended|" + id;
            if (dedup(key)) {
                // 필요 시 종료 알림/상태 전환 트리거
            }
            return;  // 현재 구현에서는 별도 테이블 포워딩 없이 종료(필요 시 주석 해제)
        }
        // 그 외 FIRE_EVENT는 이 경로에서 종료하여 이중 경로 유입을 차단
        return;
    }

    // [중요] 로봇 이동 실패 알림
    // - 동일 id+reason의 반복 실패 스팸 억제
    // - Alerts 포워딩은 허용(테이블 기록 목적)
    if (cmd == "GO_TO_FAIL") {
        const QString key = "GO_TO_FAIL|" + id + "|" + reason;  // 원인까지 포함해 중복 억제
        if (dedup(key)) {
            // 필요 시 즉시 알림 트리거 가능(토스트/사운드 등)
        }
        // return;  // 반환하지 않으면 아래 "기본 포워딩" 경로로 테이블에 남김
    }

    // [실패 케이스] 업로드 완료 신호이나 ok=false
    // - id+path+reason 조합으로 중복 억제
    if (cmd == "UPLOAD_DONE" && !ok) {
        const QString key = "UPLOAD_DONE|FAIL|" + id + "|" + savedPath + "|" + reason;
        if (dedup(key)) {
            // 필요 시 실패 알림 트리거
        }
        // return;  // 반환하지 않으면 Alerts에 실패 기록 남김
    }

    // [성공 케이스] 업로드 완료(ok=true)
    // - id+path 조합으로 중복 억제
    if (cmd == "UPLOAD_DONE" && ok) {
        const QString key = "UPLOAD_DONE|OK|" + id + "|" + savedPath;
        if (dedup(key)) {
            // 필요 시 성공 알림/진행 상태 알림 트리거
        }
        // return;  // 반환하지 않으면 Alerts에 성공 기록 남김
    }

    // [상태 푸시] 설비(공장) 상태 알림: UI/캐시만 갱신하고 종료
    // - 로그/알림 스팸화를 막기 위해 테이블 기록/토스트는 하지 않음
    if (cmd == "FACTORY_DATA" || cmd == "FACTORY_UPDATE" || cmd == "FACTORY_DATA_PUSH") {
        // 다양한 타입(int/bool)을 유연히 정수로 수용하는 헬퍼
        auto getInt = [&](const char* k, int def)->int {
            const auto v = msg.value(k);
            if (v.isDouble()) return int(v.toDouble());      // JSON number → int
            if (v.isBool())   return v.toBool() ? 1 : 0;     // JSON bool   → 0/1
            return def;                                       // 없으면 이전 캐시 유지
        };

        // 후보 키: run, door, helmet_ok, error/fault(서버 별칭 차이 고려)
        const int run    = getInt("run",        lastRun_);
        const int door   = getInt("door",       lastDoor_);
        const int helmet = getInt("helmet_ok",  lastHelmetOk_);
        int err          = getInt("error",      lastError_);
        if (err < 0)     err = getInt("fault",  lastError_); // 'fault' 키 호환

        // 변화 감지(초기값 -1은 무시)
        const bool changedRun    = (run    != -1 && run    != lastRun_);
        const bool changedDoor   = (door   != -1 && door   != lastDoor_);
        const bool changedHelmet = (helmet != -1 && helmet != lastHelmetOk_);
        const bool changedErr    = (err    != -1 && err    != lastError_);

        // UI 반영: 수동 조작 패널의 가동/문 상태만 즉시 업데이트
        if (manualPage && (changedRun || changedDoor)) {
            const int runForUi  = (run  == -1 ? lastRun_  : run);
            const int doorForUi = (door == -1 ? lastDoor_ : door);
            manualPage->setFactoryState(runForUi, doorForUi);
        }

        // 캐시 업데이트(표시/후속 비교 용도)
        if (changedRun)    lastRun_      = run;
        if (changedDoor)   lastDoor_     = door;
        if (changedHelmet) lastHelmetOk_ = helmet;
        if (changedErr)    lastError_    = err;

        return;  // 상태 푸시는 여기서 종료(아래 Alerts 테이블 포워딩 금지)
    }

    // === 기본 포워딩 규칙 ===
    // 위에서 별도로 'return'하지 않은 메시지는 알림 테이블에 단일 기록
    if (alertsPage) alertsPage->appendJson(msg);     // AlertsPage가 테이블/리스트로 표시
    qDebug() << "[DBG] append end" << msg.value("cmd").toString(); // 디버그: 최종 기록 확인
}

// 소멸자: 메시지 처리 루프를 안전하게 정지하고 내부 큐를 비움
AdminWindow::~AdminWindow() {
    if (msgTimer_) msgTimer_->stop(); // 배치 타이머 중지(추가 처리 방지)
    msgQueue_.clear();                // 대기 중 메시지 폐기(종료 일관성 보장)
}
