#include <math.h>  // isnan, NAN 사용을 위한 표준 수학 라이브러리 포함

#define A_1A 11    // (참고) 핀 별칭 정의 — 아래에서 PWM으로 10/11을 직접 사용하므로 실사용은 없음
#define A_1b 10    // (참고) 동일 — 유지해도 무방하나 혼동 방지를 위해 주석으로 표기

// ===== 핀 설정 =====
const byte TRIG   = 9;   // 초음파 센서 TRIG 출력 핀
const byte ECHO   = 8;   // 초음파 센서 ECHO 입력 핀
const byte TRIG_1 = 13;  // IR(물건 도착) 센서 입력 핀 — 현장 극성(HIGH=감지)을 그대로 사용

// ===== 파라미터 =====
const unsigned long ULTRA_TIMEOUT_US = 10000UL;  // pulseIn 타임아웃(us) — 10ms(≈ 최대 5m 측정)
const uint16_t      STOP_DIST_CM     = 15;       // 도어 안전 임계값(cm) — 15cm 미만이면 “닫힘”
const uint8_t       N_SAMPLES        = 3;        // 중앙값 필터 샘플 개수(노이즈 저감)
const unsigned long CON_HOLD_MS      = 2000UL;   // IR 감지 후 고정 홀드 시간(ms) — 2초

// ★ 자동 재가동 허용 + U/R 직후 IR 무시 그레이스 창
const bool          AUTO_RESUME       = true;    // 자동 재가동 정책 사용 여부
const uint8_t       PWM_RUN           = 200;     // 컨베이어 동작 PWM 듀티(0~255)
const unsigned long RESUME_GRACE_MS   = 1500;    // U/R 이후 IR 무시 그레이스(ms)

// ===== 상태 =====
unsigned long lastPrint = 0;     // 주기적 로그 억제를 위한 타임스탬프
unsigned long boot_ms   = 0;     // 부팅 시각(ms) — 타임아웃/그레이스 계산에 사용

int run = 0;                     // 라인 RUN 상태(0=정지, 1=동작)

// 15cm 기준 상태 변수 (15 이상=1, 15 미만=0)
uint8_t door_sensor = 0;         // 도어 상태(1=열림/안전거리 확보, 0=닫힘/근접)
bool door_sent_once = false;     // 부팅 후 최초 1회 상태 송신 여부(초기 동기화용)

// NEW: 홀드 2종
bool hold_for_start = false;     // START 시퀀스 동안 컨베이어 홀드(석션 OFF 완료까지)
bool hold_for_job   = false;     // 작업(런) 시퀀스 동안 컨베이어 홀드(로봇 취급 중)

// IR 2초 홀드
bool           con_hold_active = false;  // IR 감지로 인한 홀드 활성화 플래그
unsigned long  con_hold_until  = 0;      // IR 홀드 만료 시각(ms)

// U/R 직후 R 허용 + IR 무시 그레이스 창
unsigned long  resume_grace_until = 0;   // U 또는 R 이후 IR 무시 그레이스 만료 시각(ms)

// UI로 내보내는 “물건도착센서” 값(활성 Low 기준: 0=감지, 1=비감지)
int  con_sensor = 1;                 // UI 표현 값(부팅 기본은 비감지=1)
static int last_con_sensor = -1;     // 변화 감지용 이전값(초기 -1로 강제 송신 유도)

// ─────────────────────────────────────────────────────────────
// 초음파를 1회 측정하여 cm로 환산해 반환(실패 시 NAN)
// ─────────────────────────────────────────────────────────────
float readOnceCm() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);     // TRIG 안정화(LOW 펄스)
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);    // 10us HIGH로 트리거 펄스 발생
  digitalWrite(TRIG, LOW);                             // 다시 LOW로 복귀
  unsigned long dur = pulseIn(ECHO, HIGH, ULTRA_TIMEOUT_US);  // ECHO HIGH 폭 측정
  if (dur == 0) return NAN;                           // 타임아웃 또는 무반사 → NAN
  return dur / 58.0;                                  // μs → cm 근사 환산(58)
}

