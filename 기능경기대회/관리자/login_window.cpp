#include "login_window.h"   // 로그인 화면 컨테이너(페이지 전환/네트워크 부트스트랩/스타일)
#include "networkclient.h"  // 서버와의 TCP/JSON 프로토콜 클라이언트(HELLO/LOGIN 등 송수신)
#include "admin_window.h"   // 로그인 성공 후 띄울 메인 어드민 UI

// ── UI 구성/리소스 ────────────────────────────────────────────────────────
#include <QVBoxLayout>                  // 전체 페이지 수직 배치(로고 → 스택 → 하단바)
#include <QHBoxLayout>                  // 버튼/상태 등 수평 배치
#include <QGridLayout>                  // 로그인 폼 라벨/필드 2열 배치
#include <QFormLayout>                  // 계정 탭 입력 폼(라벨-필드 한 줄)
#include <QLabel>                       // 정적 텍스트(로고 대체/상태/라벨)
#include <QLineEdit>                    // ID/PW 및 계정 관련 입력 필드
#include <QPushButton>                  // 로그인/찾기/변경/종료 등 액션
#include <QComboBox>                    // 회사 선택(에디터블 콤보)
#include <QFrame>                       // 카드형 컨테이너(라운드/테두리)
#include <QGraphicsDropShadowEffect>    // 카드 그림자(선택적)
#include <QPalette>                     // 페이지 배경톤 고정(하늘색)
#include <QPixmap>                      // 로고 이미지 로드/스케일
#include <QCoreApplication>             // appDirPath()로 INI 경로 구성
#include <QSettings>                    // admin_client.ini에서 서버/환경 파라미터 로드
#include <QStackedWidget>               // 로그인/계정 찾기 2개 페이지 전환 컨테이너
#include <QTabWidget>                   // 계정 페이지 내부 탭(아이디 찾기/비밀번호 변경)
#include <QMessageBox>                  // 사용자 경고/안내 다이얼로그
#include <QJsonObject>                  // 네트워크 요청/응답 JSON
#include <QDialog>                      // 새 비밀번호 입력 팝업
#include <QDialogButtonBox>             // 팝업 OK/Cancel 버튼 모음
#include <QTimer>                       // 앱 시작 직후 네트워크 연결 시도(0ms single-shot)
#include <QDebug>                       // 디버그 로그

#ifndef USE_CARD_SHADOW
#define USE_CARD_SHADOW 1               // 카드 그림자 사용 여부(시각 품질/성능 스위치)
#endif

// ─────────────────────────────────────────────────────────────
// LoginWindow
// - 앱 진입점 UI: 서버 연결 → 로그인 → AdminWindow 전환.
// - 중앙 스택: [로그인] ↔ [계정(아이디 찾기/비번 변경)].
// - admin_client.ini(server 섹션)에서 서버 host/port 로드.
// - NetworkClient를 초기화/상태 표시/오류 처리.
// ─────────────────────────────────────────────────────────────
LoginWindow::LoginWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle(u8"안전관리 시스템 – 관리자 로그인");
    resize(980, 620);
    buildUi();     // UI 트리 및 시그널 구성
    applyStyle();  // 페이지 공통 룩앤필 적용

    // 앱 시작 즉시 서버 연결 시도(HELLO/상태 표시까지)
    QTimer::singleShot(0, this, [this]{
        if (statusLabel) statusLabel->setText(u8"서버 연결 중…");
        ensureNetwork();
    });
}

