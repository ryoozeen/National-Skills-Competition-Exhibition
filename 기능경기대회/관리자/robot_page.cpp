#include "robot_page.h"
#include "notification.h"
/*
 * @file robot_page.cpp
 * @brief 로봇/증거영상 페이지 구현부.
 *        - UI 구성(buildUi) + 스타일 적용(applyStyle)
 *        - 상단 상태 표시(연결/오류), 중앙(재생/파일리스트), 하단(로그)
 *        - 파일/URL 유효성 검사, 폴더 감시, 드래그&드롭, 로그 내보내기 등
 */

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QStyle>
#include <QDateTime>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QUrl>
#include <QSplitter>
#include <QListView>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QDir>
#include <QStandardPaths>
#include <QPalette>
#include <QTimer>
#include <QEvent>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QTextStream>

static QString niceSize(qint64 b){
    double d=b; const char* u[]={"B","KB","MB","GB"}; int i=0;
    while (d>1024 && i<3){ d/=1024; i++; }
    return QString("%1 %2").arg(QString::number(d,'f',(i?1:0))).arg(u[i]);
}
void RobotPage::setChip(QLabel* chip, const QString& state, const QString& tip){
    if (!chip) return;
    QString color="#9ca3af"; if(state=="ok") color="#10b981"; if(state=="bad") color="#ef4444"; if(state=="idle") color="#60a5fa";
    chip->setFixedSize(10,10);
    chip->setStyleSheet(QString("background:%1; border-radius:5px;").arg(color));
    chip->setToolTip(tip);
}
bool RobotPage::isVideoFile(const QString& path) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QStringList ok = {"mp4","mkv","avi","mov","webm"};
    return ok.contains(ext);
}
/**
 * @brief 생성자
 *  - buildUi()/applyStyle()로 UI 구성을 완료
 *  - 초기 연결/오류 상태 설정
 *  - 창 이동/리사이즈 이벤트 스로틀링 타이머 준비 및 이벤트 필터 설치
 *  - 드래그&드롭 허용 및 초기 폴더(/home/sms/Uploads) 설정
 */

RobotPage::RobotPage(QWidget* parent) noexcept : QWidget(parent){
    buildUi();
    applyStyle();

    // 초기 상태
    setConnectionState(QAbstractSocket::UnconnectedState);
    setNetworkError(QString());

    // 리사이즈/이동 스로틀링
    resizeTimer_ = new QTimer(this);
    resizeTimer_->setSingleShot(true);            // 스로틀 타이머 1회성

    connect(resizeTimer_, &QTimer::timeout, this, [this]{ setUpdatesEnabled(true); updatesSuppressed_ = false; });

    // 이벤트 필터 설치 (자신 & 나중에 생길 창 위젯)
    this->installEventFilter(this);
    if (window()) window()->installEventFilter(this);
    else QTimer::singleShot(0, this, [this]{ if (window()) window()->installEventFilter(this); });

    // 드래그&드롭 허용
    setAcceptDrops(true);

    // 초기 폴더: 고정 경로 (/home/sms/Uploads)
    const QString initDir = QStringLiteral("/home/sms/Uploads");
    QDir().mkpath(initDir);                              // 초기 폴더 경로가 없으면 생성
      // 경로가 없으면 생성(무해)
    setVideoFolder(initDir);
}
/** @brief 소멸자: 플레이어 안전 정리(출력 분리 후 deleteLater) */ 

RobotPage::~RobotPage(){
    if (player){
        player->stop();
        player->setVideoOutput(nullptr);
        player->deleteLater();
        player = nullptr;
    }
}
/**
 * @brief UI 구성
 *  - 상단 상태바(연결/오류)
 *  - 중앙 수평 분할: 좌(비디오), 우(파일 브라우저)
 *  - 하단 로그 테이블
 *  - 파일 선택/재생/우클릭 메뉴 시그널 연결
 *  - QMediaPlayer 초기화 및 상태/에러 핸들러
 */

