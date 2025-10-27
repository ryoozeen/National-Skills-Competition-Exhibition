#include "settings_page.h"
/*
 * @file settings_page.cpp
 * @brief 시스템 설정(서버 연결 정보) + 사용자/권한 관리(서버 연동) 구현부.
 *        - QSettings로 서버 호스트/포트 저장/로드
 *        - USER_LIST/ADD/UPDATE/DELETE 요청/응답 처리
 *        - 테이블 갱신 및 편집 다이얼로그
 */

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QSettings>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>   // ✅ QCoreApplication 사용

#include "networkclient.h"
#include "user_editor_dialog.h"

namespace {
constexpr int kDefaultPort = 8888;  // 기본 포트(미입력 시 사용)

QString trStateActive()   { return QObject::tr("활성"); }
QString trStateInactive() { return QObject::tr("비활성"); }
}
/** @brief 생성자: UI/스타일 구성 후 QSettings에서 서버 정보 로드 */ 

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    applyStyle();
    loadSettings();
}
/**
 * @brief 네트워크 객체 주입
 *  - 기존 연결 해제 → 새 객체로 교체
 *  - messageReceived 시그널을 onMessageFromServer에 연결
 *  - 진입 시 사용자 목록 요청
 */

void SettingsPage::setNetwork(NetworkClient* net)
{
    if (msgConn_) {
        disconnect(msgConn_);
        msgConn_ = {};
    }
    net_ = net;
    if (!net_) return;  // 네트워크 없으면 조용히 종료


    // 서버에서 오는 모든 JSON 메시지 수신
    msgConn_ = connect(net_, &NetworkClient::messageReceived,
                       this, &SettingsPage::onMessageFromServer);

    // 진입 시 사용자 목록 요청
    requestUserList();
}

/* ===================== UI ===================== */
/**
 * @brief UI 구성
 *  - 시스템 설정 박스: 서버 주소/포트 입력, 테스트/저장 버튼, 상태 라벨
 *  - 사용자/권한 박스: 추가/수정/삭제 버튼 + 테이블(5열)
 *  - 시그널 연결
 */

void SettingsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);

    // ── 시스템 설정 ──
    auto* boxSys = new QGroupBox(tr("시스템 설정"), this);
    auto* sysLay = new QVBoxLayout(boxSys);

    auto* form = new QFormLayout;
    serverHost = new QLineEdit;
    serverPort = new QLineEdit;
    form->addRow(tr("서버 주소"), serverHost);
    form->addRow(tr("포트"), serverPort);

    auto* rowBtns = new QHBoxLayout;
    btnTestServer = new QPushButton(tr("연결 테스트"));
    btnSaveSys    = new QPushButton(tr("저장"));
    rowBtns->addStretch();
    rowBtns->addWidget(btnTestServer);
    rowBtns->addWidget(btnSaveSys);

    sysStatus = new QLabel;
    sysStatus->setText(tr("미확인"));

    sysLay->addLayout(form);
    sysLay->addLayout(rowBtns);
    sysLay->addWidget(sysStatus);

    connect(btnTestServer, &QPushButton::clicked, this, &SettingsPage::onClickTestServer);
    connect(btnSaveSys,    &QPushButton::clicked, this, &SettingsPage::onClickSaveSystem);

    // ── 사용자/권한 ──
    auto* boxUsers = new QGroupBox(tr("사용자 / 권한"), this);
    auto* usersLay = new QVBoxLayout(boxUsers);

    // 상단 버튼들
    auto* btnRow = new QHBoxLayout;
    btnAddUser    = new QPushButton(tr("추가"));
    btnEditUser   = new QPushButton(tr("수정"));
    btnRemoveUser = new QPushButton(tr("삭제"));
    btnRow->addWidget(btnAddUser);
    btnRow->addWidget(btnEditUser);
    btnRow->addWidget(btnRemoveUser);
    btnRow->addStretch();
    usersLay->addLayout(btnRow);

    // 테이블 (5열: ID / 이름 / 권한 / 상태 / 연락처)
    tblUsers = new QTableWidget(0, 5, this);

    // 컬럼 헤더
    QStringList headers{ tr("ID"), tr("이름"), tr("권한"), tr("상태"), tr("연락처") };
    tblUsers->setHorizontalHeaderLabels(headers);

    // 선택/편집 속성
    tblUsers->setSelectionBehavior(QAbstractItemView::SelectRows); // 행 단위 선택

    tblUsers->setSelectionMode(QAbstractItemView::SingleSelection);
    tblUsers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tblUsers->setAlternatingRowColors(true);

    // 열 너비 정책: 연락처(마지막 열)를 가장 넓게
    auto* hh = tblUsers->horizontalHeader();
    hh->setStretchLastSection(false);
    hh->setMinimumSectionSize(50);
    hh->setSectionResizeMode(0, QHeaderView::ResizeToContents); // ID
    hh->setSectionResizeMode(1, QHeaderView::ResizeToContents); // 이름
    hh->setSectionResizeMode(2, QHeaderView::ResizeToContents); // 권한
    hh->setSectionResizeMode(3, QHeaderView::ResizeToContents); // 상태
    hh->setSectionResizeMode(4, QHeaderView::Stretch);  // 연락처 칼럼을 크게
          // 연락처(넓게)

    // 테이블 배치
    usersLay->addWidget(tblUsers);

    // 버튼 시그널 연결
    connect(btnAddUser,    &QPushButton::clicked, this, &SettingsPage::onClickAddUser);
    connect(btnEditUser,   &QPushButton::clicked, this, &SettingsPage::onClickEditUser);
    connect(btnRemoveUser, &QPushButton::clicked, this, &SettingsPage::onClickRemoveUser);

    root->addWidget(boxSys);
    root->addWidget(boxUsers);
    root->addStretch();
}
/** @brief 스타일 적용 지점(현재는 기본값 사용) */ 

