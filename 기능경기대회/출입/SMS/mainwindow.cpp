// mainwindow.cpp
#include "mainwindow.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGraphicsDropShadowEffect>
#include <QTimer>
#include <QtMath>

// 카드 컨테이너 공통(그림자/확장 정책)
QFrame* MainWindow::makeCard(QWidget *parent){
    auto f = new QFrame(parent);
    f->setObjectName("card");
    f->setFrameShape(QFrame::NoFrame);

    auto sh = new QGraphicsDropShadowEffect(f);
    sh->setBlurRadius(18);
    sh->setOffset(0,4);
    sh->setColor(QColor(0,0,0,40));
    f->setGraphicsEffect(sh);

    f->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return f;
}

/*
 * 라벨 + 읽기전용 입력칸 1줄 생성
 * - 라벨 정렬: ROW_LABEL_ALIGN (헤더의 토큰)
 * - 입력칸 세로 크기: EDIT_MIN_H (헤더의 토큰)
 * - 플레이스홀더 “-” 제거(빈칸 유지)
 */
QFrame* MainWindow::makeRow(QWidget *parent, const QString &label, QLineEdit **outEdit,
                            int labelMinW, int hgap, int vgap, int editMinH)
{
    auto row = new QFrame(parent);
    row->setObjectName("row");

    auto gl = new QGridLayout(row);
    gl->setContentsMargins(0,0,0,0);
    gl->setHorizontalSpacing(hgap);
    gl->setVerticalSpacing(vgap);

    auto lb = new QLabel(label, row);
    lb->setObjectName("rowLabel");
    lb->setAlignment(ROW_LABEL_ALIGN);        // ◀ 토큰 적용
    lb->setMinimumWidth(labelMinW);

    auto ed = new QLineEdit(row);
    ed->setObjectName("rowEdit");
    ed->setReadOnly(true);
    ed->setPlaceholderText("");               // “-” 제거
    ed->setText("");
    ed->setMinimumHeight(editMinH);           // ◀ 토큰 적용(세로 높이)
    ed->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    gl->addWidget(lb, 0, 0);
    gl->addWidget(ed, 0, 1);
    gl->setColumnStretch(0, 0);
    gl->setColumnStretch(1, 1);

    if(outEdit) *outEdit = ed;
    return row;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent){
    setupUI();
    applyTheme();

    nam = new QNetworkAccessManager(this);
    startMjpegStream();

    statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    statusTimer->start(250); // 4Hz
}

MainWindow::~MainWindow(){ stopMjpegStream(); }

