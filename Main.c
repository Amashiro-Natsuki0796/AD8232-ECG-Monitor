#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <errno.h>
#include <stdint.h>
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <linux/input.h>
#include <linux/uinput.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SR 250.0
#define MAX_DP 750
#define PORT "/dev/ttyS0"
#define BR B115200
#define BS 1024

// 显示参数 - 树莓派Zero 480x320 分辨率 16bpp
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 320
#define BYTES_PER_PIXEL 2  // 16bpp = 2 bytes per pixel

// 网络参数
#define SERVER_IP "**.***.***.***"
#define SERVER_PORT 23956
int network_socket = -1;
time_t last_connect_attempt = 0;
const int RECONNECT_INTERVAL = 5; // 重连间隔5秒

// 数据缓冲区，每0.5秒发送一次数据
#define NETWORK_BUFFER_SIZE 2048
char network_buffer[NETWORK_BUFFER_SIZE]; // 缓冲区用于存储0.5秒的数据
int buffer_pos = 0;
struct timeval last_send_time; // 使用timeval记录精确时间
double SEND_INTERVAL = 0.5; // 每0.5秒发送一次数据
int consecutive_send_failures = 0; // 连续发送失败次数
const int MAX_CONSECUTIVE_FAILURES = 3; // 最大连续失败次数后重置连接

// 触摸屏参数
#define TOUCH_DEVICE "/dev/input/event0"
int touch_fd = -1;
int paused = 0; // 暂停标志
// 关机功能相关变量
int touch_down = 0; // 触摸按下状态
struct timeval touch_start_time; // 触摸开始时间
int shutdown_triggered = 0; // 关机是否已触发

// 滤波器参数
double b_h[3], a_h[3], z_h[2];
double f_d[MAX_DP], ma_b[5];
int dc = 0, mc = 0;

// BPM计算相关变量
double last_peak_time = 0.0;
double current_heart_rate = 0.0;
double signal_peaks[50];  // 存储最近的峰值
int peak_count = 0;
double rr_intervals[50];  // 存储RR间期
int rr_count = 0;
double dynamic_threshold = 0.1;  // 动态阈值
double peak_hold = 0.0;          // 峰值保持
double last_voltage = 0.0;       // 上次电压值用于检测下降沿
int refractory_period_counter = 0; // 不应期计数器
const int REFRACTORY_PERIOD = 50;  // 不应期（采样点数）
double start_time = 0.0;           // 开始时间戳

// 函数声明
void draw_pixel(int x, int y, uint16_t color);

// framebuffer相关变量
int fb_fd = -1;
char *fb_ptr = NULL;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;

// 字符大小
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 12

