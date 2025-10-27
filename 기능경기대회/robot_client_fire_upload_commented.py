# === 로봇 TCP 클라이언트 + 화재 녹화/탐지 + 업로드(양방향 DONE 처리) + 10초 경보음 ===
# 기능 개요:
# - 제어 서버(Qt, TCP 8888)와 JSON 라인 프로토콜로 양방향 통신
# - 명령 처리: GO_TO(이동), FIRE_ALERT(트리거 녹화/현장 이동), RECORD_SET(수동 녹화 on/off)
# - 이벤트 송신: FIRE_EVENT(세션 시작/종료, 화재 확정 등), GO_TO_* (OK/FAIL 등)
# - 업로드 프로토콜: 제어 채널로 INIT/READY 교환 → 파일 소켓으로 RAW 송신 → DONE 확인(파일소켓 또는 제어채널)
# - 비전: HSV 기반 불꽃 영역 탐지(연속 프레임 히스테리시스) + (옵션) 모션 마스크 결합
# - 녹화: 트리거 시작, 화재 확정 시 POST_RECORD_SEC 추가 녹화 후 업로드, 세션·FPS 보정
# - 로봇 이동: ROS actionlib(send_goal) 또는 토픽 퍼블리시 폴백, HOME 포즈 저장/복귀
# - 경보: 사건(BUSY)당 1회 10초 사이렌

import socket, json, threading, time, math, os, uuid, shutil, subprocess, queue
from datetime import datetime, timezone, timedelta
from socket import timeout as SocketTimeout
from pathlib import Path
from typing import Optional
import cv2

# (선택) 위젯 UI가 가능한 환경이면 사용
try:
    import ipywidgets as widgets
    import IPython.display
    _has_widgets = True
except Exception:
    _has_widgets = False

# --------------------------
# 연결/일반 설정
# --------------------------
SERVER_HOST = "192.168.0.11"
SERVER_PORT = 8888
ROBOT_NAME  = "jetson-01"

# 녹화/카메라
CAMERA_URL          = "tcp://127.0.0.1:5000"      # 환경에 맞게 수정
SAVE_DIR            = "/home/bready/Videos"
os.makedirs(SAVE_DIR, exist_ok=True)
FOURCC              = cv2.VideoWriter_fourcc(*"XVID")
TARGET_FPS          = 30
MIN_RECORD_FPS      = 24
MAX_WAIT_SEC        = 180       # 일반 세션 최대 대기(트리거만 발생하고 확정이 없을 때)
POST_RECORD_SEC     = 30        # 화재 확정 후 추가 녹화 유지 시간
TRIGGER_RECORD_SEC  = 60        # FIRE_ALERT 수신 시 트리거 녹화 시간

# 알람(경보음) — 반드시 존재하는 mp3 경로
ALARM_MP3    = "/home/bready/alarm.mp3"
ALARM_VOLUME = 0.10

# FIRE(불꽃) HSV 파라미터 (smoke 관련 항목 전부 제거)
FIRE_HSV_RANGES = [((0,80,160),(15,255,255)), ((15,80,160),(50,255,255))]
FIRE_MIN_AREA   = 500
HYSTERESIS_FR   = 4
COOLDOWN_FR     = 12
USE_MOTION_MASK = True
BUSY = False
ALARM_PLAYED_THIS_RUN = False   # 사건(BUSY) 동안 사이렌 재생 여부

# --------------------------
# ROS 가드/메시지
# --------------------------
try:
    import rospy
    from geometry_msgs.msg import PoseStamped, Quaternion, PoseWithCovarianceStamped
    from std_msgs.msg import Header
    _has_rospy = True
    def _ros_is_shutdown():
        return rospy.is_shutdown()
except Exception:
    rospy = None
    _has_rospy = False
    def _ros_is_shutdown():
        return False

# 액션 (있으면 사용)
try:
    import actionlib
    from move_base_msgs.msg import MoveBaseAction, MoveBaseGoal
    _has_actionlib = True
except Exception:
    _has_actionlib = False


def q_from_yaw(yaw):
    """yaw(라디안) → Quaternion(z축 회전) 변환."""
    half = (yaw or 0.0) * 0.5
    return Quaternion(0.0, 0.0, math.sin(half), math.cos(half))

# --------------------------
# 경보음 플레이어
# --------------------------
class SirenPlayer:
    """사이렌 플레이어.
    - pygame을 우선 사용하고, 미설치 시 mpg123 CLI로 폴백.
    - start(loop=True)로 루프로 재생, stop()으로 중단.
    """
    def __init__(self, filepath: str, volume=1.0):
        self.filepath = filepath
        self.volume = float(volume)
        self.backend = None
        self._pg = None
        self._sound = None
        self._channel = None
        self._proc = None
        # pygame 시도
        try:
            import pygame
            pygame.mixer.init()
            self._pg = pygame
            self._sound = pygame.mixer.Sound(filepath)
            self._sound.set_volume(self.volume)
            self.backend = "pygame"
        except Exception:
            self.backend = None
        # mpg123 대안
        if self.backend is None and shutil.which("mpg123"):
            self.backend = "mpg123"

    def start(self, loop=False):
        """사운드 재생 시작. loop=True면 무한루프 재생."""
        if self.backend == "pygame":
            loops = -1 if loop else 0
            self._channel = self._sound.play(loops=loops)
        elif self.backend == "mpg123":
            loop_count = "0" if loop else "1"
            self._proc = subprocess.Popen(
                ["mpg123","-q","-l",loop_count,self.filepath],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )

    def stop(self):
        """사운드 재생 중단/프로세스 종료."""
        if self.backend == "pygame" and self._channel:
            try: self._channel.stop()
            except Exception: pass
        elif self.backend == "mpg123" and self._proc:
            try:
                if self._proc.poll() is None:
                    self._proc.terminate()
            except Exception:
                pass
            self._proc = None


