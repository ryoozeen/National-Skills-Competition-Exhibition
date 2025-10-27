#include "manual_control_page.h"

// ───── Qt 위젯/레이아웃·스타일 구성용 기본 헤더 ──────────────────────────────
#include <QVBoxLayout>   // 수직 배치(페이지 골격)
#include <QHBoxLayout>   // 수평 배치(바/타일 행)
#include <QLabel>        // 텍스트/상태 칩(원형) 표시
#include <QPushButton>   // 조작 버튼(비상정지/문 열기/닫기)
#include <QFrame>        // 카드/바 컨테이너(스타일 구분)
#include <QStyle>        // 플랫폼 표준 아이콘 사용(SP_MediaPlay 등)
#include <QToolTip>      // 위젯별 툴팁 설정

// ─────────────────────────────────────────────────────────────────────────────
// setChip
//  - 상태 칩(원형 12x12)을 주어진 상태값에 따라 색상 지정하고 툴팁을 부여.
//  - state: "on"(초록), "off"(빨강), 그 외(회색)로 간단한 3상 매핑.
//  - chip이 nullptr이면 조용히 무시(안전성).
// ─────────────────────────────────────────────────────────────────────────────
static void setChip(QLabel* chip, const QString& state, const QString& tip)
{
    if (!chip) return;
    QString color = "#9ca3af";                 // 기본: unknown → gray-400
    if (state == "on")  color = "#10b981";     // ON    → green-500
    if (state == "off") color = "#ef4444";     // OFF   → red-500

    chip->setFixedSize(12,12);
    chip->setStyleSheet(QString("background:%1; border-radius:6px;").arg(color));
    chip->setToolTip(tip);
}

// ─────────────────────────────────────────────────────────────────────────────
// ManualControlPage
//  - 수동 제어 화면의 루트 위젯.
//  - 상단 비상정지(E-Stop) 바 + 설비/문 상태 타일 + 출입문 수동조작 카드로 구성.
//  - 생성 시 UI 빌드/스타일 적용 후 즉시 상태 UI를 초기 렌더.
// ─────────────────────────────────────────────────────────────────────────────
ManualControlPage::ManualControlPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();         // 레이아웃/위젯 트리 구성
    applyStyle();      // QSS 스타일 일괄 적용
    refreshEStopUi();  // 비상정지 표시 초기화
    refreshStateUi();  // 설비/문 상태 표시 초기화
}

