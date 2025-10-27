#include "alerts_page.h"          // AlertsPage 선언부(시그널/슬롯, 멤버 접근)

// ===== Qt 위젯/레이아웃/표시 컴포넌트 =====
#include <QVBoxLayout>            // 세로 레이아웃(루트·상단바·하단바 배치)
#include <QHBoxLayout>            // 가로 레이아웃(필터 바, 하단 페이지네이션)
#include <QLabel>                 // 텍스트 라벨(타이틀, 라벨들)
#include <QLineEdit>              // 텍스트 입력(기간 필터: 시작/끝 날짜)
#include <QComboBox>              // 드롭다운(유형/레벨 필터)
#include <QPushButton>            // 버튼(새로고침 등)
#include <QTableWidget>           // 표 컴포넌트(알람/이벤트 로그 표시)
#include <QHeaderView>            // 테이블 헤더 설정(리사이즈 모드 등)
#include <QPalette>               // 위젯 배경·전경 색 구성
#include <QDateTime>              // 타임스탬프 표시/포맷
#include <QJsonObject>            // 서버 메시지(JSON) 파싱
#include <QJsonDocument>          // JSON 직렬화(Compact 문자열)
#include <QTableWidgetItem>       // 테이블 셀 아이템(툴팁·문자열 등)
#include <QThread>                // 스레드 확인(메인/UI 스레드 보장)
#include <QApplication>           // qApp (전역 QApplication 인스턴스 접근)
#include <QMetaObject>            // invokeMethod(스레드 전환/큐잉)

// ---- 관리자/설정 계열 메시지 필터 (표에 표시하지 않음) ----
// - 유저/관리자 관리용 트래픽(리스트, 추가/수정/삭제)은 Alerts 테이블의 "사고/이벤트 로그"
//   컨셉과 무관하므로 노이즈를 제거하기 위해 여기서 필터링
static inline bool isAdminMgmtMessage(const QJsonObject& m)
{
    const QString cmd = m.value("cmd").toString().toUpper();

    // USER_* 계열: 사용자 목록/CRUD/관리
    if (cmd == "USER_LIST" || cmd == "USER_LIST_OK" || cmd == "USER_LIST_FAIL" ||
        cmd == "USER_ADD"  || cmd == "USER_ADD_OK"  || cmd == "USER_ADD_FAIL"  ||
        cmd == "USER_UPDATE" || cmd == "USER_UPDATE_OK" || cmd == "USER_UPDATE_FAIL" ||
        cmd == "USER_DELETE" || cmd == "USER_DELETE_OK" || cmd == "USER_DELETE_FAIL" ||
        cmd.startsWith("ADMIN_") || cmd.startsWith("USER_"))
        return true;

    // 연결 헬스체크/핸드셰이크/업로드 준비 등 관리성 이벤트도 제외
    if (cmd == "HELLO" || cmd == "HELLO_OK" || cmd == "HELLO_FAIL" ||
        cmd == "PING"  || cmd == "PONG"     ||
        cmd == "UPLOAD_READY")
        return true;

    return false;
}

AlertsPage::AlertsPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();    // 화면 구성 요소(타이틀/필터/테이블/하단바) 조립
    applyStyle(); // 페이지 톤앤매너 적용(폰트/색/테이블 룩앤필)
}