def _play_alarm_10s():
    """사이렌을 최대 10초간 재생했다가 정지한다(사건당 1회)."""
    try:
        if alarm_player is not None and alarm_player.backend is not None:
            alarm_player.start(loop=True)
            time.sleep(10)
    finally:
        try:
            if alarm_player is not None:
                alarm_player.stop()
        except Exception:
            pass


alarm_player = SirenPlayer(ALARM_MP3, volume=ALARM_VOLUME)

# --------------------------
# TCP 상태
# --------------------------
_sock = None
_sock_lock = threading.Lock()
_STOP_TCP = False
_CLIENT_RUN = False
_RUN_ID = 0
_PAUSE_TCP_READER = False              # 업로드 중 리더 잠깐 멈춤(충돌 방지)
_upload_q = queue.Queue()              # 업로드 제어 신호 큐(UPLOAD_READY / UPLOAD_DONE)
INCIDENT_ID = None   # ✅ 서버가 발급한 사건 id 캐시
HOME_POSE = {"x": None, "y": None, "yaw": None, "ts": 0.0}


def _send_json_line(obj: dict):
    """JSON 객체를 한 줄로 직렬화해 제어 채널로 송신(\n 종료).
    - 스레드 안전을 위해 소켓 락 사용.
    - 송신 실패 시 로그만 남김(자동 재접속 루프로 회복).
    """
    line = (json.dumps(obj, separators=(',', ':')) + "\n").encode('utf-8')
    with _sock_lock:
        if _sock:
            try:
                _sock.sendall(line)
            except Exception as e:
                print("[TCP] send 실패:", e)


KST = timezone(timedelta(hours=9))   # 한국 시간대 정의

def _ts_iso_kst():
    """현재 시각을 KST ISO8601 문자열로 반환."""
    return datetime.now(KST).isoformat()

# --------------------------
# 현재 포즈(map)
# --------------------------

def _ensure_ros_node():
    """rospy 노드를 필요 시 초기화(신호 비활성)."""
    if not _has_rospy:
        raise RuntimeError("ROS(rospy) 미로딩")
    if not rospy.core.is_initialized():
        rospy.init_node('robot_client_bridge', anonymous=True, disable_signals=True)


def _get_pose_xyyaw_map(timeout=1.5):
    """AMCL 포즈를 읽어 (x, y, yaw) 반환. 실패 시 (None, None, None).
    - 전역 odom_pose 캐시가 있으면 우선 사용.
    - /amcl_pose 토픽을 timeout 내 대기.
    """
    try:
        if 'odom_pose' in globals() and odom_pose.get('x') is not None:
            return odom_pose['x'], odom_pose['y'], odom_pose['yaw']
    except Exception:
        pass
    try:
        if not _has_rospy: return None, None, None
        if not rospy.core.is_initialized():
            rospy.init_node('robot_client_bridge', anonymous=True, disable_signals=True)
        msg = rospy.wait_for_message('/amcl_pose', PoseWithCovarianceStamped, timeout=timeout)
        p = msg.pose.pose.position
        q = msg.pose.pose.orientation
        # 쿼터니언 → yaw 추정
        siny_cosp = 2.0*(q.w*q.z + q.x*q.y)
        cosy_cosp = 1.0 - 2.0*(q.y*q.y + q.z*q.z)
        yaw = math.atan2(siny_cosp, cosy_cosp)
        return p.x, p.y, yaw
    except Exception:
        return None, None, None

# --------------------------
# move_base 호출(+도착검증)
# --------------------------
STATE_NAME = {0:"PENDING",1:"ACTIVE",2:"PREEMPTED",3:"SUCCEEDED",4:"ABORTED",
              5:"REJECTED",6:"PREEMPTING",7:"RECALLING",8:"RECALLED",9:"LOST"}


def send_goal_via_action(x, y, yaw=None, frame_id='map', timeout=120.0):
    """move_base 액션 서버로 목표를 전송하고 결과를 동기 대기.
    - 성공(SUCCEEDED) 시 True, 타임아웃/실패 시 False 반환.
    - 예외 발생 시 상위에서 토픽 퍼블리시로 폴백.
    """
    if not _has_actionlib:
        raise RuntimeError("actionlib 미로드")
    _ensure_ros_node()
    client = actionlib.SimpleActionClient('/move_base', MoveBaseAction)
    if not client.wait_for_server(rospy.Duration(3.0)):
        raise RuntimeError("/move_base 액션 서버 미대기")
    client.cancel_all_goals()
    rospy.sleep(0.05)

    goal = MoveBaseGoal()
    goal.target_pose.header.frame_id = frame_id
    goal.target_pose.header.stamp = rospy.Time.now()
    goal.target_pose.pose.position.x = float(x)
    goal.target_pose.pose.position.y = float(y)
    # yaw 지정이 없으면 현재 자세의 yaw 유지
    x0, y0, yaw0 = _get_pose_xyyaw_map(timeout=0.8)
    yaw_use = float(yaw) if yaw is not None else (yaw0 if yaw0 is not None else 0.0)
    goal.target_pose.pose.orientation = q_from_yaw(yaw_use)

    client.send_goal(goal)
    print(f"[move_base] Goal 보냄: (x={x:.3f}, y={y:.3f}, yaw={yaw_use:.3f})")
    ok = client.wait_for_result(rospy.Duration(timeout))
    if not ok:
        client.cancel_goal()
        print("[move_base] timeout → 실패")
        return False
    st = client.get_state()
    print(f"[move_base] 결과 state={st} ({STATE_NAME.get(st,'?')})")
    return st == 3


