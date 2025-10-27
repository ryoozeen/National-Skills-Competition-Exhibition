#pragma once
/**
 * @file settings_page.h
 * @brief 시스템 설정 + 사용자/권한 관리 페이지 헤더.
 *        - 시스템: 서버 호스트/포트 저장(QSettings) 및 연결 테스트
 *        - 사용자: USER_LIST/ADD/UPDATE/DELETE JSON 프로토콜로 서버와 동기화
 *        - NetworkClient는 AdminWindow에서 주입(setNetwork)
 */

#include <QWidget>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>

class QTabWidget;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QCheckBox;
class QLabel;

class NetworkClient;                // 네트워크 주입
#include "user_editor_dialog.h"     // UserRecord 정의 사용

class SettingsPage : public QWidget {  // 설정/권한 UI와 서버 통신을 담당하는 페이지

    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

    // AdminWindow 쪽에서 주입
    void setNetwork(NetworkClient* net);  // 외부에서 네트워크 객체 주입(시그널 연결 및 초기 목록 요청)


signals:
    // 시스템 설정(서버 주소/포트) 저장 시 알림
    void serverConfigChanged(const QString& host, quint16 port);  // 서버 주소/포트 저장 시 상위에 알림(재연결 등)


private slots:
    // ── 시스템 설정 탭 ──
    void onClickTestServer();  // USER_LIST 요청으로 연결 확인

    void onClickSaveSystem();  // QSettings에 저장 후 serverConfigChanged 방출


    // ── 사용자/권한 탭 ──
    void onClickAddUser();  // 사용자 추가 다이얼로그 → USER_ADD 전송

    void onClickEditUser();  // 선택 사용자 수정 다이얼로그 → USER_UPDATE 전송

    void onClickRemoveUser();  // 선택 사용자 삭제 확인 → USER_DELETE 전송


    // 서버에서 온 JSON 응답 처리
    void onMessageFromServer(const QJsonObject& msg);  // USER_* 응답 분기 처리 및 테이블 갱신


private:
    // 내부 유틸
    void buildUi();  // 폼/버튼/테이블 생성 및 속성 설정, 시그널 연결

    void applyStyle();  // 필요 시 스타일시트 적용 지점

    void loadSettings();  // QSettings에서 서버 주소/포트 로드

    bool saveSettings();  // 유효성 검사 후 QSettings 저장


    // 사용자 테이블 관련
    void requestUserList();  // {"cmd":"USER_LIST"} 전송

    void refreshTableFromJson(const QJsonArray& items);  // 테이블을 서버 JSON 배열로부터 재구성

    bool currentRowToRecord(UserRecord& out) const;  // 현재 선택된 행을 UserRecord로 추출


    static int      stateToActive(const QString& state);  // "활성/비활성" ↔ 1/0 변환
 // "활성"/"비활성" -> 1/0
    static QString  activeToState(int active);  // 1/0 ↔ "활성/비활성" 변환
           // 1/0 -> "활성"/"비활성"

private:
    // ── 시스템 설정 탭 ──
    QLineEdit *serverHost{};  // 서버 주소 입력 필드

    QLineEdit *serverPort{};  // 포트 입력 필드

    QPushButton *btnTestServer{};  // 연결 테스트 버튼

    QPushButton *btnSaveSys{};  // 설정 저장 버튼

    QLabel *sysStatus{};  // 연결 테스트 상태 라벨


    // ── 사용자/권한 탭 ──
    QTableWidget *tblUsers{};  // 사용자 목록 테이블(ID/이름/권한/상태/연락처)

    QPushButton *btnAddUser{};  // 추가

    QPushButton *btnEditUser{};  // 수정

    QPushButton *btnRemoveUser{};  // 삭제


    // (이전 더미용) 로컬 캐시 — 이제는 서버 단일 소스로 대체
    QHash<QString, UserRecord> profileStore;  // 현재 테이블과 동기화된 간단 캐시


    // 네트워크
    NetworkClient* net_{};  // 서버 통신 객체(외부 주입)

    QMetaObject::Connection msgConn_;
};