// ─────────────────────────────────────────────────────────────
// buildUi()
// - 상단 로고 + 중앙 스택(로그인/계정) + 하단 상태/종료 버튼.
// - 로그인/계정 각각 build*Page()에서 카드/폼 구성.
// - 종료 버튼: 네트워크 종료 후 앱 종료.
// ─────────────────────────────────────────────────────────────
void LoginWindow::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(32, 28, 32, 32); // 페이지 외곽 여백
    root->setSpacing(12);

    // 상단 로고(이미지 실패 시 텍스트 대체)
    logoLabel = new QLabel(this);
    QPixmap logoPx(":/assets/logo_placeholder.png");
    if (!logoPx.isNull())
        logoLabel->setPixmap(logoPx.scaledToHeight(48, Qt::SmoothTransformation));
    else
        logoLabel->setText(u8"<b>Safety Admin</b>");
    logoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    root->addWidget(logoLabel);

    // 하단 상태/종료 행(상태는 좌측, 종료는 우측)
    auto *bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(0,12,0,0);
    bottomRow->setSpacing(12);

    statusLabel = new QLabel(this);                                 // 네트워크/로그인 진행 상태
    statusLabel->setStyleSheet("color:#dc2626; font-weight:600;");  // 눈에 띄는 적색

    btnExit = new QPushButton(u8"종료", this);                      // 앱 종료 액션
    btnExit->setObjectName("bottomExit");
    btnExit->setFixedHeight(36);
    btnExit->setCursor(Qt::PointingHandCursor);
    connect(btnExit, &QPushButton::clicked, this, [this]{
        if (net_) net_->disconnectFromHost(); // 서버 세션 정리
        QCoreApplication::quit();             // 프로세스 종료
    });

    // 중앙 컨텐츠: 로그인/계정 페이지 스택
    stack = new QStackedWidget(this);
    buildLoginPage();   // 로그인 카드/폼
    buildAccountPage(); // 계정탭(아이디 찾기/비번 변경)
    stack->addWidget(pageLogin);
    stack->addWidget(pageAccount);
    showLoginPage();    // 기본은 로그인 페이지를 표시

    // 루트 배치
    root->addWidget(stack, 1);
    bottomRow->addWidget(statusLabel, 1);
    bottomRow->addStretch();
    bottomRow->addWidget(btnExit);
    root->addLayout(bottomRow);
}