def send_goal_via_topic(x, y, yaw=None, frame_id='map', wait_connected=True):
    """토픽 퍼블리시로 목표 전송(액션 실패 시 폴백 경로)."""
    _ensure_ros_node()
    pub_goal = rospy.Publisher('/move_base_simple/goal', PoseStamped, queue_size=1)
    if wait_connected:
        t0 = time.time()
        while pub_goal.get_num_connections() == 0 and (time.time()-t0) < 3.0 and not rospy.is_shutdown():
            rospy.sleep(0.05)
    ps = PoseStamped()
    ps.header = Header(frame_id=frame_id, stamp=rospy.Time.now())
    ps.pose.position.x = float(x)
    ps.pose.position.y = float(y)
    ps.pose.position.z = 0.0
    x0, y0, yaw0 = _get_pose_xyyaw_map(timeout=0.8)
    yaw_use = float(yaw) if yaw is not None else (yaw0 if yaw0 is not None else 0.0)
    ps.pose.orientation = q_from_yaw(yaw_use)
    pub_goal.publish(ps)
    print(f"[move_base_simple/goal] 보냄: (x={x:.3f}, y={y:.3f}, yaw={yaw_use:.3f}) in '{frame_id}'")


def _wait_until_arrived(x_goal, y_goal, yaw_goal=None,
                        pos_tol=0.15, yaw_tol=0.35, dwell=0.3, timeout=12.0):
    """단순 도착 판정 유틸(토픽 폴백 시 사용).
    - 목표 근처(pos_tol)에서 dwell(체류시간) 유지되면 True.
    - yaw_goal이 있을 때 yaw 오차 허용 범위도 함께 체크.
    """
    t0 = time.time()
    first_in = None
    while (time.time()-t0) < timeout and not _ros_is_shutdown():
        x, y, yaw = _get_pose_xyyaw_map(timeout=0.5)
        if x is None: continue
        dist = math.hypot(x_goal - x, y_goal - y)
        ok_pos = dist <= pos_tol
        ok_yaw = True
        if yaw_goal is not None:
            err = yaw_goal - yaw
            while err > math.pi: err -= 2*math.pi
            while err < -math.pi: err += 2*math.pi
            ok_yaw = abs(err) <= yaw_tol
        if ok_pos and ok_yaw:
            if first_in is None: first_in = time.time()
            if (time.time() - first_in) >= dwell:
                return True
        else:
            first_in = None
        time.sleep(0.05)
    return False


def _capture_home_pose(force=False):
    """현재 map 포즈를 HOME으로 저장(복귀용). 이미 있으면 기본 보존."""
    if (not force) and (HOME_POSE["x"] is not None):
        return
    x, y, yaw = _get_pose_xyyaw_map(timeout=1.5)
    if x is not None:
        HOME_POSE["x"] = x
        HOME_POSE["y"] = y
        HOME_POSE["yaw"] = yaw if yaw is not None else 0.0
        HOME_POSE["ts"] = time.time()
        print(f"[HOME] 저장: x={x:.3f}, y={y:.3f}, yaw={HOME_POSE['yaw']:.3f}")
    else:
        print("[HOME] 현재 포즈를 얻지 못해 저장 실패")


def _return_home(timeout_move=180.0):
    """HOME으로 복귀. 복귀 완료 후 BUSY 해제 및 상태 리셋."""
    xh, yh, yawh = HOME_POSE["x"], HOME_POSE["y"], HOME_POSE["yaw"]
    if xh is None:
        print("[HOME] 저장된 HOME 없음 → 복귀 생략")
    else:
        print(f"[HOME] 복귀 시작 → ({xh:.3f}, {yh:.3f}, yaw={yawh:.3f})")
        try:
            moved = False
            if _has_actionlib:
                try:
                    moved = send_goal_via_action(xh, yh, yawh, frame_id='map', timeout=timeout_move)
                except Exception as e:
                    print("[HOME] action 예외 → 토픽 폴백:", e)
            if not moved:
                send_goal_via_topic(xh, yh, yawh, frame_id='map')
                _wait_until_arrived(xh, yh, yaw_goal=yawh,
                                    pos_tol=0.15, yaw_tol=0.35, dwell=0.3, timeout=30.0)
            print("[HOME] 복귀 완료")
        finally:
            # 🔓 100% 완료 후에만 잠금 해제
            globals()["BUSY"] = False
            globals()["ALARM_PLAYED_THIS_RUN"] = False   # ← 다음 사건을 위해 리셋
            # HOME 초기화
            HOME_POSE["x"] = HOME_POSE["y"] = HOME_POSE["yaw"] = None
            HOME_POSE["ts"] = 0.0


# --------------------------
# GO_TO 처리(절대 x,y 또는 상대 dx,dy)
# --------------------------
_last_goal = {"id": None, "x": None, "y": None, "yaw": None, "t": 0.0}


