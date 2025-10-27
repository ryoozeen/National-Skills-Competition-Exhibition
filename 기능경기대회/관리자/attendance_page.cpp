#include "attendance_page.h"
#include <QVBoxLayout>            // 세로 레이아웃: 페이지 상하 배치
#include <QHBoxLayout>            // 가로 레이아웃: 상단 툴바/검색바 구성
#include <QFormLayout>            // (미사용) 폼 레이아웃(필요 시 라벨-필드 쌍 만들 때 사용)
#include <QLabel>                 // 정적 텍스트(타이틀/라벨)
#include <QLineEdit>              // 텍스트 입력(검색/기간)
#include <QComboBox>              // 드롭다운(부서/상태/근로자)
#include <QPushButton>            // 버튼(검색/추가/수정/삭제/조회)
#include <QTableWidget>           // 테이블(사원/근태 목록 표시)
#include <QHeaderView>            // 테이블 헤더 설정(리사이즈/정렬)
#include <QPalette>               // 위젯 배경/전경 팔레트
#include <QStringList>            // 문자열 목록(헤더 라벨 등)
#include <QDate>                  // 날짜 처리(기간 기본값/파싱)
#include <QSqlQuery>              // SQL 실행(SELECT/바인딩)
#include <QSqlError>              // SQL 에러 추적
#include <QSqlRecord>             // 결과 레코드 접근
#include <QMessageBox>            // 사용자 피드백(알림/경고)
#include <QVariant>               // 타입 안전 값 컨테이너(SQL 바인딩/읽기)
#include <QApplication>           // qApp(전역 스타일/팔레트 접근)
#include <QStyleFactory>          // 스타일 팩토리(Fusion 강제)

AttendancePage::AttendancePage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();    // 페이지 UI 트리 구성(탭, 검색바, 테이블 등)
    applyStyle(); // 일관된 룩앤필 적용(폰트/색/테이블/탭)

    // DB 연결 시도(필수 의존성: Qt::Sql 링크 + QMYSQL 드라이버)
    if (!openDb()) {
        QMessageBox::warning(this, u8"DB 연결 실패",
                             u8"DB에 연결할 수 없습니다.\nCMake에 Qt::Sql이 링크되었는지, MySQL(QMYSQL) 드라이버가 설치되었는지 확인하세요.");
        return; // DB 없이는 목록/근태 로딩 불가 → 초기화 중단
    }

    // 기본 조회 기간: 최근 7일
    atTo->setText(QDate::currentDate().toString("yyyy-MM-dd"));
    atFrom->setText(QDate::currentDate().addDays(-7).toString("yyyy-MM-dd"));

    // 초기 데이터 로딩: 콤보/사원 목록/근태 기록
    reloadWorkerCombo(); // 근로자 드롭다운(“전체 근로자”+이름(사번))
    loadEmployees();     // 사원 테이블
    loadAttendance();    // 근태 테이블

    // 사용자 액션 연결: 검색/조회
    connect(btnSearch,  &QPushButton::clicked, this, &AttendancePage::loadEmployees);
    connect(btnRefresh, &QPushButton::clicked, this, &AttendancePage::loadAttendance);
}


