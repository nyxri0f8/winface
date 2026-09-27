"""Low-latency webcam capture: Media Foundation + MJPG 720p, background grab thread."""
import threading
import time

import cv2


class Camera:
    def __init__(self, index=0, width=1280, height=720):
        t = time.perf_counter()
        self.cap = cv2.VideoCapture(index, cv2.CAP_MSMF)
        self.cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        ok, frame = self.cap.read()
        if not ok:
            raise RuntimeError("camera did not deliver a frame")
        self.open_ms = (time.perf_counter() - t) * 1000
        self._frame, self._ts, self._seq = frame, time.perf_counter(), 1
        self._lock = threading.Lock()
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _loop(self):
        while self._running:
            ok, frame = self.cap.read()
            if ok:
                with self._lock:
                    self._frame, self._ts, self._seq = frame, time.perf_counter(), self._seq + 1

    def read(self):
        """Latest frame, its capture time and a sequence number (to skip duplicates)."""
        with self._lock:
            return self._frame, self._ts, self._seq

    def close(self):
        self._running = False
        self._thread.join(timeout=1)
        self.cap.release()
