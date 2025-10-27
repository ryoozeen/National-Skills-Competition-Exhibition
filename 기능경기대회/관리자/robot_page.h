#pragma once
/**
 * @file robot_page.h
 * @brief 로봇/증거영상 뷰 페이지 헤더.
 *        - 좌측: QMediaPlayer + QVideoWidget 재생 영역
 *        - 우측: 영상 파일 리스트(QFileSystemModel 기반)
 *        - 하단: 로봇 관련 이벤트 로그(QTableWidget)
 *        - 상단: 연결 상태/네트워크 오류 칩(LED) + 라벨
 *
 * 기능 요약:
 *   - setConnectionState()/setNetworkError(): 상단 상태칩/문구 갱신
 *   - playEvidenceFile(): 파일/URL 유효성 검사 후 재생
 *   - setVideoFolder(): 파일 브라우저 루트 변경 및 폴더 감시
 *   - appendRobotEvent(): 로그 테이블에 한 줄 추가
 *   - dragEnterEvent()/dropEvent(): 드래그-드롭으로 바로 재생
 *   - eventFilter(): 창 이동/리사이즈 시 과도한 리렌더 방지(스로틀링)
 */
#include <QWidget>
#include <QAbstractSocket>

class QLabel;
class QPushButton;
class QTableWidget;
class QFrame;
class QVideoWidget;
class QMediaPlayer;
class QListView;
class QFileSystemModel;
class QSplitter;
class QFileSystemWatcher;
class QEvent;

class RobotPage : public QWidget {  // 증거영상 재생/파일 탐색/로그/상태 표시를 담당하는 메인 페이지

    Q_OBJECT
public:
    explicit RobotPage(QWidget* parent = nullptr) noexcept;  // UI/스타일/폴더 감시 초기화

    ~RobotPage() override;  // QMediaPlayer 정리, 비디오 출력 분리


public slots:
    // 상단 상태
    void setConnectionState(QAbstractSocket::SocketState s);  // 서버 연결 상태칩/문구 업데이트

    void setNetworkError(const QString& err);  // 네트워크/재생 오류 상태칩/문구 업데이트


    // 재생
    void playEvidenceFile(const QString& filePath);  // 파일/URL 검사 → QMediaPlayer에 소스 설정 후 재생

    void setVideoFolder(const QString& dirPath);  // 우측 파일 리스트 루트 경로 설정 + QFileSystemWatcher 갱신
  // 우측 파일 리스트의 루트 폴더 지정

    // 로그
    void appendRobotEvent(const QString& time, const QString& event, const QString& detail);  // 하단 로그 테이블에 한 줄 추가


protected:
    // 드래그&드롭(영상 파일 끌어다 재생)
    void dragEnterEvent(class QDragEnterEvent* e) override;  // 드래그 진입 시 확장자 확인 후 accept

    void dropEvent(class QDropEvent* e) override;  // 드롭된 첫 파일을 재생 시도


    // 이동/리사이즈 스로틀링
    bool eventFilter(QObject*, QEvent*) override;  // 이동/리사이즈 중 업데이트 억제(스로틀) → 80ms 후 재개


private:
    void buildUi();  // 레이아웃/위젯 구성 및 시그널 연결

    void applyStyle();  // 폰트/배경/버튼 모양 등 스타일시트 적용

    static void setChip(QLabel* chip, const QString& state, const QString& tip);  // 상태 칩(원형 색상) 스타일/툴팁 적용

    bool isVideoFile(const QString& path) const;
    void exportLogCsv();  // 로그를 CSV로 저장(UTF-8 BOM 포함, 엑셀 호환)
                 // 로그 CSV 내보내기
    void copySelectedLogRowsToClipboard();  // 선택 행을 탭 구분 텍스트로 복사


    // ── 상단: 연결/에러 상태 바
    QFrame* statusBar{};  // 상단 상태바 컨테이너

    QLabel* connChip{};  QLabel* connLabel{};  // 연결 상태 칩/문구

    QLabel* errChip{};   QLabel* errLabel{};  // 오류 상태 칩/문구


    // ── 중앙 분할(수평): 좌(플레이어) / 우(파일 리스트)
    QSplitter*      splitH{};  // 중앙 수평 분할기: 좌(플레이어)/우(파일리스트)

    QFrame*         videoCard{};  // 좌측 재생 카드

    QLabel*         fileLabel{};  // 현재 파일명/크기/URL 표시 라벨

    QVideoWidget*   videoView{};  // 영상 출력 위젯(검정 배경)

    QMediaPlayer*   player{};  // 재생기(에러 핸들러 포함)

    QPushButton*    btnOpenFolder{};  // 폴더 선택 버튼

    QPushButton*    btnPlayPause{};  // 재생/일시정지 토글 버튼


    QFrame*         browserCard{};  // 우측 파일 브라우저 카드

    QListView*      fileList{};  // 파일 리스트 뷰(단일 선택)

    QFileSystemModel* fsModel{};  // 파일 시스템 모델(확장자 필터 적용)


    // 폴더 자동감시
    QFileSystemWatcher* fsWatcher{};  // 폴더 변경 감시자(모델 갱신 트리거)

    QString            currentDir;  // 현재 루트 폴더 경로


    // ── 하단 로그(수직 분할의 아래쪽)
    QSplitter*    splitV{};  // 전체 수직 분할기(상:플레이어/리스트, 하:로그)

    QTableWidget* logTable{};  // 하단 로봇 이벤트 로그 테이블


    // 이동/리사이즈 스로틀링
    class QTimer* resizeTimer_{}; bool updatesSuppressed_ = false;  // 리사이즈/이동 중 리렌더 억제용 타이머/플래그

};