void SettingsPage::applyStyle()
{
    // 필요한 경우 스타일 지정
}
/** @brief QSettings에서 서버 주소/포트를 읽어 입력창에 반영 */ 

void SettingsPage::loadSettings()
{
    QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    const QString host = s.value("server/host", "127.0.0.1").toString();
    const int port = s.value("server/port", kDefaultPort).toInt();

    serverHost->setText(host);
    serverPort->setText(QString::number(port));
}
/**
 * @brief 서버 주소/포트 유효성 검사 후 QSettings에 저장
 *  - 포트 범위(1~65535) 확인 및 경고
 */

bool SettingsPage::saveSettings()
{
    bool ok = false;
    const int port = serverPort->text().toInt(&ok);
    if (!ok || port <= 0 || port > 65535) {
        QMessageBox::warning(this, tr("오류"), tr("포트 번호가 올바르지 않습니다."));
        return false;
    }

    QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    s.setValue("server/host", serverHost->text());
    s.setValue("server/port", port);
    return true;
}

/* ========== 시스템 설정 탭 동작 ========== */
/**
 * @brief '연결 테스트' 버튼: 네트워크가 준비되었는지 확인 후 USER_LIST 요청
 *  - 응답 성공 시 '서버 연결 OK'로 상태 업데이트
 */

void SettingsPage::onClickTestServer()
{
    if (!net_) {
        QMessageBox::information(this, tr("정보"), tr("네트워크가 초기화되지 않았습니다."));
        return;
    }
    sysStatus->setText(tr("연결 테스트 중..."));

    // 간단한 ping 느낌으로 USER_LIST 시도 (권한 탭 동기화 겸용)
    requestUserList();
}
/** @brief 저장 버튼: QSettings 저장 후 serverConfigChanged(host,port) 방출 */ 

void SettingsPage::onClickSaveSystem()
{
    if (!saveSettings()) return;
    bool ok = false;
    quint16 p = serverPort->text().toUShort(&ok);
    if (!ok) p = kDefaultPort;
    emit serverConfigChanged(serverHost->text(), p);
    QMessageBox::information(this, tr("완료"), tr("설정을 저장했습니다."));
}