void RobotPage::buildUi(){
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16,16,16,16);
    root->setSpacing(12);

    // ── 상단 상태 바
    statusBar = new QFrame(this); statusBar->setObjectName("statusBar");
    auto* sLy = new QHBoxLayout(statusBar); sLy->setContentsMargins(12,8,12,8); sLy->setSpacing(10);
    connChip = new QLabel(statusBar); connLabel= new QLabel(u8"연결 안 됨", statusBar);
    errChip  = new QLabel(statusBar); errLabel = new QLabel(u8"오류 없음", statusBar);
    sLy->addWidget(new QLabel(u8"연결",statusBar)); sLy->addWidget(connChip,0,Qt::AlignVCenter); sLy->addWidget(connLabel);
    sLy->addSpacing(20);
    sLy->addWidget(new QLabel(u8"오류",statusBar));  sLy->addWidget(errChip,0,Qt::AlignVCenter); sLy->addWidget(errLabel,1);
    root->addWidget(statusBar);

    // ── 중앙: 수평 분할 (좌: 플레이어, 우: 파일 리스트)
    splitH = new QSplitter(Qt::Horizontal, this);
    splitH->setChildrenCollapsible(false);   // 분할기 자식 패널 자동 접힘 방지


    // 좌) 플레이어 카드
    videoCard = new QFrame(splitH); videoCard->setObjectName("videoCard");
    auto* vLy = new QVBoxLayout(videoCard); vLy->setContentsMargins(12,12,12,12); vLy->setSpacing(8);
    fileLabel = new QLabel(u8"파일: (없음)", videoCard);
    videoView = new QVideoWidget(videoCard);

    // 화면 채움 + 불투명
    videoView->setAspectRatioMode(Qt::KeepAspectRatioByExpanding); // 화면 채우면서 비율 유지

    videoView->setAutoFillBackground(true);  // 배경을 검정으로 채움

    videoView->setAttribute(Qt::WA_OpaquePaintEvent, true); // 불투명 그리기로 성능/잔상 개선

    {
        QPalette pal = videoView->palette(); pal.setColor(QPalette::Window, Qt::black); videoView->setPalette(pal);
    }

    auto* controls = new QHBoxLayout; controls->setSpacing(8);
    btnOpenFolder = new QPushButton(u8"폴더 선택", videoCard);
    btnOpenFolder->setObjectName("pillBtn");
    btnOpenFolder->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    btnPlayPause  = new QPushButton(u8"재생", videoCard);
    btnPlayPause->setObjectName("pillBtn");
    btnPlayPause->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    btnPlayPause->setEnabled(false);               // 재생 가능한 소스가 없으면 비활성화


    controls->addWidget(btnOpenFolder);
    controls->addWidget(btnPlayPause);
    controls->addStretch();

    vLy->addWidget(fileLabel);
    vLy->addWidget(videoView, 1);
    vLy->addLayout(controls);

    // 우) 파일 브라우저 카드
    browserCard = new QFrame(splitH); browserCard->setObjectName("browserCard");
    auto* bLy = new QVBoxLayout(browserCard); bLy->setContentsMargins(12,12,12,12); bLy->setSpacing(8);
    auto* bTitle = new QLabel(u8"영상 파일", browserCard); bTitle->setObjectName("cardTitle");
    fileList = new QListView(browserCard);
    fileList->setSelectionMode(QListView::SingleSelection);
    fileList->setUniformItemSizes(true);
    fileList->setEditTriggers(QListView::NoEditTriggers);

    fsModel = new QFileSystemModel(this);
    fsModel->setFilter(QDir::NoDotAndDotDot | QDir::Files);
    fsModel->setNameFilters({"*.mp4","*.mkv","*.avi","*.mov","*.webm"});
    fsModel->setNameFilterDisables(false);    // 필터에 맞지 않는 파일 숨김
      // 필터 미일치 파일 숨김
    fileList->setModel(fsModel);

    bLy->addWidget(bTitle);
    bLy->addWidget(fileList, 1);

    splitH->addWidget(videoCard);
    splitH->addWidget(browserCard);
    splitH->setStretchFactor(0, 3);               // 좌:우 넓이 비중 3:2
  // 좌:우 ≈ 3:2
    splitH->setStretchFactor(1, 2);

    // ── 하단 로그를 위한 수직 분할
    splitV = new QSplitter(Qt::Vertical, this);
    splitV->setChildrenCollapsible(false);
    splitV->addWidget(splitH);

    logTable = new QTableWidget(splitV);
    logTable->setContextMenuPolicy(Qt::CustomContextMenu);  // ⬅️ 우클릭 메뉴
    logTable->setColumnCount(3);
    logTable->setHorizontalHeaderLabels({u8"시각", u8"이벤트", u8"세부"});
    logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    logTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    logTable->horizontalHeader()->setStretchLastSection(true);
    logTable->verticalHeader()->setVisible(false);
    logTable->setSelectionBehavior(QTableWidget::SelectRows);
    logTable->setEditTriggers(QTableWidget::NoEditTriggers);
    logTable->setShowGrid(false);
    splitV->addWidget(logTable);
    splitV->setStretchFactor(0, 3);               // 상:하 비중 3:1
  // 상:하 ≈ 3:1
    splitV->setStretchFactor(1, 1);

    root->addWidget(splitV, 1);

    // ── 플레이어 초기화
    player = new QMediaPlayer(this);
    player->setVideoOutput(videoView);           // 비디오 출력 대상 연결

    connect(player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st){
        const bool playing = (st == QMediaPlayer::PlayingState);
        btnPlayPause->setText(playing ? u8"일시정지" : u8"재생");
        btnPlayPause->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
#if (QT_VERSION >= QT_VERSION_CHECK(6,5,0))
    connect(player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& errStr){
        setNetworkError(errStr.isEmpty() ? u8"재생 오류" : errStr);
    });