def _is_dup_goal(o, window=2.0):
    """최근 동일 목표(또는 동일 id)인지 중복 체크하여 스팸 이동 방지."""
    now = time.time()
    try:
        x = float(o.get("x")) if o.get("x") is not None else None
        y = float(o.get("y")) if o.get("y") is not None else None
    except Exception:
        x = y = None
    yaw = o.get("yaw", None)
    same_id  = (o.get("id") is not None and o.get("id") == _last_goal["id"])
    same_xyz = (x is not None and _last_goal["x"] is not None and
                abs(x-(_last_goal["x"]))<1e-6 and abs(y-(_last_goal["y"]))<1e-6 and yaw == _last_goal["yaw"])
    recent   = (now - _last_goal["t"] < window)
    return (same_id or same_xyz) and recent


def _mark_goal(o):
    """마지막 목표 캐시 업데이트(중복 억제용)."""
    _last_goal["id"]  = o.get("id")
    _last_goal["x"]   = float(o.get("x")) if o.get("x") is not None else None
    _last_goal["y"]   = float(o.get("y")) if o.get("y") is not None else None
    _last_goal["yaw"] = o.get("yaw", None)
    _last_goal["t"]   = time.time()


def _handle_go_to(o: dict, allow_when_busy: bool = False):
    """GO_TO 처리 진입점.
    - 일반적으로 BUSY/session_active 상태에서는 거부, 단 사건 초기 1회는 allow_when_busy=True로 허용.
    - 상대 좌표(dx,dy) → 절대 좌표 변환 지원, yaw_deg도 라디안으로 변환 처리.
    - actionlib 우선 → 실패 시 토픽 퍼블리시 + 도착 판정.
    - 결과를 제어 채널로 GO_TO_ACCEPTED / GO_TO_OK / GO_TO_FAIL 로 회신.
    """
    global BUSY
    if (BUSY or session_active) and not allow_when_busy:
        print("[ROBOT] BUSY: ignore GO_TO")
        _send_json_line({"cmd": "GO_TO_DENY", "id": o.get("id"), "reason": "busy"})
        return

    _capture_home_pose(force=False)

    # 좌표 해석 (상대 → 절대 변환)
    if ("dx" in o) or ("dy" in o):
        try:
            dx = float(o.get("dx", 0.0)); dy = float(o.get("dy", 0.0))
        except Exception:
            _send_json_line({"cmd":"GO_TO_FAIL","id":o.get("id"),"reason":"invalid dx/dy"})
            return
        x0, y0, _ = _get_pose_xyyaw_map(timeout=2.0)
        if x0 is None:
            _send_json_line({"cmd":"GO_TO_FAIL","id":o.get("id"),"reason":"no current pose"})
            return
        x = x0 + dx; y = y0 + dy
        print(f"[GO_TO] 상대→절대: ({x0:.3f},{y0:.3f}) + ({dx:.3f},{dy:.3f}) = ({x:.3f},{y:.3f})")
    else:
        try:
            x = float(o["x"]); y = float(o["y"])
        except Exception:
            _send_json_line({"cmd":"GO_TO_FAIL","id":o.get("id"),"reason":"invalid x/y"})
            return

    # yaw
    yaw = None
    if "yaw" in o:
        try: yaw = float(o["yaw"])
        except: yaw = None
    elif "yaw_deg" in o:
        try: yaw = math.radians(float(o["yaw_deg"]))
        except: yaw = None

    frame_id, req_id = o.get("frame", "map"), o.get("id")
    _send_json_line({"cmd": "GO_TO_ACCEPTED", **({"id": req_id} if req_id else {})})

    # 중복 억제용으로 '절대 좌표' 기준으로 마킹
    _mark_goal({"id": req_id, "x": x, "y": y, "yaw": yaw})

    try:
        moved = False
        if _has_actionlib:
            try:
                moved = send_goal_via_action(x, y, yaw, frame_id=frame_id, timeout=180.0)
            except Exception as e:
                print("[move_base/action] 예외 → 토픽 폴백:", e)

        if not moved:
            send_goal_via_topic(x, y, yaw, frame_id=frame_id)
            arrived = _wait_until_arrived(x, y, yaw_goal=yaw,
                                          pos_tol=0.15, yaw_tol=0.35, dwell=0.3, timeout=15.0)
            if arrived:
                _send_json_line({"cmd":"GO_TO_OK","id":req_id,"ts":_ts_iso_kst()})
            else:
                _send_json_line({"cmd":"GO_TO_FAIL","id":req_id,"reason":"aborted or unreachable"})
            return

        # 액션 성공
        _send_json_line({"cmd":"GO_TO_OK","id":req_id,"ts":_ts_iso_kst()})
    except Exception as e:
        _send_json_line({"cmd":"GO_TO_FAIL","id":req_id,"reason":str(e)})


# --------------------------
# 비전/감지 준비 (FIRE만)
# --------------------------
bg = cv2.createBackgroundSubtractorMOG2(history=300, varThreshold=16, detectShadows=True) if USE_MOTION_MASK else None
cap = cv2.VideoCapture(CAMERA_URL)

# (옵션) 간단한 위젯 상태 표시
if _has_widgets:
    image_widget = widgets.Image(format='jpeg')
    fps_label    = widgets.HTML("<b>FPS:</b> -")
    fire_label   = widgets.HTML("<b>FIRE:</b> -")
    IPython.display.display(widgets.HBox([fps_label, widgets.HTML("&nbsp;&nbsp;"), fire_label]))
    IPython.display.display(image_widget)