void MainWindow::setupUI(){
    resize(1280, 800);

    cw = new QWidget(this);
    setCentralWidget(cw);
    cw->setObjectName("canvas");

    auto root = new QHBoxLayout(cw);
    root->setContentsMargins(OUTER_MARGIN, OUTER_MARGIN, OUTER_MARGIN, OUTER_MARGIN);
    root->setSpacing(COL_GAP);

    // 좌/우 비율 배치(비율 토큰 RIGHT_COL_RATIO 사용)
    auto leftWrap  = new QWidget(cw);
    auto rightWrap = new QWidget(cw);
    leftWrap->setSizePolicy(QSizePolicy::Expanding,  QSizePolicy::Expanding);
    rightWrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    int leftStretch  = qMax(1, (int)qRound((1.0 - RIGHT_COL_RATIO) * 100.0));
    int rightStretch = qMax(1, (int)qRound(RIGHT_COL_RATIO * 100.0));
    root->addWidget(leftWrap,  leftStretch);
    root->addWidget(rightWrap, rightStretch);

    // ─ 좌측: 실시간 모니터링 카드
    {
        auto lay = new QVBoxLayout(leftWrap);
        lay->setContentsMargins(0,0,0,0);

        videoPanel = makeCard(leftWrap);
        videoPanel->setMinimumSize(VIDEO_MIN_W, VIDEO_MIN_H);
        videoPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        auto v = new QVBoxLayout(videoPanel);
        v->setContentsMargins(CARD_PAD, CARD_PAD, CARD_PAD, CARD_PAD);
        v->setSpacing(CARD_GAP);

        videoTitle = new QLabel("실시간 모니터링", videoPanel);
        videoTitle->setObjectName("cardTitle");

        videoLabel = new QLabel(videoPanel);
        videoLabel->setObjectName("videoSurface");
        videoLabel->setAlignment(Qt::AlignCenter);
        videoLabel->setMinimumSize(VIDEO_MIN_W - 2*CARD_PAD, VIDEO_MIN_H - 2*CARD_PAD);
        videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        v->addWidget(videoTitle, 0, Qt::AlignLeft);
        v->addWidget(videoLabel, 1);
        lay->addWidget(videoPanel);
    }

    // ─ 우측: 작업자 정보 / 상태
    {
        auto col = new QVBoxLayout(rightWrap);
        col->setContentsMargins(0,0,0,0);
        col->setSpacing(CARD_GAP);

        // 작업자 정보
        workerPanel = makeCard(rightWrap);
        workerPanel->setMinimumSize(CARD_MIN_W, WORKER_MIN_H);
        workerPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        {
            auto w = new QVBoxLayout(workerPanel);
            w->setContentsMargins(CARD_PAD, CARD_PAD, CARD_PAD, CARD_PAD);
            w->setSpacing(CARD_GAP);

            workerTitle = new QLabel("작업자 정보", workerPanel);
            workerTitle->setObjectName("cardTitle");

            workerCard = new QFrame(workerPanel);
            workerCard->setObjectName("formCard");
            auto form = new QVBoxLayout(workerCard);
            form->setContentsMargins(FORM_CARD_PAD, FORM_CARD_PAD, FORM_CARD_PAD, FORM_CARD_PAD);
            form->setSpacing(FORM_VGAP);

            form->addWidget(makeRow(workerCard, "이름", &nameEdit,  LABEL_MIN_W, FORM_HGAP, FORM_VGAP, EDIT_MIN_H));
            form->addWidget(makeRow(workerCard, "전화", &phoneEdit, LABEL_MIN_W, FORM_HGAP, FORM_VGAP, EDIT_MIN_H));
            form->addWidget(makeRow(workerCard, "소속", &deptEdit,  LABEL_MIN_W, FORM_HGAP, FORM_VGAP, EDIT_MIN_H));
            form->addWidget(makeRow(workerCard, "직급", &posEdit,   LABEL_MIN_W, FORM_HGAP, FORM_VGAP, EDIT_MIN_H));

            w->addWidget(workerTitle);
            w->addWidget(workerCard, 1);
        }

        // 상태
        statusPanel = makeCard(rightWrap);
        statusPanel->setMinimumSize(CARD_MIN_W, STATUS_MIN_H);
        statusPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        {
            auto s = new QVBoxLayout(statusPanel);
            s->setContentsMargins(CARD_PAD, CARD_PAD, CARD_PAD, CARD_PAD);
            s->setSpacing(CARD_GAP);

            statusTitle = new QLabel("상태 모니터", statusPanel);
            statusTitle->setObjectName("cardTitle");

            statusCard  = new QFrame(statusPanel);
            statusCard->setObjectName("formCard");
            auto c = new QVBoxLayout(statusCard);
            c->setContentsMargins(16, STATUS_PAD_TOP, 16, 16);
            c->setSpacing(STATUS_VSPACE);

            statusText = new QLabel("QR 대기", statusCard);
            statusText->setObjectName("statusHeadline");
            statusText->setAlignment(STATUS_HEAD_ALIGN);

            statusDesc = new QLabel("휴대폰의 QR을 카메라에 보여주세요", statusCard);
            statusDesc->setObjectName("statusSub");
            statusDesc->setAlignment(STATUS_SUB_ALIGN);
            statusDesc->setWordWrap(true);

            gauge = new QProgressBar(statusCard);
            gauge->setObjectName("gauge");
            gauge->setRange(0,100);
            gauge->setValue(0);
            gauge->setTextVisible(false);
            gauge->setFixedHeight(GAUGE_H);

            gaugePct = new QLabel("0%", statusCard);
            gaugePct->setObjectName("gaugePct");
            gaugePct->setAlignment(Qt::AlignRight);

            c->addWidget(statusText);
            c->addWidget(statusDesc);
            c->addSpacing(6);
            c->addWidget(gauge);
            c->addWidget(gaugePct);

            s->addWidget(statusTitle);
            s->addWidget(statusCard, 1);
        }

        // 우측 상·하 비율(토큰 WORKER_HEIGHT_RATIO)
        int topStretch    = qMax(1, (int)qRound(WORKER_HEIGHT_RATIO * 100.0));
        int bottomStretch = qMax(1, 100 - topStretch);
        col->addWidget(workerPanel, topStretch);
        col->addWidget(statusPanel, bottomStretch);
    }
}

