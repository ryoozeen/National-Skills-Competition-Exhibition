#include "monitoring_page.h"
#include "mjpegview.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QFrame>
#include <QUrl>
/*
 * 파일: monitoring_page.cpp
 * 개요: "모니터링" 탭/페이지 위젯. 출입구/화재 카메라 프리뷰를 보여주고,
 *       프리뷰를 클릭하면 시그널로 상위 창(메인 윈도우 등)에 선택 이벤트를 전달합니다.
 *       외부에서 setEntranceCamUrl(), setFireCamUrl()로 스트림 URL을 주입해 재생하도록 설계되었습니다.
 *
 * 구성요소 요약:
 *   - buildUi(): 위젯/레이아웃 생성 및 MJPEG 뷰 연결
 *   - applyStyle(): 폰트/간격/색/여백 등 스타일 적용
 *   - setEntranceCamUrl(), setFireCamUrl(): 각 카메라 스트림 URL 설정 및 표시/재생
 *   - signals: entranceCamClicked(), fireCamClicked(), cameraSelected(name, url)
 *
 * 사용법:
 *   MonitoringPage* page = new MonitoringPage(this);
 *   page->setEntranceCamUrl("http://<ip>:<port>/stream1.mjpg");
 *   page->setFireCamUrl("http://<ip>:<port>/stream2.mjpg");
 *   connect(page, &MonitoringPage::cameraSelected, this, [&](const QString& name, const QString& url){{ /* 선택 반응 */ }});
 *
 * 작성자 주석: 아래에 함수/멤버별로 한국어 주석을 상세히 달아 두었습니다.
 */

namespace {

// 클릭 가능한 QLabel (카드의 이미지 영역에 사용)
class ClickLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickLabel(QWidget* p=nullptr): QLabel(p) {
        setCursor(Qt::PointingHandCursor);
    }
signals:
    void clicked();
protected:
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) emit clicked();
        QLabel::mouseReleaseEvent(e);
    }
};

} // namespace

// ─────────────────────────────────────────────────────────────

MonitoringPage::MonitoringPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyStyle();

    // 상태 라벨 기본 숨김
    entranceCamStatus->hide();
    fireCamStatus->hide();
}
/**
 * @brief UI를 구성합니다.
 *  - 좌/우(또는 위/아래) 레이아웃에 출입구/화재 프리뷰 영역을 만듭니다.
 *  - 각 프리뷰에 MJPEG 뷰(MjpegView) 또는 라벨을 배치합니다.
 *  - 프리뷰를 클릭하면 해당 카메라가 선택되도록 시그널을 연결합니다
 *    (entranceCamClicked, fireCamClicked, cameraSelected(name, url)).
 */

void MonitoringPage::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16,16,16,16);
    root->setSpacing(12);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(16);

    // ===== 카드 1: 출입 카메라 =====
    {
        auto* card = new QFrame(this);
        card->setObjectName("camCard");
        auto* v = new QVBoxLayout(card);
        v->setContentsMargins(12,12,12,12);
        v->setSpacing(8);

        auto* title = new QLabel(u8"출입 카메라", card);
        title->setObjectName("camTitle");

        auto* view = new ClickLabel(card);
        view->setObjectName("camView");
        view->setMinimumSize(320, 180);
        view->setAlignment(Qt::AlignCenter);
        view->setText(u8"\n미리보기 자리\n(클릭하면 확대)");
        entranceCamView = view;

        entranceCamStatus = new QLabel(u8"URL: (없음)", card);
        entranceCamStatus->setObjectName("camStatus");

        v->addWidget(title);
        v->addWidget(view, 1);
        v->addWidget(entranceCamStatus);

        grid->addWidget(card, 0, 0);

        // 클릭 → 이름+URL 함께 알림(빈 경우 폴백 수행)
        // 클릭 이벤트를 MonitoringPage의 시그널로 전달
        
        connect(view, &ClickLabel::clicked, this, [this]{
            // URL 폴백: 미리보기 스트림에서 베이스 URL 추출
            QString url = entranceUrl_;
            if (url.isEmpty() && entranceStream && entranceStream->url().isValid()) {
                QUrl u = entranceStream->url();   // 예: http://X.X.X.X:8000/mjpeg
                if (u.path().startsWith("/mjpeg")) u.setPath(""); // 베이스만
                url = u.toString();
            }
            emit entranceCamClicked();
        // 사용자가 선택한 카메라 이름/URL을 상위에 알림
        
            emit cameraSelected(QStringLiteral("출입 카메라"), url);
        });
    }

    // ===== 카드 2: 화재 감지 카메라 =====
    {
        auto* card = new QFrame(this);
        card->setObjectName("camCard");
        auto* v = new QVBoxLayout(card);
        v->setContentsMargins(12,12,12,12);
        v->setSpacing(8);

        auto* title = new QLabel(u8"화재 감지 카메라", card);
        title->setObjectName("camTitle");

        auto* view = new ClickLabel(card);
        view->setObjectName("camView");
        view->setMinimumSize(320, 180);
        view->setAlignment(Qt::AlignCenter);
        view->setText(u8"\n미리보기 자리\n(클릭하면 확대)");
        fireCamView = view;

        fireCamStatus = new QLabel(u8"URL: (없음)", card);
        fireCamStatus->setObjectName("camStatus");

        v->addWidget(title);
        v->addWidget(view, 1);
        v->addWidget(fireCamStatus);

        grid->addWidget(card, 0, 1);
        // 클릭 이벤트를 MonitoringPage의 시그널로 전달
        

        connect(view, &ClickLabel::clicked, this, [this]{
            // 1) 기본은 설정된 fireUrl_
            QString url = fireUrl_;

            // 2) 비어있다면, 프리뷰 스트림에서 현재 URL을 가져와 사용
            if (url.isEmpty() && fireStream && fireStream->url().isValid()) {
                url = fireStream->url().toString();
            }

            // 3) 경로가 비어 있으면 /video_feed 붙여서 "완전한 스트림 URL" 보장
            if (!url.isEmpty()) {
                QUrl u(url);
                if (u.path().isEmpty() || u.path() == "/")
                    u.setPath("/video_feed");
                url = u.toString();
            }

            emit fireCamClicked();
        // 사용자가 선택한 카메라 이름/URL을 상위에 알림
        
            emit cameraSelected(QStringLiteral("화재 감지 카메라"), url);
        });
    }

    root->addLayout(grid, 1);
}
/**
 * @brief 페이지 전반의 스타일을 적용합니다.
 *  - 폰트 크기, 굵기, 배경/테두리, 위젯 여백/간격 등을 설정합니다.
 *  - 테마 일관성을 위해 라벨/프레임의 팔레트나 스타일시트를 조정합니다.
 */

