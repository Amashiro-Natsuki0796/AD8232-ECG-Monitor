#!/usr/bin/env python3
"""
ECG波形模拟器 - 发送模拟的心电波形数据到指定服务器
"""
import socket
import math
import time
import json
import random
import threading
import numpy as np

class ECGSimulator:
    def __init__(self, host='60.215.128.117', port=23956):
        self.host = host
        self.port = port
        self.running = False
        self.socket = None
        
    def generate_ecg_signal(self, t):
        """
        生成模拟ECG波形
        使用数学函数模拟P波、QRS复合波和T波
        """
        # 心率 (bpm)
        heart_rate = 70 + random.uniform(-5, 5)  # 65-75 bpm，轻微波动
        heart_rate /= 60  # 转换为每秒心跳
        
        # 计算基本周期
        period = 1.0 / heart_rate
        
        # 将时间归一化到当前心跳周期
        t_normalized = t % period
        
        # ECG波形参数
        p_wave_start = 0.0
        p_wave_duration = 0.12  # P波持续时间
        qrs_start = 0.18  # QRS波群开始时间
        qrs_duration = 0.08  # QRS持续时间
        t_wave_start = 0.32  # T波开始时间
        t_wave_duration = 0.20  # T波持续时间
        
        signal = 0.0
        
        # 生成P波 (轻微正弦波)
        if p_wave_start <= t_normalized <= p_wave_start + p_wave_duration:
            wave_pos = (t_normalized - p_wave_start) / p_wave_duration
            signal += 0.2 * math.sin(math.pi * wave_pos)
        
        # 生成QRS复合波 (R波为主)
        if qrs_start <= t_normalized <= qrs_start + qrs_duration:
            wave_pos = (t_normalized - qrs_start) / qrs_duration
            # 模拟R波 (主峰) 和S波 (负峰)
            if wave_pos < 0.3:
                # Q波 (负向)
                signal += -0.1 * math.sin(math.pi * wave_pos / 0.3)
            elif wave_pos < 0.6:
                # R波 (正向主峰)
                signal += 2.0 * math.sin(math.pi * (wave_pos - 0.3) / 0.3)
            else:
                # S波 (负向)
                signal += -0.5 * math.sin(math.pi * (wave_pos - 0.6) / 0.4)
        
        # 生成T波
        if t_wave_start <= t_normalized <= t_wave_start + t_wave_duration:
            wave_pos = (t_normalized - t_wave_start) / t_wave_duration
            signal += 0.4 * math.sin(math.pi * wave_pos)
        
        # 添加一些生理噪音
        noise = random.uniform(-0.05, 0.05)
        signal += noise
        
        # 将信号放大到合适的范围 (模拟ADC读数)
        # 假设ADC是10位的 (0-1023)，心电信号幅度约几毫伏
        adc_value = 512 + signal * 100  # 基准512，信号范围±100
        
        # 确保值在合理范围内
        adc_value = max(0, min(1023, adc_value))
        
        return adc_value

    def connect(self):
        """连接到服务器"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(10)  # 10秒超时
            print(f"正在连接到 {self.host}:{self.port}...")
            self.socket.connect((self.host, self.port))
            print(f"成功连接到 {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"连接失败: {e}")
            return False

    def start(self):
        """开始发送ECG数据"""
        if not self.connect():
            return
            
        self.running = True
        print("开始发送ECG数据，按Ctrl+C停止...")
        
        start_time = time.time()
        sample_rate = 250  # 250 Hz采样率
        interval = 1.0 / sample_rate
        
        try:
            while self.running:
                current_time = time.time() - start_time
                ecg_value = self.generate_ecg_signal(current_time)
                
                # 创建JSON数据包
                data = {
                    'ecg_value': ecg_value,
                    'timestamp': time.time(),
                    'simulated': True
                }
                
                # 发送数据
                try:
                    self.socket.send(json.dumps(data).encode('utf-8') + b'\n')
                except socket.error as e:
                    print(f"发送数据失败: {e}")
                    # 尝试重新连接
                    if not self.connect():
                        break
                
                # 等待下一个采样时间点
                time.sleep(interval)
                
        except KeyboardInterrupt:
            print("\n停止发送数据...")
        finally:
            self.stop()

    def stop(self):
        """停止发送数据"""
        self.running = False
        if self.socket:
            self.socket.close()
        print("ECG模拟器已停止")


def main():
    simulator = ECGSimulator(host='60.215.128.117', port=23956)
    
    try:
        simulator.start()
    except KeyboardInterrupt:
        print("\n程序被用户中断")
    finally:
        simulator.stop()


if __name__ == "__main__":
    main()