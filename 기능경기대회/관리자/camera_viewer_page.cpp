#include "camera_viewer_page.h"  // 카메라 확대/단독 재생 화면 위젯 정의
#include "mjpegview.h"           // 단순 MJPEG 스트림을 받아 그려주는 뷰어 위젯

// ── UI 구성 요소/레이아웃 ─────────────────────────────────────────────
#include <QVBoxLayout>           // 수직 레이아웃: 페이지 전체/카드 내부 배치
#include <QHBoxLayout>           // 수평 레이아웃: 상단 바(뒤로/타이틀/상태)
#include <QLabel>                // 텍스트 라벨: 타이틀/상태/플레이스홀더
#include <QPushButton>           // 뒤로가기 버튼
#include <QFrame>                // 비디오 카드(테두리/라운드 적용용 컨테이너)
#include <QUrl>                  // 스트림 URL 처리(/mjpeg path 보정 등에 사용)

// ================================================================
// CameraViewerPage
// - 모니터링 페이지에서 특정 카메라를 선택했을 때, 단독으로 크게 재생.
// - 상단 바(뒤로/타이틀/상태) + 하단 비디오 카드(검정 배경 위 재생).
// - MJPEG 전용 뷰어(MjpegView) 삽입/교체/시작을 관리.
// ================================================================
CameraViewerPage::CameraViewerPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();    // 화면 요소 생성&배치
    applyStyle(); // 통일된 룩앤필(QSS) 적용
}

// ================================================================
// buildUi()
// - 페이지 레이아웃/위젯 트리를 구성한다.
// - 상단 바: ←뒤로 / "카메라 보기" / "스트림 상태".
// - 본문: 비디오 카드(흰 테두리 박스) 안에 실제 영상 영역(videoBox).
//   · 초기엔 안내용 플레이스홀더를 중앙 배치.
//   · 실제 재생 시 MjpegView를 videoBox의 레이아웃에 삽입.
// ================================================================
void CameraViewerPage::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24,24,24,24); // 페이지 외곽 여백
    root->setSpacing(12);                  // 섹션 간 간격

    // ── 상단 바: 내비게이션·상태 영역 ──────────────────────────────
    auto* top = new QHBoxLayout;

    // [뒤로] : 상위 화면(모니터링)으로 복귀 요청 신호를 보냄
    btnBack = new QPushButton(u8"← 뒤로");
    btnBack->setObjectName("backBtn"); // 스타일 지정용 ID
    btnBack->setFixedHeight(36);
    connect(btnBack, &QPushButton::clicked, this, &CameraViewerPage::backRequested);

    // "카메라 보기" : 선택된 카메라 이름을 결합해 동적으로 갱신됨
    titleLabel = new QLabel(u8"카메라 보기");
    titleLabel->setObjectName("pageTitle");

    // 재생/연결 상태 표시 : "연결 중…", "재생 중", "URL 없음" 등 즉시 피드백
    statusLabel = new QLabel(u8"스트림 준비됨");
    statusLabel->setObjectName("status");

    // 상단 바 배치: [뒤로] [타이틀] ———— [상태]
    top->addWidget(btnBack, 0, Qt::AlignLeft);
    top->addSpacing(8);
    top->addWidget(titleLabel, 0, Qt::AlignLeft);
    top->addStretch();
    top->addWidget(statusLabel, 0, Qt::AlignRight);
    root->addLayout(top);

    // ── 비디오 카드: 실제 영상이 들어가는 컨테이너 ──────────────────
    auto* card = new QFrame(this);
    card->setObjectName("videoCard"); // 흰색 카드 스타일(테두리/라운드)
    auto* cv = new QVBoxLayout(card);
    cv->setContentsMargins(0,0,0,0);
    cv->setSpacing(0);

    // videoBox: 검정 배경의 실제 렌더 타겟. 여기에 MjpegView를 붙인다.
    videoBox = new QWidget(card);
    videoBox->setObjectName("videoBox"); // 검정 배경(콘텐츠 대비용)
    videoBox->setMinimumHeight(560);     // 세로 고정 최소값(가시성 확보)

    // 최초 진입 시 표시되는 안내 텍스트(뷰어 삽입 전 플레이스홀더)
    auto* placeholder = new QLabel(u8"여기에 MJPEG 플레이어가 표시됩니다");
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("font-size:16px; color:#6b7280;");

    // videoBox 내부에 플레이스홀더 중앙 배치
    auto* vb = new QVBoxLayout(videoBox);
    vb->addStretch();
    vb->addWidget(placeholder);
    vb->addStretch();

    // 카드 → videoBox 부착, 카드 자체를 본문에 확장 배치
    cv->addWidget(videoBox);
    root->addWidget(card, 1); // stretch=1: 가용 공간을 비디오가 채우도록
}