void AttendancePage::buildUi() {
    // 루트 수직 레이아웃: 페이지 상단 타이틀 → 탭 컨테이너(사원/근태) 순서로 배치
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24,24,24,24); // 페이지 외곽 여백(좌/우/상/하)
    root->setSpacing(12);                  // 섹션 간 간격(타이틀/탭 사이 등)

    // 상단 페이지 타이틀(스타일 지정 용이하도록 ObjectName 부여)
    auto *title = new QLabel(u8"사원 목록/근태 관리");
    title->setObjectName("pageTitle");               // applyStyle()에서 개별 룩앤필 적용 대상
    title->setAttribute(Qt::WA_StyledBackground, true); // 상위 배경 전파 차단(페이지 톤 유지)
    root->addWidget(title);

    // 상단 탭 컨테이너: "사원", "근태" 두 화면을 탭으로 전환
    tabs = new QTabWidget(this);
    tabs->setObjectName("attTabs");                  // QSS에서 탭/팬 생성 규칙에 사용
    tabs->setAttribute(Qt::WA_StyledBackground, true); // 탭 배경 톤 강제
    root->addWidget(tabs, 1); // stretch=1: 탭이 남는 세로 공간을 채움

    // ===== 탭1: 근로자 관리(사원 목록/검색/CRUD) =====
    {
        auto *page = new QWidget(this);                 // 탭1 컨테이너
        page->setAttribute(Qt::WA_StyledBackground, true);
        auto *v = new QVBoxLayout(page);                // 탭 본문 수직 스택
        v->setSpacing(10);
        v->setContentsMargins(12,8,12,8);               // 탭 패널과 표 사이 여백(시각적 여유)

        // 상단 검색/액션 바: 이름/부서/상태 필터 + 검색/추가/수정/삭제
        auto *bar = new QHBoxLayout;
        bar->setSpacing(8);

        kwName = new QLineEdit(page);                   // 이름 키워드 입력
        kwName->setPlaceholderText(u8"이름 검색");     // 힌트 텍스트
        kwName->setFixedWidth(160);                     // 폭 고정(레이아웃 안정)

        kwDept = new QComboBox(page);                   // 부서 필터
        kwDept->addItems({u8"전체 부서", u8"생산", u8"품질", u8"설비", u8"관리"});
        kwDept->setFixedWidth(120);                     // 드롭다운 폭

        kwStatus = new QComboBox(page);                 // 재직 상태 필터
        kwStatus->addItems({u8"전체 상태", u8"재직", u8"휴가", u8"퇴사"});
        kwStatus->setFixedWidth(120);

        btnSearch = new QPushButton(u8"검색", page);    // 사원 목록 재검색 트리거
        btnSearch->setObjectName("priBtn");            // 프라이머리 버튼 룩 적용용

        // 오른쪽 CRUD 액션(신규/수정/삭제) — 실제 동작은 별도 슬롯 구현 예정
        btnAdd    = new QPushButton(u8"추가", page);
        btnEdit   = new QPushButton(u8"수정", page);
        btnRemove = new QPushButton(u8"삭제", page);
        for (auto *b : {btnAdd, btnEdit, btnRemove}) b->setObjectName("secBtn"); // 세컨더리 룩

        // 검색/액션 바 좌→우 배치: 필터들 → (빈공간) → 검색/CRUD 버튼
        bar->addWidget(new QLabel(u8"이름"));
        bar->addWidget(kwName);
        bar->addWidget(new QLabel(u8"부서"));
        bar->addWidget(kwDept);
        bar->addWidget(new QLabel(u8"상태"));
        bar->addWidget(kwStatus);
        bar->addStretch();          // 남는 공간 확보(우측 버튼군을 끝으로 밀기)
        bar->addWidget(btnSearch);
        bar->addSpacing(8);         // 검색과 CRUD 버튼군 사이 간격
        bar->addWidget(btnAdd);
        bar->addWidget(btnEdit);
        bar->addWidget(btnRemove);

        // 사원 테이블(7열 스키마): 사번/이름/부서/직무/상태/연락처/비고
        tblWorkers = new QTableWidget(0, 7, page);
        tblWorkers->setObjectName("workersTable");     // 스타일/테스트 식별자
        QStringList headers{u8"사번", u8"이름", u8"부서", u8"직무", u8"상태", u8"연락처", u8"비고"};
        tblWorkers->setHorizontalHeaderLabels(headers);
        tblWorkers->horizontalHeader()->setStretchLastSection(true);             // 마지막 열(비고) 확장
        tblWorkers->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); // 내용 기반 폭 조절
        tblWorkers->verticalHeader()->setVisible(false);                         // 좌측 행 번호 숨김
        tblWorkers->setSelectionBehavior(QTableWidget::SelectRows);              // 셀 대신 "행" 선택
        tblWorkers->setEditTriggers(QTableWidget::NoEditTriggers);               // 직접 편집 금지(보기 전용)
        tblWorkers->setAlternatingRowColors(false);                              // 교차색 비활성(테마 일관)
        tblWorkers->setMinimumHeight(420);                                       // 최소 높이(스크롤 확보)
        tblWorkers->verticalHeader()->setDefaultSectionSize(32);                 // 행 높이(가독성)
        tblWorkers->setShowGrid(true);                                           // 그리드 표시(셀 구분 명확)

        // 탭1 완성: 상단 바 + 테이블
        v->addLayout(bar);
        v->addWidget(tblWorkers, 1);   // 테이블이 남는 공간을 채우도록
        tabs->addTab(page, u8"사원");  // 탭 라벨
    }

    // ===== 탭2: 출석 기록(기간/근로자 필터 → 출/퇴근 집계) =====
    {
        auto *page = new QWidget(this);                 // 탭2 컨테이너
        page->setAttribute(Qt::WA_StyledBackground, true);
        auto *v = new QVBoxLayout(page);
        v->setSpacing(10);
        v->setContentsMargins(12,8,12,8);

        // 상단 조건 바: 기간(From~To) + 근로자 선택 + 조회 버튼
        auto *bar = new QHBoxLayout;
        bar->setSpacing(8);

        atFrom = new QLineEdit(page);                   // 시작일(YYYY-MM-DD)
        atTo   = new QLineEdit(page);                   // 종료일(YYYY-MM-DD)
        atFrom->setPlaceholderText("YYYY-MM-DD");      // 입력 힌트
        atTo->setPlaceholderText("YYYY-MM-DD");
        atFrom->setFixedWidth(140);                     // 날짜 필드 폭
        atTo->setFixedWidth(140);

        atWorker = new QComboBox(page);                 // 특정 근로자 필터(없으면 전체)
        atWorker->addItem(u8"전체 근로자");             // data() invalid → 전체 검색 의미
        atWorker->setFixedWidth(160);

        btnRefresh = new QPushButton(u8"조회", page);   // 근태 데이터 로딩 트리거
        btnRefresh->setObjectName("priBtn");           // 프라이머리 룩

        // 조건 바 배치: 기간 → (간격) → 근로자 → (빈공간) → 조회
        bar->addWidget(new QLabel(u8"기간"));
        bar->addWidget(atFrom);
        bar->addWidget(new QLabel("~"));
        bar->addWidget(atTo);
        bar->addSpacing(12);
        bar->addWidget(new QLabel(u8"근로자"));
        bar->addWidget(atWorker);
        bar->addStretch();
        bar->addWidget(btnRefresh);

        // 근태 테이블(7열): 일자/사번/이름/부서/출근/퇴근/근무시간
        //  - gate_check에서 같은 날짜의 MIN=출근, MAX=퇴근으로 취급(스키마 특성 반영)
        tblAttendance = new QTableWidget(0, 7, page);
        tblAttendance->setObjectName("attTable");
        QStringList headers{u8"일자", u8"사번", u8"이름", u8"부서", u8"출근", u8"퇴근", u8"근무시간"};
        tblAttendance->setHorizontalHeaderLabels(headers);
        tblAttendance->horizontalHeader()->setStretchLastSection(true);          // 근무시간 열 확장
        tblAttendance->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        tblAttendance->verticalHeader()->setVisible(false);
        tblAttendance->setSelectionBehavior(QTableWidget::SelectRows);
        tblAttendance->setEditTriggers(QTableWidget::NoEditTriggers);
        tblAttendance->setAlternatingRowColors(false);
        tblAttendance->setMinimumHeight(420);
        tblAttendance->verticalHeader()->setDefaultSectionSize(32);
        tblAttendance->setShowGrid(true);

        // 탭2 완성: 상단 조건 바 + 근태 테이블
        v->addLayout(bar);
        v->addWidget(tblAttendance, 1);
        tabs->addTab(page, u8"근태");
    }
}

