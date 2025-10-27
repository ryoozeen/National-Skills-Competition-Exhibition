#pragma once
#include <QWidget>     // 기본 윈도우/페이지 베이스 클래스
#include <QMetaObject> // QMetaObject::Connection(일회성/해제용 핸들)

// ── 전방 선언: 헤더 간 의존성 최소화/컴파일 속도 향상 ─────────────────────────
class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QFrame;
class QStackedWidget;
class QTabWidget;
class NetworkClient;

/**
 * @brief 앱 진입점 로그인 창.
 *
 * 핵심 역할(Functional Overview)
 * - 서버 연결 부팅: admin_client.ini([server] host/port) 로드 → NetworkClient 생성/접속.
 * - 인증 플로우: ID/PW로 로그인 시도 → 성공 시 AdminWindow로 전환(네트워크 소유권 이전).
 * - 계정 지원: 아이디 찾기/비밀번호 변경 탭 제공(인증 → 새 비번 설정 팝업 → 최종 변경).
 * - 상태 피드백: 네트워크 상태/오류/검증 결과를 하단 라벨에 실시간 반영.
 *
 * 아키텍처 포인트
 * - 중앙 스택(QStackedWidget)으로 [로그인]·[계정] 두 화면 전환.
 * - NetworkClient는 LoginWindow에서 초기화하되, 로그인 성공 시 AdminWindow로 소유권을 이관.
 * - 서버 응답 수신용 시그널은 QMetaObject::Connection 핸들을 보관해
 *   일회성/단발성 처리가 끝나면 즉시 disconnect 하여 중복 처리/누수 방지.
 */
class LoginWindow : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief 로그인 창 생성 및 즉시 UI/스타일 구성.
     * - 생성 직후 0ms 타이머로 ensureNetwork() 트리거 → 서버 연결 시도.
     */
    explicit LoginWindow(QWidget *parent = nullptr);

private:
    // ── UI 구성/전환/스타일 ────────────────────────────────────────────────
    /**
     * @brief 상단 로고, 중앙 스택, 하단 상태/종료 바까지 페이지 골격 구성.
     * - buildLoginPage(), buildAccountPage()에서 카드/폼/탭을 각각 채움.
     */
    void buildUi();

    /**
     * @brief 로그인 카드(회사/ID/PW + Log-In/계정찾기 버튼) 구성 및 이벤트 바인딩.
     * - Log-In 클릭 시 입력 검증 → ensureNetwork() → net_->login(id,pw).
     */
    void buildLoginPage();

    /**
     * @brief 계정(아이디 찾기/비밀번호 변경) 탭 구성.
     * - 아이디 찾기: email or phone → ADMIN_FIND_ID 전송.
     * - 비밀번호 변경: ADMIN_VERIFY_FOR_PW → 팝업에서 새 PW 입력 → ADMIN_CHANGE_PW.
     */
    void buildAccountPage();

    /**
     * @brief 페이지 전역 룩앤필(QSS) 적용.
     * - 라이트 톤 고정(시스템 다크/라이트 영향 최소화), 카드/입력/버튼/탭 일관 스타일.
     */
    void applyStyle();

    /**
     * @brief 중앙 스택을 로그인 페이지로 전환(상태 라벨 초기화).
     */
    void showLoginPage();

    /**
     * @brief 중앙 스택을 계정 페이지로 전환(상태 라벨 초기화).
     */
    void showAccountPage();

    /**
     * @brief 모달 팝업으로 새 비밀번호를 2회 입력 받아 검증.
     * @param outPw 검증 통과 시 새 비밀번호 반환.
     * @return OK(입력/일치) 시 true, 취소/검증 실패 시 false.
     */
    bool promptNewPassword(QString &outPw);

    // ── 네트워크 초기화/상태 표시 ─────────────────────────────────────────
    /**
     * @brief NetworkClient를 1회 초기화하고 서버에 연결.
     * - 역할(role)="admin"으로 설정하여 HELLO 핸드셰이크 수행.
     * - stateChanged/errorOccurred/messageReceived를 상태라벨/로그에 바인딩.
     * - 중복 호출 시 이미 초기화되어 있으면 아무 작업도 하지 않음(멱등).
     */
    void ensureNetwork();