#endif

    // ── 시그널 연결
    connect(btnOpenFolder, &QPushButton::clicked, this, [this]{
        const QString dir = QFileDialog::getExistingDirectory(this, u8"영상 폴더 선택", currentDir.isEmpty()? QDir::homePath() : currentDir);
        if (!dir.isEmpty()) setVideoFolder(dir);
    });
    connect(btnPlayPause, &QPushButton::clicked, this, [this]{
        if (!player) return;
        if (player->playbackState()==QMediaPlayer::PlayingState) player->pause(); else player->play();
    });
    connect(fileList, &QListView::clicked, this, [this](const QModelIndex& idx){
        const QString path = fsModel->filePath(idx);
        if (!path.isEmpty()) playEvidenceFile(path);
    });

    // 로그 우클릭 메뉴
    connect(logTable, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos){
        QMenu m(this);
        QAction* actCopy = m.addAction(u8"선택 행 복사");
        QAction* actCsv  = m.addAction(u8"CSV로 내보내기...");
        QAction* chosen = m.exec(logTable->viewport()->mapToGlobal(pos));
        if (chosen == actCopy) copySelectedLogRowsToClipboard();
        else if (chosen == actCsv) exportLogCsv();
    });
}
/** @brief 스타일시트 적용(배경/버튼/카드 모양 등) */ 

void RobotPage::applyStyle(){
    setStyleSheet(R"(
        QWidget { font-family:'Malgun Gothic','Noto Sans KR',sans-serif; }

        #statusBar {
            background:#f8fafc;
            border:1px solid #e5e7eb;
            border-radius:10px;
        }
        #videoCard, #browserCard {
            background:#ffffff;
            border:1px solid #ebedf1;
            border-radius:12px;
        }
        #videoCard { background:#000; }         /* 보이는 테두리+검정 배경 */
        #browserCard QLabel#cardTitle { font-weight:800; color:#0b0f19; }

        QPushButton#pillBtn {
            background:#ffffff;
            border:1px solid #e5e7eb;
            border-radius:18px;
            padding:6px 12px;
            font-weight:600;
        }
        QPushButton#pillBtn:hover { background:#f9fafb; }
    )");
}
/** @brief 서버 연결 상태칩/라벨 갱신 */ 

void RobotPage::setConnectionState(QAbstractSocket::SocketState s){
    const bool ok = (s == QAbstractSocket::ConnectedState);
    connLabel->setText(ok ? u8"연결됨" : u8"연결 안 됨");
    setChip(connChip, ok ? "ok" : "bad", ok ? u8"서버와 연결됨" : u8"서버와 연결되지 않음");
}
/** @brief 네트워크/재생 오류 상태칩/라벨 갱신 */ 
void RobotPage::setNetworkError(const QString& err){
    const bool has = !err.trimmed().isEmpty();
    errLabel->setText(has ? err : u8"오류 없음");
    setChip(errChip, has ? "bad" : "idle", has ? u8"네트워크/재생 오류" : u8"에러 없음");
}
/**
 * @brief 파일 경로 또는 URL을 받아 유효성 검사 후 재생
 *  - 로컬·원격 모두 지원(file/http/https/rtsp 등)
 *  - 확장자 필터링 및 오류 시 NotificationManager로 안내
 *  - 라벨에 파일명/크기 또는 URL을 표시
 */