// ─────────────────────────────────────────────────────────────────────────────
// buildUi
//  - 페이지 구조/위젯을 실제로 생성하고 배치.
//  - (1) 상단 E-Stop 슬림 바: 현재 상태 안내 + 토글 버튼
//  - (2) 상태 타일 3종: 설비 가동 / 공장문 / 출입문 (각각 아이콘/상태칩/값)
//  - (3) 하단 출입문 수동 조작: 열기/닫기 단일 토글 버튼
//  - 시그널 연결:
//      * 비상정지 버튼: 중복 방지 플래그(estopPending)로 연타 차단 후 requestEmergencyStop(!state) 송신
//      * 출입문 버튼: 현 상태 반전 요청(requestEntranceDoor(next))
// ─────────────────────────────────────────────────────────────────────────────
void ManualControlPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16,16,16,16);
    root->setSpacing(12);

    // (1) 상단 E-Stop 슬림 바
    estopBar = new QFrame(this);
    estopBar->setObjectName("estopBar");            // QSS 타겟
    auto* eLy = new QHBoxLayout(estopBar);
    eLy->setContentsMargins(12,8,12,8);
    eLy->setSpacing(8);

    estopText = new QLabel(u8"비상정지가 해제된 상태입니다.", estopBar);  // 상태 안내
    estopBtn  = new QPushButton(u8"비상정지 활성화", estopBar);            // 토글 버튼
    estopBtn->setObjectName("pillBtn");
    estopBtn->setCursor(Qt::PointingHandCursor);
    estopBtn->setToolTip(u8"설비/로봇을 즉시 정지합니다. (권한자만)");

    eLy->addWidget(estopText);
    eLy->addStretch();
    eLy->addWidget(estopBtn);
    root->addWidget(estopBar);

    // 비상정지 버튼 핸들러:
    //  - 이미 요청 중(estopPending=true)이면 무시 → 중복 토글 방지
    //  - UI를 '요청 중...' 문구로 바꾸고 외부로 토글 요청 시그널 발행
    connect(estopBtn, &QPushButton::clicked, this, [this]{
        if (estopPending) return;
        estopPending = true;
        estopBtn->setEnabled(false);
        estopText->setText(emergencyStop ? u8"비상정지 해제 요청 중..." : u8"비상정지 활성화 요청 중...");
        emit requestEmergencyStop(!emergencyStop);
    });

    // (2) 상단 상태 타일 3개
    tilesWrap = new QFrame(this);
    tilesWrap->setObjectName("tilesWrap");
    auto* tLy = new QHBoxLayout(tilesWrap);
    tLy->setContentsMargins(0,0,0,0);
    tLy->setSpacing(12);

    // tile 생성 헬퍼:
    //  - 제목/아이콘/칩/값 라벨을 보유하는 카드형 컨테이너 생성 후 타일 행에 추가
    auto makeTile = [&](QFrame*& tile, QLabel*& icon, QLabel*& chip, QLabel*& value,
                        const QString& titleText, const QIcon& ico) {
        tile = new QFrame(tilesWrap);
        tile->setObjectName("tile");                // QSS 타겟
        auto* v = new QVBoxLayout(tile);
        v->setContentsMargins(14,12,14,12);
        v->setSpacing(6);

        // 타이틀 라인(좌: 텍스트 / 우: 아이콘)
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* title = new QLabel(titleText, tile);
        title->setObjectName("tileTitle");
        icon = new QLabel(tile);
        icon->setPixmap(ico.pixmap(18,18));
        icon->setFixedSize(18,18);
        row->addWidget(title);
        row->addStretch();
        row->addWidget(icon);
        v->addLayout(row);

        // 값 라인(좌: 상태 칩 / 텍스트 값)
        auto* valRow = new QHBoxLayout;
        valRow->setSpacing(8);
        chip = new QLabel(tile);           // setChip로 색/툴팁 갱신
        value = new QLabel("-", tile);     // 기본값 "-"
        value->setObjectName("tileValue");
        valRow->addWidget(chip, 0, Qt::AlignVCenter);
        valRow->addWidget(value);
        valRow->addStretch();
        v->addLayout(valRow);

        tLy->addWidget(tile, 1);           // 같은 가중치로 3분할
    };

    // 설비 가동 / 공장 문 / 출입 문 타일 인스턴스 생성
    makeTile(tileRun,     runIcon,     runChip,     runLabel,     u8"설비 가동",
             style()->standardIcon(QStyle::SP_MediaPlay));
    makeTile(tileFacDoor, facDoorIcon, facDoorChip, facDoorLabel, u8"공장 문",
             style()->standardIcon(QStyle::SP_DialogOpenButton));
    makeTile(tileEntDoor, entDoorIcon, entDoorChip, entDoorLabel, u8"출입 문",
             style()->standardIcon(QStyle::SP_DialogOpenButton));

    root->addWidget(tilesWrap);

    // (3) 하단 출입 문 수동 조작 카드
    ctrlCard = new QFrame(this);
    ctrlCard->setObjectName("ctrlCard");
    auto* cLy = new QHBoxLayout(ctrlCard);
    cLy->setContentsMargins(16,12,16,12);
    cLy->setSpacing(10);

    auto* ctrlTitle = new QLabel(u8"출입 문 수동 조작", ctrlCard);
    ctrlTitle->setObjectName("cardTitle");

    entDoorToggle = new QPushButton(u8"문 열기", ctrlCard); // 상태에 따라 "문 닫기"로 변환
    entDoorToggle->setObjectName("pillBtn");
    entDoorToggle->setCursor(Qt::PointingHandCursor);
    entDoorToggle->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    entDoorToggle->setToolTip(u8"출입 문을 엽니다.");

    cLy->addWidget(ctrlTitle);
    cLy->addStretch();
    cLy->addWidget(entDoorToggle);
    root->addWidget(ctrlCard);

    // 출입 문 토글 핸들러:
    //  - entDoorState>0(열림) 이면 닫기 요청, 아니면 열기 요청을 외부로 시그널 발행
    connect(entDoorToggle, &QPushButton::clicked, this, [this]{
        const bool next = (entDoorState <= 0);   // -1/0 => 열기, 1 => 닫기
        emit requestEntranceDoor(next);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// applyStyle
//  - 페이지 내 주요 컨테이너/버튼/라벨에 대한 QSS 일괄 적용.
//  - estopBar/tilesWrap/tile/ctrlCard 등 ObjectName를 이용해 국소 스타일링.
//  - 버튼은 pill 형태로 라운드/호버 효과 지정.
// ─────────────────────────────────────────────────────────────────────────────
void ManualControlPage::applyStyle()
{
    setStyleSheet(R"(
        QWidget { font-family:'Malgun Gothic','Noto Sans KR',sans-serif; }

        /* 비상정지 슬림 바 */
        #estopBar {
            background:#fff1f2;              /* rose-50 (기본: 해제 상태) */
            border:1px solid #fecdd3;        /* rose-200 */
            border-radius:10px;
        }

        /* 타일 래퍼(개별 스타일은 #tile에서) */
        #tilesWrap {}

        /* 상태 타일: 카드형 */
        #tile {
            background:#ffffff;
            border:1px solid #ebedf1;
            border-radius:12px;
        }
        #tile > * { font-size:14px; }
        #tileTitle { font-weight:700; color:#111827; }
        #tileValue { font-weight:800; color:#0b0f19; }

        /* 하단 출입 문 컨트롤 카드 */
        #ctrlCard {
            background:#ffffff;
            border:1px solid #ebedf1;
            border-radius:12px;
        }
        #cardTitle { font-weight:800; color:#0b0f19; }

        /* pill 형태 버튼 공통 룩(비상정지/출입문 토글) */
        QPushButton#pillBtn {
            background:#ffffff;
            border:1px solid #e5e7eb;
            border-radius:18px;
            padding:6px 12px;
            font-weight:600;
        }
        QPushButton#pillBtn:hover { background:#f9fafb; }
    )");
}