// ================================================================
// applyStyle()
// - 상단 타이틀/상태/뒤로 버튼, 비디오 카드/박스의 룩앤필 적용.
// - 페이지 공통 톤과 일관되도록 검정 캔버스 + 흰 카드 테두리 구성.
// ================================================================
void CameraViewerPage::applyStyle() {
    setStyleSheet(R"(
        #pageTitle { font-size:22px; font-weight:800; color:#111827; }   /* 큰 제목 */
        #status { color:#6b7280; }                                       /* 상태 텍스트는 중간 회색 */
        #backBtn {
            background:#ffffff; border:1px solid #dbe3ff; border-radius:8px; padding:6px 12px;
            color:#111827; font-weight:700;                               /* 네비게이션 액션 */
        }
        #backBtn:hover { background:#eef3ff; }
        #videoCard {
            background:#ffffff;                                           /* 흰색 카드 */
            border:1px solid #dbe3ff;
            border-radius:14px;
        }
        #videoBox {
            background: #0b0f19;                                          /* 실제 영상 배경(검정) */
            border-radius: 14px;
        }
    )");
}

// ================================================================
// loadCamera(name, url)
// - 상단 타이틀/상태 갱신, 입력 URL을 MJPEG 엔드포인트로 보정 후 재생 시작.
// - 이전 뷰어/위젯 정리(메모리/자원 누수 및 중복 재생 방지).
// - /mjpeg 경로가 빠진 일반 베이스 URL도 수용하도록 path 자동 보정.
// ================================================================
void CameraViewerPage::loadCamera(const QString& name, const QString& url)
{
    // 타이틀에 카메라 이름 반영, 즉시 상태는 "연결 중…"으로 갱신
    titleLabel->setText(u8"카메라 보기 — " + name);
    statusLabel->setText(u8"연결 중…");
    currentUrl_ = url;   // 예: http://172.30.1.33:8000 또는 http://.../mjpeg

    // URL 미지정: 사용자에게 즉시 피드백 후 중단
    if (url.trimmed().isEmpty()) {
        statusLabel->setText(u8"URL 없음");
        return;
    }

    // ── MJPEG 경로 보정 ───────────────────────────────────────────
    // 이미 "/mjpeg"가 붙은 경우 그대로, 아니라면 "/mjpeg"를 붙여 MJPEG 엔드포인트로 정규화.
    QUrl mjpeg(url);
    if (mjpeg.path().isEmpty() || mjpeg.path() == "/")
        mjpeg.setPath("/mjpeg");

    // ── 기존 뷰어 정리 ───────────────────────────────────────────
    // 다른 카메라에서 넘어왔을 수 있으므로, 재생 중지 후 안전하게 파기.
    if (viewer_) {
        viewer_->stop();
        viewer_->deleteLater();
        viewer_ = nullptr;
    }

    // ── videoBox 내부 초기화 ─────────────────────────────────────
    // 기존 레이아웃의 플레이스홀더/자식 위젯 제거(깨끗한 캔버스 확보)
    if (auto lay = videoBox->layout()) {
        QLayoutItem* it;
        while ((it = lay->takeAt(0))) {
            if (auto w = it->widget()) w->deleteLater();
            delete it;
        }
    } else {
        // 레이아웃이 없을 가능성 대비: zero-margin 레이아웃 생성
        auto* vb = new QVBoxLayout(videoBox);
        vb->setContentsMargins(0,0,0,0);
        vb->setSpacing(0);
    }

    // ── 새 MJPEG 뷰어 생성/시작 ──────────────────────────────────
    // MjpegView는 QUrl을 기반으로 프레임을 받아 videoBox 위에 그린다.
    viewer_ = new MjpegView(videoBox);
    viewer_->setUrl(mjpeg);  // 보정된 /mjpeg 엔드포인트
    viewer_->start();        // 네트워크 연결 및 수신 시작
    videoBox->layout()->addWidget(viewer_); // 렌더 타겟 트리에 부착

    // 상태 갱신: 재생 시작됨
    statusLabel->setText(u8"스트림 재생 중");
}
