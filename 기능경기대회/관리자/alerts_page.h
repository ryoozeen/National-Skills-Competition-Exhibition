#pragma once
// 중복 인클루드 방지. 이 헤더는 AlertsPage(알람/이벤트 로그 화면)의 공개 인터페이스를 정의한다.

#include <QWidget>    // QWidget 기반: 독립 페이지로서 UI 컨테이너 역할

// ===== 전방 선언 (빌드 의존 최소화/컴파일 시간 최적화) =====
class QLineEdit;      // 기간 필터 입력(시작/종료일)
class QComboBox;      // 유형/레벨(심각도) 드롭다운
class QPushButton;    // 새로고침 버튼
class QTableWidget;   // 알람/이벤트 로그 테이블(6열 스키마)
class QLabel;         // 페이지 타이틀/하단 페이지 라벨
class QJsonObject;    // 서버 원시 메시지(JSON) 한 건

// ===== 알람/이벤트 로그 페이지 =====
// - 상단: 기간/유형/레벨 필터 + 새로고침
// - 본문: 로그 테이블(시간/유형/레벨/상태/위치/설명)
// - 하단: 페이지 표시 라벨 (실제 페이징 연동은 선택)
class AlertsPage : public QWidget
{
    Q_OBJECT
public:
    explicit AlertsPage(QWidget *parent = nullptr);
    // 화면 구성(buildUi)과 룩앤필(applyStyle)을 초기화한다.

    // 알림 센터(토스트/배지)에서 전달되는 간단 알림을 즉시 테이블 상단에 1행으로 추가
    // - title → 위치/라인 컬럼에 요약으로 반영
    // - message → 설명 컬럼에 본문으로 반영
    // - 별도 서버 ts가 없으므로 현재 클라이언트 시각을 사용
    void appendNotification(const QString& title, const QString& message);

public slots:
    // 서버에서 수신한 원시 JSON 메시지 한 건을 테이블 포맷으로 표준화하여 추가
    // - 관리자/설정류(HELLO/USER_*/ADMIN_* 등)와 FACTORY_* 상태 푸시는 표 노이즈를 줄이기 위해 제외
    // - FIRE_EVENT는 축약 필드만 표시 후 일반 경로로 내려보내지 않음(중복 기록 방지)
    // - 다른 cmd는 공통 6열 스키마로 삽입(레벨/상태는 규칙에 따라 추론)
    void appendJson(const QJsonObject& m);

signals:
    // RobotPage로 이벤트를 브릿징하기 위한 시그널 세트
    // - 업로드 성공 시: 저장 경로와 원문 JSON을 전달(재생/미디어 열람 등 후속 처리 용도)
    void robotUploadDone(const QString& savedPath, const QJsonObject& full);
    // - 일반 로봇 이벤트: 레벨/메시지/원문 전달(콘솔 로그/상태표시 용도)
    void robotEvent(const QString& level, const QString& message, const QJsonObject& full);
    // - 로봇 에러: 에러 메시지와 원문 전달(에러 배지/상태 표시에 활용)
    void robotError(const QString& message, const QJsonObject& full);

private:
    // 위젯 트리를 조립하고 레이아웃을 구성한다.
    // - 타이틀 → 필터바 → 테이블 → 하단 라벨 순서
    void buildUi();

    // 페이지 전반의 룩앤필(폰트/색/버튼/테이블 헤더 등)을 적용한다.
    void applyStyle();

private:
    // ===== 상단 필터 컨트롤 =====
    QLineEdit*   startDate   = nullptr;  // 시작일(YYYY-MM-DD) — 표시용/선택적 필터 소스
    QLineEdit*   endDate     = nullptr;  // 종료일(YYYY-MM-DD)
    QComboBox*   cameraCombo = nullptr;  // (비활성) 카메라 필터 — 요구사항에 따라 숨김 유지
    QComboBox*   typeCombo   = nullptr;  // 유형 필터(전체/침입/화재/근접/시스템 경고 등)
    QComboBox*   levelCombo  = nullptr;  // 레벨 필터(ALL/LOW/MEDIUM/HIGH/CRITICAL)
    QPushButton* btnRefresh  = nullptr;  // 새로고침 트리거(필터 적용/재조회와 연결 가능)

    // ===== 본문/하단 =====
    QTableWidget* table      = nullptr;  // 알림/이벤트 로그 테이블(6열: 시간/유형/레벨/상태/위치/설명)
    QLabel*       pagerLabel = nullptr;  // 하단 페이지 표시 라벨(“현재/전체 페이지” 단순 표기)
};