// ─────────────────────────────────────────────────────────────
// buildLoginPage()
// - 좌측 그리드(회사/ID/PW) + 우측 버튼 열(Log-In / 아이디/비번 찾기).
// - Log-In 클릭 시: ensureNetwork → ID/PW 검사 → 로그인 요청.
// - LOGIN_OK 수신 시 AdminWindow를 생성/표시하고 LoginWindow는 숨김.
//   · net_ 소유권을 AdminWindow로 넘기기 위해 this의 포인터를 nullptr로 클리어.
// ─────────────────────────────────────────────────────────────
void LoginWindow::buildLoginPage() {
    pageLogin = new QWidget(this);
    auto *outer = new QVBoxLayout(pageLogin);
    outer->setContentsMargins(0,0,0,0);
    outer->setSpacing(0);

    // 카드 컨테이너(라운드/그림자)
    cardLogin = new QFrame(pageLogin);
    cardLogin->setObjectName("loginCard");
    cardLogin->setMinimumSize(680, 320);
#if USE_CARD_SHADOW
    auto *shadow = new QGraphicsDropShadowEffect(cardLogin);
    shadow->setBlurRadius(28); shadow->setOffset(0,10); shadow->setColor(QColor(0,0,0,60));
    cardLogin->setGraphicsEffect(shadow); // 시각 품질(부드러운 부양감)
#endif

    auto *cardH = new QHBoxLayout(cardLogin);
    cardH->setContentsMargins(28,28,28,28);
    cardH->setSpacing(24);

    // 좌측: 회사/ID/PW 폼
    auto *formGrid = new QGridLayout;
    formGrid->setHorizontalSpacing(16);
    formGrid->setVerticalSpacing(12);

    auto *lbCompany = new QLabel(u8"회사명", cardLogin);
    auto *lbUser    = new QLabel("ID", cardLogin);
    auto *lbPw      = new QLabel("PW", cardLogin);

    companyCombo = new QComboBox(cardLogin);     // 회사 선택(에디터블: 수동 입력 허용)
    companyCombo->setEditable(true);
    companyCombo->addItems({ "기경물류" });      // 초기 후보(예시)

    userEdit = new QLineEdit(cardLogin);         // 관리자 ID
    userEdit->setPlaceholderText("ID");
    userEdit->setClearButtonEnabled(true);

    pwEdit = new QLineEdit(cardLogin);           // 관리자 PW
    pwEdit->setPlaceholderText("PW");
    pwEdit->setEchoMode(QLineEdit::Password);
    pwEdit->setClearButtonEnabled(true);

    formGrid->addWidget(lbCompany,    0,0);
    formGrid->addWidget(companyCombo, 0,1);
    formGrid->addWidget(lbUser,       1,0);
    formGrid->addWidget(userEdit,     1,1);
    formGrid->addWidget(lbPw,         2,0);
    formGrid->addWidget(pwEdit,       2,1);

    // 우측: 동작 버튼 열(Log-In / 아이디/비밀번호 찾기)
    auto *btnCol = new QVBoxLayout;
    btnCol->setSpacing(8);

    btnLogin = new QPushButton("Log-In", cardLogin); // 로그인 트리거
    btnLogin->setObjectName("btnLoginBig");
    btnLogin->setDefault(true);                      // 엔터키 기본 액션

    btnFind = new QPushButton(u8"아이디/비밀번호 찾기", cardLogin); // 계정 탭 전환
    btnFind->setObjectName("btnFind");
    btnFind->setCursor(Qt::PointingHandCursor);

    // 버튼 크기 통일(명확한 액션 영역)
    const int kActionW = 220;
    const int kActionH = 72;
    btnLogin->setFixedSize(kActionW, kActionH);
    btnFind ->setFixedSize(kActionW, kActionH);

    btnCol->addStretch();
    btnCol->addWidget(btnLogin);
    btnCol->addWidget(btnFind);
    btnCol->addStretch();

    // 카드 내부 배치: [폼(좌)] [버튼열(우)]
    cardH->addLayout(formGrid, 3);
    cardH->addLayout(btnCol,   1);

    // 카드 수직 중앙 정렬
    outer->addStretch();
    outer->addWidget(cardLogin, 0, Qt::AlignHCenter);
    outer->addStretch();

    // 로그인 버튼 동작
    connect(btnLogin, &QPushButton::clicked, this, [this]{
        ensureNetwork(); // 네트워크 세션이 없다면 연결 생성/HELLO 수행
        const QString id = userEdit->text().trimmed();
        const QString pw = pwEdit->text();

        // 최소 입력 검증(즉시 피드백)
        if (id.isEmpty()) { statusLabel->setText(u8"ID를 입력하세요."); userEdit->setFocus(); return; }
        if (pw.isEmpty()) { statusLabel->setText(u8"PW를 입력하세요."); pwEdit->setFocus(); return; }

        // 이전 로그인 리스너가 남아 있을 수 있으므로 정리 후 재연결
        if (connLogin_) QObject::disconnect(connLogin_);
        connLogin_ = connect(net_, &NetworkClient::messageReceived, this,
                             [this](const QJsonObject& o){
                                 const QString cmd = o.value("cmd").toString().toUpper();
                                 if (cmd == "LOGIN_OK") {
                                     // 로그인 성공 → 메인 어드민 창으로 전환
                                     auto *w = new AdminWindow;
                                     w->setNetwork(net_);          // ✅ 네트워크 소유/관리 주체를 AdminWindow로 이전
                                     w->resize(this->size());
                                     w->setUserName(userEdit->text());

                                     net_ = nullptr;               // ✅ LoginWindow는 더이상 net_를 소유/관리하지 않음
                                     if (connLogin_) { QObject::disconnect(connLogin_); connLogin_ = {}; }

                                     w->setCompanyName(companyCombo->currentText());
                                     w->show(); this->hide();

                                     // 로그아웃 요청 시: AdminWindow 닫고, 로그인 창 복귀
                                     connect(w, &AdminWindow::logoutRequested, this, [this, w]{
                                         w->close(); this->show(); this->raise(); this->activateWindow();
                                     });

                                     QObject::disconnect(connLogin_);
                                 } else if (cmd == "LOGIN_FAIL") {
                                     // 인증 실패(간단 메시지)
                                     if (statusLabel) statusLabel->setText(u8"로그인 실패: 아이디/비밀번호 확인");
                                     QObject::disconnect(connLogin_);
                                 }
                             });

        net_->login(id, pw); // 서버에 로그인 요청 전송(JSON)
    });

    // 계정 탭으로 전환(아이디 찾기/비밀번호 변경)
    connect(btnFind, &QPushButton::clicked, this, [this]{ showAccountPage(); });
}