# 상태 (녹화/세션)
_server_record=False
_prev_server_record=False
_next_clip_custom_name=None
session_active=False
session_started_at=None
session_expire_at=None
detection_marked_at=None
post_record_until=None
video_writer=None
current_clip_path=None
writer_size=None
SESSION_ID=None

# 감지 상태 (FIRE만)
fire_frames=0
fire_cooldown=0
t_last=time.time(); frame_cnt=0


def _boxes(mask, min_area):
    """이진 마스크에서 min_area 이상의 외곽 박스를 추출."""
    cnts,_ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    out=[]
    for c in cnts:
        area=cv2.contourArea(c)
        if area>=min_area:
            x,y,w,h=cv2.boundingRect(c); out.append((x,y,w,h))
    return out


def _detect_fire(frame):
    """불꽃만 감지하여 (confirmed, vis)를 반환.
    - HSV 영역 임계치로 불꽃 후보 마스크 생성
    - (옵션) 배경차분 모션 마스크와 AND 결합 → 오탐(정적 난반사 등) 감소
    - 연속 프레임 히스테리시스로 확정(>= HYSTERESIS_FR)
    - cooldown 동안 시각화 박스 유지
    """
    global fire_frames, fire_cooldown
    vis = frame.copy()
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    # FIRE 마스크
    fire_mask=None
    for lo,hi in FIRE_HSV_RANGES:
        m = cv2.inRange(hsv, lo, hi)
        fire_mask = m if fire_mask is None else cv2.bitwise_or(fire_mask, m)
    fire_mask = cv2.morphologyEx(fire_mask, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(5,5)))
    fire_mask = cv2.GaussianBlur(fire_mask,(5,5),0)

    # 모션 결합(옵션)
    if USE_MOTION_MASK and bg is not None:
        motion = bg.apply(frame); _, motion = cv2.threshold(motion,127,255,cv2.THRESH_BINARY)
        fire_mask  = cv2.bitwise_and(fire_mask,  motion)

    fire_boxes  = _boxes(fire_mask,  FIRE_MIN_AREA)
    fire_now  = len(fire_boxes)>0
    fire_frames  = fire_frames+1 if fire_now else 0
    fire_confirmed  = (fire_frames  >= HYSTERESIS_FR)
    if fire_confirmed:  fire_cooldown  = COOLDOWN_FR
    if fire_cooldown>0:
        for (x,y,w,h) in fire_boxes:
            cv2.rectangle(vis,(x,y),(x+w,y+h),(0,0,255),2)
            cv2.putText(vis,"FIRE",(x,y-8),cv2.FONT_HERSHEY_SIMPLEX,0.7,(0,0,255),2)
        fire_cooldown-=1
    return fire_confirmed, vis

# --------------------------
# 세션/업로드
# --------------------------

def _event_fire(payload: dict):
    """FIRE_EVENT 계열 이벤트를 제어 채널로 전송."""
    msg = {
        "cmd": "FIRE_EVENT",
        "session_id": (SESSION_ID or ""),
        "payload": payload,
        "ts": _ts_iso_kst()
    }
    if INCIDENT_ID:
        msg["incident_id"] = int(INCIDENT_ID)
    _send_json_line(msg)


def _start_session(first_frame):
    """녹화 세션 시작.
    - 파일명: [커스텀이름 or record]_타임스탬프.avi
    - VideoWriter 열고 상태 변수 초기화, session_started 이벤트 송신
    """
    global session_active, session_started_at, session_expire_at
    global detection_marked_at, post_record_until
    global video_writer, current_clip_path, writer_size, _next_clip_custom_name, SESSION_ID

    ts = time.strftime("%Y%m%d_%H%M%S")
    custom = _next_clip_custom_name; _next_clip_custom_name=None
    base = f"{ts}_{custom}" if custom else f"record_{ts}"
    fname = f"{base}.avi"
    current_clip_path = os.path.join(SAVE_DIR, fname)

    h,w = first_frame.shape[:2]
    writer_size=(w,h)
    video_writer = cv2.VideoWriter(current_clip_path, FOURCC, TARGET_FPS, writer_size)
    if not video_writer.isOpened():
        video_writer=None
        raise RuntimeError("VideoWriter open fail")
    globals()["_last_write_ts"] = None
    session_active=True
    session_started_at=time.time()
    if session_expire_at is None:
        session_expire_at=session_started_at+MAX_WAIT_SEC
    detection_marked_at=None
    post_record_until=None
    if not SESSION_ID: SESSION_ID = uuid.uuid4().hex
    _event_fire({"event":"session_started","filename":os.path.basename(current_clip_path),"name":custom,"max_wait_sec":MAX_WAIT_SEC})
    print(f"[SESSION] 녹화 시작: {current_clip_path}")


def _stop_session(upload: bool, fire_detected: bool):
    """녹화 세션 종료 및 (옵션) 업로드.
    - 업로드 성공 여부와 로컬 경로를 FIRE_EVENT(session_ended)로 보고.
    """
    global session_active, video_writer, current_clip_path
    if video_writer is not None:
        video_writer.release()
        video_writer=None
    print(f"[SESSION] 녹화 종료: {current_clip_path}")
    uploaded=False
    if upload and current_clip_path and os.path.exists(current_clip_path):
        uploaded = _upload_file(current_clip_path)
        print("[SESSION] 업로드", "완료 ✅" if uploaded else "실패 ❌")
    _event_fire({
        "event": "session_ended",
        "filename": os.path.basename(current_clip_path) if current_clip_path else None,
        "local_path": current_clip_path,            # ✅ 로봇 내 전체 경로 추가
        "detected": bool(fire_detected),
        "uploaded": uploaded
    })
    session_active=False