void RobotPage::playEvidenceFile(const QString& filePath){
    const QString p = filePath.trimmed();
    if (p.isEmpty()){
        if (fileLabel) fileLabel->setText(u8"파일: (없음)");
        if (btnPlayPause) btnPlayPause->setEnabled(false);               // 재생 가능한 소스가 없으면 비활성화

        if (player){ player->stop(); player->setVideoOutput(videoView);           // 비디오 출력 대상 연결
 }
        NotificationManager::instance().addNotification(u8"[증거영상]", u8"경로가 비어 있습니다.");
        return;
    }

    QUrl url;
    // URL 형태인가?
    if (p.contains("://")){
        url = QUrl(p);
        if (!url.isValid()){
            NotificationManager::instance().addNotification(u8"[증거영상]", u8"잘못된 URL 입니다.");
            return;
        }
        // file:// URL이면 로컬 파일 검증
        if (url.isLocalFile()){
            QFileInfo fi(url.toLocalFile());
            if (!fi.exists() || !fi.isFile() || !isVideoFile(fi.absoluteFilePath())){
                if (fileLabel) fileLabel->setText(u8"파일: (없음)");
                if (btnPlayPause) btnPlayPause->setEnabled(false);               // 재생 가능한 소스가 없으면 비활성화

                if (player){ player->stop(); player->setVideoOutput(videoView);           // 비디오 출력 대상 연결
 }
                NotificationManager::instance().addNotification(u8"[증거영상]", u8"파일이 존재하지 않거나 지원되지 않는 형식입니다.");
                return;
            }
            if (fileLabel) fileLabel->setText(QString(u8"파일: %1  ·  %2").arg(fi.fileName()).arg(niceSize(fi.size())));
        } else {
            // http/https/rtsp 등 원격 URL — 라벨엔 URL만 표시
            if (fileLabel) fileLabel->setText(QString(u8"URL: %1").arg(url.toString(QUrl::RemoveUserInfo)));
        }
    } else {
        // 순수 로컬 경로
        QFileInfo fi(p);
        if (!fi.exists() || !fi.isFile() || !isVideoFile(p)){
            if (fileLabel) fileLabel->setText(u8"파일: (없음)");
            if (btnPlayPause) btnPlayPause->setEnabled(false);               // 재생 가능한 소스가 없으면 비활성화

            if (player){ player->stop(); player->setVideoOutput(videoView);           // 비디오 출력 대상 연결
 }
            NotificationManager::instance().addNotification(u8"[증거영상]", u8"파일이 존재하지 않거나 지원되지 않는 형식입니다.");
            return;
        }
        if (fileLabel) fileLabel->setText(QString(u8"파일: %1  ·  %2").arg(fi.fileName()).arg(niceSize(fi.size())));
        url = QUrl::fromLocalFile(fi.absoluteFilePath());
    }

    if (player){
        player->stop();
        player->setVideoOutput(videoView);           // 비디오 출력 대상 연결

        if (!url.isValid()) url = QUrl::fromLocalFile(p); // 혹시 모를 보정
        player->setSource(url);
        if (btnPlayPause) btnPlayPause->setEnabled(true);
        player->play();
    }
}
/**
 * @brief 파일 브라우저의 루트 폴더를 변경하고 감시자를 해당 폴더에 바인딩
 *  - QFileSystemModel의 setRootPath로 리스트 루트 업데이트
 *  - QFileSystemWatcher로 디렉토리 변경 시 루트 인덱스 재설정
 */

void RobotPage::setVideoFolder(const QString& dirPath){
    if (!fsModel) return;
    const QString norm = QDir::fromNativeSeparators(dirPath);
    currentDir = norm;

    // 파일 시스템 모델 루트 지정
    const QModelIndex rootIdx = fsModel->setRootPath(norm);
    fileList->setRootIndex(rootIdx);

    // 폴더 감시자 갱신
    if (!fsWatcher) {
        fsWatcher = new QFileSystemWatcher(this);
        connect(fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& changed){
            Q_UNUSED(changed);
            // 모델은 대부분 자동 갱신되지만, 확실히 반영되도록 rootIndex를 재설정
            const QModelIndex idx = fsModel->index(currentDir);
            if (idx.isValid()) fileList->setRootIndex(idx);
        });
    }
    // 기존 감시 해제 후 새 폴더 등록
    if (!fsWatcher->directories().isEmpty())
        fsWatcher->removePaths(fsWatcher->directories());
    if (QDir(norm).exists())
        fsWatcher->addPath(norm);
}
/** @brief 하단 로그 테이블에 (시각/이벤트/세부) 한 줄 추가 후 스크롤 하단으로 이동 */ 