// ─────────────────────────────────────────────────────────────
// buildAccountPage()
// - QTabWidget에 "아이디 찾기" / "비밀번호 변경" 두 탭 구성.
// - 각 탭은 우측 하단에 [← 로그인으로] / [실행] 버튼 두 개(크기 통일).
// - 아이디 찾기: email/phone 중 하나로 ADMIN_FIND_ID 요청.
// - 비밀번호 변경: ADMIN_VERIFY_FOR_PW → 팝업으로 새 비번 입력 → ADMIN_CHANGE_PW.
// ─────────────────────────────────────────────────────────────
void LoginWindow::buildAccountPage() {
    pageAccount = new QWidget(this);
    auto *outer = new QVBoxLayout(pageAccount);
    outer->setContentsMargins(0,0,0,0);
    outer->setSpacing(0);

    cardAccount = new QFrame(pageAccount);
    cardAccount->setObjectName("loginCard");
    cardAccount->setMinimumSize(680, 360);
#if USE_CARD_SHADOW
    auto *shadow = new QGraphicsDropShadowEffect(cardAccount);
    shadow->setBlurRadius(28); shadow->setOffset(0,10); shadow->setColor(QColor(0,0,0,60));
    cardAccount->setGraphicsEffect(shadow);
#endif

    auto *cardV = new QVBoxLayout(cardAccount);
    cardV->setContentsMargins(24,24,24,24);
    cardV->setSpacing(12);

    tabs = new QTabWidget(cardAccount); // 계정 관련 액션들을 한 화면에서 전환

    // 공통 버튼 크기(명확한 터치 타깃)
    const int kSecW = 200;
    const int kSecH = 56;

    // ===== 탭 1: 아이디 찾기 =====
    QWidget *tabFind = new QWidget(tabs);
    {
        auto *v = new QVBoxLayout(tabFind);
        auto *form = new QFormLayout;
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(8);

        fiEmail = new QLineEdit(tabFind);                      // 이메일로 검색
        fiEmail->setPlaceholderText("example@company.com");
        form->addRow(u8"이메일", fiEmail);

        fiPhone = new QLineEdit(tabFind);                      // 전화번호로 검색(대안)
        fiPhone->setPlaceholderText("010-0000-0000");
        form->addRow(u8"전화", fiPhone);

        auto *row = new QHBoxLayout;
        row->addStretch();
        btnBack1 = new QPushButton(u8"← 로그인으로", tabFind); // 스택 되돌리기
        btnFindId = new QPushButton(u8"아이디 찾기", tabFind);  // ADMIN_FIND_ID 트리거
        btnBack1->setObjectName("btnSecondary");
        btnFindId->setObjectName("btnSecondary");

        btnBack1->setFixedSize(kSecW, kSecH);
        btnFindId->setFixedSize(kSecW, kSecH);

        row->addWidget(btnBack1);
        row->addSpacing(8);
        row->addWidget(btnFindId);

        v->addLayout(form);
        v->addLayout(row);
        v->addStretch();
    }
    tabs->addTab(tabFind, u8"아이디 찾기");

    // ===== 탭 2: 비밀번호 변경 =====
    QWidget *tabPw = new QWidget(tabs);
    {
        auto *v = new QVBoxLayout(tabPw);
        auto *form = new QFormLayout;
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(8);

        cpId = new QLineEdit(tabPw);                          // 관리자 ID
        form->addRow(u8"관리자 ID", cpId);

        cpEmail = new QLineEdit(tabPw);                       // 인증용 이메일(선택)
        cpEmail->setPlaceholderText("example@company.com");
        form->addRow(u8"이메일", cpEmail);

        cpPhone = new QLineEdit(tabPw);                       // 인증용 전화(이메일 대안)
        cpPhone->setPlaceholderText("010-0000-0000");
        form->addRow(u8"전화", cpPhone);

        auto *row = new QHBoxLayout;
        row->addStretch();
        btnBack2 = new QPushButton(u8"← 로그인으로", tabPw);
        btnChangePw = new QPushButton(u8"비밀번호 변경", tabPw);
        btnBack2->setObjectName("btnSecondary");
        btnChangePw->setObjectName("btnSecondary");

        btnBack2->setFixedSize(kSecW, kSecH);
        btnChangePw->setFixedSize(kSecW, kSecH);

        row->addWidget(btnBack2);
        row->addSpacing(8);
        row->addWidget(btnChangePw);

        v->addLayout(form);
        v->addLayout(row);
        v->addStretch();
    }
    tabs->addTab(tabPw, u8"비밀번호 변경");

    cardV->addWidget(tabs);
    outer->addStretch();
    outer->addWidget(cardAccount, 0, Qt::AlignHCenter);
    outer->addStretch();

    // 뒤로가기(스택 전환)
    connect(btnBack1, &QPushButton::clicked, this, [this]{ showLoginPage(); });
    connect(btnBack2, &QPushButton::clicked, this, [this]{ showLoginPage(); });

    // 아이디 찾기: email 또는 phone 중 하나만 보냄
    connect(btnFindId, &QPushButton::clicked, this, [this]{
        ensureNetwork();

        QJsonObject req; req["cmd"]="ADMIN_FIND_ID";
        const QString email = fiEmail->text().trimmed();
        if (!email.isEmpty()) {
            req["email"] = email;
        } else {
            const QString phone = fiPhone->text().trimmed();
            if (phone.isEmpty()) {
                QMessageBox::warning(this, u8"입력 확인", u8"이메일 또는 전화번호를 입력하세요.");
                return;
            }
            req["phone"] = phone;
        }

        // 일회성 응답 리스너 구성 → 완료 후 즉시 해제
        if (connFindId_) QObject::disconnect(connFindId_);
        connFindId_ = connect(net_, &NetworkClient::messageReceived, this,
                              [this](const QJsonObject& o){
                                  const QString cmd = o.value("cmd").toString();
                                  if (cmd == "ADMIN_FIND_ID_OK") {
                                      const QString id = o.value("admin_id").toString();
                                      QMessageBox::information(this, u8"아이디 확인", u8"관리자 아이디: " + id);
                                      cpId->setText(id);           // 비번 변경 탭으로 컨텍스트 전달
                                      tabs->setCurrentIndex(1);    // “비밀번호 변경” 탭으로 전환
                                  } else if (cmd == "ADMIN_FIND_ID_FAIL") {
                                      QMessageBox::critical(this, u8"실패", u8"일치하는 계정을 찾지 못했습니다.");
                                  }
                                  QObject::disconnect(connFindId_);
                              });
        net_->sendJson(req);
    });

    // 비밀번호 변경: (1) 인증 → (2) 새 비번 입력 팝업 → (3) 최종 변경
    connect(btnChangePw, &QPushButton::clicked, this, [this]{
        ensureNetwork();

        const QString id    = cpId->text().trimmed();
        const QString email = cpEmail->text().trimmed();
        const QString phone = cpPhone->text().trimmed();

        // 최소 입력 검증
        if (id.isEmpty()) {
            QMessageBox::warning(this, u8"입력 확인", u8"관리자 ID를 입력하세요.");
            return;
        }
        if (email.isEmpty() && phone.isEmpty()) {
            QMessageBox::warning(this, u8"입력 확인", u8"이메일 또는 전화번호는 입력해야 합니다.");
            return;
        }

        // (1) 서버에 인증 요청(이메일 또는 전화 중 하나)
        QJsonObject verify;
        verify["cmd"] = "ADMIN_VERIFY_FOR_PW";
        verify["admin_id"] = id;
        if (!email.isEmpty()) verify["email"] = email;
        else                  verify["phone"] = phone;

        if (connVerify_) QObject::disconnect(connVerify_);
        connVerify_ = connect(net_, &NetworkClient::messageReceived, this,
                              [this, id, email, phone](const QJsonObject& o){
                                  const QString cmd = o.value("cmd").toString();
                                  if (cmd == "ADMIN_VERIFY_OK") {
                                      QObject::disconnect(connVerify_);

                                      // (2) 팝업으로 새 비밀번호 입력(확인까지)
                                      QString newPw;
                                      if (!promptNewPassword(newPw)) return;

                                      // (3) 최종 변경 요청
                                      QJsonObject req;
                                      req["cmd"] = "ADMIN_CHANGE_PW";
                                      req["admin_id"] = id;
                                      req["new_pw"] = newPw;
                                      if (!email.isEmpty()) req["email"] = email;
                                      else                  req["phone"] = phone;

                                      if (connChangePw_) QObject::disconnect(connChangePw_);
                                      connChangePw_ = connect(net_, &NetworkClient::messageReceived, this,
                                                              [this](const QJsonObject& o2){
                                                                  const QString cmd2 = o2.value("cmd").toString();
                                                                  if (cmd2 == "ADMIN_CHANGE_PW_OK") {
                                                                      QMessageBox::information(this, u8"완료", u8"비밀번호가 변경되었습니다.");
                                                                      showLoginPage(); // 변경 후 로그인 화면으로
                                                                  } else if (cmd2 == "ADMIN_CHANGE_PW_FAIL") {
                                                                      QMessageBox::critical(this, u8"실패", u8"비밀번호 변경에 실패했습니다.");
                                                                  }
                                                                  QObject::disconnect(connChangePw_);
                                                              });
                                      net_->sendJson(req);
                                  } else if (cmd == "ADMIN_VERIFY_FAIL") {
                                      QMessageBox::critical(this, u8"실패", u8"인증 정보가 일치하지 않습니다.");
                                      QObject::disconnect(connVerify_);
                                  }
                              });
        net_->sendJson(verify);
    });
}

