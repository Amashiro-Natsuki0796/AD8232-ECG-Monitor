#!/usr/bin/env python3
"""
ECG TCP接收器和绘图器 - 实时绘制ECG数据
"""
import sys, socket, json, threading, queue, numpy as np
from PyQt5.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton
from PyQt5.QtCore import QTimer
import pyqtgraph as pg
from scipy import signal
from datetime import datetime
import time, os

class ECGDataReceiver:
    def __init__(self, host='0.0.0.0', port=8888, data_queue=None):
        self.host, self.port, self.socket, self.running, self.data_queue = host, port, None, False, data_queue
        
    def start_server(self):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.bind((self.host, self.port))
            self.socket.listen(1)
            self.running = True
            print(f"ECG监听于 {self.host}:{self.port}")
            
            while self.running:
                try:
                    conn, addr = self.socket.accept()
                    print(f"连接 {addr}")
                    threading.Thread(target=self.handle_client, args=(conn, addr), daemon=True).start()
                except socket.error as e:
                    if self.running: print(f"套接字错误: {e}")
        except Exception as e: print(f"服务器错误: {e}")
        finally:
            if self.socket: self.socket.close()
    
    def handle_client(self, conn, addr):
        try:
            buffer = ""
            while self.running:
                data = conn.recv(4096).decode('utf-8')
                if not data: break
                buffer += data
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()
                    if line:
                        try:
                            ecg_data = json.loads(line)
                            self.process_ecg_data(ecg_data, addr)
                        except json.JSONDecodeError:
                            continue
        except: pass
        finally:
            conn.close()
    
    def process_ecg_data(self, data, addr):
        ecg_value = data.get('ecg_value', 0)
        if self.data_queue:
            self.data_queue.put({'ecg_value': ecg_value})
    
    def stop_server(self):
        self.running = False
        if self.socket: self.socket.close()



