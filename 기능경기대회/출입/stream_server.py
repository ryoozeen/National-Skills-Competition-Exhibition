# stream_server.py
# - QR 우선 흐름 + PASS 후 자동 반복
# - 상단 오버레이: person / helmet 만 표기 (phase 텍스트 없음)
# - PASS 후 QR 복귀 시 상태를 FAIL로 즉시 초기화(팝업 재등장 방지)
# - [추가] QR 성공 시 gate_check(check_time, emp_id, name, department) 기록
# ↑ 파일 상단 주석: 이 서버의 동작 개요를 설명.
#   1) 기본은 QR 스캔 대기 상태에서 시작(QR_WAIT) → 스캔 성공 시 검출(DETECT)로 전환
#   2) 화면 상단에는 사람/헬멧 카운트만 표시
#   3) PASS가 일정 시간 유지되면 자동으로 QR 대기로 복귀하면서 상태를 FAIL로 리셋
#   4) QR 인증 성공 순간 gate_check 테이블에 출입 기록을 한 건 남김

import os, time, threading              # OS 경로/환경, 시간 처리, 멀티스레딩 유틸 표준 라이브러리 임포트
import cv2                              # OpenCV: 카메라 캡처, 영상 처리, 그리기, JPEG 인코딩 등
import numpy as np                      # 수치 연산 라이브러리: 좌표/박스 계산, 배열 연산에 사용
from dataclasses import dataclass       # 간단 데이터 컨테이너(불변/가독성) 정의용

from fastapi import FastAPI, Body, Response                       # FastAPI: 경량 ASGI 웹 프레임워크
from fastapi.responses import HTMLResponse, JSONResponse, PlainTextResponse  # 다양한 응답 타입(HTML/JSON/텍스트)
from starlette.responses import StreamingResponse                  # 스트리밍 응답(MJPEG) 제공용
from ultralytics import YOLO                                       # YOLOv8 모델 로더/추론기
from pyzbar.pyzbar import decode, ZBarSymbol                       # pyzbar: QR 코드 디코더
import uvicorn                                                     # uvicorn: ASGI 서버 실행기
import mysql.connector                                             # MySQL 파이썬 커넥터(동기식)

# -------------------- 기본값(환경변수 없을 때) --------------------
#CAMERA      = os.getenv("CAMERA", "/dev/video4")   # (비활성화된 중복 라인: 아래와 동일 기능)
CAMERA      = os.getenv("CAMERA", "/dev/video4")    # 카메라 선택(경로 혹은 인덱스). 기본값은 /dev/video4
MODEL_PATH  = os.getenv("HELMET_MODEL", "/home/jin/project/models/best.pt") # YOLO 가중치 파일 경로
CONF        = float(os.getenv("CONF", "0.35"))      # 추론 최소 신뢰도 임계값(낮을수록 검출 많아짐)
IOU_PASS    = float(os.getenv("IOU_PASS", "0.10"))  # 머리 ROI와 헬멧 박스의 IoU 임계값(착용 판정)
ROI_RATIO   = float(os.getenv("ROI_RATIO", "0.40")) # 사람 박스 상단 중 머리로 간주하는 비율(0.4 → 위 40%)
PASS_HOLD   = float(os.getenv("PASS_HOLD", "3.0"))  # PASS 상태 유지 시간(초). 이후 자동 복귀 트리거 일부

SMOOTH_ALPHA= float(os.getenv("SMOOTH_ALPHA", "0.70")) # 추적 보정 가중치(신규 박스에 가중치)
TRACK_IOU   = float(os.getenv("TRACK_IOU", "0.40"))    # IOU 기반 트랙-검출 매칭 임계값
TRACK_TTL   = int(os.getenv("TRACK_TTL", "8"))         # 트랙 미검출 허용 프레임 수(시간 초과 시 제거)

QR_SCAN_PERIOD = float(os.getenv("QR_SCAN_PERIOD", "0.15")) # QR 스캔 주기(과도한 디코딩 연산 방지)
QR_DRAW_COLOR  = (0,255,255)                                # QR 가이드/박스 색상(BGR: 노란색)