// ─────────────────────────────────────────────────────────────────────────────
// refreshEStopUi
//  - emergencyStop / estopPending 플래그를 바탕으로
//    상단 E-Stop 바의 배경/테두리/문구/버튼 상태를 즉시 반영.
//  - 요청 중(estopPending=true)일 때는 버튼 비활성화 및 "요청 중…" 문구 유지.
// ─────────────────────────────────────────────────────────────────────────────
void ManualControlPage::refreshEStopUi()
{
    if (!estopBar || !estopBtn || !estopText) return;

    // 현재 상태에 따라 estopBar 색상 계열 전환(활성: 더 진한 빨강)
    estopBar->setStyleSheet(emergencyStop
                                ? "#estopBar{background:#fee2e2; border:1px solid #fca5a5; border-radius:10px;}"
                                : "#estopBar{background:#fff1f2; border:1px solid #fecdd3; border-radius:10px;}");

    // 버튼 라벨/툴팁을 토글 상태에 맞게 변경
    estopBtn->setText(emergencyStop ? u8"비상정지 해제" : u8"비상정지 활성화");
    estopBtn->setToolTip(emergencyStop ? u8"비상정지를 해제합니다." : u8"비상정지를 즉시 활성화합니다.");

    // 요청 중이 아닐 때만 안내 문구를 정상 상태로 되돌림
    if (!estopPending) {
        estopText->setText(emergencyStop
                               ? u8"비상정지 활성화됨 — 관리자 조작이 제한됩니다."
                               : u8"비상정지가 해제된 상태입니다.");
    }
    estopBtn->setEnabled(!estopPending);
}

