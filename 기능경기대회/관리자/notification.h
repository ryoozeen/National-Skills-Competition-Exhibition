#pragma once
/**
 * @file notification.h
 * @brief 알림 모듈 헤더 (NotificationManager/Popup/Button/ListPopup).
 *        - NotificationManager: 전체 알림 갯수/신규 알림 신호 관리 (싱글턴)
 *        - NotificationPopup  : 화면 하단-우측에 뜨는 토스트 팝업
 *        - NotificationButton : 오른쪽 상단의 '알림' 배지 버튼(개수 표시)
 *        - NotificationListPopup: 최근 알림 목록 패널(항목 클릭 시 신호)
 *
 * 프로토타입 사용법:
 *   // 1) 상단 툴바에 배지 버튼 생성
 *   auto* notiBtn = new NotificationButton(this);
 *   connect(&NotificationManager::instance(), &NotificationManager::notificationCountChanged,
 *           notiBtn, &NotificationButton::setNotificationCount);
 *
 *   // 2) 토스트 알림 보여주기
 *   static NotificationPopup toast(this);
 *   toast.showNotification(u8"화재 감지", u8"zone-A에서 화재 이벤트가 발생했습니다.");
 *
 *   // 3) 최근 알림 패널 열기/닫기
 *   static NotificationListPopup listPopup(this);
 *   connect(&listPopup, &NotificationListPopup::itemActivated, this, [&](auto t, auto m, auto when){
 *       // AlertsPage로 전환 등 원하는 동작 수행
 *   });
 *   listPopup.toggleFor(notiBtn); // notiBtn을 앵커로 패널 위치 계산
 */
#include <QObject>
#include <QPushButton>
#include <QWidget>
#include <QString>
#include <QDateTime>
#include <QVector>
#include <QPointer>

class QTimer;
class QPropertyAnimation;

//
// NotificationManager / NotificationPopup / NotificationButton
// — 한 파일에 묶은 단일 모듈
//

// ===================== NotificationManager =====================
class NotificationManager : public QObject {  // 싱글턴: 알림 갯수/신규 알림 신호 관리

    Q_OBJECT
public:
    static NotificationManager& instance() {  // 전역 접근 포인트

        static NotificationManager inst;
        return inst;
    }

    Q_INVOKABLE void addNotification(const QString& title, const QString& message) {  // 신규 알림 추가 → count 증가 + 신호 방출

        ++m_count;
        emit notificationAdded(title, message);
        emit notificationCountChanged(m_count);
    }
    Q_INVOKABLE void clearNotifications() {  // 카운터 초기화 후 변경 신호 방출

        m_count = 0;
        emit notificationCountChanged(m_count);
    }
    int count() const {  // 현재 알림 수 반환
 return m_count; }

signals:
      // 외부: 새 알림/카운트 변경을 구독 가능
void notificationAdded(const QString& title, const QString& message);
    void notificationCountChanged(int count);

private:
    explicit NotificationManager(QObject* parent = nullptr) : QObject(parent) {}
      // 외부에서 생성 금지(싱글턴)
Q_DISABLE_COPY_MOVE(NotificationManager)
    int m_count = 0;  // 현재 표시 중인 카운트(0이면 배지 숨김)

  // 현재 알림 수(최근 갯수)
};

// ===================== NotificationPopup =====================
class QLabel;
class NotificationPopup : public QWidget {  // 하단-우측에 잠시 뜨는 토스트 팝업

    Q_OBJECT
public:
    explicit NotificationPopup(QWidget* parent = nullptr);

public slots:
    void showNotification(const QString& title, const QString& message);  // 제목/메시지로 토스트 표시(페이드 인)


private:
    QLabel* titleLabel{};
    QLabel* msgLabel{};
    // ▼ 추가: 재사용 타이머 & 페이드 애니메이션(이전 것 안전 정리)
    QTimer* autoHide_{nullptr};  // 자동 닫기 타이머(원하면 start(ms)로 사용)

    QPointer<QPropertyAnimation> fadeIn_;  // 페이드 인 애니메이션(안전한 포인터로 수명 관리)

};

// ===================== NotificationButton (badge) =====================
class NotificationButton : public QPushButton {  // 우상단 배지 버튼(알림 갯수 렌더)

    Q_OBJECT
public:
    explicit NotificationButton(QWidget* parent = nullptr);

public slots:
    void setNotificationCount(int count);  // 외부에서 카운트를 받아 배지 갱신


protected:
    void paintEvent(QPaintEvent* event) override;  // 기본 버튼 위에 배지(원형/숫자) 오버레이


private:
    int m_count = 0;  // 현재 표시 중인 카운트(0이면 배지 숨김)

  // 현재 알림 수(최근 갯수)
};

// === 최근 알림 목록 팝업 (클릭 이동 지원) =========================
#include <QDateTime>
#include <QVector>

class QVBoxLayout;
class QMouseEvent;

// 개별 알림 레코드 (팝업 내부 전용)
struct NotificationItemLite {  // 목록 팝업의 단순 레코드 구조체

    QString   title;
    QString   message;
    QDateTime when;
};

// 클릭 가능한 박스 위젯 (마우스 클릭 시 clicked() 발생)
class ClickableWidget : public QWidget {  // 마우스 클릭 시 clicked() 내보내는 상자 위젯

    Q_OBJECT
public:
    explicit ClickableWidget(QWidget* parent = nullptr);
signals:
      // 외부: 새 알림/카운트 변경을 구독 가능
void clicked();
protected:
    void mousePressEvent(QMouseEvent* ev) override;  // 좌클릭 시 clicked() 방출

};

// 최근 알림 목록 팝업
class NotificationListPopup : public QWidget {  // 최근 알림 패널(스크롤 목록)

    Q_OBJECT
public:
    explicit NotificationListPopup(QWidget* parent = nullptr);

    // 앵커(알림 버튼) 기준으로 열고/닫기
    void toggleFor(QWidget* anchor);  // 앵커 버튼 기준 위치로 열기/닫기 토글

    void hidePopup();  // 강제 닫기


signals:
      // 외부: 새 알림/카운트 변경을 구독 가능
// ✅ 항목 클릭됨 → AdminWindow가 받아서 AlertsPage로 전환
    void itemActivated(const QString& title, const QString& message, const QDateTime& when);

private slots:
    // NotificationManager::notificationAdded 수신 → 목록 갱신
    void onNotificationAdded(const QString& title, const QString& message);  // 새 알림 수신 → 내부 items_와 UI 갱신


protected:
    void leaveEvent(QEvent* e) override;  // 마우스가 벗어나면 필요 시 자동 닫기
 // 필요시 자동 닫기

private:
    QWidget*     makeItem(const NotificationItemLite& it);
    QWidget*     panel_{};  // 흰 패널(테두리/라운드)

    QVBoxLayout* listLayout_{};  // 카드들을 담는 레이아웃(맨 아래 stretch 유지)

    bool         visible_{false};  // 현재 표시 상태 플래그


    QVector<NotificationItemLite> items_;
      // 내부 보관 목록(최대 maxKeep_)
int maxKeep_ = 50;  // 최대 보관 개수(초과 시 오래된 것 삭제)

};