def _upload_file(path: str, timeout=60):
    """영상 파일 업로드(양쪽 DONE 경로 지원).
    흐름:
      1) 제어 채널로 UPLOAD_INIT 전송(파일명/사이즈/incident_id)
      2) 리더 스레드가 제어 채널에서 UPLOAD_READY(포트+토큰) 수신 → 큐로 전달
      3) 파일 소켓 연결 후 헤더 1줄 + RAW 전송
      4) (A) 파일 소켓에서 UPLOAD_DONE 1줄 시도(짧은 타임아웃)
         (B) 실패 시 제어 채널 큐에서 UPLOAD_DONE 대기
    반환: 성공 여부(bool)
    """
    from json import dumps, loads

    p = Path(path)
    if not p.exists():
        print("[UPLOAD] 파일 없음:", path)
        return False

    raw = p.read_bytes()
    size = len(raw)
    name = p.name

    # 1) INIT (제어채널)
    _send_json_line({
        "cmd": "UPLOAD_INIT",
        "filename": name,
        "filesize": size,
        # ✅ 사건 id를 함께 전송(없으면 서버 캐시에 의존)
        "incident_id": (INCIDENT_ID or 0)
    })

    # 2) READY (제어채널 큐)
    ready = None
    t0 = time.time()
    while True:
        remain = timeout - (time.time() - t0)
        if remain <= 0:
            print("[UPLOAD] READY timeout")
            return False
        try:
            o = _upload_q.get(timeout=min(2.0, max(0.1, remain)))
        except queue.Empty:
            continue
        if str(o.get("cmd","")).upper() == "UPLOAD_READY":
            if not o.get("ok", False):
                print("[UPLOAD] READY not ok:", o)
                return False
            ready = o
            break

    port  = int(ready.get("port", 0))
    token = ready.get("token")
    if not port or not token:
        print("[UPLOAD] READY 응답 이상(포트/토큰 없음):", ready)
        return False

    # 3) 파일 소켓 채널: 헤더 + RAW
    done = None
    try:
        with socket.create_connection((SERVER_HOST, port), timeout=10.0) as fs:
            # 헤더 1줄
            hdr = {"token": token, "filename": name, "filesize": size}
            fs.sendall((dumps(hdr, separators=(',',':')) + "\n").encode('utf-8'))

            # RAW 전송 (chunked)
            view = memoryview(raw)
            sent = 0
            CHUNK = 64*1024
            while sent < size:
                n = fs.send(view[sent:sent+CHUNK])
                if n <= 0:
                    raise RuntimeError("send failed")
                sent += n

            # 4-A) 파일 소켓에서 DONE 한 줄 시도 (짧은 타임아웃)
            fs.settimeout(3.0)
            line = b""
            try:
                while True:
                    ch = fs.recv(1)
                    if not ch:
                        break
                    if ch == b'\n':
                        break
                    line += ch
                if line:
                    o = loads(line.decode('utf-8').strip())
                    if str(o.get("cmd","")) == "UPLOAD_DONE":
                        done = o
            except socket.timeout:
                pass
    except Exception as e:
        print("[UPLOAD] 파일채널 전송 실패:", e)
        return False

    # 4-B) 제어 채널 큐에서 DONE 대기 (A에서 못 받았을 때만)
    if done is None:
        t1 = time.time()
        while True:
            remain = timeout - (time.time() - t1)
            if remain <= 0:
                print("[UPLOAD] DONE timeout")
                return False
            try:
                o = _upload_q.get(timeout=min(2.0, max(0.1, remain)))
            except queue.Empty:
                continue
            if str(o.get("cmd","")) == "UPLOAD_DONE":
                done = o
                break

    ok = bool(done.get("ok"))
    saved = done.get("saved_path") or done.get("path")
    if ok:
        print("[UPLOAD] 완료 ✅:", saved if saved else "(경로미기재)")
    else:
        print("[UPLOAD] 실패:", done)

    return ok

# --------------------------
# 서버 명령 처리: FIRE_ALERT / RECORD_SET
# --------------------------

def _handle_fire_alert(o: dict):
    """서버의 화재 트리거 처리.
    - BUSY/세션 중이 아니어야 시작.
    - 사건 단위 BUSY 잠금 설정, 사이렌 재생 플래그 초기화.
    - 1분 트리거 녹화 시작(세션 만료 시각 설정), 좌표 없으면 기본 상대 이동.
    - 첫 이동은 allow_when_busy=True로 통과(사건 시작 직후 허용).
    """
    global _server_record, _next_clip_custom_name, session_expire_at, BUSY

    # 이미 작업 중이면 무시
    if BUSY or session_active:
        _send_json_line({"cmd": "BUSY", "reason": "session_active"})
        return

    # 🔒 사건 잠금: 복귀 끝날 때까지 유지
    BUSY = True
    ALARM_PLAYED_THIS_RUN = False     # ← 사건 시작: 사이렌 아직 안 울림

    # 1분 트리거 녹화 시작
    _server_record = True
    if o.get("name"):
        _next_clip_custom_name = str(o["name"])
    session_expire_at = time.time() + TRIGGER_RECORD_SEC

    # 좌표 없으면 기본 상대이동(dx,dy) 채워넣기
    go = dict(o)  # shallow copy
    if not any(k in go for k in ("x","y","dx","dy")):
        go.update({"dx": 0.5, "dy": 0.5, "yaw_deg": 0.0, "frame": "map"})

    # 첫 이동은 BUSY 상태라도 통과시키기 위해 allow_when_busy=True
    threading.Thread(target=_handle_go_to, args=(go, True), daemon=True).start()