// ─────────────────────────────────────────────────────────────
// promptNewPassword(outPw)
// - 모달 팝업으로 새 비밀번호 2회 입력/검증.
// - 공백/불일치 검증 후 OK → outPw에 설정, true 반환.
// - Cancel 또는 검증 실패 → false.
// ─────────────────────────────────────────────────────────────
bool LoginWindow::promptNewPassword(QString &outPw) {
    QDialog dlg(this);
    dlg.setWindowTitle(u8"새 비밀번호 설정");
    dlg.setModal(true);

    auto *v = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    auto *e1 = new QLineEdit(&dlg);
    auto *e2 = new QLineEdit(&dlg);
    e1->setEchoMode(QLineEdit::Password);
    e2->setEchoMode(QLineEdit::Password);

    form->addRow(u8"새 비밀번호", e1);
    form->addRow(u8"새 비번 확인", e2);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    v->addLayout(form);
    v->addWidget(btns);

    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, [&]{
        if (e1->text().isEmpty()) {
            QMessageBox::warning(&dlg, u8"입력 확인", u8"새 비밀번호를 입력하세요.");
            return;
        }
        if (e1->text() != e2->text()) {
            QMessageBox::warning(&dlg, u8"확인", u8"두 칸이 일치하지 않습니다.");
            return;
        }
        outPw = e1->text();
        dlg.accept();
    });
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    return dlg.exec() == QDialog::Accepted;
}

