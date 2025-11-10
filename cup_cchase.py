import cv2
import requests
import numpy as np
import time
from ultralytics import YOLO
from queue import Queue

# === CONFIG ===
ESP_IP = "192.168.4.1"
stream_url = f"http://{ESP_IP}/capture"
command_url = f"http://{ESP_IP}/action?go="

# Your YOLO model
model = YOLO(r"C:\ASP\esp32car\yolov8-digits-detection\yolov8m.pt")

# Behavior parameters

LOST_LIMIT = 5
SEARCH_TIMEOUT = 10       # seconds
STEP_TIME = 0.06          # pulse turn time
PAUSE_TIME = 0.5         # wait after turn for YOLO frame
SEARCH_DIRECTION = "forward"  # "left" or "forward"
SEARCH_STEPS_LR = 3      # steps to turn left before forward
SEARCH_STEPS_FW = 5      # steps to move forward during search

lost_frames = 0
search_steps_left = 0
search_steps_right = 0  
search_steps_forward = 0
ttl_forward = 0

found_frames = 0
searching = False
search_start = 0

def send_cmd(cmd):
    try:
        requests.get(command_url + cmd, timeout=0.2)
    except:
        pass

def move_step(cmd, step_time=STEP_TIME  ):
    send_cmd(cmd)
    time.sleep(step_time)
    send_cmd("stop")

print("🚗 Starting — reading frames from ESP32 /capture...")

# turn LED on at program start
try:
    requests.get(f"http://{ESP_IP}/action?led=on", timeout=0.3)
    print("💡 LED ON")
except:
    print("⚠️ LED command failed")
time.sleep(0.5)

send_cmd("minus")
send_cmd("minus")
send_cmd("minus")

while True:
    try:
        # ---- CAPTURE FRAME ----
        resp = requests.get(stream_url, timeout=3)
        img = np.frombuffer(resp.content, dtype=np.uint8)
        frame = cv2.imdecode(img, cv2.IMREAD_COLOR)

        if frame is None:
            print("⚠️ Frame decode error")
            send_cmd("stop")
            continue

        # ---- YOLO ----
        results = model(frame, verbose=False)
        cup = None
        for r in results:
            for box in r.boxes:
                cls = int(box.cls[0])
                label = model.names[cls]  # class name text
                print(f"Detected: {label}")
                conf = float(box.conf[0])
                
                cv2.imshow("ESP Cup Chase", frame)
                x1, y1, x2, y2 = map(int, box.xyxy[0])

                # Draw detection
                cv2.rectangle(frame,(x1,y1),(x2,y2),(0,255,0),2)
                cv2.putText(frame, label, (x1, y1-10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0,255,0), 2)

                # Adjust class list to your cup classes
                if (label in ["cup", "sports ball"] and conf > 0.2):
                    cup = box
                    send_cmd("stop")

        if cup:
            # ✅ CUP FOUND            
            send_cmd("stop")
            if searching:
                print("🎯 Cup found — exit search mode")
                searching = False
                # send_cmd("right")
                # send_cmd("stop")
                # time.sleep(0.2)

            time.sleep(0.2)
            lost_frames = 0
            lost_frames_left = 0
            lost_frames_forward = 0
            found_frames += 1

            x1, y1, x2, y2 = map(int, cup.xyxy[0])
            cx = (x1 + x2) // 2
            width = x2 - x1
            frame_center = frame.shape[1] // 2
            offset = cx - frame_center
            print(f"➡️ Cup offset: {offset}")

            # Draw detection
            cv2.imshow("ESP Cup Chase", frame)
            cv2.rectangle(frame,(x1,y1),(x2,y2),(0,255,0),2)
            cv2.putText(frame, label, (x1, y1-10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0,255,0), 2)


            if abs(offset) < 30:
                print("✅ CENTER → FORWARD")
                move_step("forward", step_time=0.1 * width / 10)
            elif offset > 0:
                print("➡️ RIGHT")
                move_step("right", step_time=STEP_TIME * offset / (frame.shape[1] // 2))
                move_step("forward", step_time=0.1)
                send_cmd("stop")
            else:
                print("⬅️ LEFT")
                move_step("left", step_time=STEP_TIME * abs(offset) / (frame.shape[1] // 2))
                move_step("forward", step_time=0.1)
                send_cmd("stop")

        else:
            # ❌ NO CUP FOUND
            found_frames = 0
            lost_frames += 1
            # print(f"❌ CUP LOST — {lost_frames}")
            send_cmd("stop")

            if lost_frames >= LOST_LIMIT and not searching:
                searching = True
                search_start = time.time()
                # print("🔍 SEARCH MODE — sweeping...")
            
            if searching:                
                # sweeping search
                if ttl_forward == 30:
                    for i in range(8):
                        move_step("left")
                    ttl_forward = 0
                    print("U-turn to re-search")

                if search_steps_left < SEARCH_STEPS_LR:
                    move_step("left")

                    search_steps_left += 1
                    print("🔎 scan step → LEFT")
                elif search_steps_right < SEARCH_STEPS_LR * 2:
                    move_step("right")
                    search_steps_right += 1
                    print("🔎 scan step → RIGHT")
                else:  
                    if search_steps_forward == 0:
                        for i in range(SEARCH_STEPS_LR):
                            move_step("left")
                            time.sleep(STEP_TIME)                            

                    if search_steps_forward < SEARCH_STEPS_FW:
                        move_step("forward")
                        print("🔎 scan step → FORWARD")
                        # lost_frames += 1
                        search_steps_forward += 1
                        ttl_forward += 1
                    else:                                    
                        search_steps_left = 0
                        search_steps_right = 0
                        search_steps_forward = 0
                        

                time.sleep(STEP_TIME)
                send_cmd("stop")
                time.sleep(PAUSE_TIME)

                # stop if too long without finding cup
                if time.time() - search_start > SEARCH_TIMEOUT:
                    print("⛔ search timeout")
                    searching = False
                    lost_frames_left = 0
                    lost_frames_forward = 0

                    send_cmd("stop")
        
        time.sleep(0.2)
        if cv2.waitKey(1) == ord("q"):
            send_cmd("stop")
            break

    except Exception as e:
        print("⚠️ Error:", e)
        send_cmd("stop")

send_cmd("stop")
cv2.destroyAllWindows()
print("👋 Exit")
