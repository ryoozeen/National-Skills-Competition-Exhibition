// mainwindow.h
#pragma once
#include <QMainWindow>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QTimer>
#include <QPixmap>
#include <QDialog>
#include <QPushButton>

// QJsonObject를 헤더에서 파라미터로 사용하므로 전방 선언만 필요
class QJsonObject;

/*
 * ─────────────────────────────────────────────────────────────────────────────
 *  MainWindow
 *  - 출입 클라이언트의 화면(UI) 컨테이너
 *  - "디자인 토큰"을 상단에 모아두어 컬러/사이즈/정렬/레이아웃을 한곳에서 제어
 *  - 구조/기능을 건드리지 않고 외형만 바꾸고 싶을 때 토큰만 수정
 * ─────────────────────────────────────────────────────────────────────────────
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent=nullptr);
    ~MainWindow() override;

private:
    // ───────────────────────── 디자인 토큰(여기만 바꾸면 외형 전체가 바뀜) ─────────────────────────
    // [레이아웃 비율]
    double RIGHT_COL_RATIO      = 0.34;  // 우측 열(작업자 정보+상태)의 가로 비율 (0.0~1.0)
    double WORKER_HEIGHT_RATIO  = 0.40;  // 우측 내부에서 "작업자 정보"가 차지하는 세로 비율 (나머지는 상태 모니터)

    // [최소 사이즈] : 너무 작게 줄었을 때 UI가 무너지지 않도록 하는 하한값
    int VIDEO_MIN_W   = 640;            // 실시간 영상 카드의 최소 너비
    int VIDEO_MIN_H   = 360;            // 실시간 영상 카드의 최소 높이
    int CARD_MIN_W    = 360;            // 우측 카드(작업자 정보/상태)의 최소 너비
    int WORKER_MIN_H  = 200;            // 작업자 정보 카드의 최소 높이
    int STATUS_MIN_H  = 240;            // 상태 모니터 카드의 최소 높이

    // [여백/간격] : 외곽 여백/컬럼 간격/카드 내부 패딩 등
    int OUTER_MARGIN  = 16;             // 창 가장자리 바깥 여백
    int COL_GAP       = 16;             // 좌/우 컬럼 사이 간격
    int CARD_PAD      = 14;             // 카드 내부 패딩
    int CARD_GAP      = 10;             // 카드 내부 요소 사이 간격
    int FORM_CARD_PAD = 8;              // 작업자 정보 내부 폼 카드의 패딩
    int FORM_HGAP     = 10;             // 라벨-입력칸 수평 간격
    int FORM_VGAP     = 8;              // 행(이름/전화/소속/직급) 사이 수직 간격
    int LABEL_MIN_W   = 56;             // 라벨 최소 너비(축소 시 줄바꿈 방지)
    int GAUGE_H       = 28;             // 상태 모니터 게이지 바 높이
    int STATUS_VSPACE = 12;             // 상태 카드의 요소 간 수직 간격
    int STATUS_PAD_TOP= 24;             // 상태 카드 상단 패딩(“QR 대기” 머리말과 상단 사이)

    // [폰트 크기]
    int TITLE_FS        = 18;           // 카드 제목(실시간 모니터링/작업자 정보/상태 모니터)
    int ROW_LABEL_FS    = 15;           // 작업자 정보 라벨(이름/전화/소속/직급)
    int ROW_EDIT_FS     = 15;           // 작업자 정보 입력칸(읽기전용) 폰트
    int STATUS_HEAD_FS  = 40;           // 상태 머리말(“QR 대기”, “FAIL”, “PASS”)
    int STATUS_SUB_FS   = 14;           // 상태 설명(“휴대폰의 QR을 …”)
    int GAUGE_PCT_FS    = 13;           // 게이지 우측의 % 텍스트

    // [정렬/높이] ★요청사항 반영★
    Qt::Alignment ROW_LABEL_ALIGN = Qt::AlignHCenter | Qt::AlignVCenter; // 라벨(이름/전화/소속/직급) 텍스트 가운데 정렬
    int EDIT_MIN_H      = 20;           // 작업자 정보 입력칸(읽기전용)의 세로 높이(박스 높이)
    int EDIT_RADIUS     = 10;           // 입력칸 모서리 둥근 정도(px)

    // 입력칸 패딩(세로/가로) 토큰
    int EDIT_PAD_V     = 4;   // 위·아래 패딩(px) → 작게 보이고 싶으면 줄이기
    int EDIT_PAD_H     = 10;  // 좌·우 패딩(px)


    Qt::Alignment STATUS_HEAD_ALIGN = Qt::AlignHCenter | Qt::AlignVCenter; // 상태 머리말 정렬
    Qt::Alignment STATUS_SUB_ALIGN  = Qt::AlignHCenter | Qt::AlignVCenter; // 상태 설명 정렬

    // [컬러 팔레트] : 기본/성공/위험/배경/텍스트/테두리 등
    QString PRIMARY     = "#1e88e5";    // 포인트(게이지 채움 등)
    QString SUCCESS     = "#2e7d32";    // PASS 색
    QString DANGER      = "#c62828";    // FAIL/QR 대기 기본 머리말 색
    QString SURFACE     = "#ffffff";    // 카드 바탕
    QString SURFACE_ALT = "#fafcff";    // 작업자 정보 내부 폼 카드 바탕
    QString CANVAS      = "#eef2f7";    // 전체 배경
    QString TEXT_MAIN   = "#233044";    // 일반 텍스트
    QString TITLE_COLOR = "#2962ff";    // 카드 제목 색
    QString TEXT_MUTED  = "#5b6b82";    // 보조 텍스트(설명/라벨)
    QString INPUT_BG    = "#ffffff";    // 입력칸 바탕t
    QString INPUT_BD    = "#dbe3ef";    // 입력칸 테두리
    QString CARD_BD     = "rgba(0,0,0,18%)"; // 카드 테두리
    QString GAUGE_BG    = "#eaf1fb";    // 게이지 배경

    int RADIUS          = 6;            // 카드 모서리 둥글기(px)

private: // ───────────────────────── UI 위젯 포인터(실제 화면 구성 요소) ─────────────────────────
    QWidget *cw = nullptr;              // 중앙 위젯(전체 캔버스 역할)

    // 좌측: 실시간 모니터링 카드
    QFrame *videoPanel = nullptr;       // 카드 컨테이너
    QLabel *videoTitle = nullptr;       // 카드 제목 "실시간 모니터링"
    QLabel *videoLabel = nullptr;       // MJPEG 스트림을 그리는 표면

    // 우측 상단: 작업자 정보 카드
    QFrame   *workerPanel = nullptr;    // 카드 컨테이너
    QLabel   *workerTitle = nullptr;    // 카드 제목 "작업자 정보"
    QFrame   *workerCard  = nullptr;    // 내부 폼(라벨+읽기전용 입력칸 묶음)
    QLineEdit *nameEdit   = nullptr;    // 이름 표시
    QLineEdit *phoneEdit  = nullptr;    // 전화 표시
    QLineEdit *deptEdit   = nullptr;    // 소속 표시
    QLineEdit *posEdit    = nullptr;    // 직급 표시

    // 우측 하단: 상태 모니터 카드
    QFrame *statusPanel = nullptr;      // 카드 컨테이너
    QLabel *statusTitle = nullptr;      // 카드 제목 "상태 모니터"
    QFrame *statusCard  = nullptr;      // 내부 박스
    QLabel *statusText  = nullptr;      // 머리말(“QR 대기/FAIL/PASS”)
    QLabel *statusDesc  = nullptr;      // 안내문(“휴대폰의 QR을 …”)
    QProgressBar *gauge = nullptr;      // 진행률(안전모 PASS 유지시간)
    QLabel *gaugePct    = nullptr;      // 진행률 % 텍스트

    // 네트워킹(MJPEG/상태 폴링)
    QString serverBase = "http://192.168.0.7:8000";   // 서버 베이스 URL (필요 시 여기만 수정)
    QNetworkAccessManager *nam = nullptr;           // HTTP 요청 전송자
    QNetworkReply *mjpegReply = nullptr;            // /mjpeg 응답 스트림
    QByteArray mjpegBuf;                            // MJPEG 바운더리 버퍼
    QPixmap lastFrame;                              // 마지막 프레임(리사이즈 시 재그리기)

    // 주기 상태 업데이트 타이머
    QTimer *statusTimer = nullptr;

    // 상태 캐시(/status 응답값)
    QString currentState = "FAIL";      // PASS/FAIL
    double  stableSecs   = 0.0;         // 동일 상태 유지 시간(초)
    int     personCount  = 0;           // 감지된 사람 수(서버 표기)
    int     helmetCount  = 0;           // 헬멧 PASS 수(서버 표기)
    QString phase        = "QR_WAIT";   // QR 대기 / DETECT
    bool    passPopupShown = false;     // PASS 배너 중복 표시 방지
    int     lastQrEventId = -1;         // QR 성공/실패 이벤트 중복 방지 키

private:
    // ───────────────────────── 내부 동작(구현은 cpp) ─────────────────────────
    void setupUI();                     // 위젯 배치/레이아웃 생성
    void applyTheme();                  // 디자인 토큰을 QSS로 반영

    static QFrame* makeCard(QWidget *parent); // 공통 카드 컨테이너 생성

    /*
     * makeRow(...)
     *  - "라벨 + 읽기전용 입력박스" 한 줄을 만들어 반환
     *  - 라벨 정렬은 ROW_LABEL_ALIGN 토큰으로, 입력칸 세로 높이는 EDIT_MIN_H 토큰으로 제어
     *  - outEdit 포인터를 통해 생성된 QLineEdit를 호출자에게 넘김
     */
    QFrame* makeRow(QWidget *parent, const QString &label, QLineEdit **outEdit,
                    int labelMinW, int hgap, int vgap, int editMinH);

    // 이벤트/그리기 훅
    void resizeEvent(QResizeEvent*) override; // 창 크기 변경 시 영상 프레임 재스케일
    void drawFrame(const QPixmap &pix);       // videoLabel 영역에 프레임 그림

    // MJPEG 스트림 제어
    void startMjpegStream();
    void stopMjpegStream();
    void onMjpegReadyRead();
    void onMjpegFinished();
    void onMjpegError(QNetworkReply::NetworkError);

    // 상태 폴링 및 UI 반영
    void updateStatus();                      // /status GET 후 내부 상태 갱신
    void updateStatusStyle();                 // 상태 문자열/게이지/텍스트 갱신
    void maybeShowPassPopup(bool passFor3s);  // PASS 3초 유지 시 팝업
    void handleQrEvents(const QJsonObject &o);// QR 성공/실패 이벤트 처리(작업자 정보 채움/초기화)

    // 상단 배너 팝업(성공/실패)
    void showBannerPopup(const QString &title, const QColor &bg, const QColor &fg, bool withOkBtn=false);
};
