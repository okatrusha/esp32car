import cv2
import numpy as np
import requests

url = "http://192.168.4.1/capture"

while True:
    img_resp = requests.get(url)

    img_arr = np.frombuffer(img_resp.content, np.uint8)
    frame = cv2.imdecode(img_arr, cv2.IMREAD_COLOR)

    cv2.imshow("ESP-CAM Snapshot", frame)

    if cv2.waitKey(1) == ord('q'):
        break

cv2.destroyAllWindows()