class NetworkECGPlotter(QMainWindow):
    def __init__(self, host='localhost', port=8888):
        super().__init__()
        self.host, self.port = host, port
        self.data_queue = queue.Queue()
        self.receiver = ECGDataReceiver(data_queue=self.data_queue)
        
        # 初始化处理参数
        self.SAMPLE_RATE, nyquist = 250.0, 0.5 * 250.0
        self.b_high, self.a_high = signal.butter(2, 0.5/nyquist, btype='high')
        self.zi_high = signal.lfilter_zi(self.b_high, self.a_high) * 0.0
        self.filtered_data, self.signal_peaks, self.ma_buffer = [], [], []
        self.last_peak_time, self.current_heart_rate, self.rr_intervals = 0, 0, []
        self.threshold_factor, self.dynamic_threshold, self.peak_hold = 0.6, 0.2, 0.0
        self.sample_interval = 1.0 / self.SAMPLE_RATE
        
        # 异常检测参数
        self.no_pqrst_detected, self.vfib_detected, self.alarm_active, self.last_alarm_time = False, False, False, 0
        self.alarm_cooldown, self.delayed_alarm_duration, self.normal_reset_delay = 5, 10, 5  # 报警间隔5秒, 延迟警报10秒, 重置延迟5秒
        
        # 延迟警报参数
        self.arrhythmia_start_times, self.arrhythmia_accumulated_times, self.reset_start_times = {
            'no_pqrst': None,
            'vfib': None
        }, {
            'no_pqrst': 0,
            'vfib': 0
        }, {}
        
        # GUI设置
        central_widget = QWidget()
        layout = QVBoxLayout()
        central_widget.setLayout(layout)
        self.setCentralWidget(central_widget)
        
        # 控件区域
        self.control_panel = QWidget()
        self.control_panel.setStyleSheet("background-color: lightgray;")
        control_layout = QHBoxLayout()
        self.control_panel.setLayout(control_layout)
        
        self.hr_label = QLabel("HR: --")
        self.hr_label.setStyleSheet("color: red; font-size: 16px; font-weight: bold;")
        control_layout.addWidget(self.hr_label)
        
        self.alert_label = QLabel("状态: 正常")
        self.alert_label.setStyleSheet("color: blue; font-size: 16px;")
        control_layout.addWidget(self.alert_label)
        
        self.alert_toggle_button = QPushButton("关闭警报")
        self.alert_toggle_button.setStyleSheet("background-color: orange; color: white; font-weight: bold;")
        self.alert_toggle_button.clicked.connect(self.toggle_alerts)
        control_layout.addWidget(self.alert_toggle_button)
        
        layout.addWidget(self.control_panel)
        
        # 绘图组件
        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground('black')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.curve = self.plot_widget.plot(pen=pg.mkPen(color=(0, 255, 0), width=1))  # 绿色心电波形线
        layout.addWidget(self.plot_widget)
        
        self.alerts_enabled = True
        self.start_tcp_server()
        self.timer = QTimer()
        self.timer.timeout.connect(self.update)
        self.timer.start(1)  # 每毫秒更新一次
    
    def start_tcp_server(self):
        threading.Thread(target=self.receiver.start_server, daemon=True).start()
    
    def update(self):
        # 处理队列数据
        while not self.data_queue.empty():
            try:
                item = self.data_queue.get_nowait()
                raw_value = float(item['ecg_value'])
                self.process_ecg_sample(raw_value)
            except queue.Empty: break
        
        # 更新绘图
        display_data = self.filtered_data[-1000:] if len(self.filtered_data) >= 1000 else self.filtered_data
        if len(display_data) > 0:
            y = np.array(display_data)
            x = np.arange(len(y))
            
            min_y, max_y = np.min(y), np.max(y)
            center_y, range_y = (min_y + max_y) / 2, max(max_y - min_y, 2)
            self.plot_widget.setYRange(center_y - range_y, center_y + range_y)
            
            self.curve.setData(x, y)
            self.plot_widget.setXRange(max(0, len(y)-1000), max(1000, len(y)), padding=0)
        
        # 检测异常并更新UI
        self.detect_arrhythmias()
        self.hr_label.setText(f"HR: {self.current_heart_rate:.0f}")
        
        # 检查异常状态并计算持续时间
        current_time = time.time()
        
        # 获取各种异常类型的累计时间
        flatline_total = self.arrhythmia_accumulated_times['no_pqrst']
        if self.arrhythmia_start_times['no_pqrst'] is not None:
            flatline_total += current_time - self.arrhythmia_start_times['no_pqrst']
        
        vfib_total = self.arrhythmia_accumulated_times['vfib']
        if self.arrhythmia_start_times['vfib'] is not None:
            vfib_total += current_time - self.arrhythmia_start_times['vfib']
        
        # 确定主要异常类型
        max_total = max(flatline_total, vfib_total)
        
        # 更新警报状态标签
        if max_total > 0:
            if max_total >= self.delayed_alarm_duration:
                if flatline_total >= vfib_total:
                    self.alert_label.setText(f"警报: FLATLINE ({max_total:.1f}s)!")
                else:
                    self.alert_label.setText(f"警报: VFIB ({max_total:.1f}s)!")
                self.alert_label.setStyleSheet("color: orange; font-size: 16px; font-weight: bold;")
            else:
                # 显示检测到的异常，但尚未达到报警阈值
                if flatline_total > 0 and vfib_total > 0:
                    self.alert_label.setText(f"检测: FLATLINE & VFIB ({max_total:.1f}s)")
                elif flatline_total > 0:
                    self.alert_label.setText(f"检测: FLATLINE ({flatline_total:.1f}s)")
                else:
                    self.alert_label.setText(f"检测: VFIB ({vfib_total:.1f}s)")
                self.alert_label.setStyleSheet("color: blue; font-size: 16px;")
        else:
            self.alert_label.setText("状态: 正常")
            self.alert_label.setStyleSheet("color: blue; font-size: 16px;")
    
    def process_ecg_sample(self, raw_value):
        voltage = raw_value * 3.3 / 1023.0
        high_out, self.zi_high = signal.lfilter(self.b_high, self.a_high, [voltage], zi=self.zi_high)
        filtered_value = high_out[0]
        self.ma_buffer.append(filtered_value)
        if len(self.ma_buffer) > 5: self.ma_buffer.pop(0)
        final_filtered_value = sum(self.ma_buffer) / len(self.ma_buffer)
        self.filtered_data.append(final_filtered_value)
        current_time = len(self.filtered_data) * self.sample_interval
        self.detect_r_peak(final_filtered_value, current_time)
    
    def detect_r_peak(self, value, current_time):
        # 维护峰值列表，限制大小
        if len(self.signal_peaks) > 50:
            self.signal_peaks.pop(0)
        
        # 动态调整阈值，但要稳定，避免过度调整
        if len(self.signal_peaks) >= 10:
            # 使用较稳定的平均值而非最大值，避免阈值剧烈变化
            recent_avg = np.mean(self.signal_peaks[-10:])
            self.dynamic_threshold = max(0.1, recent_avg * self.threshold_factor * 1.2)  # 略微提高阈值稳定性
        else:
            self.dynamic_threshold = max(0.1, abs(value) * 0.5)
        
        min_rr_interval = 0.4  # 最小RR间距，防止过快的心率计算
        if value > self.dynamic_threshold and value > self.peak_hold:
            time_since_last_peak = current_time - self.last_peak_time
            if time_since_last_peak > min_rr_interval:
                self.peak_hold = value
                self.signal_peaks.append(value)
                if self.last_peak_time > 0:
                    rr_interval = time_since_last_peak
                    self.rr_intervals.append(rr_interval)
                    # 计算心率，限制在合理范围内
                    calculated_hr = 60.0 / rr_interval
                    # 限制心率在30-250 BPM之间，避免异常值
                    self.current_heart_rate = max(30, min(250, calculated_hr))
                self.last_peak_time = current_time
        elif value < self.dynamic_threshold * 0.6:
            # 逐渐释放峰值持有值，避免锁定
            self.peak_hold *= 0.95  # 缓慢衰减
    
    def detect_arrhythmias(self):
        self.no_pqrst_detected = False
        self.vfib_detected = False
        
        if len(self.filtered_data) >= 100:  # 需要更多数据来进行VFib检测
            recent_signal = np.array(self.filtered_data[-100:])
            signal_std, signal_range = np.std(recent_signal), np.max(recent_signal) - np.min(recent_signal)
            
            # Flatline检测：信号几乎无变化
            if signal_std < 0.01 and signal_range < 0.05:
                self.no_pqrst_detected = True
            
            # 室颤检测：高频率、不规则、混乱的信号（更激进的检测）
            # VFib的特点是高频、无明显QRS波、信号混乱
            if signal_std > 0.16:  # 提高标准差阈值，降低敏感度
                # 检查信号的不规则性
                from scipy.signal import find_peaks
                # 查找局部峰值（适度的高度阈值，减少噪声触发）
                peaks, _ = find_peaks(recent_signal, height=np.mean(recent_signal)*0.4, distance=4)
                peak_count = len(peaks)
                
                # 检查零交叉率（VFib通常有高频率变化）
                zero_crossings = np.where(np.diff(np.sign(recent_signal)))[0].size
                
                # 检查信号熵（混乱程度）
                # 归一化信号
                normalized_signal = (recent_signal - np.mean(recent_signal)) / (np.std(recent_signal) + 1e-10)
                # 计算近似熵相关指标
                signal_deriv = np.diff(normalized_signal)
                deriv_variance = np.var(signal_deriv)
                
                # 多个指标综合判断VFib
                if (peak_count > 4 or  # 峰值数量多
                    zero_crossings > 30 or  # 零交叉频繁
                    (peak_count > 5 and signal_std > 0.2) or  # 峰值多且变化大
                    (deriv_variance > 0.5 and peak_count > 6)):  # 导数方差大且有足够峰值
                    self.vfib_detected = True
        
        current_time = time.time()
        self.update_arrhythmia_timers(current_time)
        
        # 检查是否有任何异常持续超过阈值时间
        total_time = 0
        for arr_type in ['no_pqrst', 'vfib']:
            temp_time = self.arrhythmia_accumulated_times[arr_type]
            if self.arrhythmia_start_times[arr_type] is not None:
                temp_time += current_time - self.arrhythmia_start_times[arr_type]
            total_time = max(total_time, temp_time)
        
        if total_time >= self.delayed_alarm_duration:
            if not self.alarm_active or (current_time - self.last_alarm_time) > self.alarm_cooldown:
                self.trigger_alarm()
                self.alarm_active = True
                self.last_alarm_time = current_time
        else:
            self.alarm_active = False
    
    def trigger_alarm(self):
        if not self.alerts_enabled: return
        import threading
        threading.Thread(target=self.play_sound_alarm, daemon=True).start()
    
    def play_sound_alarm(self):
        try:
            if os.name == 'nt':
                import winsound
                winsound.Beep(800, 500)
            else:
                result = os.system("beep -f 800 -l 500 2>/dev/null")
                if result != 0: result = os.system("speaker-test -t sine -f 800 -l 1 2>/dev/null")
                if result != 0: result = os.system("echo 'WARNING' | festival --tts 2>/dev/null")
                if result != 0: print("\a", end='', flush=True); print("警报：检测到危险心律失常！", flush=True)
        except Exception as e:
            print(f"警报失败: {e}")
            print("!!! 紧急警报 !!!", flush=True)
    


    def update_arrhythmia_timers(self, current_time):
        # 为每种异常类型更新计时器
        for arr_type in ['no_pqrst', 'vfib']:
            is_detected = getattr(self, f"{arr_type}_detected")
            if is_detected:
                if self.arrhythmia_start_times[arr_type] is None:
                    if arr_type in self.reset_start_times: del self.reset_start_times[arr_type]
                    self.arrhythmia_start_times[arr_type] = current_time
            else:
                if self.arrhythmia_start_times[arr_type] is not None:
                    accumulated = current_time - self.arrhythmia_start_times[arr_type]
                    self.arrhythmia_accumulated_times[arr_type] += accumulated
                    self.arrhythmia_start_times[arr_type] = None
                    self.reset_start_times[arr_type] = current_time
                elif arr_type in self.reset_start_times:
                    if current_time - self.reset_start_times[arr_type] >= self.normal_reset_delay:
                        self.arrhythmia_accumulated_times[arr_type] = 0
                        del self.reset_start_times[arr_type]

    def toggle_alerts(self):
        if self.alerts_enabled:
            self.alerts_enabled = False
            self.alert_toggle_button.setText("启用警报")
            self.alert_toggle_button.setStyleSheet("background-color: green; color: white; font-weight: bold;")
        else:
            self.alerts_enabled = True
            self.alert_toggle_button.setText("关闭警报")
            self.alert_toggle_button.setStyleSheet("background-color: orange; color: white; font-weight: bold;")

    def closeEvent(self, event):
        self.receiver.stop_server()
        event.accept()

def main():
    port = 8888
    if len(sys.argv) > 1:
        try: port = int(sys.argv[1])
        except ValueError: print("端口号无效，使用默认8888")
    app = QApplication([])
    plotter = NetworkECGPlotter(port=port)
    plotter.showMaximized()
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()