void AttendancePage::applyStyle() {
    // =========================
    // THEME — 여기만 바꾸면 전체가 변합니다
    //  - UI 톤/치수/라운드 등 공통 상수 정의
    //  - 아래 QSS 조립 시 자리표시자(%1..%16)로 주입됨
    // =========================
    const QString COL_BG_WINDOW   = "#eaf0ff"; // 전체 페이지 배경(하늘색, 눈부심 낮춤)
    const QString COL_TEXT        = "#111827"; // 기본 텍스트 컬러(가독성 높은 짙은 회색)
    const QString COL_BTN_BLUE_0  = "#4B8BFF"; // 프라이머리/세컨더리 버튼 그라데이션 상단
    const QString COL_BTN_BLUE_1  = "#1E5EEA"; // 프라이머리/세컨더리 버튼 그라데이션 하단

    const QString COL_PANEL       = COL_BG_WINDOW; // 탭 컨테이너/페이지 패널 배경(일관 톤)
    const QString COL_PANEL_BORDER= "transparent"; // 탭 컨테이너 외곽선 비표시(플랫 느낌)
    const QString COL_TAB         = COL_BG_WINDOW; // 비선택 탭 배경
    const QString COL_TAB_SEL     = COL_BG_WINDOW; // 선택 탭 배경(동일 톤 적용)

    const QString COL_TABLE_BG    = "#ffffff"; // 테이블 카드 배경(흰색, 데이터 대비 강화)
    const QString COL_TABLE_SEL   = "#dfe9ff"; // 테이블 행 선택 색(라이트 블루)
    const int     RADIUS_BOX      = 8;         // 박스형 UI(버튼/패널) 라운드 반경(px)
    const int     RADIUS_TABLE    = 12;        // 테이블 카드 라운드 반경(px)
    const int     FONT_BASE       = 14;        // 페이지 기본 폰트 크기(px)
    const int     FONT_TITLE      = 22;        // 타이틀 폰트 크기(px)
    const int     BTN_PAD_V       = 8;         // 버튼 세로 패딩(px)
    const int     BTN_PAD_H       = 14;        // 버튼 가로 패딩(px)

    // 0) 전역 렌더링 일관성 강제:
    //    - OS 테마/다크모드·플랫폼 기본 팔레트 영향 차단
    //    - 주의: qApp 전역 스타일 변경 → 앱 전체에 영향. 페이지 로컬화가 필요하면 삭제/이동 고려.
    qApp->setStyle(QStyleFactory::create("Fusion"));
    qApp->setPalette(QPalette()); // 시스템 팔레트 초기화(라이트/다크 영향 제거)

    // 1) 이 페이지의 기본 배경을 팔레트로 고정
    //    - 상위 위젯 팔레트 전파를 받지 않도록 AutoFillBackground 사용
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(COL_BG_WINDOW));
    setAutoFillBackground(true);
    setPalette(pal);

    // 2) QSS 일괄 적용
    //    - 위에서 정의한 THEME 상수를 자리표시자에 주입하여 한 번에 룩앤필 구성
    //    - 개별 위젯 ObjectName/클래스 셀렉터로 섬세하게 스코프 제어
    const QString qss = QString(R"qss(
        /* ====== 전체 공통: 폰트/텍스트/배경 고정 ====== */
        QWidget{
            font-family:'Malgun Gothic','Noto Sans KR',sans-serif;
            font-size:%1px;
            color:%2;
            background:%14;                  /* 시스템/부모 테마 영향 차단용 기본 배경 */
        }

        /* 페이지 타이틀(별도 ObjectName) */
        #pageTitle{
            font-size:%3px;
            font-weight:700;
            color:%2;
            background: transparent;         /* 여백 겹침 시 잔상 방지 */
            background:%14;                  /* 최종 배경은 페이지 톤으로 통일 */
        }

        /* ====== 탭 컨테이너/탭 버튼 ====== */
        QTabWidget#attTabs::pane{
            background:%4;                   /* 패널 배경(하늘색) */
            border:1px solid %5;             /* 외곽선 비표시 */
            border-radius:%6px;              /* 컨테이너 라운드 */
        }
        /* 탭 페이지(콘텐츠 영역) 바탕도 하늘색으로 고정: 플랫폼 기본 회색 비침 방지 */
        QTabWidget#attTabs > QWidget { background:%4; }

        /* 탭 버튼(선택/비선택 동일 톤 → 시각적 안정) */
        QTabBar::tab{
            background:%7;
            color:%2;
            padding:6px 12px;
            border:1px solid #d1dbff;        /* 은은한 분리감 */
            border-bottom:none;               /* 콘텐츠 영역과 자연스럽게 접합 */
            border-top-left-radius:6px;
            border-top-right-radius:6px;
            margin-right:6px;                 /* 탭 간 간격 */
        }
        QTabBar::tab:selected{
            background:%8;
            color:%2;
        }

        /* ====== 입력/콤보: 라이트 스킨 강제 ====== */
        QLineEdit, QComboBox{
            height:32px;
            border:1px solid #c7d2fe;
            border-radius:6px;
            background:#ffffff;
            color:%2;
            padding:0 8px;                    /* 텍스트 좌우 여백 */
        }

        /* 콤보 드롭다운(view)도 라이트 스킨 고정(다크모드 침투 방지) */
        QComboBox QAbstractItemView{
            background:#ffffff;
            color:%2;
            selection-background-color:%9;
            selection-color:%2;
            border:1px solid #c7d2fe;
            border-radius:6px;
            outline:0;                        /* 포커스 링 제거 */
        }
        QComboBox QAbstractItemView::item{
            background:#ffffff;
            color:%2;
        }

        /* ====== 버튼(프라이머리/세컨더리 공통 룩) ====== */
        QPushButton#priBtn, QPushButton#secBtn{
            font-weight:700;
            color:#ffffff;
            border:none;
            border-radius:%6px;
            padding:%10px %11px;              /* 터치 타겟 넉넉히 */
            background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %12, stop:1 %13);
        }
        QPushButton#priBtn:disabled, QPushButton#secBtn:disabled{
            opacity:0.6;                      /* 비활성 가시성 */
        }

        /* ====== 테이블(사원/근태 공통) ====== */
        QTableWidget#workersTable, QTableWidget#attTable{
            background:%15;                   /* 카드형 흰 배경 */
            color:%2;
            border:1px solid #dbe3ff;
            border-radius:%16px;
            border-top:none;                  /* 헤더와 하나의 카드처럼 보이게 */
            border-top-left-radius:0;
            border-top-right-radius:0;
            gridline-color:#e6ecff;           /* 옅은 그리드 */
        }
        /* 셀 텍스트 여백 통일(셀렉터 스코프는 TableView 공통) */
        QTableView::item{
            padding:4px 8px;
        }

        /* 행 선택 강조색 */
        QTableWidget::item:selected{
            background:%9;
            color:%2;
        }

        /* 헤더(열 제목) 룩 — 카드 상단 바처럼 표현 */
        QHeaderView::section{
            background:%15;                   /* 테이블 배경과 동일 톤 */
            color:%2;
            padding:6px;
            font-weight:700;
            border:none;
            border-bottom:1px solid #dbe3ff;  /* 본문과 구분 */
            qproperty-alignment: 'AlignCenter';
        }
        QHeaderView::section:first{ border-top-left-radius:%16px; }
        QHeaderView::section:last{  border-top-right-radius:%16px; }

        /* 좌상단 코너 버튼(헤더-본문 이음새 시각 품질 보정) */
        QTableCornerButton::section{
            background:%15;
            border:none;
            border-bottom:1px solid #dbe3ff;
            border-right:1px solid #dbe3ff;
        }

        /* (플랫폼 호환) 스플리터 핸들 어두운 바탕 문제 보정 */
        QSplitter{ background:%14; }
        QSplitter::handle{
            background:%14;
            width:10px;
            border-left:1px solid #dbe3ff;
        }
    )qss")
                            .arg(FONT_BASE)        // %1  → 기본 폰트 크기
                            .arg(COL_TEXT)         // %2  → 기본 텍스트 컬러
                            .arg(FONT_TITLE)       // %3  → 타이틀 폰트 크기
                            .arg(COL_PANEL)        // %4  → 탭 패널 배경
                            .arg(COL_PANEL_BORDER) // %5  → 탭 패널 테두리
                            .arg(RADIUS_BOX)       // %6  → 박스 라운드
                            .arg(COL_TAB)          // %7  → 비선택 탭 배경
                            .arg(COL_TAB_SEL)      // %8  → 선택 탭 배경
                            .arg(COL_TABLE_SEL)    // %9  → 테이블 선택 배경
                            .arg(BTN_PAD_V)        // %10 → 버튼 세로 패딩
                            .arg(BTN_PAD_H)        // %11 → 버튼 가로 패딩
                            .arg(COL_BTN_BLUE_0)   // %12 → 버튼 그라데이션 상단색
                            .arg(COL_BTN_BLUE_1)   // %13 → 버튼 그라데이션 하단색
                            .arg(COL_BG_WINDOW)    // %14 → 공통 배경색
                            .arg(COL_TABLE_BG)     // %15 → 테이블 카드 배경
                            .arg(RADIUS_TABLE);    // %16 → 테이블 라운드
    setStyleSheet(qss); // 조립된 QSS를 한 번에 적용

    // (보수) 일부 플랫폼/테마에서 콤보의 팝업(view)이 시스템 스타일을 따라가는 현상 보정
    //  - 드롭다운 리스트까지 라이트 스킨/테마 컬러 일치
    for (auto *cb : {kwDept, kwStatus, atWorker}) {
        cb->view()->setStyleSheet(
            "background:#ffffff; color:#111827;"
            "selection-background-color:#dfe9ff; selection-color:#111827;"
            "border:1px solid #c7d2fe; border-radius:6px; outline:0;"
            );
        QPalette vp = cb->view()->palette();
        vp.setColor(QPalette::Base, Qt::white);
        vp.setColor(QPalette::Text, QColor("#111827"));
        cb->view()->setPalette(vp);
    }

    // (가독성 보정) 입력 필드 정렬 기본값 미세 조정
    kwName->setAlignment(Qt::AlignLeft);     // 텍스트 입력: 좌측 정렬
    atFrom->setAlignment(Qt::AlignCenter);   // 날짜 입력: 중앙 정렬
    atTo->setAlignment(Qt::AlignCenter);
}