AUTORESET_ON_PASS = int(os.getenv("AUTORESET_ON_PASS", "1"))  # PASS 유지 후 자동 QR_WAIT 복귀 on/off(1/0)
RESET_AFTER_PASS  = float(os.getenv("RESET_AFTER_PASS", "2.0")) # PASS 유지 후 복귀까지 추가 지연(초)

DB_HOST = os.getenv("DB_HOST", "192.168.0.15")   # DB 호스트(기본값 주의: 실제 환경과 일치해야 접속 가능)
DB_PORT = int(os.getenv("DB_PORT", "3306"))      # DB 포트
DB_USER = os.getenv("DB_USER", "user1")          # DB 사용자명
DB_PASS = os.getenv("DB_PASS", "1234")           # DB 비밀번호(테스트 기본값 → 운영 시 비밀변수 처리 권장)
DB_NAME = os.getenv("DB_NAME", "safetydb")       # DB 스키마명

# --- [NEW] 성능/중복 제어 옵션 ---
QR_DEBOUNCE_SEC = float(os.getenv("QR_DEBOUNCE_SEC", "1.2"))  # 같은 QR 문자열의 재검출 무시 시간(디바운스)
INFER_IMGSZ     = int(os.getenv("INFER_IMGSZ", "480"))        # YOLO 입력 해상도(작을수록 빠름, 정확도는 하락)
STREAM_FPS      = float(os.getenv("STREAM_FPS", "12.0"))      # MJPEG 출력 프레임레이트 상한

# --- [NEW] 락 & 최근 QR 상태(중복 차단용) ---
qr_lock     = threading.Lock()   # QR 처리 임계영역 보호용 락(다중 스레드 경쟁 방지)
infer_lock  = threading.Lock()   # YOLO 추론 단일화 락(동시 추론 방지로 자원 보호)
_last_qr_text = None             # 최근 처리한 QR 텍스트(디바운스 비교)
_last_qr_at   = 0.0              # 최근 QR 처리 시각(epoch 초)
# ---------------------------------------------------------------

app = FastAPI()                  # FastAPI 앱 인스턴스 생성(라우팅/수명주기 관리)

cap = None                       # 전역 카메라 핸들(초기화 시 VideoCapture 할당)
opened_as = "unknown"            # 카메라 오픈 방식 표시 문자열(path:/dev/videoX or index:N)
cap_lock = threading.Lock()      # 프레임 read() 임계영역 보호용 락(스레드 안전)

model = None                     # YOLO 모델 핸들(시작 시 로드)
model_names = {}                 # 클래스 id → 이름 매핑 딕셔너리
person_id = None                 # person 클래스 id 저장용
helmet_id = None                 # helmet(혹은 유사명) 클래스 id 저장용

PHASE = "QR_WAIT"               # 현재 단계: QR 대기("QR_WAIT") 또는 검출("DETECT")
current_worker = None           # 현재 인증된 근로자 정보(dict: emp_id, name, department 등)
last_qr_scan_ts = 0.0           # 마지막 QR 스캔 시각(스캔 주기 관리)
last_qr_event_id = 0            # QR 이벤트 증가 id(순번 추적용)
last_qr_event = None            # 최근 QR 이벤트 정보(성공/실패, 텍스트)

state_now   = "FAIL"            # 현재 착용 판정 상태("PASS"/"FAIL"). 기본 FAIL로 시작
state_since = time.time()       # 해당 상태로 진입한 시점(epoch). 지속시간 계산에 사용
last_counts = {"person": 0, "helmet_pass": 0}  # 상단 표시용 카운터(사람 수, 헬멧 착용 수)

last_raw_frame = None           # 최근 원본 프레임 보관(스냅샷/등록 API에서 활용)

