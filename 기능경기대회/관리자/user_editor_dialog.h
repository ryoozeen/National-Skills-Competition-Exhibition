#pragma once
/**
 * @file user_editor_dialog.h
 * @brief 사용자 추가/수정 다이얼로그.
 *        - 기본정보 탭: ID/이름/권한/상태/비고/비밀번호(확인 포함)
 *        - 프로필 탭: 아바타/이메일/전화/부서/직책
 *        - resultRecord(): 입력값을 UserRecord로 반환
 */
#include <QDialog>
#include <QString>

struct UserRecord {  // 서버와 주고받는 사용자 정보 구조체

    QString id;
    QString name;
    QString role;
    QString state;
    QString note;
    QString password; // 변경시에만 채움
    // 프로필
    QString email;
    QString phone;
    QString department;
    QString position;
    QString avatarPath;
};

class QLineEdit;
class QComboBox;
class QLabel;
class QTabWidget;

class UserEditorDialog : public QDialog {  // 사용자 추가/수정 입력을 담당하는 모달 대화상자

    Q_OBJECT
public:
    enum Mode { AddMode, EditMode };  // 모드: 새로 추가 vs 기존 수정

    explicit UserEditorDialog(Mode m, QWidget* parent=nullptr);  // 모드 지정 생성자(제목/위젯 구성)


    void setInitial(const UserRecord& rec);  // 편집 모드에서 초기값을 UI에 반영

    UserRecord resultRecord() const;  // 현재 UI 값을 UserRecord로 빌드하여 반환


private slots:
    void chooseAvatar();  // 이미지 파일 선택 → 아바타 미리보기/경로 저장


private:
    void buildUi();  // 탭/폼/버튼 박스 구성 및 시그널 연결

    void buildBasicTab(QWidget* w);  // 기본정보 탭 구성

    void buildProfileTab(QWidget* w);  // 프로필 탭 구성


private:
    Mode mode;

    QTabWidget* tabs{};  // 탭 컨테이너


    // --- 기본정보 탭 ---
    QLineEdit* idEdit{};  // 아이디

    QLineEdit* nameEdit{};  // 이름

    QComboBox* roleCombo{};  // 권한(작업자/감독자/최고관리자)

    QComboBox* stateCombo{};  // 상태(활성/비활성)

    QLineEdit* noteEdit{};  // 비고

    QLineEdit* passEdit{};  // 새 비밀번호(비어있으면 변경 안 함)

    QLineEdit* pass2Edit{};  // 새 비밀번호 확인


    // --- 프로필 탭 ---
    QLabel*    avatarPreview{};  // 아바타 미리보기(원형 스타일)

    QString    avatarPath_;
      // 아바타 파일 경로
QLineEdit* emailEdit{};  // 이메일

    QLineEdit* phoneEdit{};  // 전화

    QLineEdit* deptEdit{};  // 부서

    QLineEdit* posEdit{};  // 직책

};