void RobotPage::appendRobotEvent(const QString& time, const QString& event, const QString& detail){
    const int row = logTable->rowCount();
    logTable->insertRow(row);
    logTable->setItem(row,0,new QTableWidgetItem(time));
    logTable->setItem(row,1,new QTableWidgetItem(event));
    logTable->setItem(row,2,new QTableWidgetItem(detail));
    logTable->scrollToBottom();
}

/* ===== 드래그&드롭으로 영상 재생 ===== */
/** @brief 드래그된 URL 중 첫 파일의 확장자가 허용이면 acceptProposedAction */ 
void RobotPage::dragEnterEvent(QDragEnterEvent* e){
    if (e->mimeData()->hasUrls()){
        const QList<QUrl> urls = e->mimeData()->urls();
        if (!urls.isEmpty()){
            const QString path = urls.first().toLocalFile();
            if (isVideoFile(path)) { e->acceptProposedAction(); return; }
        }
    }
    QWidget::dragEnterEvent(e);
}
/** @brief 드롭된 첫 파일을 playEvidenceFile로 재생 시도 */ 
void RobotPage::dropEvent(QDropEvent* e){
    const QList<QUrl> urls = e->mimeData()->urls();
    if (!urls.isEmpty()){
        const QString path = urls.first().toLocalFile();
        if (isVideoFile(path)) playEvidenceFile(path);
    }
    QWidget::dropEvent(e);
}

/* ===== 이동/리사이즈 스로틀링 ===== */
/**
 * @brief 창 이동/리사이즈 동안 리렌더링을 잠시 끄고, 멈춘 뒤 80ms 후 다시 켭니다.
 *        (고해상도/느린 하드웨어에서 끊김 방지 및 CPU 사용량 완화)
 */
bool RobotPage::eventFilter(QObject* obj, QEvent* e){
    const QEvent::Type t = e->type();
    if ((obj == this || obj == window()) && (t == QEvent::Resize || t == QEvent::Move)){
        if (!updatesSuppressed_) { setUpdatesEnabled(false); updatesSuppressed_ = true; }
        resizeTimer_->start(80);                                // 80ms 뒤 업데이트 재개
   // 드래그 멈춘 뒤 80ms 후 갱신 재개
    }
    return QWidget::eventFilter(obj, e);
}

/* ===== 로그 내보내기/복사 ===== */
/**
 * @brief 로그를 CSV로 저장(UTF-8 BOM 추가로 Excel 호환)
 *  - 셀 값은 CSV 규약에 맞게 큰따옴표 이스케이프
 */
void RobotPage::exportLogCsv(){
    const QString path = QFileDialog::getSaveFileName(this, u8"CSV로 내보내기", QDir::homePath()+"/robot_log.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;

    auto esc = [](const QString& s){
        QString t = s;
        t.replace('"', "\"\"");
        return QString("\"%1\"").arg(t);
    };

    // UTF-8 BOM (엑셀 호환)
    static const unsigned char bom[3] = {0xEF,0xBB,0xBF};
    f.write(reinterpret_cast<const char*>(bom), 3);

    QTextStream out(&f);

    // 헤더
    out << esc(u8"시각") << "," << esc(u8"이벤트") << "," << esc(u8"세부") << "\n";

    // 데이터
    for (int r=0; r<logTable->rowCount(); ++r){
        const QString c0 = logTable->item(r,0) ? logTable->item(r,0)->text() : "";
        const QString c1 = logTable->item(r,1) ? logTable->item(r,1)->text() : "";
        const QString c2 = logTable->item(r,2) ? logTable->item(r,2)->text() : "";
        out << esc(c0) << "," << esc(c1) << "," << esc(c2) << "\n";
    }
    f.close();
}
/** @brief 선택된 로그 행을 탭으로 구분하여 클립보드에 복사(첫 줄은 헤더) */ 

void RobotPage::copySelectedLogRowsToClipboard(){
    QList<int> rows;
    for (const auto& idx : logTable->selectionModel()->selectedRows())
        rows << idx.row();
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    QString text;
    text += u8"시각\t이벤트\t세부\n";
    for (int r : rows){
        const QString c0 = logTable->item(r,0) ? logTable->item(r,0)->text() : "";
        const QString c1 = logTable->item(r,1) ? logTable->item(r,1)->text() : "";
        const QString c2 = logTable->item(r,2) ? logTable->item(r,2)->text() : "";
        text += c0 + "\t" + c1 + "\t" + c2 + "\n";
    }
    QGuiApplication::clipboard()->setText(text); // 시스템 클립보드에 문자열 복사

}
