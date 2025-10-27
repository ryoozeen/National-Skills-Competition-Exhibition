# factorycamera_win.py
# Fullscreen Dashboard — PyQt5 + Serial + TCP(JSON) + Robot TCP
# 정책 고정:
# 1) conv_has_part == 1 이고 IR(내부용) 클리어이고 로봇이 IDLE일 때만 레일 START, 그 외는 STOP
# 2) conv_has_part는 디스크에 영구 저장 (Stop/Reset/재시작 후 유지)
# 3) START 누를 때 conv_has_part==1 이면 START 시퀀스(올려놓기) 스킵, 레일만 가동
# 4) 도어 오픈 시 에러 라치 → Reset으로만 해제, 라치 중 START 금지

import os, sys, time, json, platform  # 표준 라이브러리: 경로/인자/시간/직렬화/플랫폼
from PyQt5.QtCore import QTimer, QThread, pyqtSignal, Qt  # 타이머/스레드/시그널/정렬 등 핵심 Qt 코어
from PyQt5.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,  # 앱/위젯/레이아웃
    QLabel, QPushButton, QGroupBox, QDialog, QFrame, QSizePolicy, QMessageBox  # UI 컴포넌트
)
from PyQt5.QtNetwork import QTcpSocket  # TCP 소켓(메인/로봇 서버와 통신)
from PyQt5.QtGui import QImage, QPixmap, QFont, QPalette, QColor  # 색/폰트/이미지

try:
    import serial  # pyserial: 아두이노와 시리얼 통신
except ImportError:
    raise SystemExit("pyserial이 필요합니다. 관리자 권한 PowerShell에서 `pip install pyserial` 실행 후 다시 시도하세요.")  # 하드 실패

IS_WINDOWS = platform.system().lower().startswith("win")  # 윈도우 여부 플래그
DEFAULT_SERIAL = "COM8" if IS_WINDOWS else "/dev/ttyACM0"  # OS별 기본 시리얼 포트
DEFAULT_CAMERA = "0"   if IS_WINDOWS else "/dev/video0"    # OS별 기본 카메라

def _get_serial_port() -> str:
    return os.getenv("SERIAL_PORT", DEFAULT_SERIAL)  # 환경변수 우선, 없으면 기본값

def _get_camera_device():
    raw = os.getenv("CAMERA", DEFAULT_CAMERA)  # CAMERA 환경변수 또는 기본값
    try:
        return int(raw.strip())  # 숫자면 정수(인덱스)로
    except Exception:
        return raw.strip()  # 아니면 문자열 경로로

# ================= 공통: 큼지막한 다크테마 팝업 =================
def big_msg(kind: str, title: str, text: str, parent=None):
    box = QMessageBox(parent)  # 메시지 박스 생성
    icon = {"info": QMessageBox.Information,
            "warn": QMessageBox.Warning,
            "crit": QMessageBox.Critical}.get(kind, QMessageBox.Information)  # 아이콘 매핑
    box.setIcon(icon)  # 아이콘 설정
    box.setWindowTitle(title)  # 타이틀
    box.setText(text)  # 본문
    box.setStandardButtons(QMessageBox.Ok)  # OK 버튼만
    box.setStyleSheet("""
        QMessageBox { background-color:#141821; }
        QMessageBox QLabel { color:#eaeef5; font-size:22px; }
        QMessageBox QPushButton {
            background:#1f2430; color:#eaeef5; border:1px solid #2b3140;
            border-radius:10px; padding:10px 18px; font-size:20px; font-weight:700;
            min-width:120px; min-height:44px;
        }
        QMessageBox QPushButton:hover { background:#283144; }
        QMessageBox QPushButton:pressed { background:#202637; }
    """)  # 다크테마 일관성 유지
    box.setMinimumWidth(520)  # 가독성 확보
    return box.exec_()  # 동기 실행

# ===== 한글 오버레이 유틸 (OpenCV + Pillow) =====
def draw_korean_text_on_bgr(bgr_img, text, xy, size=28, color=(255,255,255)):
    font_candidates = [
        os.getenv("KOREAN_FONT_PATH", ""),  # 사용자 지정 경로가 있으면 최우선
        r"C:\Windows\Fonts\malgun.ttf",     # Windows 기본(맑은고딕)
        r"C:\Windows\Fonts\Malgun.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",  # Linux Noto CJK
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.otf",
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",  # macOS
    ]
    font_path = next((p for p in font_candidates if p and os.path.exists(p)), None)  # 첫 가용 폰트 선택

    try:
        from PIL import ImageFont, ImageDraw, Image  # Pillow: 한글 렌더링
        import numpy as np
        import cv2

        if font_path is None:
            # 폰트가 없으면 ASCII만 안전히 렌더링(한글은 제거) → 운영 안전
            safe_txt = text.encode("ascii", "ignore").decode()
            cv2.putText(bgr_img, safe_txt, xy, cv2.FONT_HERSHEY_SIMPLEX,
                        size/32.0, color, 2, cv2.LINE_AA)
            return bgr_img

        img_rgb = cv2.cvtColor(bgr_img, cv2.COLOR_BGR2RGB)  # OpenCV BGR → RGB
        pil_img = Image.fromarray(img_rgb)  # Pillow 이미지
        draw = ImageDraw.Draw(pil_img)  # 드로잉 컨텍스트
        font = ImageFont.truetype(font_path, size=size)  # TrueType 로드
        rgb = (int(color[2]), int(color[1]), int(color[0]))  # BGR→RGB 변환
        draw.text(xy, text, fill=rgb, font=font)  # 텍스트 그리기
        return cv2.cvtColor(np.array(pil_img), cv2.COLOR_RGB2BGR)  # 다시 BGR로
    except Exception:
        # Pillow 실패 시 백업 경로: ASCII만 그리기
        import cv2
        safe_txt = text.encode("ascii", "ignore").decode()
        cv2.putText(bgr_img, safe_txt, xy, cv2.FONT_HERSHEY_SIMPLEX,
                    size/32.0, color, 2, cv2.LINE_AA)
        return bgr_img

# ================= 작은 원형 램프 =================
class Lamp(QWidget):
    def __init__(self, title: str, tooltip: str = "", diameter: int = 36):
        super().__init__()  # QWidget 초기화
        self._diameter = int(diameter)  # 원형 크기
        self.title = QLabel(title)  # 라벨 텍스트
        self.title.setStyleSheet("font-weight:700; color:#eaeef5; font-size:20px;")  # 스타일
        self.circle = QLabel()  # 색 원
        self.circle.setFixedSize(self._diameter, self._diameter)  # 고정 크기
        self.circle.setFrameShape(QFrame.NoFrame)  # 테두리 제거
        self.value = QLabel("")  # 상태값 텍스트
        self.value.setStyleSheet("color:#cfd6ff; font-size:18px;")  # 서브 컬러
        lay = QHBoxLayout(self); lay.setContentsMargins(12,10,12,10); lay.setSpacing(14)  # 수평 레이아웃
        lay.addWidget(self.title, 1)  # 좌측 제목
        lay.addWidget(self.value, 0)  # 가운데 값
        lay.addWidget(self.circle, 0, Qt.AlignRight)  # 우측 원
        if tooltip: self.setToolTip(tooltip)  # 툴팁
        self.set_unknown()  # 초기 상태: 미확정

    def _set_color(self, hex_color: str):
        r = self._diameter // 2  # 반지름
        self.circle.setStyleSheet(
            f"background-color:{hex_color}; border:2px solid #0b0e14; border-radius:{r}px;"  # 원형 렌더링
        )

    def set_on(self, text: str = ""):      self._set_color("#2ecc71"); self.value.setText(text)  # 녹색=OK
    def set_off(self, text: str = ""):     self._set_color("#e74c3c"); self.value.setText(text)  # 빨강=OFF/에러
    def set_warn(self, text: str = ""):    self._set_color("#f39c12"); self.value.setText(text)  # 주황=경고
    def set_unknown(self, text: str = ""): self._set_color("#7f8c8d"); self.value.setText(text)  # 회색=미확정

