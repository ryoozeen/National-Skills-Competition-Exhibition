#include "notification.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QPropertyAnimation>
#include <QPainter>
#include <QStyleOptionButton>
#include <QScrollArea>
#include <QMouseEvent>
/*
 * @file notification.cpp
 * @brief 알림 모듈 구현.
 *        - Popup: 투명 배경, 페이드 인 애니메이션으로 토스트 표시
 *        - Button: 기본 QPushButton에 배지(숫자) 오버레이 렌더링
 *        - ListPopup: 최근 알림 카드 리스트, 항목 클릭 시 itemActivated() 신호
 *        - Manager: addNotification()/clearNotifications()로 수량 관리 및 신호 방출
 */

// ===================== NotificationPopup =====================
/**
 * @brief 토스트 팝업 초기화
 *  - Frameless + ToolTip 플래그로 독립 창처럼 표시
 *  - 투명 배경 사용(둥근 모서리/그라디언트 효과)
 *  - autoHide_ 타이머/페이드 인 애니메이션 준비
 */
NotificationPopup::NotificationPopup(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);   // 테두리 없는 툴팁형 창(항상 위, 포커스 방해 적음)
setAttribute(Qt::WA_TranslucentBackground);              // 투명 배경(둥근모서리/그림자 표현)
setAttribute(Qt::WA_DeleteOnClose, false);               // 닫혀도 파괴하지 않고 재사용
autoHide_ = new QTimer(this);                            // 자동 닫기용 타이머(사용 시 start 필요)
autoHide_->setSingleShot(true);                          // 1회성 타이머로 동작
connect(autoHide_, &QTimer::timeout, this, &NotificationPopup::hide, Qt::QueuedConnection);  // 타이머 만료 시 hide()
auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0,0,0,0);

    auto* frame = new QFrame(this);
    frame->setStyleSheet(
        "background-color: rgba(17,24,39,0.92);"
        "border-radius: 10px;"
        "border: 1px solid rgba(99,102,241,0.35);"
        );
    outer->addWidget(frame);

    auto* v = new QVBoxLayout(frame);
    v->setContentsMargins(16,12,16,12);
    v->setSpacing(6);

    titleLabel = new QLabel(this);
    titleLabel->setStyleSheet("font-size:15px; font-weight:700; color:#ffffff;");
    msgLabel = new QLabel(this);
    msgLabel->setStyleSheet("font-size:13px; color:#e5e7eb;");
    msgLabel->setWordWrap(true);

    v->addWidget(titleLabel);
    v->addWidget(msgLabel);

    setFixedSize(340, 110);                                  // 팝업 고정 크기
}
/**
 * @brief 토스트 팝업을 화면 우하단에 띄우고, 페이드 인으로 표시합니다.
 *  - 타이틀/메시지 적용 → 기본 스크린의 usable 영역 기준 위치 계산
 *  - 이전 애니메이션/타이머가 살아있으면 안전하게 중단/정리
 *  - windowOpacity 0 → 1.0으로 250ms 페이드 인
 *  @note 자동 닫기를 원하면 autoHide_->start(표시시간ms)를 호출하면 됩니다.
 */

void NotificationPopup::showNotification(const QString& title, const QString& message) {
    titleLabel->setText(title);
    msgLabel->setText(message);

    if (auto* screen = QGuiApplication::primaryScreen()) {   // 기본 스크린의 안전 가용 영역을 기준으로 위치 계산

        const QRect g = screen->availableGeometry();
        move(g.right() - width() - 16, g.bottom() - height() - 16);
    }

    // ▼ 숨겨지는 중이었다면 즉시 멈춤(타이머/애니메이션 모두)
    if (autoHide_ && autoHide_->isActive()) autoHide_->stop();
    if (fadeIn_) { fadeIn_->stop(); fadeIn_->deleteLater(); fadeIn_ = nullptr; }

    setWindowOpacity(0.0);                                   // 페이드 인 시작값
show();                                                  // 팝업 표시
raise();                                                 // 다른 창 위로 올리기
// 이전 애니메이션이 돌고 있으면 안전하게 정지/정리
    if (fadeIn_) {
        fadeIn_->stop();
        fadeIn_->deleteLater();
        fadeIn_ = nullptr;                    // ★ 반드시 nullptr 로
    }

    fadeIn_ = new QPropertyAnimation(this, "windowOpacity", this);  // 창 투명도 애니메이션
fadeIn_->setDuration(250);                               // 250ms 페이드 인
fadeIn_->setStartValue(0.0);                             // 시작: 완전 투명
fadeIn_->setEndValue(1.0);                               // 종료: 완전 불투명
// 애니메이션이 끝나거나 파괴될 때 포인터 자동 정리
    connect(fadeIn_, &QObject::destroyed, this, [this]{ fadeIn_ = nullptr; }); // 파괴 시 포인터 무효화
// ★ DeleteWhenStopped 쓰지 말고 일반 start 사용 (스스로 지우지 않음)
    fadeIn_->start();
}