// ===== DB 연결 =====
// - 단일 커넥션 이름("att_conn")을 사용해 재진입/재사용 가능하도록 구성
// - QMYSQL 드라이버로 MySQL/MariaDB에 연결하며, 드라이버 미설치 시 open()에서 실패 반환
bool AttendancePage::openDb() {
    // 이미 동일 이름의 커넥션이 존재하면 재사용(중복 연결 방지)
    if (QSqlDatabase::contains("att_conn"))
        db_ = QSqlDatabase::database("att_conn");
    else
        db_ = QSqlDatabase::addDatabase("QMYSQL", "att_conn"); // 새 커넥션 생성(QMYSQL 드라이버)

    // ⏱ 연결/읽기/쓰기 타임아웃(초) 설정으로 지연 발생 시 빠르게 실패하도록 유도
    //    - 지원 드라이버(QMYSQL)에서만 실제 적용, 미지원 환경에서도 무해(무시됨)
    db_.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=3;"
                          "MYSQL_OPT_READ_TIMEOUT=5;"
                          "MYSQL_OPT_WRITE_TIMEOUT=5");

    // 접속 파라미터(예시값) — 실제 배포 시 환경변수/설정파일(.ini)로 분리 권장
    db_.setHostName("192.168.0.15");
    db_.setPort(3306);
    db_.setDatabaseName("safetydb");
    db_.setUserName("user1");
    db_.setPassword("1234");

    // 실제 DB 연결 시도
    if (!db_.open()) {
        // 실패 시 에러 메시지를 로그로 남기고 false 반환(상위에서 사용자 알림 처리)
        qWarning() << "DB open failed:" << db_.lastError().text();
        return false;
    }
    return true; // 연결 성공
}