// ─────────────────────────────────────────────────────────────
// 중앙값 필터로 안정화된 거리(cm) 반환 — n은 최대 9로 클램프
// ─────────────────────────────────────────────────────────────
float readMedianCm(uint8_t n = N_SAMPLES) {
  float buf[9];                    // 최대 9샘플 버퍼
  if (n > 9) n = 9;                // 방어적 클램프
  for (uint8_t i=0; i<n; ++i) {    // n회 샘플링
    buf[i]=readOnceCm(); 
    delay(3);                      // 샘플 간 미세 대기(센서 안정화)
  }
  for (uint8_t i=1; i<n; ++i) {    // 삽입 정렬로 오름차순 정렬
    float key = buf[i]; 
    int j = i-1;
    while (j>=0 && buf[j]>key) {   // 좌측으로 큰 값 밀어내기
      buf[j+1]=buf[j]; 
      j--;
    }
    buf[j+1] = key;                // key 삽입
  }
  return buf[n/2];                 // 중앙값 반환(홀수 샘플 가정)
}

// ─────────────────────────────────────────────────────────────
// 초기화: 핀 모드, 직렬 통신, 컨베이어 PWM 초기 상태, 부팅 시 IR 상태 1회 브로드캐스트
// ─────────────────────────────────────────────────────────────
void setup() {
  pinMode(TRIG, OUTPUT);           // 초음파 TRIG 출력
  pinMode(ECHO, INPUT);            // 초음파 ECHO 입력
  pinMode(TRIG_1, INPUT);          // IR 센서 입력(HIGH=감지)
  digitalWrite(TRIG, LOW);         // TRIG 기본 LOW

  Serial.begin(9600);              // PC와 시리얼 통신 속도 설정

  pinMode(10, OUTPUT);             // 컨베이어 모터 PWM 핀(하드웨어 PWM)
  pinMode(11, OUTPUT);             // 방향/보조 핀(현장 배선 정책에 따라 사용)

  // 초기 상태: 컨베이어 정지
  analogWrite(10, 0);

  // 부팅 시 IR 원시값 1회 취득 → UI 초기값으로 송신(Active-Low로 표준화)
  {
    int  ir_raw_boot       = digitalRead(TRIG_1);        // 원시 극성 그대로 읽기
    bool ir_detected_boot  = (ir_raw_boot == HIGH);      // HIGH=감지
    con_sensor = ir_detected_boot ? 0 : 1;               // UI값(0=감지, 1=비감지)
    Serial.print("CON_SENSOR="); Serial.println(con_sensor);  // 초기 상태 송신
    last_con_sensor = con_sensor;                        // 이전값 업데이트
  }

  // 부팅 기준 시각 저장
  boot_ms = millis();
}