def _handle_record_set(o: dict):
    """서버의 녹화 on/off 지시 반영(커스텀 파일명 선택적).
    - record=True이면 다음 _start_session 시 커스텀 이름 반영.
    """
    global _server_record, _next_clip_custom_name
    rec = bool(o.get("record", False))
    _server_record = rec
    if rec and o.get("name"):
        _next_clip_custom_name = str(o["name"])

# --------------------------
# TCP 수신 루프
# --------------------------

def _tcp_reader_loop(my_run_id: int):
    """제어 채널 리더 스레드.
    - 한 줄 JSON 수신 → cmd 스위치로 분기 처리
    - 업로드 관련 신호(READY/DONE)는 큐로 전달하여 동기화
    - INCIDENT_CREATED 수신 시 사건 ID 캐시
    """
    global _sock, _STOP_TCP
    buf = b""
    with _sock_lock:
        try:
            if _sock: _sock.settimeout(None)
        except: pass
    while not _STOP_TCP and my_run_id == _RUN_ID:
        try:
            if _PAUSE_TCP_READER:
                time.sleep(0.01)
                continue
            data = _sock.recv(4096)
        except SocketTimeout:
            continue
        except Exception as e:
            print("[TCP] recv 에러:", e); break
        if not data:
            print("[TCP] 서버 연결 종료됨."); break
        buf += data
        while True:
            i = buf.find(b'\n')
            if i < 0: break
            line, buf = buf[:i].strip(), buf[i+1:]
            if not line: continue
            try: o = json.loads(line.decode('utf-8', errors='replace'))
            except Exception as e: print("[TCP] 잘못된 JSON:", e, line[:120]); continue

            cmd = str(o.get("cmd","")).upper()

            if cmd == "GO_TO":
                _handle_go_to(o)

            elif cmd == "FIRE_ALERT":
                _handle_fire_alert(o)

            elif cmd == "RECORD_SET":
                _handle_record_set(o)

            elif cmd == "FIRE_EVENT":
                # 서버가 fire_event를 릴레이할 수도 있음(정보용)
                print("[TCP] 수신 FIRE_EVENT:", o.get("payload", {}))

            elif cmd == "UPLOAD_READY":
                _upload_q.put(o)

            elif cmd == "UPLOAD_DONE":
                _upload_q.put(o)

            elif cmd == "INCIDENT_CREATED":
                # 서버가 새 사건 id 발급
                try:
                    inc = int(o.get("incident_id", 0))
                except Exception:
                    inc = 0
                if inc > 0:
                    print(f"[TCP] INCIDENT_CREATED: id={inc}")
                    globals()["INCIDENT_ID"] = inc

            else:
                print("[TCP] 수신:", o)

# --------------------------
# TCP 시작 / 중지
# --------------------------

def start_tcp_client():
    """제어 서버에 지속 재접속을 수행하는 메인 커넥터.
    - 연결 성립 시 리더 스레드를 띄우고 HELLO 전송.
    - 연결이 끊기면 일정 지연 후 재접속.
    """
    global _sock, _STOP_TCP, _CLIENT_RUN, _RUN_ID, _tcp_thread
    if _CLIENT_RUN:
        print("[TCP] 이미 실행 중입니다."); return
    _CLIENT_RUN=True; _STOP_TCP=False; _RUN_ID+=1; my_run_id=_RUN_ID
    try:
        while not _STOP_TCP and not _ros_is_shutdown() and my_run_id==_RUN_ID:
            try:
                print(f"[TCP] 연결 시도 {SERVER_HOST}:{SERVER_PORT} ...")
                s=socket.create_connection((SERVER_HOST,SERVER_PORT),timeout=5.0)
                try: s.settimeout(None); s.setsockopt(socket.SOL_SOCKET,socket.SO_KEEPALIVE,1)
                except: pass
                with _sock_lock: _sock=s
                _send_json_line({"cmd":"HELLO","role":"robot","name":ROBOT_NAME})
                t=threading.Thread(target=_tcp_reader_loop,args=(my_run_id,),daemon=True)
                t.start(); _tcp_thread = t
                print("[TCP] 로봇 TCP 클라이언트 연결됨.")
                while t.is_alive() and not _STOP_TCP and not _ros_is_shutdown() and my_run_id==_RUN_ID:
                    time.sleep(0.5)
                with _sock_lock:
                    try: _sock.close()
                    except: pass
                    _sock=None
                if not _STOP_TCP and not _ros_is_shutdown() and my_run_id==_RUN_ID:
                    print("[TCP] 재접속 대기..."); time.sleep(2.0)
            except Exception as e:
                print("[TCP] 연결 실패:", e)
                if not _STOP_TCP and not _ros_is_shutdown() and my_run_id==_RUN_ID:
                    time.sleep(2.0)
    finally:
        if my_run_id==_RUN_ID: _CLIENT_RUN=False