# -------------------- DB 공통 --------------------
def db_connect():
    """ MySQL 연결을 생성해 반환. 호출마다 새 커넥션(open) → with 문으로 자동 close.
    환경변수 기반의 호스트/포트/계정/스키마 사용. 예외 발생 시 상위로 전파. """
    return mysql.connector.connect(
        host=DB_HOST, port=DB_PORT, user=DB_USER, password=DB_PASS, database=DB_NAME
    )

def get_worker_by_qr(qr_text: str):
    """ QR 문자열로 employee 테이블에서 활성(status=1) 사원을 조회해 dict로 반환. 없으면 None.
    컬럼: emp_id, name, department, phone, position """
    sql = """
    SELECT emp_id, name, department, phone, position
      FROM employee
     WHERE qr_code=%s AND status=1
    """
    with db_connect() as con:                  # 커넥션 컨텍스트(자동 정리)
        cur = con.cursor()                     # 커서 획득
        cur.execute(sql, (qr_text,))           # 파라미터 바인딩으로 SQL 인젝션 방지
        row = cur.fetchone()                   # 단일 행만 취득
    if not row:                                # 결과 없으면 None 리턴
        return None
    emp_id, name, dept, phone, position = row  # 컬럼 언패킹
    return {                                   # 내부 사용을 위한 dict 변환
        "emp_id": emp_id,
        "name": name,
        "department": dept,
        "phone": phone,
        "position": position or ""
    }

def set_qr_to_emp(emp_id: int, qr_text: str):
    """ 특정 사원(emp_id)의 qr_code를 주어진 문자열로 갱신. 1건 갱신 시 True. """
    sql = "UPDATE employee SET qr_code=%s WHERE emp_id=%s"
    with db_connect() as con:
        cur = con.cursor()
        cur.execute(sql, (qr_text, emp_id))    # 안전한 파라미터 바인딩
        con.commit()                           # 변경사항 커밋
        return cur.rowcount == 1               # 정확히 1건 갱신되면 True

# [추가] QR 성공 시 게이트 기록
def log_gate_check(worker: dict):
    """
    gate_check(check_time, emp_id, name, department)에 1건 추가
    - check_time: NOW()
    - emp_id / name / department: employee 조회값 그대로 입력
    DB 에러는 서비스 중단을 막기 위해 캣치 후 False 반환.
    """
    if not worker:
        return False                           # 방어적 코딩: None 입력 시 무시
    sql = """
    INSERT INTO gate_check (check_time, emp_id, name, department)
    VALUES (NOW(), %s, %s, %s)
    """
    try:
        with db_connect() as con:
            cur = con.cursor()
            cur.execute(sql, (worker["emp_id"], worker["name"], worker["department"]))
            con.commit()
        return True
    except mysql.connector.Error as e:
        print(f"[gate_check] insert error: {e}")  # 오류 로깅만 하고 서비스 지속
        return False

# -------------------- 유틸 --------------------
def annotate_text(img, text, org, scale=0.8, color=(255,255,255)):
    """ 문자열을 두 번 그려 외곽선 효과(가독성) 부여: 검은색 두껍게 → 지정색 얇게 """
    cv2.putText(img, text, org, cv2.FONT_HERSHEY_SIMPLEX, scale, (0,0,0), 3, cv2.LINE_AA)
    cv2.putText(img, text, org, cv2.FONT_HERSHEY_SIMPLEX, scale, color, 2, cv2.LINE_AA)

def head_roi(box, ratio=0.4):
    """ 사람 박스 상단부 중 머리로 간주하는 ROI(x1,y1,x2,y2) 계산. ratio=0.4 → 위 40%. """
    x1,y1,x2,y2 = map(int, box)
    h = y2 - y1
    return (x1, y1, x2, y1 + int(h * ratio))

def iou(a,b):
    """ 두 박스 a,b의 IoU(교집합/합집합) 계산. 겹침 없으면 0.0. """
    ax1,ay1,ax2,ay2 = a; bx1,by1,bx2,by2 = b
    ix1,iy1 = max(ax1,bx1), max(ay1,by1)
    ix2,iy2 = min(ax2,bx2), min(ay2,by2)
    iw, ih  = max(0, ix2-ix1), max(0, iy2-iy1)
    inter = iw * ih
    if inter <= 0: return 0.0
    ua = (ax2-ax1)*(ay2-ay1) + (bx2-bx1)*(by2-by1) - inter
    return inter/ua if ua>0 else 0.0