# ================= 좌측 하단 대형 라이트 =================
class BigLight(QWidget):
    def __init__(self, label="상태"):
        super().__init__()  # QWidget 초기화
        self.circle = QLabel()  # 대형 원형 표시
        self.text = QLabel(label)  # 상태 텍스트
        self.text.setAlignment(Qt.AlignCenter)  # 중앙 정렬
        self.text.setStyleSheet("color:#eaeef5; font-size:30px; font-weight:800;")  # 눈에 띄게
        self._size = 200  # 원형 크기
        self.circle.setFixedSize(self._size, self._size)  # 고정 크기
        self.circle.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)  # 레이아웃 영향 최소화
        lay = QVBoxLayout(self)  # 수직 레이아웃
        lay.setContentsMargins(0,0,0,0); lay.setSpacing(16)  # 간격
        lay.addWidget(self.circle, 0, Qt.AlignLeft)  # 원형
        lay.addWidget(self.text, 0, Qt.AlignLeft)  # 텍스트
        self.set_red("정비 인증 대기")  # 초기 상태: 안전 우선(빨강)

    def _paint(self, color):
        r = self._size // 2  # 반지름
        self.circle.setStyleSheet(
            f"background-color:{color}; border:8px solid #0a0c12; border-radius:{r}px;"  # 두꺼운 테두리
        )

    def set_green(self, txt="가동"):    self._paint("#2ecc71"); self.text.setText(txt)  # 가동
    def set_orange(self, txt="정지"):   self._paint("#f39c12"); self.text.setText(txt)  # 정지
    def set_red(self, txt="정비 인증"): self._paint("#e74c3c"); self.text.setText(txt)  # 인증/경고

# ================= YOLO 헬멧 인증 =================
class HelmetVerifyWorker(QThread):
    frame_ready = pyqtSignal(object); progress = pyqtSignal(float, int)  # 프레임/진행률 시그널
    finished_ok = pyqtSignal(); error = pyqtSignal(str)  # 완료/에러 시그널
    def __init__(self, model_path: str, dev=None, conf: float=0.25, required_sec: float=3.0, miss_tolerance: float=0.30):
        super().__init__(); self.model_path=model_path; self.dev=dev if dev is not None else _get_camera_device()
        self.conf=conf; self.required_sec=required_sec; self.miss_tolerance=miss_tolerance; self._run=True  # 런 플래그
    def stop(self): self._run=False  # 외부에서 정지 요청
    def run(self):
        try:
            import cv2
            from ultralytics import YOLO  # YOLOv8 로더
            model = YOLO(self.model_path)  # 모델 로드(가중치 경로)
            helmet_ids = {1}  # 클래스 ID(1=헬멧 가정)
            cam_arg = int(self.dev) if isinstance(self.dev, str) and self.dev.isdigit() else self.dev  # 디바이스 파싱
            cap = cv2.VideoCapture(cam_arg)  # 캡처 오픈
            if not cap.isOpened(): self.error.emit(f"카메라 열기 실패: {self.dev}"); return  # 하드 실패
            cap.set(cv2.CAP_PROP_FRAME_WIDTH,640); cap.set(cv2.CAP_PROP_FRAME_HEIGHT,480)  # 해상도
            try: cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))  # MJPG로 지연 감소 시도
            except Exception: pass
            try: cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # 버퍼 1장
            except Exception: pass
            streak_start=None; miss_since=None  # 연속 검출/미검출 타임스탬프
            while self._run:
                ok, frame = cap.read()  # 프레임 획득
                if not ok or frame is None: time.sleep(0.03); continue  # 실패 시 짧게 대기
                r = model.predict(source=frame, conf=self.conf, imgsz=640, verbose=False)  # 추론
                boxes = r[0].boxes  # 감지 결과
                found=False  # 헬멧 감지 플래그
                if boxes is not None and boxes.cls is not None and len(boxes)>0:
                    try: clses = boxes.cls.detach().cpu().numpy().astype(int).tolist()  # 안전 변환
                    except Exception: clses=[int(c) for c in boxes.cls]
                    found = any(c in helmet_ids for c in clses)  # 헬멧 여부
                now=time.time()  # 현재 시각
                if found:
                    if miss_since is not None and (now-miss_since)>self.miss_tolerance: streak_start=None  # 긴 미검출 후 재가동 시 누적 초기화
                    miss_since=None
                    if streak_start is None: streak_start=now  # 검출 시작 시간
                else:
                    if miss_since is None: miss_since=now  # 미검출 최초 시간
                    if (now-miss_since)>self.miss_tolerance: streak_start=None  # 미검출 지속 시 누적 초기화
                held_sec = 0.0 if streak_start is None else (now-streak_start)  # 유지 시간
                helmet_ok = 1 if held_sec>=self.required_sec else 0  # 인증 여부

                import cv2 as _cv2
                _bar_h = 44
                _cv2.rectangle(frame, (0, 0), (frame.shape[1], _bar_h), (0, 0, 0), -1)  # 상단 바
                msg = f"정비 헬멧 인증  |  유지 {held_sec:0.1f}s / 필요 {self.required_sec:.1f}s"  # 오버레이 텍스트
                bgr = (0, 200, 0) if helmet_ok else (255, 255, 255)  # 컬러
                frame = draw_korean_text_on_bgr(frame, msg, (14, 28), size=30, color=bgr)  # 한글 렌더링

                self.frame_ready.emit(frame); self.progress.emit(held_sec, helmet_ok)  # UI로 푸시
                if helmet_ok: self.finished_ok.emit(); break  # 인증 완료 시 종료
                time.sleep(0.01)  # CPU 양보
            try: cap.release()  # 자원 해제
            except Exception: pass
        except Exception as e:
            self.error.emit(str(e))  # 예외 전달

