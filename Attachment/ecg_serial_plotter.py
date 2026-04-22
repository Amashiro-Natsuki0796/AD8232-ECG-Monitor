#!/usr/bin/env python3
import sys
import serial
import numpy as np
from PyQt5.QtWidgets import QApplication, QMainWindow
from PyQt5.QtCore import QTimer
import pyqtgraph as pg
from scipy import signal

class MinimalECGPlotter(QMainWindow):
    def __init__(self):
        super().__init__()
        self.serial_conn = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
        self.SAMPLE_RATE = 250.0
        nyquist = 0.5 * self.SAMPLE_RATE
        self.b_high, self.a_high = signal.butter(2, 0.5/nyquist, btype='high')
        self.zi_high = signal.lfilter_zi(self.b_high, self.a_high) * 0.0
        self.raw_data = []
        self.filtered_data = []
        self.ma_buffer = []
        self.peaks = []
        self.last_peak_time = 0
        self.heart_rates = []
        self.sample_interval = 1.0 / self.SAMPLE_RATE
        self.rr_intervals = []
        self.current_heart_rate = 0
        self.threshold_factor = 0.6
        self.signal_peaks = []
        self.dynamic_threshold = 0.2
        self.peak_hold = 0.0
        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground('black')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.curve = self.plot_widget.plot(pen=pg.mkPen(color='green', width=2))
        self.hr_text = pg.TextItem(text="", color=(0, 255, 0), anchor=(0, 0))
        self.plot_widget.addItem(self.hr_text)
        self.hr_text.setPos(0.1, 1.0)
        self.setCentralWidget(self.plot_widget)
        self.timer = QTimer()
        self.timer.timeout.connect(self.update)
        self.timer.start(1)

    def update(self):
        try:
            line = self.serial_conn.readline().decode('utf-8').strip()
            if line:
                raw_value = float(line)
                voltage = raw_value * 3.3 / 1023.0
                high_out, self.zi_high = signal.lfilter(self.b_high, self.a_high, [voltage], zi=self.zi_high)
                filtered_value = high_out[0]
                self.ma_buffer.append(filtered_value)
                if len(self.ma_buffer) > 5:
                    self.ma_buffer.pop(0)
                final_filtered_value = sum(self.ma_buffer) / len(self.ma_buffer)
                self.filtered_data.append(final_filtered_value)
                current_time = len(self.filtered_data) * self.sample_interval
                self.detect_r_peak(final_filtered_value, current_time)
                display_data = self.filtered_data[-500:] if len(self.filtered_data) >= 500 else self.filtered_data
                y = np.array(display_data)
                x = np.arange(len(y))
                if len(y) > 0:
                    min_y, max_y = np.min(y), np.max(y)
                    center_y = (min_y + max_y) / 2
                    range_y = max(max_y - min_y, 2)
                    self.plot_widget.setYRange(center_y - range_y, center_y + range_y)
                self.curve.setData(x, y)
                self.plot_widget.setXRange(max(0, len(y)-500), max(500, len(y)), padding=0)
        except:
            pass

    def detect_r_peak(self, value, current_time):
        if len(self.signal_peaks) > 10:
            if len(self.signal_peaks) > 50:
                self.signal_peaks.pop(0)
            recent_max = max(self.signal_peaks[-20:]) if len(self.signal_peaks) >= 20 else max(self.signal_peaks)
            self.dynamic_threshold = max(0.1, recent_max * self.threshold_factor)
        else:
            self.dynamic_threshold = max(0.1, abs(value) * 0.5)
        min_rr_interval = 0.4
        if value > self.dynamic_threshold and value > self.peak_hold:
            time_since_last_peak = current_time - self.last_peak_time
            if time_since_last_peak > min_rr_interval:
                self.peak_hold = value
                self.signal_peaks.append(value)
                if self.last_peak_time > 0:
                    rr_interval = time_since_last_peak
                    self.rr_intervals.append(rr_interval)
                    current_hr = 60.0 / rr_interval
                    self.current_heart_rate = current_hr
                self.last_peak_time = current_time
        elif value < self.dynamic_threshold * 0.6:
            self.peak_hold = self.dynamic_threshold * 0.4
        hr_text_content = f"HR: {self.current_heart_rate:.0f}"
        self.hr_text.setText(hr_text_content)

app = QApplication([])
plotter = MinimalECGPlotter()
plotter.showMaximized()
app.exec_()