#pragma once
#include <QWidget>

// 전방 선언(컴파일 시간 단축/의존 최소화)
class QPushButton;
class QLabel;
class QFrame;

/**
 * @brief 수동 제어(Manual) 화면 위젯
 *
 * 기능 초점 설명:
 *  - 상단 ‘비상정지(E-Stop)’ 상태 표시/토글: 사용자의 클릭으로 활성/해제를 요청하고,
 *    실제 반영은 서버 ACK 수신 후 setEmergencyStop() 슬롯에서 갱신합니다.
 *  - 설비/문 상태 타일 3종 표시: 설비 가동, 공장문(factory), 출입문(entrance)의
 *    현재 상태를 텍스트 + 칩(색상)으로 시각화합니다.
 *  - 하단 ‘출입문’ 단일 수동 조작: 열기/닫기 버튼 1개만 제공(요건)하며,
 *    클릭 시 requestEntranceDoor(true/false) 시그널만 발행합니다.
 *
 * 설계 원칙:
 *  - 이 위젯은 네트워크/비즈니스 로직을 소유하지 않습니다. (UI 전용)
 *    모든 제어 요청은 시그널로 외부(컨트롤러/상위 윈도우)로 위임합니다.
 *  - 상태 반영은 외부에서 슬롯(set*State) 호출로만 갱신합니다.
 *    (서버 푸시/ACK → 상위 → 본 위젯 슬롯 호출)
 *  - 스레드: UI 스레드에서만 직접 호출해야 합니다.
 *    다른 스레드에서 상태를 갱신할 경우 Qt::QueuedConnection으로 연결하세요.
 *  - 메모리: Qt 부모-자식 소유권 규칙(본 위젯 파괴 시 자식 위젯 자동 해제)
 */
class ManualControlPage : public QWidget {
    Q_OBJECT
public:
    explicit ManualControlPage(QWidget* parent = nullptr);

signals:
    /**
     * @brief 비상정지 상태 변경 요청
     * @param engaged true면 ‘비상정지 활성화’, false면 ‘비상정지 해제’ 요청
     *
     * 동작 계약:
     *  - 사용자가 E-Stop 버튼을 클릭하면 UI는 일시적으로 비활성화(중복 방지)하고
     *    이 시그널을 발행합니다. 실제 상태 반영은 서버 응답 후 상위에서
     *    setEmergencyStop()을 호출해 주어야 합니다.
     *  - 권한/검증/로깅은 외부에서 처리하십시오.
     */
    void requestEmergencyStop(bool engaged);

    /**
     * @brief 출입 문(Entrance Door) 수동 조작 요청
     * @param open true → 열기 요청, false → 닫기 요청
     *
     * 동작 계약:
     *  - 버튼은 현재 상태에 따라 "문 열기/닫기"로 라벨이 바뀌며,
     *    클릭 시 현재 상태의 반대를 요청합니다.
     *  - 실제 개폐 결과는 서버/장치에서 확정 후 상위가
     *    setEntranceDoorState()로 반영해야 합니다.
     */
    void requestEntranceDoor(bool open);    // 출입 문 열기/닫기

public slots:
    /**
     * @brief 비상정지 상태를 확정 반영
     * @param engaged true면 ‘비상정지 활성화됨’, false면 ‘해제됨’
     *
     * 사용처:
     *  - 서버 ACK/브로드캐스트 수신 후 상위에서 호출.
     *  - 내부 대기 플래그(estopPending)를 해제하고, 배너 색/문구/버튼 상태를
     *    즉시 갱신합니다.
     */
    void setEmergencyStop(bool engaged);

    /**
     * @brief 설비 가동/공장문 상태를 확정 반영
     * @param run         0/1 (0=정지, 1=가동)
     * @param doorFactory 0/1 (0=닫힘, 1=열림)
     *
     * 표시만 담당:
     *  - 상단 타일의 텍스트와 칩(색상)을 즉시 업데이트합니다.
     *  - 입력값은 내부적으로 0/1로 정규화해 저장(-1은 ‘미정(unknown)’).
     */
    void setFactoryState(int run, int doorFactory); // run 0/1, 공장 문 0/1