class HelmetVerifyDialog(QDialog):
    def __init__(self, parent=None, model_path: str=None, dev=None):
        super().__init__(parent)  # QDialog 초기화
        self.setWindowTitle("정비 인증 - 헬멧 카메라")  # 타이틀
        self.setModal(True)  # 모달
        self.setMinimumSize(900,700)  # 최소 사이즈
        self.setStyleSheet("""
            QDialog { background-color:#0f1115; }
            QLabel#Title { color:#eaeef5; font-size:28px; font-weight:800; }
            QLabel#Info  { color:#cfd6ff; font-size:20px; }
            QLabel#Video {
                background:#0a0c12; border:2px solid #2b3140; border-radius:14px;
            }
        """)  # 다크테마

        self.title = QLabel("정비 인증"); self.title.setObjectName("Title")  # 제목
        self.info  = QLabel("헬멧을 카메라에 연속 3초간 보여주세요."); self.info.setObjectName("Info")  # 안내
        self.lbl=QLabel("카메라 초기화 중…"); self.lbl.setObjectName("Video")  # 비디오 영역
        self.lbl.setAlignment(Qt.AlignCenter); self.lbl.setMinimumSize(860, 540)  # 정렬/크기

        lay = QVBoxLayout(self)  # 레이아웃
        lay.setContentsMargins(24,24,24,24); lay.setSpacing(16)  # 마진/스페이싱
        lay.addWidget(self.title, 0, Qt.AlignLeft)  # 타이틀 배치
        lay.addWidget(self.lbl, 1)  # 영상 배치
        lay.addWidget(self.info, 0, Qt.AlignLeft)  # 안내 배치

        self.worker=HelmetVerifyWorker(
            model_path=model_path or os.getenv("HELMET_MODEL", r"C:\Users\evita60\Downloads\best.pt" if IS_WINDOWS else "/home/aiot121/camera/models/best.pt"),
            dev=dev if dev is not None else _get_camera_device(),
            conf=float(os.getenv("CONF","0.25")),
            required_sec=float(os.getenv("REQUIRED_SEC","3.0")),
            miss_tolerance=float(os.getenv("MISS_TOLERANCE_SEC","0.30")),
        )  # 워커 초기화(환경변수로 튜닝)
        self.worker.frame_ready.connect(self.on_frame)  # 프레임 콜백
        self.worker.progress.connect(self.on_progress)  # 진행률 콜백
        self.worker.finished_ok.connect(self.on_finished_ok)  # 완료 콜백
        self.worker.error.connect(self.on_error)  # 에러 콜백
        self.worker.start()  # 백그라운드 스레드 시작

    def on_frame(self, frame):
        import cv2  # 지역 import(지연 로딩)
        rgb=cv2.cvtColor(frame, cv2.COLOR_BGR2RGB); h,w,ch=rgb.shape  # 색 변환/사이즈
        qimg=QImage(rgb.data,w,h,ch*w,QImage.Format_RGB888)  # QImage 래핑
        self.lbl.setPixmap(QPixmap.fromImage(qimg).scaled(self.lbl.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation))  # 스케일링 표시

    def on_progress(self, sec, ok_flag):
        self.info.setText(f"유지 {sec:.1f}s / 필요 3.0s  |  상태: {'OK' if ok_flag else '인증 중…'}")  # 상태 표시

    def on_finished_ok(self):
        big_msg("info","인증 완료","정비 인증이 완료되었습니다.", self); self.accept()  # 성공 팝업 후 닫기

    def on_error(self, msg):
        big_msg("crit","오류", f"헬멧 인증 중 오류가 발생했습니다.\n\n{msg}", self); self.reject()  # 에러 팝업 후 닫기

    def closeEvent(self, e):
        try:
            if self.worker and self.worker.isRunning():  # 워커 종료 시그널
                self.worker.stop(); self.worker.wait(500)
        except Exception: pass
        return super().closeEvent(e)  # 기본 핸들러 호출

# ================= 시리얼 리더 =================
class SerialReader(QThread):
    received = pyqtSignal(str); stopped = pyqtSignal()  # 수신/종료 시그널
    def __init__(self, ser: serial.Serial): super().__init__(); self.ser=ser; self._run=True  # 상태 플래그
    def run(self):
        while self._run:
            try:
                if self.ser and self.ser.in_waiting:  # 수신 버퍼에 데이터 있으면
                    line=self.ser.readline().decode(errors="ignore").strip()  # 라인 읽기
                    if line: self.received.emit(line)  # 시그널로 전달
                self.msleep(10)  # 폴링 간격
            except Exception: break  # 포트 오류 시 종료
        self.stopped.emit()  # 종료 시그널
    def stop(self): self._run=False  # 외부에서 정지