// px 헬퍼
static inline QString px(int v){ return QString::number(v) + "px"; }

/*
 * 스타일(QSS)에 토큰 적용
 * - 입력박스 min-height에 EDIT_MIN_H 주입
 * - 레이블·제목·게이지 등 크기/색 모두 토큰 기반
 */
void MainWindow::applyTheme(){
    QString qss = QString(R"(
        QWidget#canvas { background: %1; }
        QLabel { color: %2; }

        QFrame#card {
            background: %3;
            border: 1px solid %4;
            border-radius: %5px;
        }
        QLabel#cardTitle {
            color: %21;
            font-weight: 600;
            font-size: %6;
        }
        QLabel#videoSurface {
            background: #0f1116;
            border-radius: %5px;
        }
        QFrame#formCard {
            background: %7;
            border: 1px solid rgba(0,0,0,10%);
            border-radius: %5px;
        }
        QLabel#rowLabel {
            color: %8;
            font-size: %9;
            padding-right: 2px;
        }
        QLineEdit#rowEdit {
            padding: 6px 10px;
            min-height: %10;               /* ◀ 토큰 EDIT_MIN_H */
            border: 1px solid %11;
            border-radius: %12px;
            background: %13;
            color: %2;
            font-size: %14;
        }
        QLineEdit#rowEdit:read-only { background: %13; }

        QLabel#statusHeadline {
            font-size: %15;
            font-weight: 800;
            color: %16;
            letter-spacing: 0.4px;
        }
        QLabel#statusSub {
            color: %8;
            font-size: %17;
        }

        QProgressBar#gauge {
            border: 1px solid #d7e3f4;
            border-radius: 8px;
            background: %18;
        }
        QProgressBar#gauge::chunk {
            border-radius: 8px;
            background: %19;
        }
        QLabel#gaugePct {
            color: %8;
            font-size: %20;
            margin-top: 4px;
        }
    )")
                      .arg(CANVAS)                       // %1
                      .arg(TEXT_MAIN)                    // %2
                      .arg(SURFACE)                      // %3
                      .arg(CARD_BD)                      // %4
                      .arg(QString::number(RADIUS))      // %5
                      .arg(px(TITLE_FS))                 // %6
                      .arg(SURFACE_ALT)                  // %7
                      .arg(TEXT_MUTED)                   // %8
                      .arg(px(ROW_LABEL_FS))             // %9
                      .arg(px(EDIT_MIN_H))               // %10  ← 입력칸 높이
                      .arg(INPUT_BD)                     // %11
                      .arg(QString::number(EDIT_RADIUS)) // %12
                      .arg(INPUT_BG)                     // %13
                      .arg(px(ROW_EDIT_FS))              // %14
                      .arg(px(STATUS_HEAD_FS))           // %15
                      .arg(DANGER)                       // %16 (초기색: QR 대기/FAIL)
                      .arg(px(STATUS_SUB_FS))            // %17
                      .arg(GAUGE_BG)                     // %18
                      .arg(PRIMARY)                      // %19
                      .arg(px(GAUGE_PCT_FS))             // %20
                      .arg(TITLE_COLOR)                  // %21
        ;
    setStyleSheet(qss);
}