void AlertsPage::buildUi() {
    // 루트 세로 레이아웃: (타이틀) → (필터 바) → (테이블) → (하단 페이지네이션)
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24,24,24,24);  // 페이지 전체 여백
    root->setSpacing(12);                   // 섹션 간 간격

    // 상단 타이틀
    auto *title = new QLabel(u8"알람 / 이벤트 로그");
    title->setObjectName("pageTitle");      // 스타일 시트에서 개별 지정할 ID
    root->addWidget(title);

    // 상단 필터 바(기간/유형/레벨 + 새로고침)
    auto *bar = new QHBoxLayout;
    bar->setSpacing(8);

    // 기간 필터(YYYY-MM-DD) — 표면 UI만, 실제 필터링 로직은 서비스 요건에 맞춰 별도 연결 가능
    startDate = new QLineEdit(this);
    endDate   = new QLineEdit(this);
    startDate->setPlaceholderText("YYYY-MM-DD");
    endDate->setPlaceholderText("YYYY-MM-DD");
    startDate->setFixedWidth(140);
    endDate->setFixedWidth(140);

    // 카메라 필터는 비활성화(요청에 따라 컬럼 제거와 일관성 유지)
    cameraCombo = nullptr;

    // 유형 필터(카테고리 드롭다운) — 실제 필터링을 붙이면 테이블 재구성을 트리거
    typeCombo = new QComboBox(this);
    typeCombo->addItems({u8"전체 유형", u8"침입 감지", u8"화재 감지", u8"근접 위험", u8"시스템 경고"});
    typeCombo->setFixedWidth(140);

    // 레벨 필터(심각도 드롭다운)
    levelCombo = new QComboBox(this);
    levelCombo->addItems({"ALL", "LOW", "MEDIUM", "HIGH", "CRITICAL"});
    levelCombo->setFixedWidth(120);

    // 수동 새로고침 버튼(데이터 소스와 연결 시 재조회 트리거)
    btnRefresh = new QPushButton(u8"새로고침", this);
    btnRefresh->setObjectName("priBtn"); // 스타일 시트에서 프라이머리 버튼 룩 적용

    // 필터 바 레이아웃 구성
    bar->addWidget(new QLabel(u8"기간"));
    bar->addWidget(startDate);
    bar->addWidget(new QLabel("~"));
    bar->addWidget(endDate);
    bar->addSpacing(12);
    // (카메라 라벨/콤보는 추가하지 않음)
    bar->addWidget(new QLabel(u8"유형"));
    bar->addWidget(typeCombo);
    bar->addWidget(new QLabel(u8"레벨"));
    bar->addWidget(levelCombo);
    bar->addStretch();            // 우측 정렬: 새로고침 버튼을 맨 오른쪽으로
    bar->addWidget(btnRefresh);

    root->addLayout(bar);

    // 이벤트 테이블 구성(6열: 시간, 유형, 레벨, 상태, 위치/라인, 설명)
    table = new QTableWidget(0, 6, this);
    table->setObjectName("alertsTable");
    QStringList headers{u8"시간", u8"유형", u8"레벨", u8"상태", u8"위치/라인", u8"설명"};
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);         // 마지막 열(설명) 가변 확장
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); // 내용 기반 리사이즈
    table->verticalHeader()->setVisible(false);                     // 행 헤더 숨김
    table->setSelectionBehavior(QTableWidget::SelectRows);          // 행 단위 선택
    table->setEditTriggers(QTableWidget::NoEditTriggers);           // 사용자 편집 비활성화
    table->setAlternatingRowColors(true);                           // 홀/짝 줄 배경 교차
    table->setMinimumHeight(420);                                   // 최소 높이(스크롤 영역 확보)

    root->addWidget(table, 1); // stretch=1: 테이블이 남는 세로 공간 채움

    // 하단 페이지네이션(표시 전용 라벨, 실제 페이저와의 연동은 필요 시 확장)
    auto *bottom = new QHBoxLayout;
    bottom->addStretch();                      // 라벨을 오른쪽 정렬
    pagerLabel = new QLabel(u8"1 / 1 페이지"); // 현재/총 페이지 간단 표시
    bottom->addWidget(pagerLabel);
    root->addLayout(bottom);
}

