#include "user_editor_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTabWidget>
#include <QFileDialog>
#include <QPixmap>
#include <QDialogButtonBox>
/*
 * @file user_editor_dialog.cpp
 * @brief 사용자 추가/수정 다이얼로그 구현부.
 *        - buildUi(): 탭과 버튼 박스 구성
 *        - buildBasicTab()/buildProfileTab(): 각 탭 구성
 *        - chooseAvatar(): 이미지 선택 → 미리보기
 *        - setInitial()/resultRecord(): 편집 초기화/결과 수집
 */
/** @brief 생성자: 제목을 모드에 맞게 설정하고 UI를 구성 */ 

UserEditorDialog::UserEditorDialog(Mode m, QWidget* parent)
    : QDialog(parent), mode(m)
{
    setWindowTitle(m==AddMode ? u8"사용자 추가" : u8"사용자 수정"); // 모드별 제목

    buildUi();
    resize(520, 420);
}
/** @brief 루트 레이아웃/탭 추가/확인-취소 버튼 박스 구성 및 시그널 연결 */ 

void UserEditorDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    tabs = new QTabWidget(this);

    auto* basic = new QWidget(this);
    auto* prof  = new QWidget(this);

    buildBasicTab(basic);
    buildProfileTab(prof);

    tabs->addTab(basic,  u8"기본정보");
    tabs->addTab(prof,   u8"프로필");

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, [this]{
        // 간단 검증(아이디/이름/비밀번호 일치)
        if (idEdit->text().trimmed().isEmpty() || nameEdit->text().trimmed().isEmpty()) {
            // 경고는 settings_page 쪽에서 이미 한 번 더 하므로 여기선 조용히 실패시키지 않음
        }
        if (!passEdit->text().isEmpty() && passEdit->text()!=pass2Edit->text()) return;
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    root->addWidget(tabs, 1);
    root->addWidget(box, 0);
}
/**
 * @brief 기본정보 탭 구성
 *  - ID/이름/권한/상태/비고/새 비밀번호/비밀번호 확인
 *  - 수정 모드에서는 ID 수정 불가
 */

void UserEditorDialog::buildBasicTab(QWidget* w) {
    auto* v = new QVBoxLayout(w);
    auto* f = new QFormLayout;
    f->setHorizontalSpacing(10);
    f->setVerticalSpacing(8);

    idEdit   = new QLineEdit(w);
    nameEdit = new QLineEdit(w);
    roleCombo  = new QComboBox(w);
    roleCombo->addItems({u8"작업자", u8"감독자", u8"최고관리자"}); // 권한 프리셋

    stateCombo = new QComboBox(w);
    stateCombo->addItems({u8"활성", u8"비활성"}); // 상태 프리셋

    noteEdit = new QLineEdit(w);

    passEdit  = new QLineEdit(w);  passEdit->setEchoMode(QLineEdit::Password);   // 비밀번호 입력 가림

    pass2Edit = new QLineEdit(w);  pass2Edit->setEchoMode(QLineEdit::Password);  // 비밀번호 확인 입력 가림


    if (mode==EditMode) idEdit->setReadOnly(true); // 수정 모드에서는 ID 잠금


    f->addRow(u8"아이디", idEdit);
    f->addRow(u8"이름", nameEdit);
    f->addRow(u8"권한", roleCombo);
    f->addRow(u8"상태", stateCombo);
    f->addRow(u8"비고", noteEdit);
    f->addRow(u8"새 비밀번호", passEdit);
    f->addRow(u8"비밀번호 확인", pass2Edit);

    v->addLayout(f);
    v->addStretch();
}
/** @brief 프로필 탭 구성(아바타 + 이메일/전화/부서/직책 입력) */ 

void UserEditorDialog::buildProfileTab(QWidget* w) {
    auto* v = new QVBoxLayout(w);

    // 상단: 아바타 + 변경 버튼
    auto* row = new QHBoxLayout;
    avatarPreview = new QLabel(w);
    avatarPreview->setFixedSize(80,80); // 정사각형 프리뷰(라운드 40px)

    avatarPreview->setStyleSheet("background:#eef2ff; border:1px solid #c7d2fe; border-radius:40px;");
    avatarPreview->setAlignment(Qt::AlignCenter);

    auto* btnAvatar = new QPushButton(u8"사진 변경", w);
    connect(btnAvatar, &QPushButton::clicked, this, &UserEditorDialog::chooseAvatar);

    row->addWidget(avatarPreview);
    row->addSpacing(8);
    row->addWidget(btnAvatar);
    row->addStretch();

    // 폼: 이메일/전화/부서/직책
    auto* f = new QFormLayout;
    f->setHorizontalSpacing(10);
    f->setVerticalSpacing(8);

    emailEdit = new QLineEdit(w);
    phoneEdit = new QLineEdit(w);
    deptEdit  = new QLineEdit(w);
    posEdit   = new QLineEdit(w);

    f->addRow(u8"이메일",  emailEdit);
    f->addRow(u8"전화",    phoneEdit);
    f->addRow(u8"부서",    deptEdit);
    f->addRow(u8"직책",    posEdit);

    v->addLayout(row);
    v->addSpacing(12);
    v->addLayout(f);
    v->addStretch();
}
/**
 * @brief 아바타 이미지 선택 대화상자 실행 후 미리보기 라벨에 스케일해서 표시
 *  - 지원 확장자: png/jpg/jpeg
 */

void UserEditorDialog::chooseAvatar() {
    const QString f = QFileDialog::getOpenFileName(this, u8"프로필 사진 선택", QString(),
                                                   "Images (*.png *.jpg *.jpeg)");
    if (f.isEmpty()) return;
    avatarPath_ = f;
    QPixmap pm(f);  // 선택한 이미지를 로드하여 원형 프리뷰에 표시

    if (!pm.isNull())
        avatarPreview->setPixmap(pm.scaled(80,80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
/** @brief 편집 모드에서 전달된 UserRecord 값을 각 필드에 채워 넣음 */ 

void UserEditorDialog::setInitial(const UserRecord& r) {
    idEdit->setText(r.id);
    nameEdit->setText(r.name);
    roleCombo->setCurrentText(r.role.isEmpty()? u8"작업자" : r.role);
    stateCombo->setCurrentText(r.state.isEmpty()? u8"활성" : r.state);
    noteEdit->setText(r.note);

    emailEdit->setText(r.email);
    phoneEdit->setText(r.phone);
    deptEdit->setText(r.department);
    posEdit->setText(r.position);

    avatarPath_ = r.avatarPath;
    if (!avatarPath_.isEmpty()) {
        QPixmap pm(f);  // 선택한 이미지를 로드하여 원형 프리뷰에 표시

        if (!pm.isNull())
            avatarPreview->setPixmap(pm.scaled(80,80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}
/** @brief 현재 입력값을 UserRecord 개체로 구성해 반환 */ 

UserRecord UserEditorDialog::resultRecord() const {
    UserRecord r;
    r.id    = idEdit->text().trimmed();
    r.name  = nameEdit->text().trimmed();
    r.role  = roleCombo->currentText();
    r.state = stateCombo->currentText();
    r.note  = noteEdit->text().trimmed();
    r.password = passEdit->text(); // 비어있으면 변경 안 함

    r.email = emailEdit->text().trimmed();
    r.phone = phoneEdit->text().trimmed();
    r.department = deptEdit->text().trimmed();
    r.position   = posEdit->text().trimmed();
    r.avatarPath = avatarPath_;
    return r;
}
