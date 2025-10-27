#pragma once
#include <QWidget>       // QWidget 기반 페이지(탭/버튼/테이블 등 UI 컨테이너)
#include <QSqlDatabase>  // MySQL 등 RDB 연결 핸들(근태/사원 조회에 사용)

class QTabWidget;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTableWidget;
class QLabel;

/**
 * @brief 사원 관리/근태 조회 화면의 메인 페이지 위젯.
 *
 * 기능 개요
 * - "사원" 탭: employee 테이블을 조건(이름/부서/상태)으로 조회해 목록을 표시.
 * - "근태" 탭: gate_check 테이블을 기간/사원으로 필터링하여 일자별 출근/퇴근/근무시간을 집계 표시.
 * - DB 연결은 페이지 생성 시 1회 시도하고, 콤보/테이블 로딩을 트리거하는 버튼 액션 제공.
 *
 * UI 구성
 * - 상단 탭(QTabWidget): [사원], [근태]
 *   - 사원 탭: 검색바(이름/부서/상태 + 검색/추가/수정/삭제), 결과 테이블(tblWorkers)
 *   - 근태 탭: 조건바(기간 From~To + 근로자 콤보 + 조회), 결과 테이블(tblAttendance)
 *
 * 데이터 특성
 * - 사원 상태(status)는 정수 코드(예: 1/0)를 한글 레이블(재직/퇴사)로 매핑해 표시.
 * - gate_check 스키마에는 입/퇴근 플래그가 없다는 가정 하에,
 *   같은 날짜의 MIN(check_time)=출근, MAX(check_time)=퇴근으로 간주하여 집계.
 *
 * 스타일/테마
 * - applyStyle(): 배경/폰트/버튼/테이블 룩앤필을 통일(다크/라이트 OS 테마 무시).
 *
 * 주의사항
 * - DB 접속 정보는 샘플 값이며, 실제 배포에서는 설정 파일/환경변수로 분리 권장.
 * - 대용량 환경에서는 employee(name/department/status), gate_check(emp_id, check_time) 인덱스 필요.
 */
class AttendancePage : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief 페이지 위젯 생성자.
     * - buildUi()로 탭/검색바/테이블 등 UI 생성
     * - applyStyle()로 공통 테마 적용
     * - openDb() 성공 시 콤보/테이블 초기 로딩
     */
    explicit AttendancePage(QWidget *parent = nullptr);

private:
    /**
     * @brief UI 트리 구성.
     * - 상단 타이틀, 탭 컨테이너, 각 탭의 검색/조건 바와 테이블을 생성/배치.
     * - 버튼/콤보/라인에디트의 ObjectName/속성을 설정해 스타일/테스트에 활용.
     */
    void buildUi();

    /**
     * @brief 통일된 테마(QSS) 적용.
     * - 배경/폰트/버튼/테이블 룩을 하늘색 톤으로 고정하고 플랫폼 테마 영향 차단.
     * - 콤보 드롭다운(view)까지 라이트 스킨으로 강제.
     */
    void applyStyle();

    // ⬇ DB/로딩 헬퍼 — 각각의 “무엇을 하는지” 중심 설명

    /**
     * @brief RDB 연결을 초기화하고 오픈.
     * - QMYSQL 드라이버 사용(미설치 시 open 실패)
     * - 단일 커넥션 이름을 사용해 재호출 시 재사용
     * - 연결/읽기/쓰기 타임아웃을 짧게 설정해 지연 최소화
     * @return true: 연결 성공, false: 실패(상위에서 사용자 알림)
     */
    bool openDb();

    /**
     * @brief employee 테이블을 조건(이름/부서/상태)으로 조회하여
     *        “사원” 탭의 테이블(tblWorkers)을 채움.
     * - LIKE/동등 조건을 바인딩으로 적용(SQL 인젝션 완화, 캐시 힌트)
     * - COALESCE로 NULL 필드 표시 일관성 유지
     */
    void loadEmployees();

    /**
     * @brief “근태” 탭의 근로자 콤보(atWorker)를 DB에서 재구성.
     * - 첫 항목은 “전체 근로자”(data 미지정)로, 필터 미적용 의미.
     * - 이후 항목은 “이름 (사번)” 텍스트 + data=emp_id 형태로 채움.
     */
    void reloadWorkerCombo();

    /**
     * @brief gate_check 테이블에서 기간/직원으로 필터링하여
     *        일자별 출근/퇴근 시간과 총 근무시간을 집계하고 표(tblAttendance)에 표시.
     * - 스키마에 ‘입/퇴근 구분’이 없는 가정: 같은 날짜의 MIN=출근, MAX=퇴근
     * - DB에서 DATE/MIN/MAX/TIMEDIFF로 집계(클라이언트 연산 부담 감소)
     */
    void loadAttendance();

    /**
     * @brief 직원 상태 코드를 한글 라벨로 변환(표시용 헬퍼).
     * - 예) 1 → “재직”, 그 외 → “퇴사”(스키마 확장 시 이 매핑도 확장)
     */
    QString statusToKorean(int s) const;

    // ===== 공통 UI 루트 =====
    QTabWidget *tabs{};  ///< 상단 탭 컨테이너([사원], [근태])

    // ===== 탭1: 근로자 관리(검색바 + 결과 테이블) =====
    QLineEdit   *kwName{};      ///< 이름 검색 키워드 입력
    QComboBox   *kwDept{};      ///< 부서 필터(전체/생산/품질/설비/관리 등)
    QComboBox   *kwStatus{};    ///< 상태 필터(전체/재직/휴가/퇴사 등)
    QPushButton *btnSearch{};   ///< 검색(사원 목록 재질의 트리거)
    QPushButton *btnAdd{};      ///< 추가(신규 사원 등록; 실제 동작은 별도 구현)
    QPushButton *btnEdit{};     ///< 수정(선택 행 편집; 실제 동작은 별도 구현)
    QPushButton *btnRemove{};   ///< 삭제(선택 행 삭제; 실제 동작은 별도 구현)
    QTableWidget *tblWorkers{}; ///< 결과 테이블(사번/이름/부서/직무/상태/연락처/비고)

    // ===== 탭2: 출석 기록(조건 바 + 결과 테이블) =====
    QLineEdit   *atFrom{};        ///< 조회 시작일(YYYY-MM-DD)
    QLineEdit   *atTo{};          ///< 조회 종료일(YYYY-MM-DD)
    QComboBox   *atWorker{};      ///< 특정 근로자 필터(미선택 시 전체)
    QPushButton *btnRefresh{};    ///< 조회(근태 데이터 로딩 트리거)
    QTableWidget *tblAttendance{};///< 결과 테이블(일자/사번/이름/부서/출근/퇴근/근무시간)

    // ===== 데이터베이스 핸들 =====
    QSqlDatabase db_; ///< RDB 연결 핸들(단일 커넥션 이름 재사용; 연결 실패 시 비유효)
};