@dataclass
class _Track:
    id:int; cls:str; conf:float; box:np.ndarray; miss:int=0  # 간단 추적 항목 구조체


def _iou(a: np.ndarray, b: np.ndarray) -> float:
    """ float 좌표 배열 간 IoU. 1e-6 보정으로 0-나누기 방지. 추적 내부용. """
    xx1=max(a[0],b[0]); yy1=max(a[1],b[1])
    xx2=min(a[2],b[2]); yy2=min(a[3],b[3])
    w=max(0.0, xx2-xx1); h=max(0.0, yy2-yy1)
    inter=w*h
    if inter<=0: return 0.0
    area_a=(a[2]-a[0])*(a[3]-a[1]); area_b=(b[2]-b[0])*(b[3]-b[1])
    return inter/(area_a+area_b-inter+1e-6)

class SimpleTracker:
    """ IOU 기반의 초간단 온라인 추적기.
    - 프레임 간 동일 객체 연결
    - 지수평활로 박스 흔들림 완화
    - miss>ttl 이면 트랙 제거 """
    def __init__(self, alpha=0.7, iou_thr=0.4, ttl=8):
        self.alpha=alpha; self.iou_thr=iou_thr; self.ttl=ttl
        self.tracks=[]; self._next_id=1
    def update(self, dets):
        # dets: [(cls_name, conf, (x1,y1,x2,y2)), ...]
        dets=[d for d in dets if d[0] in {"person","helmet"}]  # 관심 클래스만 유지
        boxes=[np.array(d[2],dtype=float) for d in dets]          # 좌표 float 배열화
        for t in self.tracks: t.miss+=1                           # 모든 트랙 miss 증가
        used=set()                                                # 매칭에 사용된 detection 인덱스 기록
        for t in self.tracks:
            best_j=-1; best_i=0.0
            for j,b in enumerate(boxes):
                if j in used: continue
                if dets[j][0]!=t.cls: continue                   # 클래스 불일치 제외
                i=_iou(t.box,b)
                if i>best_i: best_i=i; best_j=j
            if best_j>=0 and best_i>=self.iou_thr:
                new_box=boxes[best_j]
                t.box=self.alpha*new_box+(1.0-self.alpha)*t.box   # 지수평활로 박스 보정
                t.conf=max(t.conf*0.8, dets[best_j][1])           # conf 갱신(완만히 감소)
                t.miss=0
                used.add(best_j)
        for j,d in enumerate(dets):
            if j in used: continue
            cls,cf,box=d
            self.tracks.append(_Track(self._next_id, cls, float(cf), np.array(box)))  # 신규 트랙 생성
            self._next_id+=1
        self.tracks=[t for t in self.tracks if t.miss<=self.ttl]  # TTL 초과 제거
        return [(t.cls, float(t.conf), tuple(t.box.tolist())) for t in self.tracks]  # 외부로 간단 표현

_tracker = None  # 전역 추적기 핸들


def open_exact(sel: str):
    """ sel 이 '/dev/videoX'면 경로 열기, 숫자면 인덱스로 열기. (cv2.VideoCapture) """
    if str(sel).startswith("/dev/video"):
        c=cv2.VideoCapture(str(sel)); return c,f"path:{sel}"
    else:
        idx=int(str(sel)); c=cv2.VideoCapture(idx); return c,f"index:{idx}"


def pick_nearest(person_boxes):
    """ 사람 박스 중 가장 키가 큰(=높이가 큰) 박스를 선택 → 카메라에 가장 가까운 사람 가정. """
    if not person_boxes: return None
    return max(person_boxes, key=lambda b:(b[3]-b[1]))