// 직원 상태 코드 → 한글 라벨 변환(표시 전용)
// - 현재 스키마 기준: 1=재직, 그 외=퇴사 (필요 시 확장 가능)
QString AttendancePage::statusToKorean(int s) const {
    return s == 1 ? u8"재직" : u8"퇴사";
}

// ===== 근로자 콤보 리로드 =====
// - atWorker 콤보박스의 항목을 DB에서 재구성
// - 첫 항목은 "전체 근로자"(data 없음)로, 조회 시 직원 필터 미적용을 의미
void AttendancePage::reloadWorkerCombo() {
    atWorker->clear();
    atWorker->addItem(u8"전체 근로자"); // data()가 invalid → 전체 검색

    QSqlQuery q(db_);
    // 이름 기준 정렬로 사용자 선택 편의성 향상
    if (!q.exec("SELECT emp_id, name FROM employee ORDER BY name ASC")) {
        qWarning() << "reloadWorkerCombo err:" << q.lastError().text();
        return;
    }
    // 각 항목: "이름 (사번)" 형태의 표시 텍스트 + 사용자 데이터(data)=emp_id
    while (q.next()) {
        const int empId = q.value(0).toInt();
        const QString name = q.value(1).toString();
        atWorker->addItem(QString("%1 (%2)").arg(name).arg(empId), empId);
    }
}