void MainWindow::resizeEvent(QResizeEvent*){
    if(!lastFrame.isNull()) drawFrame(lastFrame);
}

void MainWindow::drawFrame(const QPixmap &pix){
    if(videoLabel->size().isEmpty()) return;
    QPixmap scaled = pix.scaled(videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    videoLabel->setPixmap(scaled);
}

/* ───────── MJPEG 스트림 & 상태 폴링 ───────── */
void MainWindow::startMjpegStream(){
    QNetworkRequest req(serverBase + "/mjpeg");
    mjpegReply = nam->get(req);
    connect(mjpegReply, &QNetworkReply::readyRead,     this, &MainWindow::onMjpegReadyRead);
    connect(mjpegReply, &QNetworkReply::finished,      this, &MainWindow::onMjpegFinished);
    connect(mjpegReply, &QNetworkReply::errorOccurred, this, &MainWindow::onMjpegError);
}
void MainWindow::stopMjpegStream(){
    if(mjpegReply){ mjpegReply->abort(); mjpegReply->deleteLater(); mjpegReply=nullptr; }
}
void MainWindow::onMjpegError(QNetworkReply::NetworkError){ }
void MainWindow::onMjpegFinished(){ }

void MainWindow::onMjpegReadyRead(){
    mjpegBuf += mjpegReply->readAll();
    while(true){
        int soi = mjpegBuf.indexOf(QByteArray::fromHex("FFD8"));
        if(soi < 0){ mjpegBuf.clear(); break; }
        int eoi = mjpegBuf.indexOf(QByteArray::fromHex("FFD9"), soi+2);
        if(eoi < 0) break;

        QByteArray jpg = mjpegBuf.mid(soi, eoi - soi + 2);
        mjpegBuf.remove(0, eoi + 2);

        QPixmap pix; pix.loadFromData(jpg, "JPG");
        if(!pix.isNull()){ lastFrame = pix; drawFrame(pix); }
    }
}

void MainWindow::updateStatus(){
    QNetworkRequest req(serverBase + "/status");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto r = nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r](){
        QByteArray raw = r->readAll(); r->deleteLater();
        const auto doc = QJsonDocument::fromJson(raw);
        if(!doc.isObject()) return;
        const auto o = doc.object();

        currentState = o.value("state").toString("FAIL");
        stableSecs   = o.value("stable_secs").toDouble();
        personCount  = o.value("person").toInt();
        helmetCount  = o.value("helmet_pass").toInt();
        phase        = o.value("phase").toString("QR_WAIT");

        handleQrEvents(o);
        maybeShowPassPopup(o.value("pass_for_3s").toBool());
        updateStatusStyle();
    });
}

/* ───────── 상태 패널 스타일 갱신 ───────── */
void MainWindow::updateStatusStyle(){
    if(phase=="QR_WAIT"){
        statusText->setText("QR 대기");
        statusText->setStyleSheet(QString("QLabel#statusHeadline{color:%1;font-size:%2px;font-weight:800;}")
                                      .arg(DANGER).arg(STATUS_HEAD_FS));
        statusText->setAlignment(STATUS_HEAD_ALIGN);

        statusDesc->setText("휴대폰의 QR을 카메라에 보여주세요");
        statusDesc->setAlignment(STATUS_SUB_ALIGN);

        gauge->setValue(0);
        gaugePct->setText("0%");
        passPopupShown = false;

        nameEdit ->clear();
        phoneEdit->clear();
        deptEdit ->clear();
        posEdit  ->clear();
        return;
    }

    if(currentState=="PASS"){
        statusText->setText("PASS");
        statusText->setStyleSheet(QString("QLabel#statusHeadline{color:%1;font-size:%2px;font-weight:800;}")
                                      .arg(SUCCESS).arg(STATUS_HEAD_FS));
        statusDesc->setText("안전모 착용 확인");
        int pct = qBound(0, (int)qRound(stableSecs*100.0/3.0), 100);
        gauge->setValue(pct);
        gaugePct->setText(QString::number(pct) + "%");
    }else{
        statusText->setText("FAIL");
        statusText->setStyleSheet(QString("QLabel#statusHeadline{color:%1;font-size:%2px;font-weight:800;}")
                                      .arg(DANGER).arg(STATUS_HEAD_FS));
        statusDesc->setText("안전모 미착용 감지됨");
        gauge->setValue(0);
        gaugePct->setText("0%");
    }
}