/* ========== 사용자/권한 탭 동작 ========== */
/**
 * @brief '추가' 버튼: 다이얼로그로 정보를 입력받아 USER_ADD 전송
 *  - 비밀번호는 비어 있으면 미변경 의미
 */

void SettingsPage::onClickAddUser()
{
    UserEditorDialog dlg(UserEditorDialog::AddMode, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const UserRecord rec = dlg.resultRecord();
    if (rec.id.trimmed().isEmpty() || rec.name.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("오류"), tr("ID와 이름은 필수입니다."));
        return;
    }

    if (!net_) {
        QMessageBox::warning(this, tr("오류"), tr("네트워크가 준비되지 않았습니다."));
        return;
    }

    QJsonObject user{
        {"id", rec.id},
        {"name", rec.name},
        {"role", rec.role},
        {"active", stateToActive(rec.state)},
        {"note", rec.note},
        {"email", rec.email},
        {"phone", rec.phone},
        {"department", rec.department},
        {"position", rec.position},
        {"avatarPath", rec.avatarPath}
    };
    if (!rec.password.isEmpty()) user.insert("password", rec.password);

    net_->sendJson(QJsonObject{{"cmd","USER_ADD"}, {"user", user}});
}
/** @brief '수정' 버튼: 현재 행을 레코드로 로드 → 다이얼로그 수정 → USER_UPDATE 전송 */ 

void SettingsPage::onClickEditUser()
{
    UserRecord cur;
    if (!currentRowToRecord(cur)) {
        QMessageBox::information(this, tr("안내"), tr("수정할 사용자를 선택하세요."));
        return;
    }

    UserEditorDialog dlg(UserEditorDialog::EditMode, this);
    dlg.setInitial(cur);
    if (dlg.exec() != QDialog::Accepted) return;

    const UserRecord rec = dlg.resultRecord();
    if (rec.id.trimmed().isEmpty() || rec.name.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("오류"), tr("ID와 이름은 필수입니다."));
        return;
    }

    if (!net_) {
        QMessageBox::warning(this, tr("오류"), tr("네트워크가 준비되지 않았습니다."));
        return;
    }

    QJsonObject user{
        {"id", rec.id},
        {"name", rec.name},
        {"role", rec.role},
        {"active", stateToActive(rec.state)},
        {"note", rec.note},
        {"email", rec.email},
        {"phone", rec.phone},
        {"department", rec.department},
        {"position", rec.position},
        {"avatarPath", rec.avatarPath}
    };
    if (!rec.password.isEmpty()) user.insert("password", rec.password);

    net_->sendJson(QJsonObject{{"cmd","USER_UPDATE"}, {"user", user}});
}
/** @brief '삭제' 버튼: 확인 후 USER_DELETE 전송 */ 

void SettingsPage::onClickRemoveUser()
{
    const int row = tblUsers->currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("안내"), tr("삭제할 사용자를 선택하세요."));
        return;
    }
    const QString id = tblUsers->item(row, 0)->text();
    if (id.isEmpty()) return;

    if (QMessageBox::question(this, tr("확인"),
                              tr("선택한 사용자(%1)를 삭제하시겠습니까?").arg(id))
        != QMessageBox::Yes) return;

    if (!net_) {
        QMessageBox::warning(this, tr("오류"), tr("네트워크가 준비되지 않았습니다."));
        return;
    }

    net_->sendJson(QJsonObject{{"cmd","USER_DELETE"}, {"id", id}});
}

/* ========== 서버 메시지 처리 ========== */
/**
 * @brief 서버로부터의 USER_* 응답 처리
 *  - USER_LIST_OK: 테이블 갱신 후 상태 '서버 연결 OK'
 *  - *_FAIL: 오류 메시지 표시
 *  - *_OK: 성공 시 목록 재요청으로 최신화
 */

