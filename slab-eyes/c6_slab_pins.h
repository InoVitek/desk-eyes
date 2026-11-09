#pragma once
// ESP32-C6-LCD-1.47 (Amazon B0DK5J6LX3) — LCD only, no touch / IMU / charger

static const int C6_LCD_W = 172;
static const int C6_LCD_H = 320;
static const int C6_LCD_MOSI_PIN = 6;
static const int C6_LCD_SCLK_PIN = 7;
static const int C6_LCD_CS_PIN = 14;
static const int C6_LCD_DC_PIN = 15;
static const int C6_LCD_RST_PIN = 21;
static const int C6_LCD_BL_PIN = 22;
static const int C6_RGB_PIN = 8;
static const int C6_TF_MISO_PIN = 5;
static const int C6_TF_MOSI_PIN = 6;
static const int C6_TF_SCLK_PIN = 7;
static const int C6_TF_CS_PIN = 4;
static const int C6_BOOT_PIN = 9;
static const int C6_BL_PWM = 128;  // ≤ 50%