// ===== 근로자 목록 로딩 (employee) =====
// - 검색바(이름/부서/상태) 조건을 적용해 사원 테이블(tblWorkers)을 채움
// - 바인딩(:name, :dept, :status) 사용으로 SQL 인젝션 방지 및 캐시 힌트 제공
void AttendancePage::loadEmployees() {
    // 기존 행 제거 후 재구성
    tblWorkers->setRowCount(0);

    // 기본 SELECT: NULL 대비 COALESCE로 표시 일관성 유지
    QString sql =
        "SELECT emp_id, name, COALESCE(department,''), COALESCE(position,''), "
        "       status, COALESCE(phone,''), '' AS etc "
        "FROM employee WHERE 1=1 ";

    // 조건절 구성 — 입력값이 있을 때만 조건 추가(불필요한 인덱스 스캔 방지)
    // 이름 LIKE (부분 일치)
    if (!kwName->text().trimmed().isEmpty())
        sql += " AND name LIKE :name ";

    // 부서 완전 일치
    if (kwDept->currentText() != u8"전체 부서")
        sql += " AND department = :dept ";

    // 상태 완전 일치 (UI: 재직/휴가/퇴사 → 스키마: 1/0 등으로 매핑)
    if (kwStatus->currentText() != u8"전체 상태")
        sql += " AND status = :status ";

    // 정렬: 사번 오름차순(안정적인 정렬 기준)
    sql += " ORDER BY emp_id ASC";

    QSqlQuery q(db_);
    q.prepare(sql);
    // 이름 바인딩 — 앞/뒤 와일드카드로 부분 검색
    if (!kwName->text().trimmed().isEmpty())
        q.bindValue(":name", "%" + kwName->text().trimmed() + "%");
    // 부서 바인딩
    if (kwDept->currentText() != u8"전체 부서")
        q.bindValue(":dept", kwDept->currentText().trimmed());
    // 상태 바인딩 — UI 라벨을 코드로 변환(예: 재직→1, 그 외→0)
    if (kwStatus->currentText() != u8"전체 상태")
        q.bindValue(":status", kwStatus->currentText()==u8"재직" ? 1 : 0);

    // 쿼리 실행 및 에러 처리
    if (!q.exec()) {
        qWarning() << "loadEmployees err:" << q.lastError().text();
        return;
    }

    // 결과 → 테이블 행으로 매핑
    int r = 0;
    while (q.next()) {
        // 각 셀 생성(중앙 정렬로 표 형태 가독성 고정)
        auto *c0 = new QTableWidgetItem(q.value(0).toString());               // 사번
        auto *c1 = new QTableWidgetItem(q.value(1).toString());               // 이름
        auto *c2 = new QTableWidgetItem(q.value(2).toString());               // 부서
        auto *c3 = new QTableWidgetItem(q.value(3).toString());               // 직무
        auto *c4 = new QTableWidgetItem(statusToKorean(q.value(4).toInt()));  // 상태(한글 변환)
        auto *c5 = new QTableWidgetItem(q.value(5).toString());               // 연락처
        auto *c6 = new QTableWidgetItem(q.value(6).toString());               // 비고

        for (auto *it : {c0,c1,c2,c3,c4,c5,c6})
            it->setTextAlignment(Qt::AlignCenter); // 표 텍스트 가운데 정렬

        // 테이블에 행 삽입
        tblWorkers->insertRow(r);
        tblWorkers->setItem(r, 0, c0);
        tblWorkers->setItem(r, 1, c1);
        tblWorkers->setItem(r, 2, c2);
        tblWorkers->setItem(r, 3, c3);
        tblWorkers->setItem(r, 4, c4);
        tblWorkers->setItem(r, 5, c5);
        tblWorkers->setItem(r, 6, c6);
        ++r;
    }

    // [성능 팁] employee(name), employee(department), employee(status) 인덱스를 상황에 맞게 추가하면
    //           LIKE/동등 조건 성능이 크게 개선됩니다(특히 데이터가 수만 건 이상일 때).
}