// ===================== NotificationButton =====================
/**
 * @brief '알림' 배지 버튼 초기화
 *  - 포인터 커서/최소 크기/객체명 지정(QSS 스타일링 용이)
 */
NotificationButton::NotificationButton(QWidget* parent)
    : QPushButton(parent) {
    setText("알림");                                         // 기본 라벨
setMinimumSize(72, 36);
    setCursor(Qt::PointingHandCursor);                       // 손가락 커서
setObjectName("notiBtn");
}
/** @brief 외부에서 카운트를 받아 저장하고 리렌더링 요청(update) */ 

void NotificationButton::setNotificationCount(int count) {
    m_count = count;
    update();                                                // 다시 그리기 요청(배지 포함)
}
/**
 * @brief 기본 QPushButton을 그린 뒤 우상단에 원형 배지를 덧그립니다.
 *  - m_count <= 0 이면 배지를 그리지 않음
 *  - 99+ 표시 처리
 */

void NotificationButton::paintEvent(QPaintEvent* event) {
    // 기본 버튼 그리기
    QStyleOptionButton opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawControl(QStyle::CE_PushButton, &opt, &p, this);

    QPushButton::paintEvent(event);

    if (m_count <= 0) return;                                // 0이면 배지 생략
// 빨간 배지
    const int d = 18;
    const int x = width() - d - 6;
    const int y = 6;

    p.setRenderHint(QPainter::Antialiasing, true);           // 원형/테두리 안티앨리어싱
p.setBrush(QColor("#ef4444"));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRect(x, y, d, d));

    p.setPen(Qt::white);
    QFont f = font();
    f.setPointSize(f.pointSize() - 1);
    f.setBold(true);
    p.setFont(f);

    const QString text = (m_count > 99) ? "99+" : QString::number(m_count);  // 100 이상은 99+로 표시
p.drawText(QRect(x, y, d, d), Qt::AlignCenter, text);
}

//==================  최근 알림 목록 팝업 구현 =====================
/**
 * @brief 앵커(버튼)의 전역 좌표에서 '오른쪽 정렬, 아래쪽으로 margin' 위치를 계산합니다.
 *  - 목록 팝업의 좌표를 버튼 우하단에 자연스럽게 붙이기 위한 헬퍼
 */
static QRect anchorRectFor(QWidget* anchor, int w, int h, int margin = 8) {
    const QPoint p = anchor->mapToGlobal(QPoint(0, anchor->height()));
    return QRect(p.x() - w + anchor->width(), p.y() + margin, w, h);
}

// ---------- ClickableWidget ----------
/** @brief 손가락 모양 커서를 주는 클릭 가능한 컨테이너 */ 
ClickableWidget::ClickableWidget(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);                       // 손가락 커서
}
/** @brief 좌클릭 시 clicked() 신호 방출 후 기본 처리 위임 */ 
void ClickableWidget::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) emit clicked();
    QWidget::mousePressEvent(ev);
}

// ---------- NotificationListPopup ----------
/**
 * @brief 최근 알림 목록 팝업 초기화
 *  - 반투명/프레임리스 패널, 내부에 제목 + QScrollArea + 카드 레이아웃 구성
 *  - NotificationManager::notificationAdded 신호를 구독하여 실시간 갱신
 */
