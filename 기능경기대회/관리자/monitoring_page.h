// monitoring_page.h  (필요 부분만 발췌)
#pragma once
/*
 * 파일: monitoring_page.h
 * 개요: "모니터링" 탭/페이지 위젯. 출입구/화재 카메라 프리뷰를 보여주고,
 *       프리뷰를 클릭하면 시그널로 상위 창(메인 윈도우 등)에 선택 이벤트를 전달합니다.
 *       외부에서 setEntranceCamUrl(), setFireCamUrl()로 스트림 URL을 주입해 재생하도록 설계되었습니다.
 *
 * 구성요소 요약:
 *   - buildUi(): 위젯/레이아웃 생성 및 MJPEG 뷰 연결
 *   - applyStyle(): 폰트/간격/색/여백 등 스타일 적용
 *   - setEntranceCamUrl(), setFireCamUrl(): 각 카메라 스트림 URL 설정 및 표시/재생
 *   - signals:   // 외부로 전달하는 이벤트. 프리뷰 클릭/선택 시 상위에 알립니다.
entranceCamClicked(), fireCamClicked(), cameraSelected(name, url)
 *
 * 사용법:
 *   MonitoringPage* page = new MonitoringPage(this);
 *   page->setEntranceCamUrl("http://<ip>:<port>/stream1.mjpg");
 *   page->setFireCamUrl("http://<ip>:<port>/stream2.mjpg");
 *   connect(page, &MonitoringPage::cameraSelected, this, [&](const QString& name, const QString& url){{ /* 선택 반응 */ }});
 *
 * 작성자 주석: 아래에 함수/멤버별로 한국어 주석을 상세히 달아 두었습니다.
 */
#include <QWidget>
#include <QString>

class QLabel;
class MjpegView;

class MonitoringPage : public QWidget {
    Q_OBJECT
public:
    explicit MonitoringPage(QWidget* parent = nullptr);  // 생성자: 부모 위젯(없으면 nullptr). UI 구성 및 초기화 수행
void setEntranceCamUrl(const QString& url);  // 출입구 카메라(MJPEG/HTTP 등) 스트림 URL 설정
void setFireCamUrl(const QString& url);  // 화재 감시 카메라 스트림 URL 설정
signals:
      // 외부로 전달하는 이벤트. 프리뷰 클릭/선택 시 상위에 알립니다.
void entranceCamClicked();  // 출입구 프리뷰가 클릭되었음을 알림
void fireCamClicked();  // 화재 프리뷰가 클릭되었음을 알림
void cameraSelected(const QString& name, const QString& url);  // 어떤 카메라가 선택되었는지 이름/URL을 함께 전달
private:
      // 내부 구현(헬퍼 함수/멤버 변수)
void buildUi();  // 위젯과 레이아웃 생성, 클릭 시그널 연결 등
void applyStyle();  // 폰트/여백/색상 등 스타일 적용
private:
      // 내부 구현(헬퍼 함수/멤버 변수)
// 프리뷰 표시용 라벨들
    QLabel* entranceCamView{  // 출입구 카메라 프리뷰 라벨(스냅샷/상태 텍스트 표기)
};    // 좌측 카드
    QLabel* entranceCamStatus{};  // 좌측 상태 라벨
    QLabel* fireCamView{  // 화재 카메라 프리뷰 라벨
};        // 우측 카드
    QLabel* fireCamStatus{};      // 우측 상태 라벨

    // URL 캐시
    QString entranceUrl_;
    QString fireUrl_;

    // ❗스트림 뷰어 포인터 (여기가 중요)
    MjpegView* entranceStream{};  // 출입구 카메라 MJPEG 스트림 뷰어 위젯

    MjpegView* fireStream{};  // 화재 카메라 MJPEG 스트림 뷰어 위젯

};