void AlertsPage::appendNotification(const QString& title, const QString& message)
{
    // 스레드 안전: UI 조작은 반드시 메인 스레드에서 수행
    if (QThread::currentThread() != qApp->thread()) {
        // 다른 스레드에서 들어온 경우 큐잉하여 메인 스레드로 hop
        QMetaObject::invokeMethod(this, [=]{ appendNotification(title, message); }, Qt::QueuedConnection);
        return;
    }

    // 도착 시각 스탬프(로컬 시간대) — 외부에서 별도 ts 없을 때 사용
    const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    // 테이블 최상단(행 0)에 새 알림 삽입
    table->insertRow(0);
    table->setItem(0,0,new QTableWidgetItem(ts));       // 시간
    table->setItem(0,1,new QTableWidgetItem("NOTICE")); // 유형 고정: NOTICE
    table->setItem(0,2,new QTableWidgetItem("INFO"));   // 레벨: INFO
    table->setItem(0,3,new QTableWidgetItem("-"));      // 상태: 없음

    auto *locItem  = new QTableWidgetItem(title.isEmpty()? "-" : title); // 위치/라인: 제목 대체
    locItem->setToolTip(title);                                          // 전체 제목 툴팁
    table->setItem(0,4,locItem);

    auto *descItem = new QTableWidgetItem(message);      // 설명: 본문
    descItem->setToolTip(message);                       // 전체 메시지 툴팁
    table->setItem(0,5,descItem);

    table->clearSelection();  // 삽입 후 포커스 표시 제거
    table->scrollToTop();     // 맨 위로 스크롤(최신 항목 가시성 확보)

    // 행 수 상한 관리(무한 증가 방지)
    const int MAX_ROWS = 1000;
    while (table->rowCount() > MAX_ROWS)
        table->removeRow(table->rowCount()-1);
}

void AlertsPage::appendJson(const QJsonObject& m)
{
    // 스레드 안전 보장: UI 스레드 여부 확인 후 필요 시 큐잉
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, [this, m]{ appendJson(m); },
                                  Qt::QueuedConnection);
        return;
    }
    // 관리자/설정류 메시지는 표에서 제외(노이즈 필터)
    if (isAdminMgmtMessage(m)) return;

    // 공통 타임스탬프 포맷터
    // - 서버가 ISO8601 문자열(ts)을 제공하면 이를 사용, 없거나 파싱 실패 시 현재 시각
    auto fmtTs = [](const QJsonObject& o)->QString {
        const QJsonValue v = o.value("ts");
        if (v.isString()) {
            QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODate);
            if (dt.isValid()) return dt.toString("yyyy-MM-dd HH:mm:ss");
        }
        return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    };

    const QString cmd = m.value("cmd").toString().toUpper();
    // 공장 상태 푸시(FACTORY_*)는 상태 캐시/다른 UI에서 처리됨 → 테이블 기록 제외
    if (cmd == "FACTORY_DATA" || cmd == "FACTORY_UPDATE" || cmd == "FACTORY_DATA_PUSH")
        return;

    // ✅ FIRE_EVENT: 표에 축약 표시(핵심 필드만) 후 처리 종료
    // - 중복/이중 경로 기록 방지를 위해 일반 경로로 내려보내지 않음
    if (cmd == "FIRE_EVENT") {
        const auto payload = m.value("payload").toObject();
        const QString ev    = payload.value("event").toString();      // 예: session_started / fire_confirmed
        const QString fname = payload.value("filename").toString();   // 관련 파일명(있을 때)
        const QString ts    = fmtTs(m);

        table->insertRow(0);
        table->setItem(0, 0, new QTableWidgetItem(ts));               // 시간
        table->setItem(0, 1, new QTableWidgetItem("FIRE_EVENT"));     // 유형
        table->setItem(0, 2, new QTableWidgetItem("INFO"));           // 레벨(간단 고정)
        table->setItem(0, 3, new QTableWidgetItem(ev));               // 상태 = payload.event
        table->setItem(0, 4, new QTableWidgetItem("-"));              // 위치/라인 = 없음
        auto *d = new QTableWidgetItem(fname.isEmpty() ? ev : fname); // 설명 = 파일명 또는 이벤트
        d->setToolTip(fname);
        table->setItem(0, 5, d);

        table->clearSelection();
        table->scrollToTop();
        const int MAX_ROWS = 1000;
        while (table->rowCount() > MAX_ROWS) table->removeRow(table->rowCount() - 1);
        return;  // ⬅️ 일반 경로로 내려가지 않음(이중 기록 차단)
    }

    // ↓ 일반 경로: 다양한 cmd를 공통 형식(6열)으로 테이블에 표준화하여 삽입
    auto asText = [](const QJsonObject& o)->QString {
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); // JSON 전체 축약 문자열
    };

    const QString ts    = fmtTs(m);              // 시간(서버 ts 있으면 사용)
    QString type        = cmd;                   // 유형 = cmd 기본
    QString level       = m.value("level").toString().toUpper(); // 레벨(없으면 아래서 추론)
    QString state       = "-";                   // 상태 기본값
    QString loc         = m.value("saved_path").toString();      // 위치/라인: 우선순위 path 필드들
    if (loc.isEmpty()) loc = m.value("path").toString();
    if (loc.isEmpty()) loc = m.value("file").toString();
    QString desc        = m.value("msg").toString();             // 설명: msg 없으면 원문 JSON

    if (desc.isEmpty()) desc = asText(m);

    // 레벨 기본값 추론 규칙(없을 때)
    if (level.isEmpty()) {
        if (cmd == "ROBOT_ERROR") level = "ERROR";
        else if (cmd == "UPLOAD_DONE") level = m.value("ok").toBool() ? "INFO" : "ERROR";
        else level = "INFO";
    }
    // 상태 표시(UPLOAD_DONE 전용)
    if (cmd == "UPLOAD_DONE") state = m.value("ok").toBool() ? "OK" : "FAIL";

    // 6열 스키마에 맞춰 한 행 삽입
    table->insertRow(0);
    table->setItem(0, 0, new QTableWidgetItem(ts));    // 시간
    table->setItem(0, 1, new QTableWidgetItem(type));  // 유형
    table->setItem(0, 2, new QTableWidgetItem(level)); // 레벨
    table->setItem(0, 3, new QTableWidgetItem(state)); // 상태

    auto *locItem = new QTableWidgetItem(loc);         // 위치/라인(파일/경로 등)
    locItem->setToolTip(loc);
    table->setItem(0, 4, locItem);

    auto *descItem = new QTableWidgetItem(desc);       // 설명(요약/원문)
    descItem->setToolTip(desc);
    table->setItem(0, 5, descItem);

    table->clearSelection();
    table->scrollToTop();

    // 행 수 상한 관리
    const int MAX_ROWS = 1000;
    while (table->rowCount() > MAX_ROWS)
        table->removeRow(table->rowCount()-1);

    // RobotPage 브릿지: 로봇 콘솔/미디어 재생/에러 상태와의 연동
    if (cmd == "UPLOAD_DONE" && m.value("ok").toBool()) {
        QString p = m.value("saved_path").toString();
        if (p.isEmpty()) p = m.value("path").toString();
        if (p.isEmpty()) p = m.value("file").toString();
        emit robotUploadDone(p, m);          // 파일 경로 + 원문 JSON 전달
    } else if (cmd == "ROBOT_EVENT") {
        emit robotEvent(m.value("level").toString(), m.value("msg").toString(), m);
    } else if (cmd == "ROBOT_ERROR") {
        emit robotError(m.value("msg").toString(), m);
    }
}