// ===== 출석 기록 로딩 (gate_check) =====
// - 기간/직원 필터에 따라 출퇴근 데이터를 집계하여 표(tblAttendance)에 표시
// - 스키마에 '입/퇴근 구분'이 없으므로 같은 날짜에서 MIN=출근, MAX=퇴근으로 간주
// - 시간 계산은 DB에서 TIMEDIFF로 처리(클라이언트 계산 부담 감소)
void AttendancePage::loadAttendance() {
    // 기존 행 초기화
    tblAttendance->setRowCount(0);

    // 날짜 범위 파싱(YYYY-MM-DD) — 유효성 실패 시 사용자 안내 후 종료
    const QDate from = QDate::fromString(atFrom->text().trimmed(),  "yyyy-MM-dd");
    const QDate to   = QDate::fromString(atTo->text().trimmed(),    "yyyy-MM-dd");
    if (!from.isValid() || !to.isValid()) {
        QMessageBox::information(this, u8"입력 확인", u8"기간을 YYYY-MM-DD 형식으로 입력하세요.");
        return;
    }

    // 특정 직원 필터 여부 판단: 콤보 data(emp_id)가 유효할 때만 조건 추가
    QVariant empIdData = atWorker->currentData();
    const bool filterByEmp = empIdData.isValid();

    /*
     * gate_check 스키마에는 '입/퇴근 구분'이 없으므로
     * 같은 날짜의 MIN=출근, MAX=퇴근으로 간주.
     * - FROM..TO+1day 구간으로 날짜 폐구간 처리
     * - DB에서 DATE(), MIN/MAX, TIMEDIFF로 일자별 집계 수행
     * - 시간 포맷은 HH:mm으로 간략화(표시 UX 기준)
     */
    QString sql =
        "SELECT DATE(g.check_time) AS day, e.emp_id, e.name, e.department, "
        "       DATE_FORMAT(MIN(g.check_time), '%H:%i') AS in_time, "
        "       DATE_FORMAT(MAX(g.check_time), '%H:%i') AS out_time, "
        "       TIMEDIFF(MAX(g.check_time), MIN(g.check_time)) AS hours "
        "FROM gate_check g "
        "JOIN employee e ON e.emp_id = g.emp_id "
        "WHERE g.check_time >= :from AND g.check_time < DATE_ADD(:to, INTERVAL 1 DAY) ";

    // 특정 직원 조건(선택된 경우에만 추가)
    if (filterByEmp)
        sql += " AND e.emp_id = :emp ";

    // 그룹화: 일자/사번 단위 집계, 정렬: 최신 일자 DESC → 사번 ASC
    sql += " GROUP BY day, e.emp_id "
           " ORDER BY day DESC, e.emp_id ASC";

    QSqlQuery q(db_);
    q.prepare(sql);
    // 날짜 바인딩 — 문자열로 전달(서버 타임존 영향이 우려되면 UTC 고정 또는 tz 변환 고려)
    q.bindValue(":from", from.toString("yyyy-MM-dd"));
    q.bindValue(":to",   to.toString("yyyy-MM-dd"));
    if (filterByEmp)
        q.bindValue(":emp", empIdData.toInt());

    // 쿼리 실행 및 에러 처리
    if (!q.exec()) {
        qWarning() << "loadAttendance err:" << q.lastError().text();
        return;
    }

    // 결과 → 테이블에 매핑(7열: 일자/사번/이름/부서/출근/퇴근/근무시간)
    int r = 0;
    while (q.next()) {
        auto *c0 = new QTableWidgetItem(q.value("day").toString());        // 일자(YYYY-MM-DD)
        auto *c1 = new QTableWidgetItem(q.value("emp_id").toString());     // 사번
        auto *c2 = new QTableWidgetItem(q.value("name").toString());       // 이름
        auto *c3 = new QTableWidgetItem(q.value("department").toString()); // 부서
        auto *c4 = new QTableWidgetItem(q.value("in_time").toString());    // 출근(HH:mm)
        auto *c5 = new QTableWidgetItem(q.value("out_time").toString());   // 퇴근(HH:mm)
        auto *c6 = new QTableWidgetItem(q.value("hours").toString());      // 근무시간(HH:mm:ss)

        for (auto *it : {c0,c1,c2,c3,c4,c5,c6})
            it->setTextAlignment(Qt::AlignCenter); // 표 텍스트 가운데 정렬

        // 행 삽입
        tblAttendance->insertRow(r);
        tblAttendance->setItem(r, 0, c0);
        tblAttendance->setItem(r, 1, c1);
        tblAttendance->setItem(r, 2, c2);
        tblAttendance->setItem(r, 3, c3);
        tblAttendance->setItem(r, 4, c4);
        tblAttendance->setItem(r, 5, c5);
        tblAttendance->setItem(r, 6, c6);
        ++r;
    }

    // [성능 팁]
    // - gate_check(check_time), gate_check(emp_id, check_time) 복합 인덱스가 있으면
    //   기간/직원 조건에서 대용량 데이터도 빠르게 조회됩니다.
    // [정확도 팁]
    // - 장치/서버/클라이언트의 타임존이 상이하면 DATE() 경계가 달라질 수 있으니
    //   가능하면 서버/DB/앱 타임존을 통일하거나 UTC 저장/로컬 표시를 권장합니다.
}