# ================= 메인 윈도우 =================
class Main(QWidget):
    def __init__(self):
        super().__init__()  # QWidget 초기화
        self.setWindowTitle("Factory Dashboard"); self.setMinimumSize(1280, 800)  # 타이틀/크기
        pal = QPalette()  # 팔레트 생성
        pal.setColor(QPalette.Window, QColor("#0f1115")); pal.setColor(QPalette.Base, QColor("#151820"))  # 배경
        pal.setColor(QPalette.AlternateBase, QColor("#1b1f2a")); pal.setColor(QPalette.Button, QColor("#1b1f2a"))  # 대체/버튼
        pal.setColor(QPalette.ButtonText, QColor("#eaeef5")); pal.setColor(QPalette.Text, QColor("#eaeef5"))  # 텍스트
        pal.setColor(QPalette.WindowText, QColor("#eaeef5"))  # 윈도 텍스트
        self.setPalette(pal)  # 팔레트 적용
        self.setStyleSheet("""
            QLabel { color:#eaeef5; }
            QPushButton {
                background-color:#1f2430; color:#eaeef5; border:1px solid #2b3140;
                border-radius:12px; padding:16px 20px; font-size:22px; font-weight:700;
            }
            QPushButton:hover { background-color:#283144; }
            QPushButton:pressed { background-color:#202637; }
            QGroupBox {
                background-color:#151a22; border:1px solid #2b3140; border-radius:12px;
                margin-top:8px; padding:14px 16px; font-size:22px; font-weight:800;
            }
        """)  # UI 일관 테마

        self.ser: serial.Serial = None  # 시리얼 핸들
        self.reader: SerialReader = None  # 리더 스레드

        # ===== 설정 =====
        self._con_hold_enabled   = bool(int(os.getenv("ENABLE_CON_HOLD", "0")))  # 컨베이어 홀드(옵션)
        self._rail_ctrl_enabled  = bool(int(os.getenv("RAIL_CTRL", "1")))        # 레일 제어 on/off
        self._rail_via_serial    = bool(int(os.getenv("RAIL_VIA_SERIAL", "0")))  # (예비) 시리얼로만 제어 여부
        self._rail_stop_delay_ms = int(os.getenv("RAIL_STOP_DELAY_MS", "200"))   # 정지 후 상태 전이 딜레이
        self._rail_start_delay_ms= int(os.getenv("RAIL_START_DELAY_MS","200"))   # 시작 후 상태 전이 딜레이
        self._serial_cmd_stop    = os.getenv("SERIAL_CMD_STOP",  "S")            # STOP 명령 문자
        self._serial_cmd_start   = os.getenv("SERIAL_CMD_START", "R")            # START 명령 문자

        # 앱 구동 시 conv_has_part 강제 초기화(기본 ON)
        self._reset_conv_on_boot = bool(int(os.getenv("RESET_CONV_ON_BOOT", "1")))  # 부팅 시 적재상태 초기화 여부

        # IR(내부 감지) 파라미터
        self._ir_active_level    = int(os.getenv("IR_ACTIVE_LEVEL", "0"))   # 0=active-low(기본), 1=active-high
        self._ir_edge            = os.getenv("IR_TRIGGER_EDGE", "rising").lower()  # 트리거 엣지

        # 물건 도착 센서 극성 — 기본 active-low(0=감지)
        self._arrival_active_level = int(os.getenv("ARRIVAL_ACTIVE_LEVEL", "0"))  # UI 표현 정합

        # RUN 정책
        self._send_run_each_cycle = bool(int(os.getenv("RUN_EACH_CYCLE", "0")))  # 사이클마다 run 재전송 여부
        self._auto_run_after_reset = bool(int(os.getenv("AUTO_RUN_AFTER_RESET", "0")))  # 홈 후 자동 가동

        # ===== 상태값 =====
        self.door_sensor = None  # 1=열림, 0=닫힘, None=미수신
        self.run = 0  # 라인 상태
        self.helmet_ok = 0  # 헬멧 인증 결과
        self._maintenance_done = 0  # 정비 완료 플래그
        self.obj_detected = 0  # IR 내부 감지 상태(정규화)
        self._presence_prev = 0  # 이전 감지 상태(엣지 검출용)
        self.con_sensor = None  # 외부 CON_SENSOR UI 표시값

        # ★ 컨베이어 적재 상태 (영구 저장)
        self._conv_has_part = 0  # 디스크로 persist

        # 로봇/송신 제어
        self._robot_start_sent = False  # run 요청을 로봇에 송신했는지
        self._initial_sent = False  # 메인 TCP 초기 펄스 송신 여부
        self._robot_busy = False  # 로봇 busy 상태(서버 이벤트로 갱신)
        self._robot_rxbuf = b""  # 로봇 TCP 수신 버퍼

        # ★ 도어 에러 라치
        self._fault_active = False  # 문 열림 라치

        # IR 라쳇 / 레일 FSM
        self._block_armed = True  # 트리거 가능(라쳇)
        self._wait_clear_after_done = False  # 완료 후 clear 대기
        self._rail_state = "stopped"      # moving | stopping | stopped | starting
        self._rail_hold_for_start = False  # START 단계 홀드

        # ★ CON_SENSOR 엣지 검출
        self._arrival_prev = 0  # 이전 도착 상태

        # ===== 지속 저장 파일 =====
        self._persist_path = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "factory_state.json")  # 상태 파일 경로
        self._load_persist()  # 파일에서 conv_has_part 로드

        if getattr(self, "_reset_conv_on_boot", False):
            self._reset_conv_has_part_on_app_start()  # 부팅 초기화 정책 반영

        # ====== UI ======
        root = QHBoxLayout(self); root.setContentsMargins(18,18,18,18); root.setSpacing(18)  # 루트 레이아웃
        left = QVBoxLayout(); left.setSpacing(18)  # 좌측 스택
        title = QLabel("FACTORY LINE CONTROL"); title.setFont(QFont("Segoe UI", 28, QFont.Bold))  # 타이틀
        left.addWidget(title)  # 배치

        btns = QHBoxLayout(); btns.setSpacing(16)  # 버튼 행
        self.btnOn=QPushButton("시작"); self.btnOff=QPushButton("정지")  # 시작/정지
        self.btnMaint=QPushButton("정비(인증)"); self.btnReset=QPushButton("리셋")  # 정비/리셋
        for b in (self.btnOn, self.btnOff, self.btnMaint, self.btnReset):
            b.setMinimumHeight(76); b.setMinimumWidth(190); btns.addWidget(b); b.setEnabled(False)  # 사이즈/초기 비활성
        left.addLayout(btns)  # 버튼 배치

        self.big_light = BigLight("상태")  # 대형 라이트
        left.addStretch(1); left.addWidget(self.big_light, 0, Qt.AlignLeft | Qt.AlignBottom)  # 하단 정렬

        self._fs_btn=QPushButton(); self._fs_btn.setShortcut("F11"); self._fs_btn.clicked.connect(self.toggle_fullscreen); self._fs_btn.setVisible(False)  # F11 토글
        self._esc_btn=QPushButton(); self._esc_btn.setShortcut("Escape"); self._esc_btn.clicked.connect(self.toggle_fullscreen); self._esc_btn.setVisible(False)  # ESC 토글
        left.addWidget(self._fs_btn); left.addWidget(self._esc_btn)  # 핫키용 숨김 버튼

        right_box = QGroupBox("상태")  # 상태 패널
        grid = QGridLayout(right_box); grid.setContentsMargins(16,16,16,16); grid.setHorizontalSpacing(18); grid.setVerticalSpacing(10)  # 그리드 레이아웃

        self.l_run      = Lamp("가동 상태")   # 라인 상태
        self.l_door     = Lamp("문 상태")     # 도어 센서
        self.l_busy     = Lamp("로봇 상태")   # 로봇 Busy/Idle
        self.l_convpart = Lamp("컨베이어 적재")  # conv_has_part
        self.l_consens  = Lamp("물건 도착 센서")  # CON_SENSOR
        self.l_helmet   = Lamp("헬멧 인증")   # 헬멧 인증
        self.l_tcp      = Lamp("메인 TCP")    # 메인 TCP 연결
        self.l_rtcp     = Lamp("로봇 TCP")    # 로봇 TCP 연결

        for i,lmp in enumerate([self.l_run,self.l_door,self.l_busy,self.l_convpart,self.l_consens,self.l_helmet,self.l_tcp,self.l_rtcp]):
            grid.addWidget(lmp, i, 0)  # 세로로 쌓기(1열)

        right = QVBoxLayout(); right.addWidget(right_box,1)  # 우측 레이아웃
        root.addLayout(left,2); root.addLayout(right,2)  # 좌/우 비율 동일

        # 버튼 핸들러 바인딩
        self.btnOn.clicked.connect(lambda: self.send_cmd('1'))  # 시작 명령 전송
        self.btnOff.clicked.connect(lambda: self.send_cmd('0'))  # 정지 명령 전송
        self.btnMaint.clicked.connect(self.on_maintenance)  # 정비 인증 다이얼로그
        self.btnReset.clicked.connect(self.on_reset)  # 리셋
        self.btnOn.setShortcut("Ctrl+1"); self.btnOff.setShortcut("Ctrl+0"); self.btnReset.setShortcut("Ctrl+R")  # 단축키

        # 메인 TCP
        self.tcp=QTcpSocket(self); self.tcp_host=os.getenv("TCP_HOST","192.168.0.11"); self.tcp_port=int(os.getenv("TCP_PORT","8888"))  # 호스트/포트
        self.tcp.connected.connect(self.on_tcp_connected); self.tcp.disconnected.connect(lambda: self.update_indicators())  # 연결/해제 콜백
        self.tcp.readyRead.connect(self.on_tcp_ready); self.tcp.errorOccurred.connect(lambda _: self.update_indicators())  # 수신/에러 시 상태 갱신

        # 로봇 TCP
        self.tcp_robot=QTcpSocket(self); self.robot_host=os.getenv("ROBOT_HOST","127.0.0.1"); self.robot_port=int(os.getenv("ROBOT_PORT","5000"))  # 로봇 서버
        self.tcp_robot.connected.connect(self.on_robot_connected); self.tcp_robot.disconnected.connect(lambda: self.update_indicators())  # 연결/해제
        self.tcp_robot.errorOccurred.connect(lambda _: self.update_indicators()); self.tcp_robot.readyRead.connect(self.on_robot_ready)  # 에러/수신

        # 자동 연결
        self.connect_serial(); self.connect_tcp(); self.connect_robot()  # 초기 연결 시도

        # 헬스체크
        self.timer=QTimer(self); self.timer.setInterval(1000); self.timer.timeout.connect(self.health_check); self.timer.start()  # 1초 주기 헬스체크

        # ===== FACT 전송 코얼레싱 / 레이트 리밋 =====
        self._fact_min_interval_ms = int(os.getenv("FACT_MIN_INTERVAL_MS","200"))  # 최소 간격(ms)
        self._fact_last = None               # 마지막 전송 payload
        self._fact_last_time = 0.0           # 마지막 전송 시각(monotonic)
        self._fact_pending = None            # 지연 전송 대기 payload
        self._fact_timer = QTimer(self)      # 지연 타이머
        self._fact_timer.setSingleShot(True)  # 단발성
        self._fact_timer.timeout.connect(self._flush_fact)  # 타임아웃 핸들러

        self.update_indicators(); self.showFullScreen()  # 초기 인디케이터/풀스크린

    # ===== 지속 저장 =====
    def _load_persist(self):
        try:
            if os.path.exists(self._persist_path):  # 파일 존재 시
                with open(self._persist_path, "r", encoding="utf-8") as f:
                    st = json.load(f)  # JSON 로드
                self._conv_has_part = int(st.get("conv_has_part", 0))  # 값 복원
        except Exception:
            self._conv_has_part = 0  # 실패시 기본 0

    def _save_persist(self):
        try:
            with open(self._persist_path, "w", encoding="utf-8") as f:
                json.dump({"conv_has_part": int(getattr(self, "_conv_has_part", 0))}, f, ensure_ascii=False)  # JSON 저장
        except Exception:
            pass  # 저장 실패 무시(운영 지속성 우선)

    # 부팅 시 conv_has_part 초기화
    def _reset_conv_has_part_on_app_start(self):
        try:
            self._conv_has_part = 0  # 초기값으로 강제
            self._save_persist()  # 저장
            self._enforce_rail_policy_by_conv()  # 정책 즉시 적용
            self._maybe_send_fact()  # FACT 브로드캐스트
        except Exception:
            pass

    # ===== 화면 토글 =====
    def toggle_fullscreen(self):
        if self.isFullScreen(): self.showMaximized()  # 전체화면 → 최대화
        else: self.showFullScreen()  # 최대화 → 전체화면

    # ---- Reset ----
    def on_reset(self):
        if self.door_sensor == 1:  # 문 열림이면 리셋 금지(정책)
            big_msg("warn","문 열림","문을 닫은 후에 리셋하세요.", self); return

        self._fault_active = False  # 라치 해제
        self._wait_clear_after_done = False  # 완료 후 clear 대기 해제
        self._block_armed = True  # 라쳇 재무장
        self._presence_prev = self.obj_detected  # 엣지 안전
        self._robot_busy = False  # 로봇 상태 초기화
        self._robot_start_sent = False  # run 송신 플래그 초기화
        self._rail_hold_for_start = False  # START 홀드 해제

        try: self.send_robot_json({"home": 1})  # 로봇 홈 명령(비동기)
        except Exception: pass

        self._update_buttons()  # 버튼 리프레시
        self.update_indicators()  # 램프 리프레시
        self._maybe_send_fact()  # 상태 전송

    # ---- 정비(헬멧 인증) ----
    def on_maintenance(self):
        if self.run != 0:  # 가동 중에는 차단
            big_msg("warn","실행 중","정비는 정지 상태에서만 가능합니다.", self); return
        dlg=HelmetVerifyDialog(self, model_path=os.getenv("HELMET_MODEL", r"C:\Users\evita60\Downloads\best.pt" if IS_WINDOWS else "/home/aiot121/camera/models/best.pt"), dev=_get_camera_device())
        if dlg.exec_()==QDialog.Accepted:  # 인증 성공 시
            self.helmet_ok=1; self._maintenance_done=1  # 플래그 세팅
            self._maybe_send_fact(); self.update_indicators()  # 상태 브로드캐스트

    # ---- 인디케이터 ----
    def update_indicators(self):
        main_connected  = self.tcp.state()==QTcpSocket.ConnectedState  # 메인 TCP 연결 여부
        robot_connected = self.tcp_robot.state()==QTcpSocket.ConnectedState  # 로봇 TCP 연결 여부

        self.l_run.set_on("가동") if self.run==1 else self.l_run.set_off("정지")  # 라인 상태

        if self.door_sensor is None: self.l_door.set_unknown("미확인")  # 도어 수신 전
        elif self.door_sensor==1:    self.l_door.set_off("열림")        # 열림=빨강
        else:                        self.l_door.set_on("닫힘")         # 닫힘=녹색

        self.l_busy.set_on("대기") if not self._robot_busy else self.l_busy.set_off("작업중")  # 로봇 상태

        conv_has = int(getattr(self, "_conv_has_part", 0))  # 적재 플래그
        self.l_convpart.set_on("있음") if conv_has==1 else self.l_convpart.set_off("없음")  # 표시

        if self.con_sensor is None:
            self.l_consens.set_unknown("-")  # 미수신
        else:
            arrived = 1 if int(self.con_sensor) == self._arrival_active_level else 0  # 정규화
            self.l_consens.set_on("감지") if arrived else self.l_consens.set_off("없음")  # 표시

        self.l_helmet.set_on("인증") if self.helmet_ok==1 else self.l_helmet.set_off("미인증")  # 헬멧
        self.l_tcp.set_on("연결") if main_connected else self.l_tcp.set_off("끊김")  # 메인 TCP
        self.l_rtcp.set_on("연결") if robot_connected else self.l_rtcp.set_off("끊김")  # 로봇 TCP

        if self.helmet_ok==1:   self.big_light.set_red("정비 인증")  # 인증 완료(빨강 유지)
        elif self.run==1:       self.big_light.set_green("가동")     # 가동(녹색)
        else:                   self.big_light.set_orange("정지")    # 정지(주황)

        self._update_buttons()  # 버튼 상태 업데이트

    def _update_buttons(self):
        can_start = (self.run == 0) and (not self._fault_active) and (self.door_sensor != 1)  # 시작 가능 조건
        self.btnOn.setEnabled(can_start)  # 시작 버튼
        self.btnOff.setEnabled(self.run == 1)  # 정지 버튼
        self.btnMaint.setEnabled(self.run == 0 and not self._fault_active)  # 정비 버튼
        self.btnReset.setEnabled(True)  # 리셋은 항상 가능

    # ---- 시리얼 ----
    def connect_serial(self):
        port = _get_serial_port()  # 포트 결정
        try:
            self.ser = serial.Serial(port, 9600, timeout=1)  # 포트 오픈(9600)
            time.sleep(1.2)  # 부트로더 타임아웃 고려(윈도우/아두이노 안정화)

            t0 = time.time()
            while time.time() - t0 < 1.0:  # 오픈 직후 1초간 부트 메시지 플러시
                try:
                    if self.ser.in_waiting:
                        line = self.ser.readline().decode(errors="ignore").strip()
                        if line: self.on_received(line)  # 초기값 흡수
                    else:
                        time.sleep(0.02)  # 짧은 휴식
                except Exception:
                    break  # 포트 이슈시 탈출

            self.reader = SerialReader(self.ser)  # 리더 스레드 생성
            self.reader.received.connect(self.on_received)  # 수신 콜백
            self.reader.stopped.connect(self.disconnect_serial)  # 종료 콜백
            self.reader.start()  # 스레드 시작

            for b in (self.btnOn, self.btnOff, self.btnMaint, self.btnReset):
                b.setEnabled(True)  # 연결 성공 시 버튼 활성화

            self._update_buttons()  # 버튼 상태 반영
            self.update_indicators()  # 램프 반영

        except serial.SerialException as e:
            big_msg("crit","연결 실패", f"{port}: {e}", self)  # 사용자 피드백
            self.update_indicators()

    def disconnect_serial(self):
        try:
            if self.reader: self.reader.stop(); self.reader.wait(300)  # 스레드 정지
        except Exception: pass
        self.reader=None
        try:
            if self.ser and self.ser.is_open: self.ser.close()  # 포트 닫기
        except Exception: pass
        self.ser=None
        for b in (self.btnOn,self.btnOff,self.btnMaint,self.btnReset): b.setEnabled(False)  # 버튼 비활성
        self.update_indicators()  # UI 갱신

    def send_cmd(self, ch: str):
        if not self.ser or not self.ser.is_open:  # 연결 확인
            big_msg("warn","미연결","아두이노가 연결되지 않았습니다.", self); return
        try:
            self.ser.write(ch.encode())  # 단문 명령 전송
        except serial.SerialException as e:
            big_msg("crit","전송 실패", str(e), self); return

        if ch=='1':  # 시작 요청
            if self._fault_active:
                big_msg("warn","시작 불가","오류 라치 상태입니다. '리셋'을 눌러 해제한 뒤 다시 시작하세요.", self); return
            if self.door_sensor == 1:
                big_msg("warn","시작 불가","문이 열려 있습니다. 문을 닫고 '리셋' 후 시작하세요.", self); return

            if self.helmet_ok or self._maintenance_done:  # 인증 플래그 정리
                self.helmet_ok=0; self._maintenance_done=0; self._maybe_send_fact()
            if self.run==0:
                self._pending_run_request=False  # 내부 플래그 초기화
                self._set_run(1, reason="by_cmd")  # 정책적 RUN 진입

        elif ch=='0':  # 정지 요청
            self._set_run(0, reason="by_cmd")

        self.update_indicators()  # UI 갱신

    # ---- 시리얼 수신 → 센서 파싱 ----
    def on_received(self, line: str):
        line=line.strip(); updated=False  # 공백제거/업데이트 플래그
        if line.startswith("DOOR_SENSOR="):  # 도어 이벤트
            val=line.split("=",1)[1].strip()
            if val in ("0","1"): self._set_door(int(val)); updated=True
        if "LED ON" in line:  self._set_run(1, reason="echo"); updated=True  # 호환 로그
        if "LED OFF" in line: self._set_run(0, reason="echo"); updated=True
        if line.startswith("STOP"): self._set_run(0, reason="safety"); updated=True  # 안전 정지
        if "RESUME" in line:
            self.btnOn.setEnabled(True); self.btnOff.setEnabled(True); self.btnMaint.setEnabled(self.run==0); updated=True  # UI 편의

        low=line.replace(" ","").lower()  # 키워드 통일
        if any(k in low for k in ("ir=1","obj=1","object=1","sensor=1","irdetected","objectdetected","detect=1")):
            self._set_obj_raw(1); updated=True  # 내부 IR 감지=1
        elif any(k in low for k in ("ir=0","obj=0","object=0","sensor=0","noobject","detect=0")):
            self._set_obj_raw(0); updated=True  # 내부 IR 비감지=0

        if line.startswith("CON_SENSOR="):  # 외부 CON 센서 값
            val=line.split("=",1)[1].strip()
            if val in ("0","1"): self._set_con(int(val)); updated=True

        if updated: self.update_indicators()  # 변경 시 UI 갱신

    # ---- 도어 처리(표시 + 라치) ----
    def _set_door(self, v: int):
        if v not in (0,1): return  # 가드
        self.door_sensor = v  # 상태 저장
        self._maybe_send_fact()  # FACT 전파

        if v == 1 and self.run == 1 and not self._fault_active:  # 문 열림 + 가동 중 + 미라치 → 라치 인입
            self._fault_active = True  # 라치 세트
            try:
                if self._rail_ctrl_enabled:
                    self.rail_cmd("stop", reason="door_open")  # 레일 정지
                    self._rail_state = "stopping"
                    QTimer.singleShot(self._rail_stop_delay_ms, lambda: self._set_rail_state("stopped"))  # 상태 전이
            except Exception:
                pass
            self.run = 0  # 라인 정지
            self._robot_start_sent = False  # 내부 플래그 리셋
            self._pending_run_request = False
            big_msg("crit","도어 열림 - 라인 정지","문이 열려 라인을 정지했습니다.\n문을 닫은 뒤 '리셋'으로 복구하세요.", self)  # 사용자 경고

        self._update_buttons()  # 버튼 갱신
        self.update_indicators()  # 램프 갱신
        self._maybe_send_fact()  # FACT 전파

    # ---- RUN ----
    def _set_run(self, v: int, reason: str = ""):
        if v not in (0, 1): return  # 입력 가드
        if getattr(self, "run", 0) == v: return  # 상태 동일 시 무시

        if v == 1:
            if self._fault_active:
                big_msg("warn","시작 불가","오류 라치 상태입니다. '리셋'으로 해제한 뒤 다시 시작하세요.", self); return
            if self.door_sensor == 1:
                big_msg("warn","시작 불가","문이 열려 있습니다. 문을 닫고 '리셋' 후 시작하세요.", self); return

        self.run = v  # 상태 반영

        if v == 1:  # 시작 시나리오
            has_part = int(getattr(self, "_conv_has_part", 0)) == 1  # 컨베이어에 파트 존재?
            if has_part:
                # START 스킵 모드: 바로 레일만 가동(로봇 시퀀스 생략)
                self._robot_start_sent = False
                self._robot_busy = False
                self._rail_hold_for_start = False
                QTimer.singleShot(0,   lambda: self._arduino_write(b"1"))  # RUN=1
                QTimer.singleShot(40,  lambda: self._arduino_write(b"U"))  # U(홀드 해제 + 그레이스)
                for dt in (120, 220, 340):
                    QTimer.singleShot(dt, lambda: self.rail_cmd("start", reason="start_skip_conv=1"))  # 레일 스타트 팁 반복
                self._enforce_rail_policy_by_conv()  # 정책 재적용
            else:
                # START 필요: 로봇이 올려놓기 먼저 수행
                self._rail_hold_for_start = True  # 레일 홀드
                if self._rail_ctrl_enabled:
                    self.rail_cmd("stop", reason="start_hold")  # 레일 확실히 스톱
                    self._rail_state = "stopped"
                try:
                    self.send_robot_json({"run": 1})  # 로봇 START 요청
                    self._robot_start_sent = True
                    self._robot_busy = True  # Busy 진입
                except Exception:
                    self._robot_start_sent = False  # 실패 시 플래그 클리어
        else:
            # 정지 시나리오
            self._pending_run_request = False
            self._robot_start_sent = False
            self._rail_hold_for_start = False
            if self._rail_ctrl_enabled:
                if self.rail_cmd("stop", reason="run_off"):
                    self._rail_state = "stopping"
                    QTimer.singleShot(self._rail_stop_delay_ms, lambda: self._set_rail_state("stopped"))

        self._maybe_send_fact()  # FACT 브로드캐스트
        self._update_buttons()   # 버튼 갱신

    # ---- 레일 제어 ----
    def rail_cmd(self, action: str, reason: str = "") -> bool:
        try:
            if self.ser and getattr(self.ser, "is_open", False):
                ch = self._serial_cmd_stop if action == "stop" else self._serial_cmd_start  # 문자 결정
                self.ser.write(ch.encode()); return True  # 시리얼로 송신
        except Exception:
            pass
        try:
            if getattr(self, "_rail_ctrl_enabled", False):
                self.send_tcp_json({"rail": action, "reason": f"{reason}|fallback_tcp"}); return True  # TCP 백업 경로
        except Exception:
            pass
        return False  # 실패 시 False

    def _set_rail_state(self, st: str):
        self._rail_state=st; self.update_indicators()  # 상태/램프 갱신

    def _maybe_start_rail(self, reason="policy"):
        if self._rail_hold_for_start: return  # START 홀드 중이면 금지
        if not self._rail_ctrl_enabled: return  # 레일 제어 비활성
        if self._rail_state in ("moving", "starting"): return  # 이미 동작 중
        if self.obj_detected != 0: return  # IR 감지 중이면 금지
        if int(getattr(self, "_conv_has_part", 0)) != 1: return  # 파트 없으면 금지
        ok = self.rail_cmd("start", reason=reason)  # 시작 시도
        if ok:
            self._rail_state = "starting"  # 상태 전이
            QTimer.singleShot(self._rail_start_delay_ms, lambda: self._set_rail_state("moving"))  # moving으로 전환

    def _enforce_rail_policy_by_conv(self):
        has_part = int(getattr(self, "_conv_has_part", 0)) == 1  # 파트 유무
        ir_clear = (self.obj_detected == 0)  # IR 클리어
        idle     = (not self._robot_busy)  # 로봇 idle
        if self._rail_ctrl_enabled:
            if has_part and ir_clear and idle:
                self._maybe_start_rail("conv_has_part=1_and_clear")  # 가동 조건 만족
            else:
                if self._rail_state not in ("stopping", "stopped"):
                    if self.rail_cmd("stop", reason="conv_empty_or_blocked_or_busy"):
                        self._rail_state = "stopping"
                        QTimer.singleShot(self._rail_stop_delay_ms, lambda: self._set_rail_state("stopped"))

    # ======= IR 처리 =======
    def _set_obj_raw(self, raw: int):
        if raw not in (0,1): return  # 가드
        presence = 1 if raw == self._ir_active_level else 0  # 정규화(활성 레벨 기준)
        prev_presence = self._presence_prev  # 이전 상태
        if self.obj_detected != presence:
            self.obj_detected = presence  # 상태 반영
            self._maybe_send_fact()  # FACT 전파

        if self._robot_busy:
            self._presence_prev = presence; return  # 로봇 동작 중이면 엣지 처리만 갱신하고 종료

        if self._wait_clear_after_done:
            if presence == 0:  # 클리어 시
                self._block_armed = True  # 라쳇 재무장
                self._wait_clear_after_done = False  # 대기 해제
                self._enforce_rail_policy_by_conv()  # 정책 재평가
            self._presence_prev = presence; return  # 현재 루프 종료

        rising  = (prev_presence == 0 and presence == 1)  # 상승 엣지
        falling = (prev_presence == 1 and presence == 0)  # 하강 엣지
        self._presence_prev = presence  # 이전값 갱신

        should_trigger = ((self._ir_edge == "rising" and rising) or (self._ir_edge == "falling" and falling))  # 트리거 엣지 판단
        if should_trigger and self._block_armed and self.run == 1:
            if self._rail_ctrl_enabled:
                self.rail_cmd("stop", reason=f"ir_{self._ir_edge}")  # 레일 정지
                self._rail_state = "stopping"
                QTimer.singleShot(self._rail_stop_delay_ms, lambda: self._set_rail_state("stopped"))
            self.send_robot_json({"con": 0, "pick": "conv"})  # 로봇 런 시퀀스 트리거
            self._robot_busy = True  # Busy 진입
            self._block_armed = False  # 라쳇 해제
            self._wait_clear_after_done = True  # clear 대기 모드

        if falling and not self._robot_busy and self._rail_ctrl_enabled:
            self._enforce_rail_policy_by_conv()  # 하강 엣지 후 정책 재평가

    # ======= CON 센서 처리(표시 + IR 미러 + 우회 트리거) =======
    def _set_con(self, v: int):
        if v not in (0,1): return  # 가드
        self.con_sensor = v  # UI용 저장

        arrived = 1 if int(v) == self._arrival_active_level else 0  # 1=감지(정규화)

        raw_for_ir = self._ir_active_level if arrived else (1 - self._ir_active_level)  # IR 정규화 변환
        self._set_obj_raw(raw_for_ir)  # 내부 IR 파이프라인에 주입

        prev = self._arrival_prev  # 이전 도착 상태
        self._arrival_prev = arrived  # 갱신
        rising  = (prev == 0 and arrived == 1)  # 상승 엣지
        falling = (prev == 1 and arrived == 0)  # 하강 엣지
        edge_ok = (self._ir_edge == "rising" and rising) or (self._ir_edge == "falling" and falling)  # 트리거 판단

        if edge_ok and self.run == 1 and not self._robot_busy and self._block_armed:
            try: self.send_tcp_json({"debug":"CON_DIRECT_TRIGGER", "edge":self._ir_edge, "arrived":int(arrived)})  # 디버그 이벤트
            except Exception: pass

            if self._rail_ctrl_enabled:
                self.rail_cmd("stop", reason="arrival_sensor")  # 레일 정지
                self._rail_state = "stopping"
                QTimer.singleShot(self._rail_stop_delay_ms, lambda: self._set_rail_state("stopped"))

            self.send_robot_json({"con": 0, "pick": "conv"})  # 로봇 런 트리거
            self._robot_busy = True  # Busy
            self._block_armed = False  # 라쳇 해제
            self._wait_clear_after_done = True  # clear 대기

        self._maybe_send_fact()  # FACT 전파

    # ---- 메인 TCP ----
    def connect_tcp(self):
        st=self.tcp.state()  # 현재 상태 조회
        if st in (QTcpSocket.ConnectingState, QTcpSocket.ConnectedState): return  # 중복 연결 방지
        self.tcp.connectToHost(self.tcp_host, self.tcp_port)  # 연결 시도

    def on_tcp_connected(self):
        if self._initial_sent: return  # 이미 보냈으면 무시
        QTimer.singleShot(20, self.send_initial_pulse)  # 초기 펄스 딜레이 전송
        self.update_indicators()  # 램프 갱신

    def send_initial_pulse(self):
        if self._initial_sent: return  # 재진입 방지
        if self.tcp.state()!=QTcpSocket.ConnectedState:
            QTimer.singleShot(200, self.send_initial_pulse); return  # 재시도
        try:
            payload=b"1\r\n"; self.tcp.write(payload); self.tcp.waitForBytesWritten(500); self._initial_sent=True  # 초기 신호
        except Exception:
            pass

    def on_tcp_ready(self):
        try:
            data_raw=bytes(self.tcp.readAll()); data=data_raw.decode("utf-8", errors="ignore").strip()  # 수신 버퍼 읽기
            if not data: return
            handled=False
            try:
                obj=json.loads(data)  # JSON 파싱 시도
                if isinstance(obj, dict) and "rail_status" in obj:  # 상태 키가 있으면
                    st=str(obj["rail_status"]).lower()
                    if "stop" in st: self._set_rail_state("stopped"); handled=True  # 정지
                    elif "mov" in st or "run" in st: self._set_rail_state("moving"); handled=True  # 진행
            except Exception:
                pass
            if not handled:
                up=data.upper()
                if "RAIL_STOPPED" in up: self._set_rail_state("stopped"); handled=True
                elif "RAIL_RUNNING" in up or "RAIL_MOVING" in up: self._set_rail_state("moving"); handled=True
            self.update_indicators()  # UI 갱신
        except Exception:
            self.update_indicators()  # 안전 경로

    # ---- TCP 공통 ----
    def send_tcp_json(self, obj: dict):
        line=json.dumps(obj, ensure_ascii=False, separators=(',',':')) + "\n"  # JSONL
        if self.tcp.state()!=QTcpSocket.ConnectedState:  # 비연결 시 연결 재시도
            self.connect_tcp(); return
        try:
            self.tcp.write(line.encode("utf-8")); self.tcp.waitForBytesWritten(500)  # 송신
        except Exception:
            pass

    # ---- 로봇 TCP ----
    def connect_robot(self):
        st=self.tcp_robot.state()  # 상태 확인
        if st in (QTcpSocket.ConnectingState, QTcpSocket.ConnectedState): return  # 중복 방지
        self.tcp_robot.connectToHost(self.robot_host, self.robot_port)  # 연결 시도
        self.tcp_robot.waitForConnected(1500)  # 동기 대기(1.5s)
        self.update_indicators()  # 램프 갱신

    def on_robot_connected(self): self.update_indicators()  # 연결시 램프 갱신

    def on_robot_ready(self):
        try:
            self._robot_rxbuf += bytes(self.tcp_robot.readAll())  # 버퍼 누적
            while b"\n" in self._robot_rxbuf:  # 라인 단위 파싱
                line, self._robot_rxbuf = self._robot_rxbuf.split(b"\n", 1)
                msg = line.decode("utf-8", errors="ignore").strip()
                if not msg: continue
                try: obj = json.loads(msg)  # JSON 시도
                except Exception: continue

                if "conv_has_part" in obj:  # 적재 상태 이벤트
                    try:
                        self._conv_has_part = 1 if int(obj["conv_has_part"]) == 1 else 0
                    except Exception:
                        self._conv_has_part = 1 if obj["conv_has_part"] else 0
                    self._save_persist()  # 디스크 저장
                    self._enforce_rail_policy_by_conv()  # 정책 재평가

                if "robot_busy" in obj:  # Busy 이벤트
                    prev = self._robot_busy
                    self._robot_busy = (
                        bool(int(obj["robot_busy"])) if isinstance(obj["robot_busy"], (int, str))
                        else bool(obj["robot_busy"])
                    )
                    if prev and not self._robot_busy:  # Busy→Idle 전이
                        self._arduino_write(b"U")  # U(홀드 해제 + 그레이스)
                        if self._wait_clear_after_done and self.obj_detected == 0:
                            self._block_armed = True  # 라쳇 재무장
                            self._wait_clear_after_done = False  # 대기 해제
                        self._enforce_rail_policy_by_conv()  # 레일 정책

                if obj.get("start_done", 0):  # START 완료 이벤트
                    QTimer.singleShot(0,   lambda: self._arduino_write(b"1"))  # RUN=1
                    QTimer.singleShot(40,  lambda: self._arduino_write(b"U"))  # U
                    for dt in (120, 220, 340):
                        QTimer.singleShot(dt, lambda: self.rail_cmd("start", reason="fallback_start_done"))  # 레일 재시동
                    self._enforce_rail_policy_by_conv()

                if obj.get("robot_homed", 0):  # 홈 완료 이벤트
                    if getattr(self, "_auto_run_after_reset", False) and self.run == 0:
                        self._set_run(1, reason="auto_after_reset")  # 자동 RUN

                self.update_indicators()  # UI 갱신
                self._maybe_send_fact()  # FACT 전파
        except Exception:
            self.update_indicators()  # 안전 경로

    def _ensure_robot_conn(self) -> bool:
        if self.tcp_robot.state()!=QTcpSocket.ConnectedState:  # 비연결 시
            self.connect_robot()  # 재연결
        return self.tcp_robot.state()==QTcpSocket.ConnectedState  # 상태 반환

    def _arduino_write(self, data: bytes):
        try:
            if self.ser and getattr(self.ser, "is_open", False):
                self.ser.write(data)  # 바이너리 직접 송신
        except Exception:
            pass

    def send_robot_json(self, obj: dict):
        if obj.get("con", None) == 0 and self._robot_busy:  # 이미 Busy면 con 트리거 차단
            return
        line=json.dumps(obj, ensure_ascii=False, separators=(',',':')) + "\n"  # JSONL
        if not self._ensure_robot_conn(): return  # 연결 보장
        try:
            self.tcp_robot.write(line.encode("utf-8")); self.tcp_robot.waitForBytesWritten(500)  # 송신
            if obj.get("con", 1) == 0 or obj.get("run", 0) == 1:
                self._robot_busy = True  # con/run 송신 시 Busy 마킹
            if obj.get("con", 1) == 0:
                self._block_armed = False  # 라쳇 해제
                self._wait_clear_after_done = True  # clear 대기
        except Exception:
            pass

    # ===== FACT 전송(4필드만 + 코얼레싱) =====
    def _build_fact_payload(self):
        if self.door_sensor is None:
            return None  # 도어 정보 없으면 보류
        return {
            "cmd": "FACTORY_DATA",  # 메시지 타입
            "run": int(self.run),  # 가동 상태
            "door": int(self.door_sensor),  # 도어
            "error": int(self._fault_active),  # 라치
            "helmet_ok": int(self.helmet_ok),  # 헬멧 인증
        }

    def _maybe_send_fact(self):
        payload = self._build_fact_payload()  # 페이로드 구성
        if payload is None:
            return
        # 동일 페이로드면 즉시 스킵(1차 중복 제거)
        if self._fact_last is not None and payload == self._fact_last:
            return

        now = time.monotonic()
        gap = (now - getattr(self, "_fact_last_time", 0.0))  # 지난 간격
        min_gap = self._fact_min_interval_ms / 1000.0  # 최소 간격

        # 레이트리밋 안이면 대기열로 전환
        if gap < min_gap:
            self._fact_pending = payload
            # 남은 시간만큼 타이머 셋
            remain_ms = max(1, int((min_gap - gap) * 1000))
            if not self._fact_timer.isActive():
                self._fact_timer.start(remain_ms)
            return

        # 즉시 전송 경로
        self.send_tcp_json(payload)
        self._fact_last = payload
        self._fact_last_time = now
        self._fact_pending = None
        if self._fact_timer.isActive():
            self._fact_timer.stop()

    def _flush_fact(self):
        if self._fact_pending is None:
            return
        payload = self._fact_pending
        # 전송 직전에도 이전과 같아졌으면 스킵
        if self._fact_last is not None and payload == self._fact_last:
            self._fact_pending = None
            return
        self.send_tcp_json(payload)
        self._fact_last = payload
        self._fact_last_time = time.monotonic()
        self._fact_pending = None

    def health_check(self):
        if self.ser:
            try: _ = self.ser.is_open  # 속성 접근으로 연결 상태 확인
            except Exception: self.disconnect_serial()  # 예외 시 정리
        self.update_indicators()  # 주기적 UI 리프레시
        self._maybe_send_fact()   # 주기적 FACT 평가

    def closeEvent(self, e):
        try:
            if self.reader and self.reader.isRunning(): self.reader.stop(); self.reader.wait(300)  # 스레드 정지
        except Exception: pass
        try:
            if self.ser and self.ser.is_open: self.ser.close()  # 시리얼 닫기
        except Exception: pass
        try:
            if self.tcp.state()==QTcpSocket.ConnectedState: self.tcp.disconnectFromHost()  # 메인 TCP 종료
        except Exception: pass
        try:
            if self.tcp_robot.state()==QTcpSocket.ConnectedState: self.tcp_robot.disconnectFromHost()  # 로봇 TCP 종료
        except Exception: pass
        return super().closeEvent(e)  # 기본 종료 처리

# ================= 앱 구동 =================
if __name__ == "__main__":
    app = QApplication(sys.argv)  # Qt 앱 인스턴스 생성
    w = Main()  # 메인 윈도우 생성(내부에서 showFullScreen 호출)
    sys.exit(app.exec_())  # 이벤트 루프 진입