void AlertsPage::applyStyle() {
    // 페이지 배경 톤 지정(팔레트 기반) — 상위 스타일과 독립적으로 일괄 적용
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor("#eaf0ff")); // 배경 라이트 블루
    setAutoFillBackground(true);
    setPalette(pal);

    // CSS 유사 규칙으로 상세 룩앤필 적용
    setStyleSheet(R"(
        QWidget { font-family:'Malgun Gothic','Noto Sans KR',sans-serif; font-size:14px; }
        #pageTitle { font-size:22px; font-weight:700; color:#111827; }

        QLineEdit {
            height: 32px;
            padding: 4px 8px;
            border: 1px solid #c7d2fe;
            border-radius: 6px;
            background: #ffffff;
        }
        QComboBox {
            height: 32px;
            border: 1px solid #c7d2fe;
            border-radius: 6px;
            background: #ffffff;
        }
        QPushButton#priBtn {
            font-weight: 700;
            color: #fff;
            border-radius: 8px;
            padding: 8px 14px;
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #6aa3ff, stop:1 #3c73db); /* 프라이머리 그라데이션 */
        }
        QTableWidget#alertsTable {
            background:#ffffff;
            border:1px solid #dbe3ff;
            border-radius:12px;         /* 카드형 테이블 느낌 */
        }
        QHeaderView::section {
            background:#eef3ff;
            border: none;
            padding:6px;
            font-weight:700;            /* 헤더 강조 */
        }
        QTableWidget::item:selected { background:#dfe9ff; } /* 선택 강조 */
    )");
}
