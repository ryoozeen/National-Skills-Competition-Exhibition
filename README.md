# 기능경기대회 전시 안전관리시스템(SMS)

작업자 출입 관리, 근태 관리, 설비 제어, 화재 감지/대응, 관리자 통합관제를 통합한 안전관리 시스템입니다. 실시간 스트리밍과 이벤트 연동으로 현장 상황을 빠르게 파악하고 즉시 대응할 수 있도록 설계되었습니다.

---

## 프로젝트 개요
- 출입: QR 인식 → DB 조회/기록 → YOLO 안전모 판별 → PASS 시 게이트 개방 및 근태 저장
- 화재: CCTV 1차 감지/스트리밍 → 로봇 2차 검증·녹화 → 관리자 알림/좌표 전송 → 복귀
- 설비: 문 열림/접근 감지 → 관리자 알림 → 경고등/비상정지 → 이력 저장

---

## 개발 기간
- 2025.08.26(화) ~ 2025.09.22(월) [28일]
- 2025.09.23(화) 10:00 ~ 18:00 (전시)

---

## 기술 스택
- OS: Windows, Ubuntu
- Language/Lib: Python 3.x, C++17, Qt 6 (Core/Widgets/Network/Sql), OpenCV, YOLO
- Build: CMake, Qt Creator, VSCode
- DB: MySQL 8.0
- 기타: Arduino(설비 제어), TCP/IP

---

## 팀/담당
- 팀 규모: 11명
- 본인 역할: 팀장(문서/일정/조율) + 출입 파트 구현 + 안전모 인식/스트리밍 적용

---

## 데모
- 이미지: https://github.com/user-attachments/assets/651a50d2-85aa-46cd-8a12-89577680cea2

- 영상: https://www.youtube.com/watch?v=SYVyU6_6rpY

---

## 실행 환경
- Qt 6.x (Windows: MSVC 2022 빌드, Ubuntu: gcc)
- Python 3.9+ 권장
- MySQL Server 8.0+

---

## DB 준비(safetydb2.sql)
1) MySQL Workbench에서 스키마 생성 후 `safetydb2.sql` 실행
2) CLI 예시
```
mysql -u root -p
CREATE DATABASE IF NOT EXISTS sms CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE sms;
SOURCE "/absolute/path/to/safetydb2.sql";
```

---

## 환경변수(예시)
- DB_HOST=127.0.0.1
- DB_NAME=sms
- DB_USER=본인 MySQL 사용자
- DB_PASS=본인 비밀번호
- STREAM_HOST=0.0.0.0
- STREAM_PORT=8080  
  (또는 프로젝트 설정값)
- YOLO_MODEL_PATH=/path/to/model.engine  
  (필요 시)

---

## 빌드/실행
### Python 서비스
- 출입 스트리밍 서버: `출입/stream_server.py`
- 로봇 업로드/통신: `robot_client_fire_upload_commented.py`
- 실행 예시
```
cd 출입
python stream_server.py --host 0.0.0.0 --port 8080
```

### Qt 관리자 클라이언트(Windows · Qt Creator)
1) Qt Creator로 `관리자/CMakeLists.txt` 열기
2) Qt Kit 선택(Desktop Qt 6.x)
3) Build/Run → 실시간 스트리밍/알림/조회 화면 확인

### Arduino(설비)
- `설비/factory/factory/factory.ino` 업로드(센서/경고등/비상정지)

---

## 주요 기능
- 출입: QR 인식, 사용자 정보 조회, YOLO 안전모 판별, PASS 시 게이트 개방 및 근태 저장
- 화재: CCTV 1차 감지 및 관리자 스트리밍, 로봇 2차 검증/녹화/알림/복귀
- 설비: 문 열림/접근 감지, 관리자 알림, 경고등 작동, 비상 정지
- 관리자: 실시간 모니터링, 출입/근태 조회, 로그인, 설정 확인, 화재 녹화 확인

---

## 동작 개요
1) 출입: `QR → DB 기록 → YOLO 안전모 → PASS → 문 개방 → 근태 저장`
2) 화재: `CCTV 감지/스트리밍 → 로봇 2차 검증/녹화 → 관리자 알림/좌표`
3) 설비: `문 열림/접근 감지 → 알림 → 경고등/비상정지 → 이력 저장`

---

## 네트워크/프로토콜(요약)
- 스트리밍: MJPEG/HTTP(구현에 따라 상이), Qt 클라이언트에서 `mjpegview`로 표시
- 이벤트/알림: TCP/HTTP(JSON) 기반(세부 메시지 스키마는 구현에 맞춰 확장)

---

## 실행 팁
- Qt QMYSQL 드라이버 확인(Windows: `libmysql.dll`이 PATH에 존재 필요)
- DB 연결 실패 시 환경변수와 MySQL 권한 확인
- 스트리밍 연결 문제 시 HOST/PORT 및 방화벽 점검

---

## 디렉토리 구조(요약)
```
기능경기대회/
  관리자/           # Qt C++ 관리자 클라이언트
  출입/             # Python 스트림 서버 등
  설비/             # Arduino 설비 제어 코드
  safetydb2.sql     # DB 스키마
```

---

## 라이선스/저작권
- (추가 예정)

---

## 크레딧
- 팀원 11명, 역할 분담 및 공헌도(추가 예정)