// ─────────────────────────────────────────────────────────────
// 메인 루프: 안전(도어), 명령 파서, IR 홀드/재가동 정책 순서로 처리
// ─────────────────────────────────────────────────────────────
void loop() {
  // ================= 거리/도어 판정 =================
  float d = readMedianCm();                // 중앙값 필터 적용 거리(cm)
  bool valid = !isnan(d);                  // 유효 측정 여부

  if (valid) { 
    door_sensor = (d >= 15.0f) ? 1 : 0;    // 15cm 이상=열림(1), 미만=닫힘(0)
  }
  else {       
    door_sensor = 1;                       // 무반사 시 보수적으로 “열림(안전)” 처리
  }

  static int  last_door;                   // 직전 도어 상태 저장
  static bool last_door_inited = false;    // 직전값 초기화 여부

  // 최초 유효 판독 시 1회 강제 송신(초기 UI 동기화)
  if (!door_sent_once) {
    if (valid) {
      Serial.print("DOOR_SENSOR="); Serial.println(door_sensor); // 상태 송신
      last_door = door_sensor; 
      last_door_inited = true;
      door_sent_once = true;
    } else if ((millis() - boot_ms) > 800) {  // 일정 시간 내 유효값 없으면
      Serial.print("DOOR_SENSOR="); Serial.println(1);          // 안전측(열림=1)으로 송신
      last_door = 1; 
      last_door_inited = true;
      door_sent_once = true;
    }
  }
  // 이후에는 변화 있을 때만 송신(잡음/로그 스팸 방지)
  else if (last_door_inited && (door_sensor != last_door)) {
    Serial.print("DOOR_SENSOR="); Serial.println(door_sensor);
    last_door = door_sensor;
  }

  // 안전정지 로직: RUN중 + (무반사 or 임계초과) → 즉시 정지 및 상태 리셋
  if ((run == 1) && (!valid || d > STOP_DIST_CM)) {
    analogWrite(10, 0);                                // 컨베이어 정지
    if (millis() - lastPrint > 200) {                  // 로그 과다 방지(200ms 간격)
      Serial.print("STOP");
      if (!valid) Serial.println(" no-echo");          // 무반사 상황 로그
      else { Serial.print(" dist="); Serial.print((int)d); Serial.println("cm"); }
      lastPrint = millis();
    }
    run = 0;                                           // 라인 상태 리셋
    hold_for_start = false;                            // 모든 홀드 해제
    hold_for_job   = false;
    con_hold_active = false;
    resume_grace_until = 0;                            // 그레이스 창 종료
    if (con_sensor != 1) {                             // UI 표기는 비감지=1로 복귀
      con_sensor = 1; 
      Serial.print("CON_SENSOR="); Serial.println(con_sensor);
    }
    return;                                            // 안전 정지 시 루프 조기 종료
  }

  // ================= PC 명령 처리 =================
  if (Serial.available()) {                // PC에서 1바이트 명령 수신 시
    char cmd = Serial.read();              // 명령 문자 취득

    // RUN 시작: 도어 닫힘(안전거리 미확보) + 현재 정지 상태 + '1' 명령
    if (door_sensor==0 && run==0 && cmd=='1') {
      analogWrite(10, 0);                  // 시작 직전 컨베이어 정지 보증
      run = 1;                             // 라인 RUN 진입
      hold_for_start = true;               // START 홀드 활성(석션 OFF까지)
      hold_for_job   = false;              // 작업 홀드 비활성
      con_hold_active = false;             // IR 홀드 해제
      resume_grace_until = 0;              // 그레이스 창 초기화
      if (con_sensor != 1) {               // UI 상태를 비감지=1로 강제
        con_sensor = 1; 
        Serial.print("CON_SENSOR="); Serial.println(con_sensor);
      }
      Serial.println("RUN=1 HOLD_START=1"); // 상태 로그
    }
    // RUN 정지: 동작 중 + '0' 명령
    else if (run==1 && cmd=='0') {
      analogWrite(10, 0);                  // 컨베이어 정지
      run = 0;                             // 라인 정지 상태
      hold_for_start = false;              // 모든 홀드 해제
      hold_for_job   = false;
      con_hold_active = false;
      resume_grace_until = 0;              // 그레이스 창 종료
      if (con_sensor != 1) {               // UI 비감지=1로 복귀
        con_sensor = 1; 
        Serial.print("CON_SENSOR="); Serial.println(con_sensor);
      }
      Serial.println("RUN=0");             // 상태 로그
    }
    // 작업홀드 ON: 파이썬 서버가 con=0(런 트리거) 시그널 이후 보내주는 'J'
    else if (cmd=='J') {
      hold_for_job = true;                 // 작업 중 컨베이어 정지 유지
      analogWrite(10, 0);                  // 즉시 정지
      if (con_sensor != 1) {               // UI 비감지=1로 정규화
        con_sensor = 1; 
        Serial.print("CON_SENSOR="); Serial.println(con_sensor);
      }
      Serial.println("HOLD_JOB=1");        // 상태 로그
    }
    // 모든 홀드 해제: 로봇 Busy→Idle 시 파이썬이 'U' 전송
    else if (cmd=='U') {
      hold_for_start = false;              // START 홀드 해제
      hold_for_job   = false;              // 작업 홀드 해제
      Serial.println("HOLD_ALL=0");        // 상태 로그
      // U 직후 IR 무시 + R 허용 그레이스 창 시작
      resume_grace_until = millis() + RESUME_GRACE_MS;

      if (AUTO_RESUME) {                   // 자동 재가동 정책일 때
        if (run==1 && !con_hold_active) {  // 라인 동작 중이며 IR 홀드가 없으면
          analogWrite(10, PWM_RUN);        // 컨베이어 재시동
          if (con_sensor != 1) {           // UI 비감지=1 반영
            con_sensor = 1; 
            Serial.print("CON_SENSOR="); Serial.println(con_sensor); 
            last_con_sensor = con_sensor;
          }
          Serial.println("RESUME_BY_U");   // 재가동 사유 로깅
        }
      }
    }
    // 레일 강제 STOP: 'S'
    else if (cmd=='S') {
      analogWrite(10, 0);                  // 즉시 정지
      if (con_sensor != 1) {               // UI 비감지=1로 정규화
        con_sensor = 1; 
        Serial.print("CON_SENSOR="); Serial.println(con_sensor);
      }
      Serial.println("RAIL=STOP");         // 상태 로그
    }
    // 레일 강제 RUN: 'R'
    else if (cmd=='R') {
      bool grace = ((long)(millis() - resume_grace_until) <= 0); // 그레이스 창 내 여부

      // 모든 홀드/IR홀드가 없고 RUN 상태면 가동 허용
      if (run==1 && !hold_for_start && !hold_for_job && !con_hold_active) {
        analogWrite(10, PWM_RUN);          // 컨베이어 시작
        if (con_sensor != 1) {             // UI 비감지=1 표준화
          con_sensor = 1; 
          Serial.print("CON_SENSOR="); Serial.println(con_sensor); 
          last_con_sensor = con_sensor;
        }
        Serial.println(grace ? "RAIL=RUN(grace)" : "RAIL=RUN"); // 그레이스 여부 표기
        resume_grace_until = millis() + RESUME_GRACE_MS;        // 창 연장
      } else {
        analogWrite(10, 0);                // 조건 불충족 → 정지 유지
        if (con_sensor != 1) {             // UI 비감지=1로 정규화
          con_sensor = 1; 
          Serial.print("CON_SENSOR="); Serial.println(con_sensor);
        }
        Serial.println("RAIL=BLOCKED");    // 차단 사유 로깅
      }
    }
  }

  // ================= IR 처리 (감지→2초 홀드→판정) =================
  int  ir_raw      = digitalRead(TRIG_1);      // IR 원시 입력
  bool ir_detected = (ir_raw == HIGH);         // HIGH=감지(현장 극성)
  bool grace       = ((long)(millis() - resume_grace_until) <= 0); // 그레이스 유효?

  // ★ 핵심: 그레이스 중에는 IR=HIGH라도 즉시 STOP/홀드로 전환하지 않음
  if (run == 1 && ir_detected && !grace) {
    analogWrite(10, 0);                        // 컨베이어 정지
    if (con_sensor != 0) {                     // UI에 감지=0로 반영
      con_sensor = 0; 
      Serial.print("CON_SENSOR="); Serial.println(con_sensor); 
      last_con_sensor = con_sensor;
    }
    if (!con_hold_active) {                    // 홀드가 비활성 상태면 새 홀드 개시
      con_hold_active = true;
      con_hold_until  = millis() + CON_HOLD_MS; // 2초 홀드 만료 시각 설정
    }
  }

  // 감지 해제 시 자동 재가동(조건 만족 & AUTO_RESUME인 경우)
  if (run == 1 && !ir_detected) {
    if (!con_hold_active) {                    // 홀드 중이 아닐 때만
      if (!hold_for_start && !hold_for_job && AUTO_RESUME) {
        analogWrite(10, PWM_RUN);              // 컨베이어 재시동
        if (con_sensor != 1) {                 // UI 비감지=1로 정규화
          con_sensor = 1; 
          Serial.print("CON_SENSOR="); Serial.println(con_sensor); 
          last_con_sensor = con_sensor;
        }
        Serial.println("RESUME_BY_CLEAR");     // 재가동 사유 로깅
      }
    }
  }

  // 2초 홀드 만료 처리: 노이즈 대비 과반 투표로 재검증 후 재가동 판단
  if (run == 1 && con_hold_active && (long)(millis() - con_hold_until) >= 0) {
    const int N = 5; 
    int votes = 0;
    for (int i=0; i<N; ++i) {                  // 5회 샘플 투표(디바운스 강화)
      if (digitalRead(TRIG_1)==HIGH) votes++; 
      delay(1);
    }
    bool still_detected = (votes >= (N/2 + 1)); // 과반 이상이면 여전히 감지

    if (still_detected) {
      analogWrite(10, 0);                      // 감지 지속 → 정지 유지
    } else {
      if (!hold_for_start && !hold_for_job && AUTO_RESUME) {
        analogWrite(10, PWM_RUN);              // 감지 해제 → 자동 재가동
        if (con_sensor != 1) {                 // UI 비감지=1 반영
          con_sensor = 1; 
          Serial.print("CON_SENSOR="); Serial.println(con_sensor); 
          last_con_sensor = con_sensor;
        }
        Serial.println("RESUME_BY_HOLD_END");  // 홀드 만료로 재가동
      }
    }
    con_hold_active = false;                   // 홀드 종료(다음 감지 대기)
  }
}