def draw_qr_boxes(img, decoded):
    """ pyzbar decode 결과의 폴리곤/rect를 그려주는 디버그 유틸. """
    for q in decoded:
        pts=q.polygon
        if len(pts)>=4:
            pts=np.array([[p.x,p.y] for p in pts], np.int32)
            cv2.polylines(img, [pts], True, QR_DRAW_COLOR, 3)
        (x,y,w,h)=q.rect
        cv2.rectangle(img,(x,y),(x+w,y+h),QR_DRAW_COLOR,2)

# -------------------- 라이프사이클 --------------------
@app.on_event("startup")
def startup():
    """ 서버 시작 시 1회 실행: 모델/카메라/추적기 초기화. 초기 파라미터 세팅. """
    global model, model_names, person_id, helmet_id, cap, opened_as, _tracker
    cv2.setNumThreads(1)                           # OpenCV 내부 스레드 수 제한(스케줄링 오버헤드 감소)
    model = YOLO(MODEL_PATH)                       # YOLO 가중치 로드
    model_names = {int(k):str(v).lower() for k,v in model.names.items()}  # 클래스 id→이름(소문자)
    person_id = next((k for k,v in model_names.items() if v=="person"), None)  # person id 추출
    helmet_id = next((k for k,v in model_names.items()                       # helmet 유사명 대응
                      if v in {"helmet","hardhat","hard-hat","hard_hat","safety helmet","safety-helmet"}), None)
    cap, opened_as = open_exact(CAMERA)            # 카메라 열기(경로/인덱스 지원)
    if not cap or not cap.isOpened():              # 오픈 실패 시 즉시 예외
        raise RuntimeError(f"[ERR] camera open failed: {CAMERA}")
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)         # 너비 640 설정
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT,480)         # 높이 480 설정
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))  # MJPG 포맷 요청(USB 웹캠 가속)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)            # 버퍼 최소화(지연 축소)
    _tracker = SimpleTracker(alpha=SMOOTH_ALPHA, iou_thr=TRACK_IOU, ttl=TRACK_TTL)  # 추적기 준비
    print(f"[cam] opened: {opened_as}")             # 디버그 출력

@app.on_event("shutdown")
def shutdown():
    """ 서버 종료 시 카메라 자원 반납. 예외는 무시(안전 종료). """
    try:
        if cap and cap.isOpened(): cap.release()
    except Exception: pass