NotificationListPopup::NotificationListPopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint); // 외부 그림자 제거(일부 WM 호환)
setAttribute(Qt::WA_TranslucentBackground);              // 투명 배경(둥근모서리/그림자 표현)
setAttribute(Qt::WA_DeleteOnClose, false);               // 닫혀도 파괴하지 않고 재사용
auto outer = new QVBoxLayout(this);
    outer->setContentsMargins(0,0,0,0);

    panel_ = new QWidget(this);
    panel_->setObjectName("notiListPanel");
    panel_->setStyleSheet(R"(
        #notiListPanel { background:#ffffff; border:1px solid #e5e7eb; border-radius:12px; }
        .title { font-weight:700; color:#111827; }
        .meta  { color:#6b7280; font-size:12px; }
        .msg   { color:#374151; }
        QScrollArea { border:none; }
    )");

    auto root = new QVBoxLayout(panel_);
    root->setContentsMargins(12,12,12,12);
    root->setSpacing(8);

    auto header = new QLabel(u8"최근 알림", panel_);
    header->setObjectName("title");
    header->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    root->addWidget(header);

    auto scroll = new QScrollArea(panel_);
    scroll->setWidgetResizable(true);
    auto listHost = new QWidget(scroll);
    listLayout_ = new QVBoxLayout(listHost);
    listLayout_->setContentsMargins(0,0,0,0);
    listLayout_->setSpacing(8);
    listLayout_->addStretch();                               // 맨 아래 공간 확보(아이템 위로 쌓이도록)
scroll->setWidget(listHost);
    root->addWidget(scroll);

    outer->addWidget(panel_);

    // 새 알림 들어오면 목록에 축적 → UI 갱신
    QObject::connect(&NotificationManager::instance(),
                     &NotificationManager::notificationAdded,
                     this,
                     &NotificationListPopup::onNotificationAdded,
                     Qt::QueuedConnection);

    resize(360, 480);
}
/**
 * @brief 단일 알림 항목 카드를 생성합니다.
 *  - 제목/메시지/시간 라벨
 *  - 박스 클릭 시 itemActivated(...) 신호 후 팝업 닫기
 */

QWidget* NotificationListPopup::makeItem(const NotificationItemLite& it) {
    auto box = new ClickableWidget;            // ✅ 클릭 가능한 컨테이너
    auto lay = new QVBoxLayout(box);
    lay->setContentsMargins(10,10,10,10);
    lay->setSpacing(4);

    auto t = new QLabel(it.title);   t->setObjectName("title");
    auto m = new QLabel(it.message); m->setObjectName("msg");  m->setWordWrap(true);
    auto meta = new QLabel(it.when.toString("yyyy-MM-dd HH:mm:ss")); meta->setObjectName("meta");

    lay->addWidget(t);
    lay->addWidget(m);
    lay->addWidget(meta);

    box->setStyleSheet("QWidget { background:#f9fafb; border:1px solid #e5e7eb; border-radius:10px; }");

    // ✅ 항목 클릭 → 시그널 방출 + 팝업 닫기
    QObject::connect(box, &ClickableWidget::clicked, this, [this, it]{
        emit itemActivated(it.title, it.message, it.when);
        hidePopup();
    });

    return box;
}
/**
 * @brief 새 알림을 내부 items_에 추가하고, 최신순으로 UI를 다시 그립니다.
 *  - listLayout_의 stretch(마지막)만 남기고 나머지 제거 후 재삽입
 *  - 보관 한도 초과 시 오래된 기록을 제거
 */

void NotificationListPopup::onNotificationAdded(const QString& title, const QString& message) {
    items_.push_back({title, message, QDateTime::currentDateTime()});
    if (items_.size() > maxKeep_) items_.pop_front();

    if (!listLayout_) return;                                // UI 아직 안 만들어졌으면 스킵
// stretch 제외하고 모두 제거
    while (listLayout_->count() > 1) {                       // 마지막 stretch 제외 제거

        auto* it = listLayout_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    // 최신순 렌더
    for (int i = items_.size()-1; i >= 0; --i) {
        listLayout_->insertWidget(0, makeItem(items_[i]));
    }
}
/**
 * @brief 앵커 버튼 기준 위치에 팝업을 띄우거나(보이지 않으면) 닫습니다(보이면).
 */

void NotificationListPopup::toggleFor(QWidget* anchor) {
    if (visible_) { hidePopup(); return; }
    const QRect r = anchorRectFor(anchor, width(), height()); // 앵커 기준 팝업 사각형 계산
setGeometry(r);
    show();                                                  // 팝업 표시
raise();                                                 // 다른 창 위로 올리기
visible_ = true;                                         // 상태 갱신
}
/** @brief 내부 visible_ 플래그를 갱신하고 hide() 호출 */ 

void NotificationListPopup::hidePopup() {
    if (!visible_) return;                                   // 이미 닫혀 있으면 무시
visible_ = false;                                        // 상태 갱신
hide();                                                  // 실제 숨김
}
/** @brief 필요하면 마우스 아웃 시 자동 닫기 트리거 지점(현재는 주석 처리) */ 

void NotificationListPopup::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    // 필요 시 자동 닫기: hidePopup();
}
