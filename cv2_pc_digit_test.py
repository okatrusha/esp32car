import cv2
import numpy as np

def find_digit(gray):
    blur = cv2.GaussianBlur(gray, (5,5), 0)
    th = cv2.adaptiveThreshold(
        blur, 255,
        cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv2.THRESH_BINARY_INV,
        11, 2
    )

    cnts, _ = cv2.findContours(th, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    best_box = None
    best_area = 0

    for c in cnts:
        x, y, w, h = cv2.boundingRect(c)
        area = w * h

        if area < 500:  # ignore tiny blobs
            continue

        # heuristic: roughly square region → likely digit
        aspect = w/h
        if 0.3 < aspect < 1.5 and area > best_area:
            best_area = area
            best_box = (x, y, w, h)

    if best_box:
        x, y, w, h = best_box
        return th[y:y+h, x:x+w], best_box, th

    return None, None, th


def main():
    cap = cv2.VideoCapture(0)  # webcam

    while True:
        ret, frame = cap.read()
        if not ret: 
            break

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        digit_roi, bbox, th = find_digit(gray)

        if bbox:
            x,y,w,h = bbox
            cv2.rectangle(frame, (x,y), (x+w, y+h), (0,255,0), 2)
            cv2.putText(frame, "digit?", (x, y-5),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0,255,0), 2)

            # show extracted ROI (future model input)
            roi_vis = cv2.resize(digit_roi, (100,100))
            cv2.imshow("Digit ROI", roi_vis)

        cv2.imshow("Camera", frame)
        cv2.imshow("Threshold", th)

        key = cv2.waitKey(1)
        if key == 27:  # ESC to quit
            break

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