    /**
     * @brief 출입 문(Entrance Door) 상태를 확정 반영
     * @param doorOpen 0/1 (0=닫힘, 1=열림)
     *
     * UI 업데이트:
     *  - 타일 텍스트/칩, 하단 토글 버튼 라벨/아이콘/툴팁을 현재 상태에 맞게
     *    즉시 전환합니다.
     */
    void setEntranceDoorState(int doorOpen);        // 출입 문 0/1

private:
    /**
     * @brief 위젯 트리/레이아웃 구성
     *
     * 구조:
     *  - estopBar: 상단 비상정지 슬림 바(안내 레이블 + 토글 버튼)
     *  - tilesWrap: 상단 3개 타일(설비/공장문/출입문) — 칩+텍스트로 상태 표시
     *  - ctrlCard: 하단 출입문 단일 토글 버튼 카드
     *
     * 시그널 연결:
     *  - estopBtn 클릭 → requestEmergencyStop(!state) 발행
     *  - entDoorToggle 클릭 → requestEntranceDoor(open/close) 발행
     */
    void buildUi();

    /**
     * @brief 페이지 전반 QSS 적용
     *
     * 테마:
     *  - 배너/카드/버튼의 색상·라운드·테두리 통일
     *  - 버튼은 pill 형태, 호버 효과 지정
     */
    void applyStyle();

    /**
     * @brief 상단 비상정지 배너 UI만 갱신
     *
     * 로직:
     *  - emergencyStop / estopPending 플래그를 기준으로
     *    배경색/테두리/지도 문구/버튼 활성 여부를 조정합니다.
     *  - 요청 중일 때는 버튼을 비활성화해 중복 클릭을 방지합니다.
     */
    void refreshEStopUi();   // 상단 슬림 바

    /**
     * @brief 타일/버튼에 표시되는 현재 상태를 한 번에 갱신
     *
     * 로직:
     *  - runState / facDoorState / entDoorState를 텍스트와 칩으로 변환해 반영.
     *  - 출입문 토글 버튼의 라벨/아이콘/툴팁을 현재 상태에 맞게 전환.
     */
    void refreshStateUi();   // 3개 타일 + 출입 문 버튼

    // ── 상단 비상정지 슬림 바: 상태 안내 + 토글 버튼 컨테이너
    QFrame*      estopBar{};     ///< 배너 컨테이너(색상으로 활성/해제 상태 강조)
    QLabel*      estopText{};    ///< 안내 문구(활성/해제/요청 중)
    QPushButton* estopBtn{};     ///< E-Stop 토글 버튼(중복 클릭 방지 처리 포함)

    // ── 상단 3타일 래퍼 (설비/공장문/출입문 상태 요약)
    QFrame*      tilesWrap{};

    // 타일: 설비 가동(ON/OFF)
    QFrame*      tileRun{};
    QLabel*      runIcon{};      ///< 장치 아이콘(플랫폼 표준)
    QLabel*      runChip{};      ///< 상태 칩(초록/빨강/회색)
    QLabel*      runLabel{};     ///< 텍스트 값("ON"/"OFF"/"-")

    // 타일: 공장 문 (factory door)
    QFrame*      tileFacDoor{};
    QLabel*      facDoorIcon{};
    QLabel*      facDoorChip{};
    QLabel*      facDoorLabel{}; ///< "열림"/"닫힘"/"-"

    // 타일: 출입 문 (entrance door)
    QFrame*      tileEntDoor{};
    QLabel*      entDoorIcon{};
    QLabel*      entDoorChip{};
    QLabel*      entDoorLabel{}; ///< "열림"/"닫힘"/"-"

    // 하단: 출입 문 수동 조작 카드(단일 토글)
    QFrame*      ctrlCard{};
    QPushButton* entDoorToggle{};///< "문 열기"/"문 닫기" 버튼(라벨/아이콘 동적 전환)

    // ── 내부 상태 캐시(서버 확정값을 반영하여 표시만 담당)
    bool emergencyStop = false;  ///< true=E-Stop 활성화됨(조작 제한 상태)
    bool estopPending  = false;  ///< E-Stop 요청 중(버튼 비활성화로 중복 요청 방지)

    int  runState      = -1;     ///< 설비 가동: -1 미정, 0 OFF, 1 ON
    int  facDoorState  = -1;     ///< 공장 문:   -1 미정, 0 닫힘, 1 열림
    int  entDoorState  = -1;     ///< 출입 문:   -1 미정, 0 닫힘, 1 열림
};