void MonitoringPage::applyStyle() {
    setStyleSheet(R"(
        #camCard {
            background:#ffffff;
            border:1px solid #e5e7eb;
            border-radius:12px;
        }
        #camTitle {
            font-weight:700;
            color:#111827;
            background: transparent;   /* ⬅︎ 하늘색 배경 제거(부모 흰색이 비침) */
        }
        #camView {
            background:#f3f4f6;
            border:1px dashed #cbd5e1;
            border-radius:8px;
            color:#6b7280;
        }
        #camStatus {
            color:#374151;
        }
    )");
}
/**
 * @brief 출입구 카메라 스트림 URL을 설정하고 화면에 반영합니다.
 *  - 유효한 URL이면 MjpegView에 재생을 요청하고 상태 라벨을 갱신합니다.
 *  - 빈 문자열이라면 "미설정" 등의 안내를 표시하며 재생을 중단합니다.
 */

void MonitoringPage::setEntranceCamUrl(const QString& url) {
    entranceUrl_ = url;
    if (entranceCamStatus)
        entranceCamStatus->setText(url.isEmpty() ? u8"URL: (없음)" : u8"URL: " + url);

    // 여기서 미리보기 시작
    if (!url.isEmpty()) {
        const QUrl mjpeg(url + "/mjpeg");
        if (!entranceStream) {
            entranceStream = new MjpegView(entranceCamView);
            auto lay = new QVBoxLayout(entranceCamView);
            lay->setContentsMargins(0,0,0,0);
            lay->addWidget(entranceStream);
        }
        entranceStream->setUrl(mjpeg);
        entranceStream->start();
    }
}

// void MonitoringPage::setFireCamUrl(const QString& url) {
//     fireUrl_ = url;
//     if (fireCamStatus)
//         fireCamStatus->setText(url.isEmpty() ? u8"URL: (없음)" : u8"URL: " + url);

//     ❌ 지금은 화재 카메라 스트림을 시작하지 않음
//     if (!url.isEmpty()) {
//         const QUrl mjpeg(url + "/mjpeg");
//         if (!fireStream) {
//             fireStream = new MjpegView(fireCamView);
//             auto lay = new QVBoxLayout(fireCamView);
//             lay->setContentsMargins(0,0,0,0);
//             lay->addWidget(fireStream);
//         }
//         fireStream->setUrl(mjpeg);
//         fireStream->start();
//     }
// }
/**
 * @brief 화재 감시 카메라 스트림 URL을 설정하고 화면에 반영합니다.
 *  - 유효한 URL이면 MjpegView에 재생을 요청하고 상태 라벨을 갱신합니다.
 *  - 빈 문자열이라면 "미설정" 등의 안내를 표시하며 재생을 중단합니다.
 */

void MonitoringPage::setFireCamUrl(const QString& url) {
    fireUrl_ = url;
    if (fireCamStatus)
        fireCamStatus->setText(url.isEmpty() ? u8"URL: (없음)" : u8"URL: " + url);

    // 🔧 화재 카메라 프리뷰 시작: /video_feed 사용
    if (!url.isEmpty()) {
        QUrl mjpeg(url);
        // 경로가 비어 있으면 /video_feed를 붙여 완전한 스트림 URL을 만든다
        if (mjpeg.path().isEmpty() || mjpeg.path() == "/")
            mjpeg.setPath("/video_feed");

        if (!fireStream) {
            fireStream = new MjpegView(fireCamView);
            auto lay = new QVBoxLayout(fireCamView);
            lay->setContentsMargins(0,0,0,0);
            lay->addWidget(fireStream);
        }
        fireStream->setUrl(mjpeg);
        fireStream->start();
    }
}

#include "monitoring_page.moc"