private:
    // ── 네트워크 핸들 ─────────────────────────────────────────────────────
    NetworkClient*   net_{nullptr}; ///< 서버와 TCP/JSON 통신(로그인/계정/헬로/상태)

    // ── 공통 UI(헤더/푸터/스택) ───────────────────────────────────────────
    QLabel*          logoLabel{};    ///< 좌상단 로고(이미지 없을 시 텍스트 대체)
    QLabel*          statusLabel{};  ///< 네트워크/인증 진행/오류 상태 텍스트
    QPushButton*     btnExit{};      ///< 앱 종료(네트워크 정리 후 quit)
    QStackedWidget*  stack{};        ///< [로그인]↔[계정] 화면 전환 컨테이너

    // ── 로그인 페이지 뷰/입력 ────────────────────────────────────────────
    QWidget*         pageLogin{};    ///< 로그인 화면 루트 위젯
    QFrame*          cardLogin{};    ///< 로그인 카드(라운드/그림자)
    QComboBox*       companyCombo{}; ///< 회사 선택/직접 입력(현재 텍스트를 AdminWindow로 전달)
    QLineEdit*       userEdit{};     ///< 관리자 ID 입력
    QLineEdit*       pwEdit{};       ///< 관리자 PW 입력(Password 에코)
    QPushButton*     btnLogin{};     ///< 로그인 실행(ENTER 기본 버튼)
    QPushButton*     btnFind{};      ///< 계정 페이지로 이동(아이디/비번 찾기)

    // ── 계정(아이디/비번) 페이지 ─────────────────────────────────────────
    QWidget*         pageAccount{};  ///< 계정 지원 화면 루트 위젯
    QFrame*          cardAccount{};  ///< 계정 카드(라운드/그림자)
    QTabWidget*      tabs{};         ///< [아이디 찾기]/[비밀번호 변경] 탭 컨트롤

    // [탭1] 아이디 찾기 필드/버튼
    QLineEdit*       fiEmail{};      ///< 이메일로 아이디 조회(우선)
    QLineEdit*       fiPhone{};      ///< 이메일 공백 시 대안 입력(전화번호)
    QPushButton*     btnBack1{};     ///< 로그인 화면으로 복귀
    QPushButton*     btnFindId{};    ///< ADMIN_FIND_ID 요청 트리거

    // [탭2] 비밀번호 변경 필드/버튼
    QLineEdit*       cpId{};         ///< 관리자 ID(인증 대상)
    QLineEdit*       cpEmail{};      ///< 인증용 이메일(선택)
    QLineEdit*       cpPhone{};      ///< 인증용 전화(이메일 대체)
    QPushButton*     btnBack2{};     ///< 로그인 화면으로 복귀
    QPushButton*     btnChangePw{};  ///< ADMIN_VERIFY_FOR_PW → 새 비번 입력 → ADMIN_CHANGE_PW

    // ── 서버 응답 연결(일회성 핸들) ───────────────────────────────────────
    /**
     * @note 응답 대기-처리 후 반드시 disconnect 하여
     *       중복 처리/메모리 길항을 방지(특히 재시도/페이지 전환 시 안전).
     */
    QMetaObject::Connection connLogin_;     ///< LOGIN_OK/FAIL 대기용
    QMetaObject::Connection connFindId_;    ///< ADMIN_FIND_ID_OK/FAIL 대기용
    QMetaObject::Connection connVerify_;    ///< ADMIN_VERIFY_OK/FAIL 대기용
    QMetaObject::Connection connChangePw_;  ///< ADMIN_CHANGE_PW_OK/FAIL 대기용
};
