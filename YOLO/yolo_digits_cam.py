from ultralytics import YOLO
import cv2

# from cv2_pc_digit_test import main

def main():
    model = YOLO("C:\ASP\esp32car\yolov8-digits-detection\yolov8n.pt")  # path to the file you downloaded
    
    # Load a model

    cap = cv2.VideoCapture(0)

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        results = model.predict(frame, conf=0.4)
        annotated = results[0].plot()

        cv2.imshow("YOLO Digit Detector", annotated)

        if cv2.waitKey(1) == 27:  # ESC
            break

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    # model = YOLO("yolo11n-cls.pt")  # load a pretrained model (recommended for training)
    # model.train(data="mnist", epochs=100, imgsz=32)
    main()