// ASCII字符字模数据 (简化版，只包含数字和基本字符)
const unsigned char font_data[96][12] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' ' (32)
    {0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x00,0x10,0x00,0x00,0x00}, // '!' (33)
    {0x28,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '"' (34)
    {0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00}, // '#' (35)
    {0x18,0x3C,0x66,0x24,0x18,0x3C,0x66,0x66,0x3C,0x18,0x00,0x00}, // '$' (36)
    {0x00,0xC6,0xCC,0x18,0x30,0x60,0xC0,0x66,0x3C,0x00,0x00,0x00}, // '%' (37)
    {0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0x76,0x00,0x00,0x00}, // '&' (38)
    {0x10,0x10,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ''' (39)
    {0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00}, // '(' (40)
    {0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00}, // ')' (41)
    {0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}, // '*' (42)
    {0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00,0x00,0x00,0x00}, // '+' (43)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00}, // ',' (44)
    {0x00,0x00,0x00,0x00,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '-' (45)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00}, // '.' (46)
    {0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00}, // '/' (47)
    {0x38,0x6C,0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0x6C,0x38,0x00,0x00}, // '0' (48)
    {0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00}, // '1' (49)
    {0x3C,0x66,0x06,0x06,0x0C,0x18,0x30,0x60,0x60,0x7E,0x00,0x00}, // '2' (50)
    {0x3C,0x66,0x06,0x06,0x1C,0x06,0x06,0x06,0x66,0x3C,0x00,0x00}, // '3' (51)
    {0x06,0x0E,0x1E,0x66,0x66,0x66,0x7F,0x06,0x06,0x06,0x00,0x00}, // '4' (52)
    {0x7E,0x60,0x60,0x60,0x7C,0x06,0x06,0x06,0x66,0x3C,0x00,0x00}, // '5' (53)
    {0x1C,0x30,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x3C,0x00,0x00}, // '6' (54)
    {0x7E,0x66,0x06,0x06,0x0C,0x18,0x18,0x18,0x18,0x18,0x00,0x00}, // '7' (55)
    {0x3C,0x66,0x66,0x66,0x3C,0x66,0x66,0x66,0x66,0x3C,0x00,0x00}, // '8' (56)
    {0x3C,0x66,0x66,0x66,0x66,0x3E,0x06,0x06,0x66,0x3C,0x00,0x00}, // '9' (57)
    {0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00}, // ':' (58)
    {0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00}, // ';' (59)
    {0x0C,0x18,0x30,0x60,0xC0,0x60,0x30,0x18,0x0C,0x00,0x00,0x00}, // '<' (60)
    {0x00,0x00,0x00,0xFE,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00}, // '=' (61)
    {0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00}, // '>' (62)
    {0x3C,0x66,0x06,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // '?' (63)
    {0x38,0x4C,0xBA,0xBA,0xB8,0xB0,0xB0,0xB0,0x6C,0x38,0x00,0x00}, // '@' (64)
    {0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00}, // 'A' (65)
    {0x7C,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00}, // 'B' (66)
    {0x3C,0x66,0x60,0x60,0x60,0x60,0x60,0x60,0x66,0x3C,0x00,0x00}, // 'C' (67)
    {0x78,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0x78,0x00,0x00}, // 'D' (68)
    {0x7E,0x60,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x7E,0x00,0x00}, // 'E' (69)
    {0x7E,0x60,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x60,0x00,0x00}, // 'F' (70)
    {0x3C,0x66,0x06,0x06,0x06,0x7E,0x66,0x66,0x66,0x3C,0x00,0x00}, // 'G' (71)
    {0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00}, // 'H' (72)
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00}, // 'I' (73)
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0x78,0x00,0x00}, // 'J' (74)
    {0xC6,0xCC,0xD8,0xF0,0xE0,0xF0,0xD8,0xCC,0xC6,0xC6,0x00,0x00}, // 'K' (75)
    {0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00,0x00}, // 'L' (76)
    {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00}, // 'M' (77)
    {0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00}, // 'N' (78)
    {0x38,0x6C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00,0x00}, // 'O' (79)
    {0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0x60,0x00,0x00}, // 'P' (80)
    {0x38,0x6C,0xC6,0xC6,0xC6,0xC6,0xCE,0x6C,0x6C,0x3A,0x00,0x00}, // 'Q' (81)
    {0x7C,0x66,0x66,0x66,0x7C,0x78,0x6C,0x66,0x66,0x66,0x00,0x00}, // 'R' (82)
    {0x3C,0x66,0x60,0x60,0x38,0x0C,0x06,0x06,0x66,0x3C,0x00,0x00}, // 'S' (83)
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00}, // 'T' (84)
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x3C,0x00,0x00}, // 'U' (85)
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00}, // 'V' (86)
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xFE,0xFE,0xEE,0xC6,0x00,0x00}, // 'W' (87)
    {0xC6,0xC6,0x6C,0x6C,0x38,0x38,0x6C,0x6C,0xC6,0xC6,0x00,0x00}, // 'X' (88)
    {0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x10,0x10,0x10,0x10,0x00,0x00}, // 'Y' (89)
    {0x7E,0x06,0x06,0x0C,0x18,0x30,0x60,0x60,0x60,0x7E,0x00,0x00}, // 'Z' (90)
    {0x7C,0x66,0x66,0x60,0x60,0x60,0x60,0x60,0x66,0x7C,0x00,0x00}, // '[' (91)
    {0x00,0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,0x00,0x00}, // '\' (92)
    {0x3E,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x3E,0x00,0x00}, // ']' (93)
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '^' (94)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00}, // '_' (95)
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '`' (96)
    {0x00,0x00,0x00,0x00,0x38,0x0C,0x3C,0x6C,0x6C,0x3E,0x00,0x00}, // 'a' (97)
    {0x60,0x60,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00}, // 'b' (98)
    {0x00,0x00,0x00,0x00,0x3C,0x66,0x60,0x60,0x66,0x3C,0x00,0x00}, // 'c' (99)
    {0x06,0x06,0x06,0x06,0x3E,0x66,0x66,0x66,0x66,0x3E,0x00,0x00}, // 'd' (100)
    {0x00,0x00,0x00,0x00,0x3C,0x66,0x7E,0x60,0x66,0x3C,0x00,0x00}, // 'e' (101)
    {0x1C,0x36,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x30,0x00,0x00}, // 'f' (102)
    {0x00,0x00,0x00,0x00,0x3E,0x66,0x66,0x66,0x3E,0x06,0x3C,0x00}, // 'g' (103)
    {0x60,0x60,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x00,0x00}, // 'h' (104)
    {0x18,0x18,0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x3C,0x00,0x00}, // 'i' (105)
    {0x06,0x06,0x00,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x3C,0x00}, // 'j' (106)
    {0x60,0x60,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0x00,0x00}, // 'k' (107)
    {0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00}, // 'l' (108)
    {0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0x00,0x00}, // 'm' (109)
    {0x00,0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x66,0x00,0x00}, // 'n' (110)
    {0x00,0x00,0x00,0x00,0x38,0x6C,0x6C,0x6C,0x6C,0x38,0x00,0x00}, // 'o' (111)
    {0x00,0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x60}, // 'p' (112)
    {0x00,0x00,0x00,0x00,0x3E,0x66,0x66,0x66,0x3E,0x06,0x06,0x06}, // 'q' (113)
    {0x00,0x00,0x00,0x00,0x6C,0x76,0x60,0x60,0x60,0x60,0x00,0x00}, // 'r' (114)
    {0x00,0x00,0x00,0x00,0x3C,0x60,0x38,0x0C,0x66,0x3C,0x00,0x00}, // 's' (115)
    {0x30,0x30,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x1C,0x00,0x00}, // 't' (116)
    {0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x3E,0x00,0x00}, // 'u' (117)
    {0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00}, // 'v' (118)
    {0x00,0x00,0x00,0x00,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00}, // 'w' (119)
    {0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x6C,0xC6,0x00,0x00}, // 'x' (120)
    {0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x06,0x06,0x3C}, // 'y' (121)
    {0x00,0x00,0x00,0x00,0x7E,0x18,0x30,0x60,0x7E,0x00,0x00,0x00}, // 'z' (122)
    {0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00}, // '{' (123)
    {0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00}, // '|' (124)
    {0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00}, // '}' (125)
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '~' (126)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}  // DEL (127)
};

// 函数声明
void add_to_network_buffer(double value, double timestamp);
void flush_network_buffer();
void check_and_reconnect();
int init_touchscreen();
void handle_touch_events();

// 将RGB888转换为RGB565
uint16_t rgb888_to_rgb565(int r, int g, int b) {
    r = (r >> 3) & 0x1F;  // 8位红转5位
    g = (g >> 2) & 0x3F;  // 8位绿转6位
    b = (b >> 3) & 0x1F;  // 8位蓝转5位
    return (r << 11) | (g << 5) | b;
}

// 在framebuffer上绘制字符
void draw_char(int x, int y, char c, uint16_t color) {
    if (c < 32 || c > 127) return; // 只支持ASCII 32-127
    
    int char_index = c - 32;
    
    for (int row = 0; row < CHAR_HEIGHT; row++) {
        unsigned char byte = font_data[char_index][row];
        for (int col = 0; col < 8; col++) {
            if ((byte >> (7 - col)) & 1) {
                draw_pixel(x + col, y + row, color);
            }
        }
    }
}

// 在framebuffer上绘制字符串
void draw_string(int x, int y, const char* str, uint16_t color) {
    int orig_x = x;
    while (*str) {
        if (*str == '\n') {
            y += CHAR_HEIGHT + 2;
            x = orig_x;
        } else {
            draw_char(x, y, *str, color);
            x += CHAR_WIDTH;
        }
        str++;
    }
}

// 初始化framebuffer
int init_framebuffer() {
    fb_fd = open("/dev/fb1", O_RDWR);
    if (fb_fd == -1) {
        fb_fd = open("/dev/fb0", O_RDWR);
        if (fb_fd == -1) {
            return -1;
        }
    }
    
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo)) {
        close(fb_fd);
        return -1;
    }
    
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        close(fb_fd);
        return -1;
    }
    
    // 映射framebuffer到内存
    fb_ptr = (char *)mmap(0, 
                         finfo.smem_len, 
                         PROT_READ | PROT_WRITE, 
                         MAP_SHARED, 
                         fb_fd, 
                         0);
    
    if (fb_ptr == (char *)-1) {
        close(fb_fd);
        return -1;
    }
    
    return 0;
}

// 初始化触摸屏
int init_touchscreen() {
    touch_fd = open(TOUCH_DEVICE, O_RDONLY | O_NONBLOCK);
    if (touch_fd < 0) {
        return -1;
    }
    return 0;
}

// 处理触摸事件
void handle_touch_events() {
    if (touch_fd < 0) {
        return;
    }
    
    // Linux输入事件结构体
    struct input_event ev;
    ssize_t n;
    
    // 读取所有可用的触摸事件
    while ((n = read(touch_fd, &ev, sizeof(struct input_event))) > 0) {
        // 检查是否是按键事件（按下或释放）
        if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1) { // 按下触摸屏
                touch_down = 1;
                gettimeofday(&touch_start_time, NULL); // 记录按下时间
                shutdown_triggered = 0; // 重置关机触发状态
            } else if (ev.value == 0) { // 抬起触摸屏
                touch_down = 0;
                // 检查触摸持续时间，如果小于3秒则切换暂停状态
                struct timeval current_time;
                gettimeofday(&current_time, NULL);
                double elapsed = (current_time.tv_sec - touch_start_time.tv_sec) + 
                                (current_time.tv_usec - touch_start_time.tv_usec) / 1000000.0;
                
                // 如果触摸时间少于0.3秒且未触发关机，则切换暂停状态
                if (elapsed < 0.3 && !shutdown_triggered) {
                    paused = !paused; // 切换暂停状态
                }
            }
        }
    }
}

// 检查长按是否需要关机
void check_long_press_shutdown() {
    if (touch_down && !shutdown_triggered) {
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        
        double elapsed = (current_time.tv_sec - touch_start_time.tv_sec) + 
                        (current_time.tv_usec - touch_start_time.tv_usec) / 1000000.0;
        
        if (elapsed >= 3.0) { // 长按3秒触发关机
            shutdown_triggered = 1;
            system("sudo shutdown -h now"); // 执行关机命令
        }
    }
}

// 绘制关机进度条
void draw_shutdown_progress() {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    double elapsed = 0;
    int is_counting = 0;
    
    // 如果正在触摸且未触发关机，则计算长按时间
    if (touch_down && !shutdown_triggered) {
        elapsed = (current_time.tv_sec - touch_start_time.tv_sec) + 
                (current_time.tv_usec - touch_start_time.tv_usec) / 1000000.0;
        is_counting = 1;
    }
    
    // 在右上角绘制进度条，与NET状态底部对齐
    int bar_width = (int)(fmin(elapsed / 3.0, 1.0) * 100); // 最大宽度100像素
    int bar_height = 10; // 高度10像素
    int bar_x = SCREEN_WIDTH - 110; // 右边距110像素
    int bar_y = 25; // 上边距25像素，与NET状态Y坐标相同，这样底部对齐（NET状态高度约12像素，进度条高度10像素）
    
    // 绘制进度条背景（灰色）
    uint16_t gray_color = rgb888_to_rgb565(128, 128, 128);
    for (int y = bar_y; y < bar_y + bar_height; y++) {
        for (int x = bar_x; x < bar_x + 100; x++) {
            draw_pixel(x, y, gray_color);
        }
    }
    
    // 绘制进度（紫色）
    uint16_t purple_color = rgb888_to_rgb565(128, 0, 128);
    for (int y = bar_y; y < bar_y + bar_height; y++) {
        for (int x = bar_x; x < bar_x + bar_width; x++) {
            if (is_counting) { // 只有在计时过程中才显示进度
                draw_pixel(x, y, purple_color);
            }
        }
    }
    
    // 绘制"Shutdown"文字
    uint16_t white_color = rgb888_to_rgb565(255, 255, 255);
    draw_string(bar_x, bar_y - 15, "# SHUTDOWN", white_color); // 文字位置也要相应调整
}

// 清屏为黑色
void clear_screen() {
    if (fb_ptr && (vinfo.xres * vinfo.yres * BYTES_PER_PIXEL <= finfo.smem_len)) {
        memset(fb_ptr, 0, vinfo.xres * vinfo.yres * BYTES_PER_PIXEL);
    }
}

// 隐藏光标
void hide_cursor() {
    system("setterm -cursor off 2>/dev/null || true");
}

// 显示光标
void show_cursor() {
    system("setterm -cursor on 2>/dev/null || true");
}

// 在framebuffer上绘制像素点
void draw_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && (unsigned int)x < vinfo.xres && y >= 0 && (unsigned int)y < vinfo.yres) {
        long location = (x + vinfo.xoffset) * BYTES_PER_PIXEL + (y + vinfo.yoffset) * finfo.line_length;
        *(uint16_t*)(fb_ptr + location) = color;
    }
}

// 使用Bresenham算法绘制直线
void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
    int dy = abs(y1-y0), sy = y0<y1 ? 1 : -1; 
    int err = (dx>dy ? dx : -dy)/2, e2;
 
    for(;;){
        draw_pixel(x0, y0, color);
        if (x0==x1 && y0==y1) break;
        e2 = err;
        if (e2 >-dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

// 绘制ECG波形到framebuffer
void draw_ecg_waveform() {
    if (!fb_ptr) return;
    
    // 只清除波形区域，保持背景黑色
    // 计算波形显示区域的行数，仅清空这部分
    int start_row = 0;
    unsigned int end_row = vinfo.yres;
    
    // 清除波形区域（设为黑色）
    for (unsigned int y = start_row; y < end_row; y++) {
        for (unsigned int x = 0; x < vinfo.xres; x++) {
            long location = (x + vinfo.xoffset) * BYTES_PER_PIXEL + (y + vinfo.yoffset) * finfo.line_length;
            if (location < (long)finfo.smem_len) {
                *(uint16_t*)(fb_ptr + location) = 0x0000; // 黑色
            }
        }
    }

    if (dc > 1) {
        uint16_t green_color = rgb888_to_rgb565(0, 255, 0);  // 绿色
        
        int ds = (dc > MAX_DP) ? dc - MAX_DP : 0;
        int dco = dc - ds;
        
        if (dco > 1) {
            double dd[MAX_DP];
            for (int i = ds; i < dc; i++) dd[i - ds] = f_d[i];
            
            double min_y = dd[0], max_y = dd[0];
            for (int i = 0; i < dco; i++) {
                if (dd[i] < min_y) min_y = dd[i];
                if (dd[i] > max_y) max_y = dd[i];
            }
            
            double cy = (min_y + max_y) / 2.0;
            double ry = max_y - min_y;
            if (ry < 2.0) ry = 2.0;
            
            double ymin = cy - ry;
            double ymax = cy + ry;
            
            // 绘制波形线
            for (int i = 1; i < dco; i++) {
                int x1 = (i - 1) * vinfo.xres / dco;
                int x2 = i * vinfo.xres / dco;
                int y1 = (int)((dd[i-1] - ymin) / (ymax - ymin) * vinfo.yres);
                int y2 = (int)((dd[i] - ymin) / (ymax - ymin) * vinfo.yres);
                
                // 转换Y坐标（因为framebuffer的原点在左上角，而我们的数据是正向的）
                y1 = vinfo.yres - y1;
                y2 = vinfo.yres - y2;
                
                draw_line(x1, y1, x2, y2, green_color);
            }
        }
    }
    
    // 绘制BPM数值
    uint16_t red_color = rgb888_to_rgb565(255, 0, 0);  // 红色
    char bpm_text[20];
    snprintf(bpm_text, sizeof(bpm_text), "BPM: %.0f", current_heart_rate);
    draw_string(10, 10, bpm_text, red_color);  // 在左上角显示BPM
    
    // 绘制网络连接状态
    uint16_t net_status_color;
    char net_status_text[30];
    if (network_socket >= 0) {
        net_status_color = rgb888_to_rgb565(173, 216, 230);  // 浅蓝色
        strcpy(net_status_text, "NET: Connected");
    } else {
        net_status_color = rgb888_to_rgb565(255, 165, 0);     // 橙色
        strcpy(net_status_text, "NET: Disconnected");
    }
    draw_string(10, 25, net_status_text, net_status_color);  // 在BPM下方显示网络状态
}

void init_filter() {
    double nq = 0.5 * SR, Wn = 0.5 / nq;
    double a = tan(M_PI * Wn), as = a * a, f = as + 2*a + 1;
    b_h[0] = 1.0/f; b_h[1] = -2.0/f; b_h[2] = 1.0/f;
    a_h[0] = 1.0; a_h[1] = (2.0 * (as - 1))/f; a_h[2] = (as - 2*a + 1)/f;
    z_h[0] = 0.0; z_h[1] = 0.0;
}

double filter(double in) {
    double out = b_h[0] * in + z_h[0];
    z_h[0] = b_h[1] * in - a_h[1] * out + z_h[1];
    z_h[1] = b_h[2] * in - a_h[2] * out;
    return out;
}

int open_port(const char* p) {
    int fd = open(p, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) { 
        return -1; 
    }
    
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) { 
        close(fd);
        return -1; 
    }
    
    cfsetospeed(&tty, BR);
    cfsetispeed(&tty, BR);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    #ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
    #else
    tty.c_cflag &= ~020000000000;
    #endif
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;
    
    if (tcsetattr(fd, TCSANOW, &tty) != 0) { 
        close(fd);
        return -1; 
    }
    return fd;
}

void detect_peak(double v, double ct) {
    // 动态阈值更新 - 根据Python版本的算法
    // 先更新数组，然后基于数组内容计算阈值
    // 添加到signal_peaks数组
    if (peak_count >= 50) {
        // 数组已满，向前移动元素并添加新值，但不增加计数
        memmove(signal_peaks, signal_peaks + 1, 49 * sizeof(double));
        signal_peaks[49] = v;
    } else {
        signal_peaks[peak_count] = v;
        if (peak_count < 49) {  // 确保不会超过数组边界
            peak_count++;
        } else {
            peak_count = 50;  // 达到最大值
        }
    }
    
    if (peak_count > 10) {
        // 找到最近的峰值中的最大值
        double recent_max = signal_peaks[0];
        int elements_to_consider = (peak_count >= 20) ? 20 : peak_count;
        int actual_count = (peak_count >= 50) ? 50 : peak_count;  // 实际考虑的元素数量
        int start_idx = (actual_count >= elements_to_consider) ? (actual_count - elements_to_consider) : 0;
        
        for (int i = start_idx; i < actual_count; i++) {
            if (signal_peaks[i] > recent_max) {
                recent_max = signal_peaks[i];
            }
        }
        
        // 使用Python版本的阈值计算方法
        dynamic_threshold = fmax(0.1, recent_max * 0.6);
    } else {
        // 初始阈值 - 使用Python版本的方法
        dynamic_threshold = fmax(0.1, fabs(v) * 0.5);
    }

    // 使用Python版本的最小RR间隔
    const double min_rr_interval = 0.4;
    
    // 检测R波峰值 - 类似Python版本的逻辑
    if (v > dynamic_threshold && v > peak_hold) {
        double time_since_last_peak = ct - last_peak_time;
        if (time_since_last_peak > min_rr_interval) {
            peak_hold = v;
            // 添加到signal_peaks数组
            if (peak_count >= 50) {
                // 数组已满，向前移动元素并添加新值，但不增加计数
                memmove(signal_peaks, signal_peaks + 1, 49 * sizeof(double));
                signal_peaks[49] = v;
            } else {
                signal_peaks[peak_count] = v;
                peak_count++;
            }
            
            if (last_peak_time > 0.0) {
                double rr_interval = time_since_last_peak;
                // 添加RR间隔到数组
                if (rr_count >= 50) {
                    // 数组已满，向前移动元素并添加新值，但不增加计数
                    memmove(rr_intervals, rr_intervals + 1, 49 * sizeof(double));
                    rr_intervals[49] = rr_interval;
                } else {
                    rr_intervals[rr_count] = rr_interval;
                    rr_count++;
                }
                // 直接使用最新的RR间隔计算心率（类似Python版本）
                current_heart_rate = 60.0 / rr_interval;
            }
            last_peak_time = ct;
        }
    } else if (v < dynamic_threshold * 0.6) {
        peak_hold = dynamic_threshold * 0.4;
    }
    
    if (refractory_period_counter > 0) {
        refractory_period_counter--;
    }
    
    // 保存当前电压值用于后续处理
    last_voltage = v;
}

#include <time.h>

// TCP连接函数
int connect_to_server() {
    struct sockaddr_in server_addr;
    
    // 创建socket
    network_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (network_socket < 0) {
        network_socket = -1;
        return -1;
    }
    
    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    // 将IP地址转换为网络地址
    if (inet_aton(SERVER_IP, &server_addr.sin_addr) == 0) {
        close(network_socket);
        network_socket = -1;
        return -1;
    }
    
    // 设置连接超时
    struct timeval timeout;
    timeout.tv_sec = 3;  // 3秒超时
    timeout.tv_usec = 0;
    setsockopt(network_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(network_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // 连接到服务器
    if (connect(network_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(network_socket);
        network_socket = -1;
        return -1;
    }
    
    return 0;
}

// 检查并维护网络连接
void check_and_reconnect() {
    time_t now = time(NULL);
    
    // 如果没有连接且达到重连时间，则尝试连接
    if (network_socket < 0 && (now - last_connect_attempt) >= RECONNECT_INTERVAL) {
        connect_to_server();
        last_connect_attempt = now;
    }
    
    // 定期刷新网络缓冲区（使用更精确的时间检查）
    if (network_socket >= 0 && buffer_pos > 0) {
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        
        double elapsed = (current_time.tv_sec - last_send_time.tv_sec) +
                        (current_time.tv_usec - last_send_time.tv_usec) / 1000000.0;
        
        if (elapsed >= SEND_INTERVAL) {
            flush_network_buffer();
            gettimeofday(&last_send_time, NULL); // 更新发送时间
        }
    }
    
    // 检查现有连接是否仍然有效（尝试接收少量数据来检测断开）
    if (network_socket >= 0) {
        char test_byte;
        int result = recv(network_socket, &test_byte, 1, MSG_PEEK | MSG_DONTWAIT);
        if (result == 0) {
            // 连接被对方关闭
            close(network_socket);
            network_socket = -1;
        } else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // 发生错误，连接可能已断开
            close(network_socket);
            network_socket = -1;
        }
    }
}

// 将ECG数据添加到网络缓冲区
void add_to_network_buffer(double value, double timestamp) {
    // 使用Python脚本期望的JSON格式准备数据
    // 格式：{"ecg_value":value,"timestamp":timestamp}\n
    char temp_buffer[64];
    int len = snprintf(temp_buffer, sizeof(temp_buffer), "{\"ecg_value\":%.3f,\"timestamp\":%.3f}\n", value, timestamp);
    
    // 检查是否超出缓冲区容量
    if (buffer_pos + len >= (int)sizeof(network_buffer) - 1) {
        // 如果缓冲区快满了，先发送现有数据
        flush_network_buffer();
        
        // 如果仍然放不下（说明单条数据太大），则放弃这条数据
        if (buffer_pos + len >= (int)sizeof(network_buffer) - 1) {
            return; // 缓冲区仍然放不下，放弃这条数据
        }
    }
    
    // 将数据添加到缓冲区
    if (len > 0 && len < (int)sizeof(network_buffer)) {
        memcpy(network_buffer + buffer_pos, temp_buffer, len);
        buffer_pos += len;
    }
}

// 发送缓冲区中的所有数据
void flush_network_buffer() {
    if (network_socket < 0 || buffer_pos <= 0) {
        return;
    }
    
    ssize_t sent = send(network_socket, network_buffer, buffer_pos, MSG_NOSIGNAL);
    if (sent < 0) {
        // 发送失败，增加连续失败计数
        consecutive_send_failures++;
        if (consecutive_send_failures >= MAX_CONSECUTIVE_FAILURES) {
            // 连续失败过多，断开连接等待重连
            close(network_socket);
            network_socket = -1;
            consecutive_send_failures = 0; // 重置失败计数
        }
    } else if (sent != buffer_pos) {
        // 部分发送，将未发送的数据移到缓冲区开头
        memmove(network_buffer, network_buffer + sent, buffer_pos - sent);
        buffer_pos -= sent;
        consecutive_send_failures++; // 部分发送也算作一次失败
        if (consecutive_send_failures >= MAX_CONSECUTIVE_FAILURES) {
            close(network_socket);
            network_socket = -1;
            consecutive_send_failures = 0;
            buffer_pos = 0; // 清空缓冲区
        }
    } else {
        // 发送完全成功，清空缓冲区和失败计数
        buffer_pos = 0;
        consecutive_send_failures = 0;
    }
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[]) {
    // 忽略SIGPIPE信号，防止因对端关闭连接而导致程序终止
    signal(SIGPIPE, SIG_IGN);
    
    // 隐藏光标
    hide_cursor();
    
    // 初始化framebuffer
    if (init_framebuffer() < 0) {
        return -1;
    }
    
    // 初始化网络参数
    last_connect_attempt = time(NULL) - RECONNECT_INTERVAL; // 立即尝试连接
    gettimeofday(&last_send_time, NULL); // 初始化发送时间
    buffer_pos = 0; // 初始化缓冲区位置
    
    // 初始化触摸屏
    init_touchscreen();
    
    // 清屏
    clear_screen();
    
    // 初始化BPM相关变量
    last_peak_time = 0.0;
    current_heart_rate = 0.0;
    peak_count = 0;
    rr_count = 0;
    dynamic_threshold = 0.1;
    peak_hold = 0.0;
    last_voltage = 0.0;
    refractory_period_counter = 0;
    start_time = time(NULL);  // 记录开始时间
    
    init_filter();

    int sfd = open_port(PORT);

    char buf[BS];
    int bp = 0;

    while (1) {
        // 检查并维护网络连接
        check_and_reconnect();
        
        // 处理触摸事件
        handle_touch_events();
        
        // 检查长按是否需要关机
        check_long_press_shutdown();
        
        int br = read(sfd, buf + bp, sizeof(buf) - bp - 1);
        if (br > 0) {
            bp += br;
            
            int ls = 0;
            for (int i = 0; i < bp; i++) {
                if (buf[i] == '\n' || buf[i] == '\r') {
                    buf[i] = '\0';
                    char *ep;
                    double rv = strtod(buf + ls, &ep);
                    
                    if (ep != buf + ls) {
                        double v = rv * 3.3 / 1023.0;
                        double fv = filter(v);
                        
                        if (mc < 5) ma_b[mc++] = fv;
                        else {
                            for (int j = 0; j < 4; j++) ma_b[j] = ma_b[j+1];
                            ma_b[4] = fv;
                        }
                        
                        double sm = 0.0;
                        for (int j = 0; j < mc; j++) sm += ma_b[j];
                        double ffv = sm / mc;
                        
                        if (dc < MAX_DP) f_d[dc] = ffv;
                        else {
                            for (int j = 0; j < MAX_DP - 1; j++) f_d[j] = f_d[j+1];
                            f_d[MAX_DP - 1] = ffv;
                        }
                        
                        if (dc < MAX_DP) dc++;
                        struct timeval tv;
                        gettimeofday(&tv, NULL);
                        double current_time = (double)(tv.tv_sec - start_time) + (double)tv.tv_usec / 1000000.0;
                        detect_peak(ffv, current_time);
                        
                        // 将原始ADC值和时间戳添加到网络缓冲区（不受暂停状态影响）
                        add_to_network_buffer(rv, current_time);
                    }
                    
                    ls = i + 1;
                }
            }
            
            if (ls < bp) {
                memmove(buf, buf + ls, bp - ls);
                bp = bp - ls;
            } else bp = 0;
        }
        
        // 仅在非暂停状态下绘制波形
        if (!paused) {
            draw_ecg_waveform();
        }
        
        // 绘制关机进度条（无论是否暂停都显示）
        draw_shutdown_progress();
        
        usleep(10000); // 休眠10ms
    }

    // 清理资源
    if (fb_ptr) {
        munmap(fb_ptr, finfo.smem_len);
    }
    if (fb_fd >= 0) {
        close(fb_fd);
    }
    
    // 关闭网络连接前，发送剩余的数据
    if (network_socket >= 0) {
        if (buffer_pos > 0) {
            flush_network_buffer();  // 发送缓冲区剩余数据
        }
        close(network_socket);
    }
    
    // 关闭触摸屏设备
    if (touch_fd >= 0) {
        close(touch_fd);
    }
    
    // 恢复光标
    show_cursor();
    
    if(sfd >= 0) {
        close(sfd);
    }
    
    return 0;
}