def stop_tcp_client():
    """TCP 클라이언트 정지 요청(소켓 종료 및 리더 스레드 join)."""
    import socket
    global _STOP_TCP, _sock, _RUN_ID, _CLIENT_RUN, _tcp_thread
    print("[TCP/STOP] 호출됨")
    _STOP_TCP = True
    _RUN_ID += 1
    with _sock_lock:
        if _sock is not None:
            try:
                _sock.shutdown(socket.SHUT_RDWR)
                print("[TCP/STOP] socket.shutdown OK")
            except Exception as e:
                print("[TCP/STOP] socket.shutdown 예외:", e)
            try:
                _sock.close()
                print("[TCP/STOP] socket.close OK")
            except Exception as e:
                print("[TCP/STOP] socket.close 예외:", e)
            _sock = None
    try:
        if isinstance(_tcp_thread, threading.Thread):
            _tcp_thread.join(timeout=2.0)
            print(f"[TCP/STOP] 스레드 alive? {_tcp_thread.is_alive()}")
    except Exception:
        pass
    _CLIENT_RUN = False
    print("[TCP] 로봇 TCP 클라이언트 중지 요청됨.")

# 백그라운드 시작(프로세스 시작 시 곧바로 서버 접속 시도)
_tcp_thread=threading.Thread(target=start_tcp_client,daemon=True)
_tcp_thread.start()
print("[TCP] 로봇 TCP 클라이언트 시작됨.")

# --------------------------
# 메인 비전 루프 (녹화/감지) — FIRE만
# --------------------------
prev_fire_confirmed=False

try:
    while True:
        # 1) 카메라 프레임 읽기
        ok, frame = cap.read()
        if not ok or frame is None:
            print("[정보] 카메라 프레임 없음(스트림 확인)"); time.sleep(0.2); continue

        # 2) 불꽃 감지
        fire_confirmed, vis = _detect_fire(frame)

        # 3) 상승 에지에서 FIRE_EVENT 송신 + 사건당 1회 사이렌
        if fire_confirmed and not prev_fire_confirmed:
            _event_fire({"event":"fire_confirmed"})
            if BUSY and not ALARM_PLAYED_THIS_RUN:
                ALARM_PLAYED_THIS_RUN = True   # 사건당 1회만
                threading.Thread(target=_play_alarm_10s, daemon=True).start()

        prev_fire_confirmed=fire_confirmed

        # 4) (옵션) UI 업데이트
        if _has_widgets:
            frame_cnt+=1; now=time.time()
            if now - t_last >= 1.0:
                fps = frame_cnt/(now-t_last); t_last=now; frame_cnt=0
                fps_label.value=f"<b>FPS:</b> {fps:.2f}"
            fire_label.value  = f"<b>FIRE:</b>  {'DETECTED' if fire_confirmed else '-'}"
            _,enc=cv2.imencode(".jpeg", vis); image_widget.value=enc.tobytes()

        # 5) 서버 녹화 트리거 반영: false→true 전이에서 세션 시작
        if (not session_active) and _server_record and (not _prev_server_record):
            _start_session(frame)
        _prev_server_record = _server_record

        # 6) 세션 활성 시 프레임 기록/종료 판단
        if session_active:
            h,w = frame.shape[:2]
            # 입력 해상도 변경 시 세션 재시작(코덱/Writer 안정화용)
            if (video_writer is None) or (writer_size != (w,h)):
                _stop_session(upload=False, fire_detected=False)
                _start_session(frame)
            if video_writer is not None:
                nowt = time.time()
                # 첫 프레임이면 기준 시간 설정 후 1프레임 기록
                if globals().get("_last_write_ts") is None:
                    video_writer.write(frame)
                    globals()["_last_write_ts"] = nowt
                else:
                    # 지난 기록 이후 흐른 시간만큼 최소 FPS에 맞춰 필요한 개수만큼 프레임 보강
                    dt = max(0.0, nowt - globals()["_last_write_ts"])
                    # 흐른 시간 × 최소 FPS = 써야 할 프레임 수 (최소 1)
                    n_write = max(1, int(dt * MIN_RECORD_FPS + 1e-6))
                    for _ in range(n_write):
                        video_writer.write(frame)
                    # 기준 시간을 그만큼 전진 (드리프트 최소화)
                    globals()["_last_write_ts"] += n_write / float(MIN_RECORD_FPS)

            # 6-1) 화재 확정 시 추가 녹화 타이머 설정
            now = time.time()
            if fire_confirmed and (detection_marked_at is None):
                detection_marked_at = now
                post_record_until = detection_marked_at + POST_RECORD_SEC
                print(f"[DETECT] 화재 확정! 추가 {POST_RECORD_SEC}s 녹화 후 종료 예정")

            # 6-2) 종료 조건
            if (detection_marked_at is not None) and (now >= post_record_until):
                # 화재 확정 케이스: 업로드 후 복귀
                _stop_session(upload=True, fire_detected=True)
                _server_record = False
                session_expire_at = None
                threading.Thread(target=_return_home, daemon=True).start()
            elif (session_expire_at is not None) and (now >= session_expire_at) and (detection_marked_at is None):
                # 트리거만 있었던 케이스: 업로드 후 복귀
                _stop_session(upload=True, fire_detected=False)  # 트리거 녹화만 했어도 업로드
                _server_record = False
                session_expire_at = None
                threading.Thread(target=_return_home, daemon=True).start()

        time.sleep(0.01)

except KeyboardInterrupt:
    print("서버 연결 종료")
finally:
    # 자원 정리: VideoWriter/cap 해제, 창 닫기
    try:
        if video_writer is not None:
            video_writer.release()
            print(f"[정리] 파일 저장 완료: {current_clip_path}")
    except: pass
    try: cap.release()
    except: pass
    cv2.destroyAllWindows()