// ─────────────────────────────────────────────────────────────
// applyStyle()
// - 페이지 배경/카드/입력/버튼/탭 룩앤필 고정(하늘색 톤).
// - OS 다크/라이트 등 시스템 테마에 영향받지 않도록 QSS로 강제.
// ─────────────────────────────────────────────────────────────
void LoginWindow::applyStyle() {
    QPalette pal = palette(); pal.setColor(QPalette::Window, QColor("#eaf0ff")); // 전체 배경 하늘색
    setAutoFillBackground(true); setPalette(pal);

    setStyleSheet(R"(
        QWidget { font-family:'Noto Sans KR','Malgun Gothic',sans-serif; font-size:14px; color:#111827; }

        #loginCard { background:#fff; border-radius:18px; border:1px solid #e6ecff; }

        QLineEdit, QComboBox {
            height:36px; padding:6px 10px; border:1px solid #cfe0ff; border-radius:8px; background:#fff;
        }
        QLineEdit:focus, QComboBox:focus { border:2px solid #8bb1ff; }

        QPushButton#btnLoginBig {
            font-weight:700; color:#fff; border:none; border-radius:12px;
            background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #6aa3ff, stop:1 #3c73db);
        }
        QPushButton#btnFind,
        QPushButton#btnSecondary {
            font-weight:600; color:#374151;
            background:#e5e7eb; border:none; border-radius:10px;
        }
        QPushButton#bottomExit {
            border:1px solid #c7d2fe; border-radius:18px; padding:6px 16px;
            background:#f8fafc; color:#4b5563; min-width:96px;
        }

        QTabWidget::pane { border:1px solid #dbe3ff; border-radius:8px; background:#f8fbff; }
        QTabBar::tab {
            padding:8px 16px; margin:4px; border:1px solid #dbe3ff; border-bottom:none;
            border-top-left-radius:8px; border-top-right-radius:8px; background:#eef2ff; color:#374151;
        }
        QTabBar::tab:selected { background:#ffffff; color:#111827; }
    )");
}

// ─────────────────────────────────────────────────────────────
// ensureNetwork()
// - NetworkClient 단일 인스턴스를 생성/연결(역할: "admin").
// - 상태(stateChanged)/오류(errorOccurred) UI 피드백.
// - admin_client.ini [server]의 host/port 읽어 접속 시도.
// - 중복 호출 시 이미 존재하면 재생성하지 않음(멱등).
// ─────────────────────────────────────────────────────────────
void LoginWindow::ensureNetwork() {
    if (net_) return; // 이미 초기화됨

    net_ = new NetworkClient(this);
    net_->setRole("admin"); // 연결되면 서버로 HELLO 자동 전송(역할 식별)

    // 상태 변화 라벨 갱신(사용자에게 현재 네트워크 상황 노출)
    connect(net_, &NetworkClient::stateChanged, this,
            [this](QAbstractSocket::SocketState s){
                QString msg;
                switch (s) {
                case QAbstractSocket::UnconnectedState: msg = u8"서버 미연결"; break;
                case QAbstractSocket::HostLookupState:  msg = u8"서버 조회 중…"; break;
                case QAbstractSocket::ConnectingState:  msg = u8"서버 연결 중…"; break;
                case QAbstractSocket::ConnectedState:   msg = u8"서버 연결됨"; break;
                case QAbstractSocket::ClosingState:     msg = u8"연결 종료 중…"; break;
                default: msg = u8"상태 변경"; break;
                }
                if (statusLabel) statusLabel->setText(msg);
            });

    // 오류 발생 시 사용자에게 즉시 피드백
    connect(net_, &NetworkClient::errorOccurred, this,
            [this](const QString& err){
                if (statusLabel) statusLabel->setText(u8"서버 오류: " + err);
            });

    // 초기 핸드셰이크(HELLO_OK) 로깅(개발/운영 진단용)
    connect(net_, &NetworkClient::messageReceived, this,
            [](const QJsonObject& o){
                if (o.value("cmd").toString() == "HELLO_OK")
                    qInfo() << "[NET] recv HELLO_OK";
            });

    // 서버 주소 로드(INI): ./admin_client.ini의 [server] 섹션
    QSettings ini(QCoreApplication::applicationDirPath()+"/admin_client.ini", QSettings::IniFormat);
    ini.beginGroup("server");
    const QString host = ini.value("host","127.0.0.1").toString();
    const quint16 port = ini.value("port", 8888).toUInt();
    ini.endGroup();

    if (statusLabel) statusLabel->setText(u8"서버 연결 중…");
    net_->connectToHost(host, port); // 실제 TCP 접속 시도
}

// ─────────────────────────────────────────────────────────────
// showLoginPage()/showAccountPage()
// - 중앙 스택의 현재 페이지 전환 및 상태 라벨 초기화.
// ─────────────────────────────────────────────────────────────
void LoginWindow::showLoginPage()    { stack->setCurrentWidget(pageLogin);   if (statusLabel) statusLabel->clear(); }
void LoginWindow::showAccountPage()  { stack->setCurrentWidget(pageAccount); if (statusLabel) statusLabel->clear(); }