// ─────────────────────────────────────────────────────────────────────────────
// refreshStateUi
//  - runState/facDoorState/entDoorState(각 -1:미정, 0:OFF/닫힘, 1:ON/열림)를
//    사용자 친화적인 텍스트/칩으로 변환해 타일 라벨/칩을 갱신.
//  - 출입문 토글 버튼의 라벨/아이콘/툴팁도 현재 상태에 맞게 전환.
// ─────────────────────────────────────────────────────────────────────────────
void ManualControlPage::refreshStateUi()
{
    // 상태 텍스트 변환
    const QString runTxt     = (runState     < 0) ? u8"-" : (runState     ? u8"ON"   : u8"OFF");
    const QString facDoorTxt = (facDoorState < 0) ? u8"-" : (facDoorState ? u8"열림" : u8"닫힘");
    const QString entDoorTxt = (entDoorState < 0) ? u8"-" : (entDoorState ? u8"열림" : u8"닫힘");

    if (runLabel)     runLabel->setText(runTxt);
    if (facDoorLabel) facDoorLabel->setText(facDoorTxt);
    if (entDoorLabel) entDoorLabel->setText(entDoorTxt);

    // 상태 칩 + 툴팁(미정/ON/OFF 3상 매핑)
    setChip(runChip,     (runState     < 0) ? "unknown" : (runState     ? "on" : "off"),
            (runState     < 0) ? u8"가동 상태 미정" : (runState     ? u8"가동 중" : u8"정지됨"));
    setChip(facDoorChip, (facDoorState < 0) ? "unknown" : (facDoorState ? "on" : "off"),
            (facDoorState < 0) ? u8"문 상태 미정" : (facDoorState ? u8"문 열림" : u8"문 닫힘"));
    setChip(entDoorChip, (entDoorState < 0) ? "unknown" : (entDoorState ? "on" : "off"),
            (entDoorState < 0) ? u8"문 상태 미정" : (entDoorState ? u8"문 열림" : u8"문 닫힘"));

    // 출입 문 토글 버튼 라벨/아이콘/툴팁 갱신
    if (entDoorToggle) {
        if (entDoorState > 0) {
            entDoorToggle->setText(u8"문 닫기");
            entDoorToggle->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
            entDoorToggle->setToolTip(u8"출입 문을 닫습니다.");
        } else {
            entDoorToggle->setText(u8"문 열기");
            entDoorToggle->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
            entDoorToggle->setToolTip(u8"출입 문을 엽니다.");
        }
        entDoorToggle->setEnabled(true); // E-Stop과 무관하게 조작 가능(요건에 따라 변경 가능)
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// setEmergencyStop
//  - 외부에서 비상정지 상태(engaged)를 통지받았을 때 모델 플래그를 갱신.
//  - 요청 중 플래그(estopPending)는 해제하고 두 UI(상단 바/타일)를 재렌더.
// ─────────────────────────────────────────────────────────────────────────────
void ManualControlPage::setEmergencyStop(bool engaged)
{
    emergencyStop = engaged;
    estopPending  = false;
    refreshEStopUi();
    refreshStateUi();
}

// ─────────────────────────────────────────────────────────────────────────────
// setFactoryState
//  - 설비 가동(run) / 공장문(doorFactory) 상태를 외부값으로 받아 내부 0/1 정규화.
//  - 이전 상태와 동일하면 조용히 리턴(불필요한 리렌더 방지).
//  - 변경 시 타일 UI를 재렌더.
// ─────────────────────────────────────────────────────────────────────────────
void ManualControlPage::setFactoryState(int run, int doorFactory)
{
    const int newRun  = (run         != 0) ? 1 : 0;
    const int newDoor = (doorFactory != 0) ? 1 : 0;

    if (runState == newRun && facDoorState == newDoor) {
        return; // 변경 없음 → 렌더 스킵
    }
    runState     = newRun;
    facDoorState = newDoor;
    refreshStateUi();
}

// ─────────────────────────────────────────────────────────────────────────────
// setEntranceDoorState
//  - 출입문(현관) 상태를 외부값으로 받아 내부 0/1로 정규화 후 타일/UI 갱신.
// ─────────────────────────────────────────────────────────────────────────────
void ManualControlPage::setEntranceDoorState(int doorOpen)
{
    entDoorState = (doorOpen != 0) ? 1 : 0;
    refreshStateUi();
}