void SettingsPage::onMessageFromServer(const QJsonObject& msg)
{
    const QString cmd = msg.value("cmd").toString();

    if (cmd == "USER_LIST_OK") {
        const QJsonArray items = msg.value("items").toArray();
        refreshTableFromJson(items);
        sysStatus->setText(tr("서버 연결 OK"));
        return;
    }
    if (cmd == "USER_LIST_FAIL") {
        sysStatus->setText(tr("서버 응답 실패(USER_LIST)"));
        QMessageBox::warning(this, tr("오류"),
                             msg.value("error").toString(tr("목록 조회 실패")));
        return;
    }

    if (cmd == "USER_ADD_OK" || cmd == "USER_UPDATE_OK" || cmd == "USER_DELETE_OK") {
        // 성공 시 항상 새로고침
        requestUserList();
        return;
    }
    if (cmd == "USER_ADD_FAIL" || cmd == "USER_UPDATE_FAIL" || cmd == "USER_DELETE_FAIL") {
        QMessageBox::warning(this, tr("오류"),
                             msg.value("error").toString(tr("요청 실패")));
        return;
    }
}

/* ========== 내부 유틸 ========== */
/** @brief {"cmd":"USER_LIST"} 요청 전송 */ 

void SettingsPage::requestUserList()
{
    if (!net_) return;  // 네트워크 없으면 조용히 종료

    net_->sendJson(QJsonObject{{"cmd","USER_LIST"}});
}
/**
 * @brief JSON 배열(items)로부터 테이블을 재구성
 *  - 연락처 칼럼은 "전화 · 이메일" 형태로 표시하고 툴팁 제공
 *  - profileStore 캐시에 UserRecord 동기화
 */

void SettingsPage::refreshTableFromJson(const QJsonArray& items)
{
    tblUsers->setRowCount(0);
    profileStore.clear();

    int r = 0;
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        const QString id    = o.value("id").toString();
        const QString name  = o.value("name").toString();
        const QString role  = o.value("role").toString();
        const int active    = o.value("active").toInt(1);

        // 연락처 필드
        const QString phone = o.value("phone").toString();
        const QString email = o.value("email").toString();

        tblUsers->insertRow(r);
        tblUsers->setItem(r, 0, new QTableWidgetItem(id));
        tblUsers->setItem(r, 1, new QTableWidgetItem(name));
        tblUsers->setItem(r, 2, new QTableWidgetItem(role));
        tblUsers->setItem(r, 3, new QTableWidgetItem(activeToState(active)));

        QString contact;
        if (!phone.isEmpty()) contact += phone;
        if (!email.isEmpty()) {
            if (!contact.isEmpty()) contact += u8" · ";
            contact += email;
        }
        auto* contactItem = new QTableWidgetItem(contact);
        if (!phone.isEmpty() || !email.isEmpty()) {
            QStringList tip;
            if (!phone.isEmpty()) tip << tr("전화: %1").arg(phone);
            if (!email.isEmpty()) tip << tr("이메일: %1").arg(email);
            contactItem->setToolTip(tip.join("\n"));
        }
        tblUsers->setItem(r, 4, contactItem);

        UserRecord rec;
        rec.id = id; rec.name = name; rec.role = role;
        rec.state = activeToState(active);
        rec.note.clear();
        rec.email = email;
        rec.phone = phone;
        rec.department = o.value("department").toString();
        rec.position   = o.value("position").toString();
        rec.avatarPath = o.value("avatarPath").toString();
        profileStore.insert(id, rec);
        ++r;
    }

    if (r > 0) tblUsers->selectRow(0);
}
/** @brief 현재 선택된 행을 UserRecord로 반환(없으면 false) */ 

bool SettingsPage::currentRowToRecord(UserRecord& out) const
{
    const int row = tblUsers->currentRow();
    if (row < 0) return false;

    const QString id = tblUsers->item(row, 0)->text();
    if (!profileStore.contains(id)) return false;

    out = profileStore.value(id);
    return true;
}
/** @brief "활성/비활성" → 1/0 변환 */ 

int SettingsPage::stateToActive(const QString& state)
{
    return (state.trimmed() == trStateInactive()) ? 0 : 1;
}
/** @brief 1/0 → "활성/비활성" 변환 */ 

QString SettingsPage::activeToState(int active)
{
    return (active == 0) ? trStateInactive() : trStateActive();
}