/* ───────── PASS 배너 ───────── */
void MainWindow::maybeShowPassPopup(bool passFor3s){
    if(phase != "DETECT") return;
    if(passFor3s && !passPopupShown){
        passPopupShown = true;
        showBannerPopup("안전모 PASS", QColor(46,160,67), Qt::white, false);
    }
}

/* ───────── QR 이벤트(성공/실패) ───────── */
void MainWindow::handleQrEvents(const QJsonObject &o){
    int evId = o.value("qr_event_id").toInt(-1);
    if(evId>=0 && evId!=lastQrEventId){
        lastQrEventId = evId;
        const QString ev = o.value("qr_event").toString();
        if(ev=="success"){
            const auto w = o.value("worker").toObject();
            nameEdit ->setText(w.value("name").toString(""));
            phoneEdit->setText(w.value("phone").toString(""));
            deptEdit ->setText(w.value("department").toString(""));
            posEdit  ->setText(w.value("position").toString(""));
            showBannerPopup("출근 등록 완료", QColor(46,160,67), Qt::white, false);
        }else if(ev=="fail"){
            showBannerPopup("일치하는 정보가 없습니다. 관리자에게 문의", QColor(200,40,40), Qt::white, false);
            nameEdit ->clear();
            phoneEdit->clear();
            deptEdit ->clear();
            posEdit  ->clear();
        }
    }
}

/* ───────── 상단 배너 공통 ───────── */
void MainWindow::showBannerPopup(const QString &title, const QColor &bg, const QColor &fg, bool withOkBtn){
    QDialog dlg(this);
    dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setAttribute(Qt::WA_TranslucentBackground);

    auto wrap = new QWidget(&dlg);
    wrap->setObjectName("banner");
    auto lay = new QVBoxLayout(wrap);
    lay->setContentsMargins(26,18,26,18);
    lay->setSpacing(10);

    auto lbl = new QLabel(title, wrap);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet(QString("color:%1;font-size:28px;font-weight:700;").arg(fg.name()));
    lay->addWidget(lbl);

    QPushButton *ok = nullptr;
    if(withOkBtn){
        ok = new QPushButton("확인", wrap);
        ok->setCursor(Qt::PointingHandCursor);
        ok->setFixedHeight(36);
        ok->setStyleSheet("QPushButton{background:#ffffff;color:#333;border:none;border-radius:8px;padding:6px 14px;} QPushButton:hover{background:#f2f2f2;}");
        lay->addWidget(ok, 0, Qt::AlignCenter);
        connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    }

    auto card = new QFrame(&dlg);
    auto cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(0,0,0,0);
    cardLay->addWidget(wrap);
    card->setStyleSheet(QString("QFrame{background:%1;border-radius:16px;}").arg(bg.name()));

    auto top = new QVBoxLayout(&dlg);
    top->setContentsMargins(0,0,0,0);
    top->addWidget(card);

    if(!withOkBtn) QTimer::singleShot(3000, &dlg, &QDialog::accept); // 3초 자동 닫힘

    card->setMinimumWidth(width()*0.45);
    auto sh = new QGraphicsDropShadowEffect(card);
    sh->setBlurRadius(24);
    sh->setOffset(0,6);
    sh->setColor(QColor(0,0,0,80));
    card->setGraphicsEffect(sh);

    dlg.move(frameGeometry().center() - QPoint(card->minimumWidth()/2, height()/4));
    dlg.exec();
}