# -------------------- 메인 렌더 --------------------
def render_frame():
    """ 한 프레임을 캡처→오버레이/추론/판정→JPEG 인코딩하여 bytes 반환. 실패 시 None. """
    global _tracker, last_raw_frame, state_now, state_since, last_counts, PHASE, current_worker, last_qr_event

    with cap_lock:                                 # 스레드 안전한 캡처
        ok, frame = cap.read()
    if not ok or frame is None:                    # 캡처 실패 시 None 반환(상위에서 건너뜀)
        return None
    last_raw_frame = frame.copy()                   # 스냅샷 용으로 원본 보존
    H, W = frame.shape[:2]                          # 높이/너비 파생

    # 상단 바(phase 텍스트 없음) + 카운터 텍스트
    cv2.rectangle(frame,(0,0),(W,30),(0,0,0),-1)    # 상단 검은 배경 바
    annotate_text(frame, f"person: {last_counts['person']}", (10,22))          # 사람 수 표시
    annotate_text(frame, f"helmet: {last_counts['helmet_pass']}", (190,22))    # 헬멧 착용 수 표시

    if PHASE == "QR_WAIT":                         # QR 대기 단계
        tl = (int(W*0.15), int(H*0.15))             # 가이드 박스 좌상단 (여백 15%)
        br = (int(W*0.85), int(H*0.85))             # 가이드 박스 우하단 (여백 15%)
        cv2.rectangle(frame, tl, br, QR_DRAW_COLOR, 2)  # 노란 가이드 박스 그리기

        # QR 스캔 (ROI 한정 + 스캔 주기 + 락 + 디바운스)
        global last_qr_scan_ts, _last_qr_text, _last_qr_at
        now = time.time()
        if now - last_qr_scan_ts >= QR_SCAN_PERIOD: # 너무 자주 디코딩하지 않도록 간격 두기
            last_qr_scan_ts = now

            tl = (int(W*0.15), int(H*0.15))         # ROI 좌상단(동일 계산 재사용)
            br = (int(W*0.85), int(H*0.85))         # ROI 우하단
            roi = last_raw_frame[tl[1]:br[1], tl[0]:br[0]]  # ROI만 잘라 비용 절감
            gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)    # 그레이 변환(pyZbar 권장)

            with qr_lock:                            # PHASE 경쟁상태 방지
                if PHASE == "QR_WAIT":              # 다른 스레드가 바꾸지 않았는지 재확인
                    decoded = decode(gray, symbols=[ZBarSymbol.QRCODE])  # QR 디코딩
                    if decoded:                      # 하나 이상 감지되었으면
                        qr_text = decoded[0].data.decode("utf-8", errors="ignore").strip()  # 첫 QR 사용
                        if not (_last_qr_text == qr_text and (now - _last_qr_at) < QR_DEBOUNCE_SEC):
                            _last_qr_text = qr_text   # 최근 QR 문자열 갱신
                            _last_qr_at   = now       # 최근 처리 시각 갱신
                            handle_qr_detected(qr_text)  # QR 처리 로직 호출

        last_counts = {"person": 0, "helmet_pass": 0}    # 대기 중엔 0으로 표시 유지

    else:                                           # PHASE == 'DETECT': 착용 검출 단계
        with infer_lock:                            # YOLO 추론 임계영역
            r = model.predict(source=last_raw_frame, conf=CONF, imgsz=INFER_IMGSZ, verbose=False)
        dets=[]                                     # (cls_name, conf, (x1,y1,x2,y2)) 리스트
        for b in r[0].boxes:                        # YOLO 결과 순회
            cls=int(b.cls[0])
            if cls not in (person_id, helmet_id): continue  # 관심 클래스만 유지
            x1,y1,x2,y2 = map(float, b.xyxy[0].tolist())    # float 좌표 추출
            name = "person" if cls==person_id else "helmet" # 클래스명 매핑
            dets.append((name, float(b.conf[0]), (x1,y1,x2,y2)))
        dets = _tracker.update(dets)                # 추적 보정(박스 흔들림 감소)

        person_boxes=[tuple(map(int,d[2])) for d in dets if d[0]=="person"]  # 사람 박스 목록
        helmet_boxes=[tuple(map(int,d[2])) for d in dets if d[0]=="helmet"]  # 헬멧 박스 목록

        target = pick_nearest(person_boxes)         # 가장 큰(가까운) 사람 선택
        person_cnt = 1 if target else 0             # 타깃 존재 여부로 0/1 설정
        helmet_pass = 0                             # 기본 미착용
        if target:
            x1,y1,x2,y2 = target
            cv2.rectangle(frame,(x1,y1),(x2,y2),(255,128,0),2)  # 사람 박스 오렌지
            hx1,hy1,hx2,hy2 = head_roi(target, ratio=ROI_RATIO) # 머리 ROI 계산
            cv2.rectangle(frame,(hx1,hy1),(hx2,hy2),(200,200,200),1)  # 머리 ROI 박스
            has = any(iou((hx1,hy1,hx2,hy2), hb)>=IOU_PASS for hb in helmet_boxes)  # 착용 판정
            helmet_pass = 1 if has else 0
            label="PASS" if has else "FAIL"            # 라벨 선택
            color=(0,200,0) if has else (0,0,255)      # PASS=녹색/FAIL=빨강
            cv2.putText(frame, label,(x1,max(0,y1-8)), cv2.FONT_HERSHEY_SIMPLEX,0.9,color,2,cv2.LINE_AA)
            if has:
                hb = max(helmet_boxes, key=lambda b: iou((hx1,hy1,hx2,hy2), b))  # 최고 IoU 헬멧 박스
                cv2.rectangle(frame,(hb[0],hb[1]),(hb[2],hb[3]),(0,200,0),3)     # 두껍게 강조

        annotate_text(frame, f"CAM={CAMERA}  OPENED={opened_as}", (10, H-10), 0.55, (255,255,0))  # 디버그 표기

        new_state = "PASS" if (person_cnt==1 and helmet_pass==1) else "FAIL"  # 현재 상태
        if new_state != state_now:                     # 상태 전환 시각 갱신
            state_now = new_state
            state_since = time.time()
        last_counts = {"person": person_cnt, "helmet_pass": helmet_pass}  # 상단 카운트 갱신

        if AUTORESET_ON_PASS and state_now=="PASS":   # 자동 복귀 조건 검사
            if (time.time()-state_since) >= (PASS_HOLD + RESET_AFTER_PASS):
                PHASE = "QR_WAIT"                      # QR 대기로 전환
                current_worker = None                  # 현재 작업자 정보 해제
                _tracker = SimpleTracker(alpha=SMOOTH_ALPHA, iou_thr=TRACK_IOU, ttl=TRACK_TTL)  # 추적 리셋
                state_now = "FAIL"                     # 팝업 재등장 방지 위해 즉시 FAIL 초기화
                state_since = time.time()
                last_counts = {"person": 0, "helmet_pass": 0}
                last_qr_event = None                   # 최근 QR 이벤트 초기화

    ok, jpg = cv2.imencode(".jpg", frame, [int(cv2.IMWRITE_JPEG_QUALITY), 65])  # JPEG 인코딩(품질65)
    return jpg.tobytes() if ok else None               # 성공 시 bytes, 실패 시 None 반환

# -------------------- QR 처리 --------------------
def handle_qr_detected(qr_text: str):
    """
    QR 문자열 처리:
    - 유효 사원이면 current_worker 설정, PHASE=DETECT 전환, 상태 FAIL로 초기화, gate_check 즉시 로그 1건.
    - 유효하지 않으면 실패 이벤트만 기록.
    디바운스는 호출 전 단계에서 이미 처리됨.
    """
    global PHASE, current_worker, last_qr_event, last_qr_event_id, state_now, state_since
    worker = get_worker_by_qr(qr_text)       # DB 조회
    last_qr_event_id += 1                    # 이벤트 순번 증가
    if worker:
        current_worker = worker              # 현재 작업자 정보 저장
        PHASE = "DETECT"                     # 검출 단계 진입
        state_now = "FAIL"                   # 착용 미확정으로 초기화
        state_since = time.time()
        last_qr_event = {"type":"success","id":last_qr_event_id,"qr":qr_text}  # 성공 이벤트 기록
        _ = log_gate_check(worker)           # 출입 로그 1건 기록(실패해도 무시)
    else:
        last_qr_event = {"type":"fail","id":last_qr_event_id,"qr":qr_text}     # 실패 이벤트 기록

# -------------------- HTTP --------------------
@app.get("/", response_class=HTMLResponse)
def index():
    """ 간단 테스트 페이지: 전체폭으로 /mjpeg 스트림 표시 """
    return '<html><body style="margin:0;background:#000;">\
<img src="/mjpeg" style="width:100vw;height:auto;display:block;"/>\
</body></html>'

@app.get("/mjpeg")
def mjpeg():
    """ 실시간 MJPEG 스트림 엔드포인트. multipart/x-mixed-replace 사용. """
    def gen():
        target_fps = STREAM_FPS               # 출력 FPS 상한
        period = 1.0/target_fps; last = time.time()
        while True:
            jpg = render_frame()              # 프레임 생성(JPEG)
            if jpg is None:
                time.sleep(0.01); continue   # 프레임 실패 시 잠시 대기
            now=time.time(); sleep=period-(now-last)
            if sleep>0: time.sleep(sleep)     # FPS 유지
            last=time.time()
            yield (b"--frame\r\nContent-Type: image/jpeg\r\n\r\n"+jpg+b"\r\n")  # 파트 경계와 함께 전송
    return StreamingResponse(gen(), media_type="multipart/x-mixed-replace; boundary=frame")

@app.get("/frame.jpg")
def frame_jpg():
    """ 단일 스냅샷 JPEG 반환(모니터링/디버그). 프레임 없으면 503. """
    jpg = render_frame()
    if jpg is None: return Response(status_code=503)
    return Response(jpg, media_type="image/jpeg")

@app.get("/health", response_class=PlainTextResponse)
def health():
    """ 헬스체크: 카메라 오픈 여부와 오픈 방식 문자열 표시. """
    st = "ok" if (cap and cap.isOpened()) else "camera-not-open"
    return f"{st}, cam={opened_as}"

@app.get("/status")
def status():
    """ 현재 상태를 JSON으로 제공: PASS/FAIL, 안정화 시간, 카운트, 단계, 작업자, 최근 QR 이벤트. """
    stable_secs = time.time()-state_since
    out = {
        "state": state_now,
        "stable_secs": round(stable_secs,2),
        "pass_for_3s": (state_now=="PASS" and stable_secs>=PASS_HOLD),
        "person": last_counts["person"],
        "helmet_pass": last_counts["helmet_pass"],
        "phase": PHASE,
        "worker": current_worker
    }
    if last_qr_event:
        out["qr_event"]   = last_qr_event["type"]
        out["qr_event_id"]= last_qr_event["id"]
        out["qr_text"]    = last_qr_event.get("qr")
    return JSONResponse(out)

@app.post("/reset_phase")
def reset_phase():
    """ 강제 리셋: QR_WAIT로 전환, 추적기/상태/이벤트 초기화. """
    global PHASE, current_worker, state_now, state_since, last_counts, last_qr_event, _tracker
    PHASE="QR_WAIT"; current_worker=None
    _tracker = SimpleTracker(alpha=SMOOTH_ALPHA, iou_thr=TRACK_IOU, ttl=TRACK_TTL)
    state_now="FAIL"; state_since=time.time()
    last_counts={"person":0,"helmet_pass":0}
    last_qr_event=None
    return {"ok":True,"phase":PHASE}

@app.post("/register_qr")
def register_qr(payload: dict = Body(...)):
    """ QR 문자열을 직접 지정해 특정 사원에 등록. 유효성 검사 후 DB 갱신. """
    emp_id  = int(payload.get("emp_id",0))
    qr_code = str(payload.get("qr_code","" )).strip()
    if not emp_id or not qr_code:
        return JSONResponse({"ok":False,"msg":"emp_id, qr_code required"}, status_code=400)
    if not emp_id or not qr_code:
        return JSONResponse({"ok":False,"msg":"emp_id, qr_code required"}, status_code=400)
    try:
        ok = set_qr_to_emp(emp_id, qr_code)
        return {"ok":ok, "emp_id":emp_id, "qr_code":qr_code}
    except mysql.connector.Error as e:
        return JSONResponse({"ok":False,"msg":str(e)}, status_code=500)

@app.post("/register_qr_from_frame")
def register_qr_from_frame(payload: dict = Body(...)):
    """ 현재 카메라 프레임에서 QR을 읽어 emp_id에게 등록. 프레임 없거나 QR 없으면 오류 메시지. """
    emp_id = int(payload.get("emp_id",0))
    if not emp_id: return JSONResponse({"ok":False,"msg":"emp_id required"}, status_code=400)
    if last_raw_frame is None: return {"ok":False,"msg":"no frame yet"}
    gray = cv2.cvtColor(last_raw_frame, cv2.COLOR_BGR2GRAY)
    decoded = decode(gray, symbols=[ZBarSymbol.QRCODE])
    if not decoded: return {"ok":False,"msg":"qr not found"}
    qr_text = decoded[0].data.decode("utf-8", errors="ignore").strip()
    try:
        ok = set_qr_to_emp(emp_id, qr_text)
        return {"ok":ok, "emp_id":emp_id, "qr_code":qr_text}
    except mysql.connector.Error as e:
        return JSONResponse({"ok":False,"msg":str(e)}, status_code=500)

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)   # 단독 실행 시 0.0.0.0:8000에서 서비스 시작

