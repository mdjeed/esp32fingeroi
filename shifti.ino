// LVGL 9.2.2 + SquareLine UI
// شاشة JC4827W543 480x272
#ifdef _p
#undef _p
#endif

#include "lv_i18n.h"
#include "ui.h"
#include <Preferences.h>
#include <lvgl.h>
#include <PINS_JC4827W543.h>
#include "TAMC_GT911.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <RTClib.h>
#include <Wire.h>
#include <Adafruit_Fingerprint.h>
#include "esp_task_wdt.h"
#include <SPI.h>
#include <SD.h>

static SPIClass spiSD(HSPI);
#define SD_SCK  12
#define SD_MISO 13
#define SD_MOSI 11
#define SD_CS   10
// ========== RTC على I2C منفصل ==========
#define RTC_SDA 15   // IO15
#define RTC_SCL 16   // IO16
// ========== Fingerprint Pins ==========
#define FINGERPRINT_RX 44
#define FINGERPRINT_TX 43

// ========== متغيرات البصمة ==========
HardwareSerial fingerSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

int enrollID = 0;
bool isEnrolling = false;
int enrollStep = 0;
String newEmployeeName = "";
int newEmployeeID = 0;
int fingerprintID = 0;
bool fingerprintSaved = false;

RTC_DS3231 rtc;
TwoWire rtcWire = TwoWire(1);
DateTime now;

// متغيرات الوقت
int currentHour = 0;
int currentMin = 0;
int currentDay = 0;
int currentMonth = 0;
int currentYear = 0;
String ampm = "AM";
String correctPassword = "0000";

// ========== إعدادات Active Buzzer ==========
#define BUZZER_PIN 18

unsigned long beepStartTime = 0;
bool beepActive = false;
int beepDuration = 50;

// ========== متغيرات التحكم بالصوت ==========
bool muteTouch = false;
bool muteEvents = false;
int soundVolume = 128;

// Touch Controller
#define TOUCH_SDA 8
#define TOUCH_SCL 4
#define TOUCH_INT 3
#define TOUCH_RST 38
#define TOUCH_WIDTH 480
#define TOUCH_HEIGHT 272

// ========== تعريف الخطوط ==========
LV_FONT_DECLARE(ui_font_arabic);
LV_FONT_DECLARE(ui_font_arabic12);

Preferences prefs;

String selectedSSID = "";
bool wifiSettingsOpen = false;
bool autoConnectStarted = false;
bool wifiConnecting = false;
unsigned long lastReconnect = 0;

bool isScanning = false;
String oldSSID = "";
String oldPSK = "";
bool wasConnected = false;


int lastFingerprintID = -1;
unsigned long lastAttendanceTime = 0;
// ========== متغيرات اللغة ==========
String currentLanguage = "ar";

TAMC_GT911 touchController =
    TAMC_GT911(
        TOUCH_SDA,
        TOUCH_SCL,
        TOUCH_INT,
        TOUCH_RST,
        TOUCH_WIDTH,
        TOUCH_HEIGHT);

// Display globals
uint32_t screenWidth;
uint32_t screenHeight;
uint32_t bufSize;

lv_display_t *disp;
lv_color_t *disp_draw_buf;

// LVGL tick
uint32_t millis_cb(void)
{
    return millis();
}

// Display flush
void my_disp_flush(
    lv_display_t *disp,
    const lv_area_t *area,
    uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    gfx->draw16bitRGBBitmap(
        area->x1,
        area->y1,
        (uint16_t *)px_map,
        w,
        h);

    lv_disp_flush_ready(disp);
}

// Touch read
void my_touchpad_read(
    lv_indev_t *indev,
    lv_indev_data_t *data)
{
    touchController.read();

    if (touchController.isTouched &&
        touchController.touches > 0)
    {
        data->point.x = touchController.points[0].x;
        data->point.y = touchController.points[0].y;

        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ========== إظهار Panel كلمة المرور ==========
void btnmenu_event(lv_event_t *e)
{
    Serial.println("📱 Showing passpanel");
    
    if (uic_passpanel != NULL)
    {
        lv_obj_clear_flag(uic_passpanel, LV_OBJ_FLAG_HIDDEN);
        
        if (uic_keypasstxt != NULL)
        {
            lv_textarea_set_text(uic_keypasstxt, "");
        }
        
        lv_obj_set_style_border_color(uic_passpanel, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
        lv_obj_set_style_border_width(uic_passpanel, 0, LV_PART_MAIN);
        lv_obj_move_foreground(uic_passpanel);
        lv_refr_now(NULL);
    }
    else
    {
        Serial.println("❌ ERROR: uic_passpanel is NULL!");
    }
}

// ========== زر العودة لإخفاء الـ Panel ==========
void backmain_event(lv_event_t *e)
{
    Serial.println("🔙 Hiding passpanel");
    
    if (uic_passpanel != NULL)
    {
        lv_obj_add_flag(uic_passpanel, LV_OBJ_FLAG_HIDDEN);
        
        if (uic_keypasstxt != NULL)
        {
            lv_textarea_set_text(uic_keypasstxt, "");
        }
        
        lv_obj_set_style_border_color(uic_passpanel, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
        lv_obj_set_style_border_width(uic_passpanel, 0, LV_PART_MAIN);
        lv_refr_now(NULL);
    }
    else
    {
        Serial.println("❌ ERROR: uic_passpanel is NULL!");
    }
}

// ========== التحقق من كلمة المرور ==========
void keypasstxt_event(lv_event_t *e)
{
    if (uic_keypasstxt == NULL)
    {
        Serial.println("❌ ERROR: uic_keypasstxt is NULL!");
        return;
    }
    
    const char *entered = lv_textarea_get_text(uic_keypasstxt);
    
    if (entered == NULL)
    {
        Serial.println("❌ ERROR: entered text is NULL!");
        return;
    }
    
    Serial.print("Entered: ");
    Serial.println(entered);

    if (strlen(entered) == 4)
    {
        if (strcmp(entered, correctPassword.c_str()) == 0)
        {
            Serial.println("✅ Password correct!");

            if (uic_passpanel != NULL)
            {
                lv_obj_add_flag(uic_passpanel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_border_color(uic_passpanel, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
                lv_obj_set_style_border_width(uic_passpanel, 0, LV_PART_MAIN);
            }

            lv_textarea_set_text(uic_keypasstxt, "");
            lv_screen_load(uic_menu);
        }
        else
        {
            Serial.println("❌ Password incorrect!");

            if (uic_passpanel != NULL)
            {
                lv_obj_set_style_border_color(uic_passpanel, lv_color_hex(0xFF0000), LV_PART_MAIN);
                lv_obj_set_style_border_width(uic_passpanel, 3, LV_PART_MAIN);
            }

            lv_timer_t *timer = lv_timer_create([](lv_timer_t *t)
            {
                if (uic_keypasstxt != NULL)
                {
                    lv_textarea_set_text(uic_keypasstxt, "");
                }
                
                if (uic_passpanel != NULL)
                {
                    lv_obj_set_style_border_color(uic_passpanel, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
                    lv_obj_set_style_border_width(uic_passpanel, 0, LV_PART_MAIN);
                }

                lv_timer_del(t);
            }, 500, NULL);

            if (timer != NULL)
            {
                lv_timer_set_repeat_count(timer, 1);
            }
        }
    }
}

// ========== قراءة الوقت من RTC ==========
void updateTime()
{
    now = rtc.now();
    
    currentHour = now.hour();
    currentMin = now.minute();
    currentDay = now.day();
    currentMonth = now.month();
    currentYear = now.year();
    
    if (currentHour >= 12)
    {
        ampm = "PM";
        if (currentHour > 12) currentHour -= 12;
    }
    else
    {
        ampm = "AM";
        if (currentHour == 0) currentHour = 12;
    }
}

// ========== عرض الوقت على الشاشة ==========
void displayTime()
{
    updateTime();
    
    char hourStr[3];
    char minStr[3];
    sprintf(hourStr, "%02d", currentHour);
    sprintf(minStr, "%02d", currentMin);
    
    lv_label_set_text(uic_hour, hourStr);
    lv_label_set_text(uic_min, minStr);
    lv_label_set_text(uic_ampmtxt, ampm.c_str());
    
    char dateStr[12];
    sprintf(dateStr, "%04d/%02d/%02d", currentYear, currentMonth, currentDay);
    lv_label_set_text(uic_datetxt, dateStr);
    
    char dayName[16];
    const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    strcpy(dayName, days[now.dayOfTheWeek()]);
    lv_label_set_text(uic_dayname, dayName);
}

void saveTime()
{
    int hour = lv_roller_get_selected(uic_timehour);
    int min = lv_roller_get_selected(uic_timemin);
    int ampmIndex = lv_roller_get_selected(uic_ampm);
    int day = lv_roller_get_selected(uic_day) + 1;
    int month = lv_roller_get_selected(uic_month) + 1;
    int year = lv_roller_get_selected(uic_years) + 2026;
    
    if (ampmIndex == 1) {
        if (hour != 12) hour += 12;
    } else {
        if (hour == 12) hour = 0;
    }
    
    rtc.adjust(DateTime(year, month, day, hour, min, 0));
    
    Serial.printf("✅ Time set to: %04d/%02d/%02d %02d:%02d\n", year, month, day, hour, min);
    
    displayTime();
    lv_screen_load(uic_main);
}

void savetime_event(lv_event_t *e)
{
    saveTime();
}

// ========== دوال Active Buzzer ==========
void setupBuzzer()
{
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("✅ Buzzer initialized");
}

void playBeep(lv_event_t *e)
{
    if (muteTouch) return;
    
    if (!beepActive)
    {
        digitalWrite(BUZZER_PIN, HIGH);
        beepActive = true;
        beepStartTime = millis();
        beepDuration = 40;
    }
}

void playBeepDirect(int duration = 40)
{
    if (muteEvents) return;
    
    if (!beepActive)
    {
        digitalWrite(BUZZER_PIN, HIGH);
        beepActive = true;
        beepStartTime = millis();
        beepDuration = duration;
    }
}

void updateBeep()
{
    if (beepActive && (millis() - beepStartTime > beepDuration))
    {
        digitalWrite(BUZZER_PIN, LOW);
        beepActive = false;
    }
}
// ========== FINGERPRINT FUNCTIONS ==========
// ========== FINGERPRINT FUNCTIONS ==========
// ========== FINGERPRINT FUNCTIONS ==========


// 🔥 دالة التحقق من وجود بصمة معينة
bool checkFingerprintExists(int id) {
    int p = finger.loadModel(id);
    if (p == FINGERPRINT_OK) {
        Serial.printf("✅ Fingerprint ID %d exists!\n", id);
        return true;
    } else {
        Serial.printf("❌ Fingerprint ID %d not found\n", id);
        return false;
    }
}

// 🔥 تهيئة المستشعر
int findFingerprintID()
{
    int p = finger.getImage();

    // لا يوجد إصبع
    if (p == FINGERPRINT_NOFINGER)
    {
        return -2;
    }

    // أي خطأ آخر
    if (p != FINGERPRINT_OK)
    {
        return -1;
    }

    p = finger.image2Tz();
    if (p != FINGERPRINT_OK)
    {
        return -1;
    }

    p = finger.fingerSearch();

    // لم يتم العثور على البصمة
    if (p == FINGERPRINT_NOTFOUND)
    {
        return -1;
    }

    // أي خطأ آخر
    if (p != FINGERPRINT_OK)
    {
        return -1;
    }

    Serial.printf("✅ Fingerprint found! ID: %d, Confidence: %d\n",
                  finger.fingerID, finger.confidence);

    if (finger.confidence < 60)
    {
        Serial.println("⚠️ Low confidence");
        return -1;
    }

    return finger.fingerID;
}
// 🔥 البحث عن ID غير مستخدم
int getFreeFingerprintID() {
    for (int id = 1; id <= 127; id++) {
        int p = finger.loadModel(id);
        if (p != FINGERPRINT_OK) {
            Serial.printf("✅ Free ID found: %d\n", id);
            return id;
        }
    }
    Serial.println("❌ No free IDs available!");
    return -1;
}

// 🔥 تهيئة المستشعر
void initFingerprint() {
    fingerSerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
    delay(200);
    finger.begin(57600);
    delay(100);
    
    if (finger.verifyPassword()) {
        Serial.println("✅ Fingerprint sensor found!");
        if (uic_fingertemp2 != NULL) {
            lv_label_set_text(uic_fingertemp2, "🖐️ Place finger to enroll");
            lv_obj_set_style_text_color(uic_fingertemp2, lv_color_hex(0x00FF00), LV_PART_MAIN);
        }
    } else {
        Serial.println("❌ Fingerprint sensor not found!");
        if (uic_fingertemp2 != NULL) {
            lv_label_set_text(uic_fingertemp2, "❌ Sensor not found!");
            lv_obj_set_style_text_color(uic_fingertemp2, lv_color_hex(0xFF0000), LV_PART_MAIN);
        }
    }
}

// 🔥 بدء تسجيل بصمة جديدة
void startFingerprintEnrollment() {
    if (uic_addemployeename != NULL) {
        newEmployeeName = String(lv_textarea_get_text(uic_addemployeename));
    }
    if (newEmployeeName.length() == 0) {
        newEmployeeName = "Employee";
    }
    
    int freeID = getFreeFingerprintID();
    if (freeID == -1) {
        Serial.println("❌ No free IDs available!");
        if (uic_fingertemp2 != NULL) {
            lv_label_set_text(uic_fingertemp2, "❌ No free IDs!");
            lv_obj_set_style_text_color(uic_fingertemp2, lv_color_hex(0xFF0000), LV_PART_MAIN);
        }
        return;
    }
    
    newEmployeeID = freeID;
    fingerprintID = freeID;
    enrollID = freeID;
    isEnrolling = true;
    enrollStep = 0;
    fingerprintSaved = false;
    
    if (uic_fingertemp2 != NULL) {
        lv_label_set_text(uic_fingertemp2, "🖐️ Place finger on sensor...");
        lv_obj_set_style_text_color(uic_fingertemp2, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        lv_obj_clear_flag(uic_fingertemp2, LV_OBJ_FLAG_HIDDEN);
    }
    if (uic_fingertemp != NULL) lv_obj_add_flag(uic_fingertemp, LV_OBJ_FLAG_HIDDEN);
    if (uic_fingertemp3 != NULL) lv_obj_add_flag(uic_fingertemp3, LV_OBJ_FLAG_HIDDEN);
    if (uic_fingertemp4 != NULL) lv_obj_add_flag(uic_fingertemp4, LV_OBJ_FLAG_HIDDEN);
    if (uic_mainpanel2 != NULL) lv_obj_clear_flag(uic_mainpanel2, LV_OBJ_FLAG_HIDDEN);
    
    Serial.println("🖐️ Enrollment started for ID: " + String(enrollID));
}

// 🔥 معالجة تسجيل البصمة
void processFingerprintEnrollment() {
    if (!isEnrolling) return;
    
    int p = -1;
    
    switch (enrollStep) {
        case 0:  // 📸 البصمة الأولى + التحقق من وجودها
            p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                Serial.println("✅ First image taken");
                
                // 🔥 تحقق من وجود البصمة
                int foundID = findFingerprintID();
                if (foundID > 0) {
                    Serial.printf("❌ Fingerprint already exists! ID: %d\n", foundID);
                    
                    if (uic_fingertemp2 != NULL) {
                        lv_obj_add_flag(uic_fingertemp2, LV_OBJ_FLAG_HIDDEN);
                    }
                    if (uic_fingertemp3 != NULL) {
                        lv_obj_clear_flag(uic_fingertemp3, LV_OBJ_FLAG_HIDDEN);
                        lv_label_set_text(uic_fingertemp3, "❌ Fingerprint already exists!");
                        lv_obj_set_style_text_color(uic_fingertemp3, lv_color_hex(0xFF0000), LV_PART_MAIN);
                    }
                    
                    isEnrolling = false;
                    enrollStep = -1;
                    
                    lv_timer_t *retryTimer = lv_timer_create([](lv_timer_t *t) {
                        if (uic_fingertemp3 != NULL) {
                            lv_obj_add_flag(uic_fingertemp3, LV_OBJ_FLAG_HIDDEN);
                        }
                        startFingerprintEnrollment();
                        lv_timer_del(t);
                    }, 3000, NULL);
                    lv_timer_set_repeat_count(retryTimer, 1);
                    
                    return;
                }
                
                // 🔥 إخفاء uic_fingertemp2
                if (uic_fingertemp2 != NULL) {
                    lv_obj_add_flag(uic_fingertemp2, LV_OBJ_FLAG_HIDDEN);
                }
                
                // 🔥 إظهار uic_fingertemp (نجاح مؤقت)
                if (uic_fingertemp != NULL) {
                    lv_obj_clear_flag(uic_fingertemp, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(uic_fingertemp, "✅ New fingerprint detected!");
                    lv_obj_set_style_text_color(uic_fingertemp, lv_color_hex(0x00FF00), LV_PART_MAIN);
                }
                
                // 🔥🔥🔥 محاولة التحويل مع إعادة المحاولة
                int convertAttempts = 0;
                bool converted = false;
                
                while (convertAttempts < 3 && !converted) {
                    p = finger.image2Tz(1);
                    if (p == FINGERPRINT_OK) {
                        converted = true;
                        Serial.println("✅ First image converted");
                    } else {
                        convertAttempts++;
                        Serial.printf("⚠️ Conversion attempt %d failed (code: %d)\n", convertAttempts, p);
                        
                        if (convertAttempts < 3) {
                            // 🔥 أعد أخذ الصورة قبل المحاولة مرة أخرى
                            delay(300);
                            p = finger.getImage();
                            if (p != FINGERPRINT_OK) {
                                Serial.println("❌ Failed to retake image");
                                break;
                            }
                        }
                    }
                }
                
                if (converted) {
                    // 🔥 بعد 2 ثانية، أظهر uic_fingertemp4
                    lv_timer_t *showSecondTimer = lv_timer_create([](lv_timer_t *t) {
                        if (uic_fingertemp != NULL) {
                            lv_obj_add_flag(uic_fingertemp, LV_OBJ_FLAG_HIDDEN);
                        }
                        if (uic_fingertemp4 != NULL) {
                            lv_obj_clear_flag(uic_fingertemp4, LV_OBJ_FLAG_HIDDEN);
                            lv_label_set_text(uic_fingertemp4, "🖐️ Place same finger again...");
                            lv_obj_set_style_text_color(uic_fingertemp4, lv_color_hex(0xFFFF00), LV_PART_MAIN);
                        }
                        lv_timer_del(t);
                    }, 2000, NULL);
                    lv_timer_set_repeat_count(showSecondTimer, 1);
                    
                    enrollStep = 1;
                } else {
                    Serial.println("❌ First image conversion failed after retries");
                    enrollStep = -1;
                }
            } else if (p == FINGERPRINT_NOFINGER) {
                // انتظر
            } else {
                Serial.println("❌ Failed to capture first image");
                enrollStep = -1;
            }
            break;
            
        case 1:  // 🖐️ انتظار إزالة الإصبع
            p = finger.getImage();
            if (p == FINGERPRINT_NOFINGER) {
                enrollStep = 2;
            }
            break;
            
        case 2:  // 📸 البصمة الثانية
            p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                Serial.println("✅ Second image taken");
                
                if (uic_fingertemp4 != NULL) {
                    lv_label_set_text(uic_fingertemp4, "✅ Converting second image...");
                }
                
                // 🔥🔥🔥 محاولة تحويل البصمة الثانية مع إعادة المحاولة
                int convertAttempts2 = 0;
                bool converted2 = false;
                
                while (convertAttempts2 < 3 && !converted2) {
                    p = finger.image2Tz(2);
                    if (p == FINGERPRINT_OK) {
                        converted2 = true;
                        Serial.println("✅ Second image converted");
                    } else {
                        convertAttempts2++;
                        Serial.printf("⚠️ Second conversion attempt %d failed (code: %d)\n", convertAttempts2, p);
                        
                        if (convertAttempts2 < 3) {
                            delay(300);
                            p = finger.getImage();
                            if (p != FINGERPRINT_OK) {
                                Serial.println("❌ Failed to retake second image");
                                break;
                            }
                        }
                    }
                }
                
                if (converted2) {
                    enrollStep = 3;
                } else {
                    Serial.println("❌ Second image conversion failed after retries");
                    enrollStep = -1;
                }
            } else if (p == FINGERPRINT_NOFINGER) {
                // انتظر
            } else {
                Serial.println("❌ Failed to capture second image");
                enrollStep = -1;
            }
            break;
            
        case 3:  // 🔧 إنشاء النموذج
            p = finger.createModel();
            if (p == FINGERPRINT_OK) {
                Serial.println("✅ Model created");
                if (uic_fingertemp4 != NULL) {
                    lv_label_set_text(uic_fingertemp4, "✅ Model created, saving...");
                }
                enrollStep = 4;
            } else if (p == FINGERPRINT_ENROLLMISMATCH) {
                Serial.println("❌ Fingerprints did not match");
                enrollStep = -1;
            } else {
                Serial.println("❌ Failed to create model");
                enrollStep = -1;
            }
            break;
            
        case 4:  // 💾 حفظ النموذج
            p = finger.storeModel(enrollID);
            if (p == FINGERPRINT_OK) {
                fingerprintSaved = true;
                Serial.printf("✅ Fingerprint #%d stored!\n", enrollID);
                
                if (uic_fingertemp4 != NULL) {
                    lv_obj_add_flag(uic_fingertemp4, LV_OBJ_FLAG_HIDDEN);
                }
                if (uic_fingertemp != NULL) {
                    lv_obj_clear_flag(uic_fingertemp, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(uic_fingertemp, "✅ Fingerprint saved!");
                    lv_obj_set_style_text_color(uic_fingertemp, lv_color_hex(0x00FF00), LV_PART_MAIN);
                }
                if (uic_mainpanel2 != NULL) {
                    lv_obj_add_flag(uic_mainpanel2, LV_OBJ_FLAG_HIDDEN);
                }
                
                lv_timer_t *hideTimer = lv_timer_create([](lv_timer_t *t) {
                    if (uic_fingertemp != NULL) {
                        lv_obj_add_flag(uic_fingertemp, LV_OBJ_FLAG_HIDDEN);
                    }
                    lv_timer_del(t);
                }, 2000, NULL);
                lv_timer_set_repeat_count(hideTimer, 1);
                
                isEnrolling = false;
                playBeepDirect(80);
                
            } else {
                Serial.println("❌ Failed to store fingerprint");
                enrollStep = -1;
            }
            break;
            
        case -1:  // ❌ الفشل
            isEnrolling = false;
            
            if (uic_fingertemp2 != NULL) lv_obj_add_flag(uic_fingertemp2, LV_OBJ_FLAG_HIDDEN);
            if (uic_fingertemp != NULL) lv_obj_add_flag(uic_fingertemp, LV_OBJ_FLAG_HIDDEN);
            if (uic_fingertemp4 != NULL) lv_obj_add_flag(uic_fingertemp4, LV_OBJ_FLAG_HIDDEN);
            
            if (uic_fingertemp3 != NULL) {
                lv_obj_clear_flag(uic_fingertemp3, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(uic_fingertemp3, "❌ Enrollment failed! Try again");
                lv_obj_set_style_text_color(uic_fingertemp3, lv_color_hex(0xFF0000), LV_PART_MAIN);
            }
            
            if (uic_mainpanel2 != NULL) {
                lv_obj_clear_flag(uic_mainpanel2, LV_OBJ_FLAG_HIDDEN);
            }
            
            lv_timer_t *retryTimer = lv_timer_create([](lv_timer_t *t) {
                if (uic_fingertemp3 != NULL) {
                    lv_obj_add_flag(uic_fingertemp3, LV_OBJ_FLAG_HIDDEN);
                }
                startFingerprintEnrollment();
                lv_timer_del(t);
            }, 3000, NULL);
            lv_timer_set_repeat_count(retryTimer, 1);
            break;
    }
}
void saveEmployeeToSD(String name, int id, int fingerprintID) {
    File file = SD.open("/employees.csv", FILE_APPEND);
    if (!file) {
        file = SD.open("/employees.csv", FILE_WRITE);
        if (file) {
            file.println("ID,Name,FingerprintID");  // 🔥 بدون Date
            file.close();
        }
        file = SD.open("/employees.csv", FILE_APPEND);
        if (!file) {
            Serial.println("❌ Failed to open employees.csv");
            return;
        }
    }
    
    // 🔥 كتابة بيانات الموظف (بدون تاريخ)
    String data = String(id) + "," + name + "," + String(fingerprintID);
    file.println(data);
    file.close();
    
    Serial.printf("✅ Employee saved: %s, ID: %d, FP: %d\n", name.c_str(), id, fingerprintID);
}
// ========== مسح كل البصمات من المستشعر ==========
void deleteAllFingerprints() {
    Serial.println("🗑️ Deleting ALL fingerprints...");
    
    int deletedCount = 0;
    
    for (int id = 1; id <= 127; id++) {
        int p = finger.deleteModel(id);
        if (p == FINGERPRINT_OK) {
            deletedCount++;
        }
    }
    
    Serial.printf("✅ Total %d fingerprints deleted!\n", deletedCount);
    
    // 🔥 إعادة تعيين المتغيرات
    fingerprintSaved = false;
    isEnrolling = false;
    enrollStep = 0;
}

void deleteAllFiles(fs::FS &fs, const char *dirname)
{
    File root = fs.open(dirname);
    if (!root || !root.isDirectory())
        return;

    File file = root.openNextFile();

    while (file)
    {
        String path = String(file.path());

        file.close();

        Serial.println("Deleting: " + path);
        fs.remove(path.c_str());

        file = root.openNextFile();
    }
}
// 🔥 إلغاء التسجيل (عند الخروج من الشاشة)
void cancelFingerprintEnrollment() {
    isEnrolling = false;
    enrollStep = 0;
    fingerprintSaved = false;
    Serial.println("🔄 Enrollment cancelled");
}

void gotoadd_event(lv_event_t *e) {
    Serial.println("📱 Going to Add Employee screen");
    if (uic_addemployee != NULL) {
        lv_screen_load(uic_addemployee);
        playBeepDirect(40);
         lv_obj_clear_flag(uic_mainpanel2, LV_OBJ_FLAG_HIDDEN);
        // 🔥 إظهار uic_mainpanel2
        
        // 🔥 بدء التسجيل
        startFingerprintEnrollment();
    }
}

// ========== زر حفظ الموظف (بدون SD Card) ==========
// ========== زر حفظ الموظف ==========
void addemployeeaction_event(lv_event_t *e) {
    Serial.println("💾 Saving employee...");
    
    if (!fingerprintSaved) {
        // 🔥 إظهار ui_fingertemp6 (خطأ - لم تسجل بصمة)
        if (ui_fingertemp6 != NULL) {
            lv_obj_clear_flag(ui_fingertemp6, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ui_fingertemp6, "❌ Enroll fingerprint first!");
            lv_obj_set_style_text_color(ui_fingertemp6, lv_color_hex(0xFF0000), LV_PART_MAIN);
        }
        
        // 🔥 إخفاء ui_fingertemp6 بعد 1.5 ثانية
        lv_timer_t *hideTimer = lv_timer_create([](lv_timer_t *t) {
            if (ui_fingertemp6 != NULL) {
                lv_obj_add_flag(ui_fingertemp6, LV_OBJ_FLAG_HIDDEN);
            }
            lv_timer_del(t);
        }, 1500, NULL);
        lv_timer_set_repeat_count(hideTimer, 1);
        return;
    }
    
    // 🔥 قراءة الاسم من الحقل
    String name = "";
    if (uic_addemployeename != NULL) {
        name = String(lv_textarea_get_text(uic_addemployeename));
    }
    if (name.length() == 0) {
        name = "Employee";
    }
    
    // 🔥 حفظ في SD Card
    saveEmployeeToSD(name, fingerprintID, fingerprintID);
    
    playBeepDirect(80);
    
    // 🔥🔥🔥 إظهار ui_fingertemp5 (نجاح) لمدة 1.5 ثانية
    if (ui_fingertemp5 != NULL) {
        lv_obj_clear_flag(ui_fingertemp5, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_fingertemp5, "✅ Employee saved!");
        lv_obj_set_style_text_color(ui_fingertemp5, lv_color_hex(0x00FF00), LV_PART_MAIN);
    }
    
    // 🔥 إخفاء ui_fingertemp5 بعد 1.5 ثانية
    lv_timer_t *hideTimer = lv_timer_create([](lv_timer_t *t) {
        if (ui_fingertemp5 != NULL) {
            lv_obj_add_flag(ui_fingertemp5, LV_OBJ_FLAG_HIDDEN);
        }
        lv_timer_del(t);
    }, 1500, NULL);
    lv_timer_set_repeat_count(hideTimer, 1);
    
    // 🔥 العودة للرئيسية بعد 2 ثانية
    lv_timer_t *timer2 = lv_timer_create([](lv_timer_t *t) {
        lv_screen_load(uic_main);
        lv_timer_del(t);
    }, 2000, NULL);
    lv_timer_set_repeat_count(timer2, 1);
    
    // 🔥 إعادة تعيين المتغيرات
    fingerprintSaved = false;
    isEnrolling = false;
}
void backtomain_event(lv_event_t *e) {
    Serial.println("🔙 Going back to main");
    isEnrolling = false;
    lv_screen_load(uic_menu);
    playBeepDirect(40);
}

String getEmployeeNameByFingerprint(int fpID) {
    File file = SD.open("/employees.csv");
    if (!file) {
        Serial.println("❌ employees.csv not found!");
        return "Unknown";
    }
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        int firstComma = line.indexOf(',');
        int secondComma = line.indexOf(',', firstComma + 1);
        
        if (firstComma > 0 && secondComma > 0) {
            String idStr = line.substring(0, firstComma);
            String name = line.substring(firstComma + 1, secondComma);
            String fpStr = line.substring(secondComma + 1);
            
            if (fpStr.toInt() == fpID) {
                file.close();
                return name;
            }
        }
    }
    file.close();
    return "Unknown";
}


String getLastAttendanceType(int fingerprintID)
{
    File file = SD.open("/attendance.csv");

    if (!file)
        return "OUT";

    String lastType = "OUT";

    while (file.available())
    {
        String line = file.readStringUntil('\n');

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);
        int p4 = line.indexOf(',', p3 + 1);

        if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0)
            continue;

        int fp = line.substring(p2 + 1, p3).toInt();

        if (fp == fingerprintID)
        {
            lastType = line.substring(p3 + 1, p4);
            lastType.trim();
        }
    }

    file.close();

    return lastType;
}
// 🔥 دالة معالجة الدخول والخروج
// ============================================================
void processAttendance()
{
    // البحث عن البصمة
    int id = findFingerprintID();

    // لا يوجد إصبع
    if (id == -2)
    {
        if (uic_Panel7)
            lv_obj_add_flag(uic_Panel7, LV_OBJ_FLAG_HIDDEN);

        return;
    }

    // البصمة غير مسجلة
    if (id == -1)
    {
        if (uic_Panel7)
        {
            lv_obj_clear_flag(uic_Panel7, LV_OBJ_FLAG_HIDDEN);
            
        }

        playBeepDirect(120);
        return;
    }

    // منع تكرار نفس البصمة خلال 3 ثوان
    if (id == lastFingerprintID &&
        millis() - lastAttendanceTime < 3000)
    {
        Serial.println("⏳ Please wait 3 seconds");
        return;
    }

    lastFingerprintID = id;
    lastAttendanceTime = millis();

    // جلب اسم الموظف
    String employeeName = getEmployeeNameByFingerprint(id);

    if (employeeName == "Unknown")
    {
        if (uic_Panel7)
        {
            lv_obj_clear_flag(uic_Panel7, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(uic_Panel7, "❌ Employee not found");
        }

        playBeepDirect(120);
        return;
    }

    // إخفاء رسالة الخطأ
    if (uic_Panel7)
        lv_obj_add_flag(uic_Panel7, LV_OBJ_FLAG_HIDDEN);

    // إظهار لوحة الاسم
    if (uic_Panel4)
        lv_obj_clear_flag(uic_Panel4, LV_OBJ_FLAG_HIDDEN);

    if (uic_name)
        lv_label_set_text(uic_name, employeeName.c_str());

    // تحديث الوقت
    updateTime();

    char timeStr[10];
    sprintf(timeStr, "%02d:%02d", currentHour, currentMin);

    delay(50);

    // تحديد دخول أو خروج
    String lastType = getLastAttendanceType(id);

    if (lastType == "IN")
    {
        saveAttendanceToSD(id, employeeName, "OUT", String(timeStr));
          lv_label_set_text(uic_inorout, "Check Out");
        Serial.printf("✅ %s Check OUT\n", employeeName.c_str());

        playBeepDirect(80);
    }
    else
    {
        saveAttendanceToSD(id, employeeName, "IN", String(timeStr));
         lv_label_set_text(uic_inorout, "Check In");
        Serial.printf("✅ %s Check IN\n", employeeName.c_str());

        playBeepDirect(80);
    }

    // إخفاء لوحة الاسم بعد ثانيتين
    lv_timer_t *hidePanelTimer = lv_timer_create([](lv_timer_t *t)
    {
        if (uic_Panel4)
            lv_obj_add_flag(uic_Panel4, LV_OBJ_FLAG_HIDDEN);

        lv_timer_del(t);

    }, 2000, NULL);

    lv_timer_set_repeat_count(hidePanelTimer, 1);
}
// ============================================================
// 🔥 دالة حفظ الحضور في SD Card
// ============================================================
void saveAttendanceToSD(int fingerprintID, String name, String type, String time) {
    // 🔥 تأكد من أن SD Card جاهزة
    if (!SD.begin(SD_CS, spiSD, 10000000)) {
        Serial.println("❌ SD Card not ready!");
        return;
    }
    
    File file = SD.open("/attendance.csv", FILE_APPEND);
    if (!file) {
        file = SD.open("/attendance.csv", FILE_WRITE);
        if (file) {
            file.println("ID,Name,FingerprintID,Type,Time,Date");
            file.close();
        }
        file = SD.open("/attendance.csv", FILE_APPEND);
        if (!file) {
            Serial.println("❌ Failed to open attendance.csv");
            return;
        }
    }
    
    String data = String(fingerprintID) + "," + name + "," + String(fingerprintID) + "," + type + "," + time + "," + String(currentYear) + "/" + String(currentMonth) + "/" + String(currentDay);
    file.println(data);
    file.close();
    
    Serial.printf("✅ Attendance saved: %s, %s, %s\n", name.c_str(), type.c_str(), time.c_str());
}

// ============================================================
// 🔥 دالة تحميل وعرض الموظفين عند الدخول لصفحة uic_employee
// ============================================================

// ============================================================
// 🔥 دوال عرض الموظفين في uic_employee باستخدام uic_employeetemple
// ============================================================

// ============================================================
// ============================================================
// 🔥 دوال عرض الموظفين في uic_employee مع الحفاظ على التصميم
// ============================================================

// ============================================================
// 🔥 دوال عرض الموظفين في uic_employee مع الحفاظ على التصميم والعناوين
// ============================================================

#define MAX_EMPLOYEES 100

struct EmployeeItem {
    int id;
    String name;
    int fingerprintID;
};

EmployeeItem employeeList[MAX_EMPLOYEES];
int employeeCount = 0;

// ============================================================
// 🔥 تحميل الموظفين من SD Card
// ============================================================
void loadEmployeesFromSD() {
    employeeCount = 0;

    File file = SD.open("/employees.csv");
    if (!file) {
        Serial.println("❌ employees.csv not found!");
        return;
    }

    while (file.available() && employeeCount < MAX_EMPLOYEES) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int firstComma = line.indexOf(',');
        int secondComma = line.indexOf(',', firstComma + 1);

        if (firstComma > 0 && secondComma > 0) {
            String idStr = line.substring(0, firstComma);
            String name = line.substring(firstComma + 1, secondComma);
            String fpStr = line.substring(secondComma + 1);

            employeeList[employeeCount].id = idStr.toInt();
            employeeList[employeeCount].name = name;
            employeeList[employeeCount].fingerprintID = fpStr.toInt();
            employeeCount++;
        }
    }
    file.close();
    
    Serial.printf("✅ Total employees loaded: %d\n", employeeCount);
}

// ============================================================
// 🔥 إعادة إنشاء العناوين (ui_mainemployee و ui_idtxt)
// ============================================================
void createEmployeeHeaders() {
    if (ui_mainpanel == NULL) return;
    
    // 🔥 اختيار الخط حسب اللغة
    const lv_font_t *font18;
    if (currentLanguage == "ar") {
        font18 = &ui_font_arabic1;  // خط عربي 18
    } else {
        font18 = &lv_font_montserrat_18;
    }
    
    // 🔥 عنوان "employee name"
    ui_mainemployee = lv_label_create(ui_mainpanel);
    lv_obj_set_width(ui_mainemployee, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_mainemployee, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_mainemployee, 20);
    lv_obj_set_y(ui_mainemployee, 5);
    lv_obj_set_align(ui_mainemployee, LV_ALIGN_TOP_LEFT);
    lv_label_set_text(ui_mainemployee, _("employee name"));
    lv_obj_set_style_text_color(ui_mainemployee, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(ui_mainemployee, 255, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_mainemployee, font18, LV_PART_MAIN);  // 🔥 الخط المناسب
    
    // 🔥 عنوان "id"
    ui_idtxt = lv_label_create(ui_mainpanel);
    lv_obj_set_width(ui_idtxt, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_idtxt, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_idtxt, -20);
    lv_obj_set_y(ui_idtxt, 5);
    lv_obj_set_align(ui_idtxt, LV_ALIGN_TOP_RIGHT);
    lv_label_set_text(ui_idtxt, _("id"));
    lv_obj_set_style_text_color(ui_idtxt, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(ui_idtxt, 255, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_idtxt, font18, LV_PART_MAIN);  // 🔥 الخط المناسب
}
void updateEmployeeFonts() {
    const lv_font_t *font18;
    if (currentLanguage == "ar") {
        font18 = &ui_font_arabic1;
    } else {
        font18 = &lv_font_montserrat_18;
    }
    
    if (ui_mainemployee != NULL) {
        lv_obj_set_style_text_font(ui_mainemployee, font18, LV_PART_MAIN);
        lv_label_set_text(ui_mainemployee, _("employee name"));
    }
    if (ui_idtxt != NULL) {
        lv_obj_set_style_text_font(ui_idtxt, font18, LV_PART_MAIN);
        lv_label_set_text(ui_idtxt, _("id"));
    }
}

// ============================================================
// 🔥 عرض جميع الموظفين في uic_employee
// ============================================================
void ShowAllEmployees()
{
    if (uic_employee == NULL)
    {
        Serial.println("❌ uic_employee is NULL!");
        return;
    }

    if (ui_mainpanel == NULL)
    {
        Serial.println("❌ ui_mainpanel is NULL!");
        return;
    }

    // =========================================================
    // تنظيف القائمة
    // =========================================================
    lv_obj_clean(ui_mainpanel);

    // =========================================================
    // تحميل الموظفين
    // =========================================================
    loadEmployeesFromSD();

    Serial.print("👥 Employees loaded: ");
    Serial.println(employeeCount);

    // =========================================================
    // إعادة إنشاء Headers
    // =========================================================
    createEmployeeHeaders();

    // =========================================================
    // تفعيل Scroll على ui_mainpanel
    // =========================================================
    lv_obj_add_flag(
        ui_mainpanel,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_set_scroll_dir(
        ui_mainpanel,
        LV_DIR_VER
    );

    lv_obj_set_scrollbar_mode(
        ui_mainpanel,
        LV_SCROLLBAR_MODE_AUTO
    );

    // منع تمرير الـ parent
    lv_obj_remove_flag(
        ui_mainpanel,
        LV_OBJ_FLAG_SCROLL_CHAIN
    );

    lv_obj_remove_flag(
        ui_mainpanel,
        LV_OBJ_FLAG_SCROLL_ELASTIC
    );

    // =========================================================
    // إذا لا يوجد موظفين
    // =========================================================
    if (employeeCount == 0)
    {
        lv_obj_t *msg =
            lv_label_create(ui_mainpanel);

        lv_label_set_text(
            msg,
            "📋 No employees found"
        );

        lv_obj_set_style_text_color(
            msg,
            lv_color_hex(0x888888),
            LV_PART_MAIN
        );

        lv_obj_set_style_text_font(
            msg,
            &lv_font_montserrat_18,
            LV_PART_MAIN
        );

        lv_obj_center(msg);

        return;
    }

    // =========================================================
    // عرض الموظفين
    // =========================================================
    for (int i = 0; i < employeeCount; i++)
    {
        AddEmployeeCard(i);
    }

    // =========================================================
    // حساب ارتفاع المحتوى
    // =========================================================
    //
    // Header = 40
    // كل موظف = 62
    // Bottom space = 30
    //
    int contentHeight =
        40 +
        (employeeCount * 62) +
        30;

    // =========================================================
    // معرفة ارتفاع الـ viewport
    // =========================================================
    lv_coord_t viewportHeight =
        lv_obj_get_height(ui_mainpanel);

    if (viewportHeight <= 0)
    {
        viewportHeight = 225;
    }

    // =========================================================
    // مهم جدًا:
    //
    // لا نجعل ui_mainpanel نفسه بطول القائمة.
    //
    // إذا جعلناه طويلًا، الـ Scroll لن يكون له معنى.
    // =========================================================

    if (contentHeight < viewportHeight)
    {
        contentHeight = viewportHeight;
    }

    // =========================================================
    // هنا نحتاج أن يكون content أكبر من viewport
    //
    // لكن بما أن العناصر نفسها أبناء ui_mainpanel،
    // LVGL يحسب content area من children.
    //
    // نضيف Bottom spacer لضمان الوصول للنهاية.
    // =========================================================

    lv_obj_t *bottomSpacer =
        lv_obj_create(ui_mainpanel);

    lv_obj_set_size(
        bottomSpacer,
        1,
        25
    );

    lv_obj_set_style_bg_opa(
        bottomSpacer,
        LV_OPA_TRANSP,
        LV_PART_MAIN
    );

    lv_obj_set_style_border_width(
        bottomSpacer,
        0,
        LV_PART_MAIN
    );

    lv_obj_set_style_pad_all(
        bottomSpacer,
        0,
        LV_PART_MAIN
    );

    lv_obj_clear_flag(
        bottomSpacer,
        LV_OBJ_FLAG_SCROLLABLE
    );

    // =========================================================
    // تحديث Layout
    // =========================================================

    lv_obj_update_layout(
        ui_mainpanel
    );

    // =========================================================
    // تأكد أن Scroll يبدأ من الأعلى
    // =========================================================

    lv_obj_scroll_to_y(
        ui_mainpanel,
        0,
        LV_ANIM_OFF
    );

    // =========================================================
    // Debug
    // =========================================================

    lv_coord_t panelH =
        lv_obj_get_height(ui_mainpanel);

    lv_coord_t contentH =
        lv_obj_get_content_height(ui_mainpanel);

    Serial.println("--------------------------------");
    Serial.print("👥 Employees : ");
    Serial.println(employeeCount);

    Serial.print("📏 Panel H   : ");
    Serial.println(panelH);

    Serial.print("📏 Content H : ");
    Serial.println(contentH);

    Serial.print("📜 Scrollable: ");

    if (contentH > panelH)
        Serial.println("YES ✅");
    else
        Serial.println("NO ⚠️");

    Serial.println("--------------------------------");
}
// ============================================================
// 🔥 حدث الانتقال لصفحة uic_employee وعرض الموظفين
// ============================================================
void go_to_employee_event(lv_event_t *e) {
    Serial.println("📱 Going to Employee screen...");
    
    if (uic_employee != NULL) {
        lv_screen_load(uic_employee);
        ShowAllEmployees();
        playBeepDirect(40);
    } else {
        Serial.println("❌ uic_employee is NULL!");
    }
}
// ========== حفظ واسترجاع إعدادات الصوت ==========
void saveSoundSettings()
{
    prefs.begin("sound", false);
    prefs.putBool("muteTouch", muteTouch);
    prefs.putBool("muteEvents", muteEvents);
    prefs.putInt("volume", soundVolume);
    prefs.end();
    Serial.println("✅ Sound settings saved to Preferences");
}

void loadSoundSettings()
{
    prefs.begin("sound", true);
    muteTouch = prefs.getBool("muteTouch", false);
    muteEvents = prefs.getBool("muteEvents", false);
    soundVolume = prefs.getInt("volume", 128);
    prefs.end();
    
    Serial.printf("📂 Sound settings loaded: muteTouch=%d, muteEvents=%d, volume=%d\n", 
                  muteTouch, muteEvents, soundVolume);
    
    updateSoundUI();
}

void updateSoundUI()
{
    if (uic_mutetouch != NULL) {
        if (muteTouch) {
            lv_obj_add_state(uic_mutetouch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(uic_mutetouch, LV_STATE_CHECKED);
        }
    }
    
    if (uic_mute != NULL) {
        if (muteEvents) {
            lv_obj_add_state(uic_mute, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(uic_mute, LV_STATE_CHECKED);
        }
    }
    
    if (uic_soundslider != NULL) {
        int sliderValue = map(soundVolume, 0, 255, 0, 100);
        lv_slider_set_value(uic_soundslider, sliderValue, LV_ANIM_OFF);
        
        char buf[8];
        sprintf(buf, "%d%%", sliderValue);
        lv_label_set_text(uic_soundval, buf);
    }
}

// ========== دوال التحكم بـ Switches ==========
void mute_touch_event(lv_event_t *e)
{
    muteTouch = lv_obj_has_state(uic_mutetouch, LV_STATE_CHECKED);
    Serial.printf("Touch mute: %s\n", muteTouch ? "ON" : "OFF");
    saveSoundSettings();
}

void mute_events_event(lv_event_t *e)
{
    muteEvents = lv_obj_has_state(uic_mute, LV_STATE_CHECKED);
    Serial.printf("Events mute: %s\n", muteEvents ? "ON" : "OFF");
    saveSoundSettings();
}

void sound_slider_event(lv_event_t *e)
{
    int value = lv_slider_get_value(uic_soundslider);
    
    char buf[8];
    sprintf(buf, "%d%%", value);
    lv_label_set_text(uic_soundval, buf);
    
    soundVolume = map(value, 0, 100, 0, 255);
    saveSoundSettings();
}

// ========== تطبيق الخطوط تلقائياً ==========
void applyFontsAuto()
{
    const lv_font_t *font16, *font12;
    
    
    if (currentLanguage == "ar")
    {
        font16 = &ui_font_arabic1;
        font12 = &ui_font_arabic12;
    }
    else if (currentLanguage == "fr")
    {
        font16 = &lv_font_montserrat_16;
        font12 = &lv_font_montserrat_12;
    }
    else if (currentLanguage == "en")
    {
        font16 = &lv_font_montserrat_16;
        font12 = &lv_font_montserrat_12;
    }
    
    lv_obj_t* translatedLabels[] = {
        uic_readytxt, uic_menubtn, uic_setbtn, uic_welcometxt,
        uic_settext, uic_txtbtn1, uic_txtbtn4, uic_txtbtn5,
        uic_txtbtn6, uic_txtbtn8, uic_wifitxt, uic_current,
        uic_available, uic_btntxt, uic_soundtxt, uic_soundtxt2,
        uic_mutetxt, uic_soundtxt3, uic_langtxt, uic_chosse,
        uic_savetxt, uic_Label1, uic_Label22, uic_Label4,
        uic_Label9, uic_Label6, uic_savetxt2, uic_menutxt,
        uic_txtbtn3, ui_txtbtn10, ui_txtbtn9, uic_employeetxt,
        ui_mainemployee, ui_idtxt, uic_addemployeetxt, ui_Label5,
        uic_inorout, uic_plustxt8, uic_plustxt7, uic_plustxt6,
        uic_plustxt5, uic_plustxt4, uic_plustxt3, ui_plustxt9,
        ui_plustxt10, ui_plustxt11, ui_plustxt15, ui_plustxt17,
        ui_plustxt16

    };
    int labelCount = sizeof(translatedLabels) / sizeof(translatedLabels[0]);
    
    for (int i = 0; i < labelCount; i++)
    {
        lv_obj_t *child = translatedLabels[i];
        if (child == NULL) continue;
        
        if (child == uic_readytxt || child == uic_current || child == uic_available || child == uic_btntxt || child == uic_Label1 || child == uic_Label22)
        {
            lv_obj_set_style_text_font(child, font12, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_text_font(child, font16, LV_PART_MAIN);
        }
    }
    updateEmployeeFonts();
}

void saveLanguage()
{
    prefs.begin("settings", false);
    prefs.putString("lang", currentLanguage);
    prefs.end();
    Serial.println("✅ Language saved: " + currentLanguage);
    playBeepDirect(60);
    delay(80);
    playBeepDirect(60);
}

void droplang_event(lv_event_t *e)
{
    lv_obj_t *dropdown = (lv_obj_t*)lv_event_get_target(e);
    int selectedIndex = lv_dropdown_get_selected(dropdown);
    
    if (selectedIndex == 0)
    {
        currentLanguage = "ar";
        lv_i18n_set_locale("ar");
        ui_relocalize();
        applyFontsAuto();
        Serial.println("✅ Arabic");
    }
    else if (selectedIndex == 1)
    {
        currentLanguage = "en";
        lv_i18n_set_locale("en");
        ui_relocalize();
        applyFontsAuto();
        Serial.println("✅ English (default)");
    }
    else if (selectedIndex == 2)
    {
        currentLanguage = "fr";
        lv_i18n_set_locale("fr");
        ui_relocalize();
        applyFontsAuto();
        Serial.println("✅ French");
    }
}

void loadSavedLanguage()
{
    prefs.begin("settings", true);
    String lang = prefs.getString("lang", "ar");
    prefs.end();
    
    currentLanguage = lang;
    
    if (lang == "en")
    {
        lv_i18n_set_locale("");
    }
    else
    {
        lv_i18n_set_locale(lang.c_str());
    }
    
    ui_relocalize();
    applyFontsAuto();
    
    if (uic_droplang != NULL)
    {
        if (lang == "ar")
            lv_dropdown_set_selected(uic_droplang, 0);
        else if (lang == "en")
            lv_dropdown_set_selected(uic_droplang, 1);
        else if (lang == "fr")
            lv_dropdown_set_selected(uic_droplang, 2);
    }
}

void savelang_event(lv_event_t *e)
{
    saveLanguage();
}

// ========== دوال التحكم في Loading2 أثناء الاتصال ==========
lv_anim_t bar_anim2;

void bar_anim2_cb(void *var, int32_t value)
{
    lv_bar_set_value((lv_obj_t *)var, value, LV_ANIM_OFF);
}

void ShowConnectingLoading()
{
    lv_obj_clear_flag(uic_loadingoverlay2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(uic_loading2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(uic_loadingoverlay2);
    lv_obj_move_foreground(uic_loading2);
    
    lv_anim_init(&bar_anim2);
    lv_anim_set_var(&bar_anim2, uic_loadingbar2);
    lv_anim_set_exec_cb(&bar_anim2, bar_anim2_cb);
    lv_anim_set_values(&bar_anim2, 0, 100);
    lv_anim_set_time(&bar_anim2, 1500);
    lv_anim_set_playback_time(&bar_anim2, 1500);
    lv_anim_set_repeat_count(&bar_anim2, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&bar_anim2, lv_anim_path_ease_in_out);
    lv_anim_start(&bar_anim2);
    
    lv_bar_set_range(uic_loadingbar2, 0, 100);
    lv_refr_now(NULL);
}

void HideConnectingLoading()
{
    lv_obj_add_flag(uic_loadingoverlay2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uic_loading2, LV_OBJ_FLAG_HIDDEN);
    
    lv_anim_del(uic_loadingbar2, NULL);
    lv_bar_set_value(uic_loadingbar2, 0, LV_ANIM_OFF);
    lv_refr_now(NULL);
}

// ========== دوال التحكم في Panels النتائج ==========
void ShowCorrectPanel()
{
    Serial.println("✅ Showing Correct Panel");
    
    lv_obj_add_flag(uic_wrong, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(uic_correct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(uic_correct);
    lv_refr_now(NULL);
    
    delay(2000);
    lv_obj_add_flag(uic_correct, LV_OBJ_FLAG_HIDDEN);
}

void ShowWrongPanel()
{
    Serial.println("❌ Showing Wrong Panel");
    
    lv_obj_add_flag(uic_correct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(uic_wrong, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(uic_wrong);
    lv_refr_now(NULL);
    
    delay(2000);
    lv_obj_add_flag(uic_wrong, LV_OBJ_FLAG_HIDDEN);
}

// ========== دوال الـ WiFi ==========
void ConnectWifi()
{
    String ssid = lv_label_get_text(uic_selectednetwork);
    String password = lv_textarea_get_text(uic_passwordtxt);

    Serial.println("Connecting...");
    Serial.println(ssid);

    lv_obj_add_flag(uic_correct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uic_wrong, LV_OBJ_FLAG_HIDDEN);

    ShowConnectingLoading();
    lv_refr_now(NULL);
    delay(50);

    WiFi.begin(ssid.c_str(), password.c_str());

    int timeout = 0;

    while (WiFi.status() != WL_CONNECTED && timeout < 30)
    {
        delay(500);
        Serial.print(".");
        timeout++;
        
        if (timeout % 2 == 0)
        {
            lv_timer_handler();
            lv_refr_now(NULL);
        }
    }

    HideConnectingLoading();
    lv_refr_now(NULL);

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("");
        Serial.println("Connected!");

        prefs.begin("wifi", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", password);
        prefs.end();
        
        UpdateCurrentNetwork();
        
        playBeepDirect(80);
        ShowCorrectPanel();
    }
    else
    {
        Serial.println("");
        Serial.println("Connection failed");
        playBeepDirect(100);
        ShowWrongPanel();
    }
}

void connect_btn_event(lv_event_t *e)
{
    ConnectWifi();
}

void wifi_item_event(lv_event_t *e)
{
    lv_obj_t *item = (lv_obj_t *)lv_event_get_target(e);
    const char *ssid = (const char *)lv_event_get_user_data(e);

    if (ssid != NULL) {
        selectedSSID = ssid;

        lv_label_set_text(
            uic_selectednetwork,
            ssid);

        lv_screen_load(uic_connectwifi);
        
        // 🔥🔥🔥 التغيير هنا: حرر الذاكرة بعد الاستخدام
        delete[] ssid;
    }
}
void UpdateWifiStatus()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        lv_label_set_text(uic_wifistat, LV_SYMBOL_CLOSE);

        lv_obj_set_style_text_color(
            uic_wifistat,
            lv_palette_main(LV_PALETTE_RED),
            LV_PART_MAIN);

        lv_obj_clear_flag(
            uic_wifistat,
            LV_OBJ_FLAG_HIDDEN);

        return;
    }

    IPAddress ip;

    if (!WiFi.hostByName("google.com", ip))
    {
        lv_label_set_text(uic_wifistat, LV_SYMBOL_WARNING);

        lv_obj_set_style_text_color(
            uic_wifistat,
            lv_palette_main(LV_PALETTE_YELLOW),
            LV_PART_MAIN);

        lv_obj_clear_flag(
            uic_wifistat,
            LV_OBJ_FLAG_HIDDEN);

        return;
    }

    lv_obj_add_flag(
        uic_wifistat,
        LV_OBJ_FLAG_HIDDEN);
}

void AddWifiNetwork(const char *ssid, int index)
{
    if (uic_wificontainer == NULL) return;
    
    lv_obj_t *item = lv_obj_create(uic_wificontainer);

    lv_obj_set_size(item, 400, 40);
    lv_obj_set_pos(item, 0, index * 44);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(
        item,
        lv_obj_get_style_bg_color(
            uic_wifiItemTemplate,
            LV_PART_MAIN),
        LV_PART_MAIN);

    lv_obj_set_style_border_color(
        item,
        lv_obj_get_style_border_color(
            uic_wifiItemTemplate,
            LV_PART_MAIN),
        LV_PART_MAIN);

    lv_obj_set_style_border_width(
        item,
        lv_obj_get_style_border_width(
            uic_wifiItemTemplate,
            LV_PART_MAIN),
        LV_PART_MAIN);

    lv_obj_set_style_radius(
        item,
        lv_obj_get_style_radius(
            uic_wifiItemTemplate,
            LV_PART_MAIN),
        LV_PART_MAIN);

    lv_obj_t *icon = lv_image_create(item);
    lv_image_set_src(icon, &ui_img_1799210772);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *label = lv_label_create(item);
    lv_label_set_text(label, ssid);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label,
        lv_obj_get_style_text_font(uic_lblWifiName, LV_PART_MAIN),
        LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 40, 0);

    lv_obj_t *arrow = lv_image_create(item);
    lv_image_set_src(arrow, &ui_img_1832640421);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);

    // 🔥🔥🔥 التغيير هنا: استخدم String بدلاً من static char
    String ssidStr = String(ssid);
    char *ssid_copy = new char[ssidStr.length() + 1];
    strcpy(ssid_copy, ssidStr.c_str());
    
    lv_obj_add_event_cb(item, wifi_item_event, LV_EVENT_CLICKED, ssid_copy);
}
void UpdateCurrentNetwork()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        lv_label_set_text(uic_currentnetwork, WiFi.SSID().c_str());
    }
    else
    {
        lv_label_set_text(uic_currentnetwork, "");
    }
}

// ========== دوال التحكم في الـ Loading الرئيسي ==========
lv_anim_t bar_anim;

void bar_anim_cb(void *var, int32_t value)
{
    lv_bar_set_value((lv_obj_t *)var, value, LV_ANIM_OFF);
}

// ========== إضافة الصوت لجميع الأزرار ==========
void AddBeepToAllButtons(lv_obj_t * parent)
{
    uint32_t count = lv_obj_get_child_count(parent);

    for(uint32_t i = 0; i < count; i++)
    {
        lv_obj_t * child = lv_obj_get_child(parent, i);

        if(lv_obj_check_type(child, &lv_button_class))
        {
            lv_obj_add_event_cb(
                child,
                playBeep,
                LV_EVENT_PRESSED,
                NULL
            );
        }

        AddBeepToAllButtons(child);
    }
}



void ShowLoading()
{
    lv_obj_clear_flag(uic_loadingoverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(uic_loading, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(uic_loadingoverlay);
    lv_obj_move_foreground(uic_loading);
    
    lv_anim_init(&bar_anim);
    lv_anim_set_var(&bar_anim, uic_loadingbar);
    lv_anim_set_exec_cb(&bar_anim, bar_anim_cb);
    lv_anim_set_values(&bar_anim, 0, 100);
    lv_anim_set_time(&bar_anim, 1500);
    lv_anim_set_playback_time(&bar_anim, 1500);
    lv_anim_set_repeat_count(&bar_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&bar_anim, lv_anim_path_ease_in_out);
    lv_anim_start(&bar_anim);
    
    lv_bar_set_range(uic_loadingbar, 0, 100);
    lv_refr_now(NULL);
}

void HideLoading()
{
    lv_obj_add_flag(uic_loadingoverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uic_loading, LV_OBJ_FLAG_HIDDEN);
    
    lv_anim_del(uic_loadingbar, NULL);
    lv_bar_set_value(uic_loadingbar, 0, LV_ANIM_OFF);
}

// ========== دالة Scan ==========
void PerformWifiScan()
{
    if (isScanning) return;
    
    isScanning = true;
    
    ShowLoading();
    lv_refr_now(NULL);
    delay(100);
    
    bool wasConnected = (WiFi.status() == WL_CONNECTED);
    String savedSSID = "";
    String savedPSK = "";
    
    if (wasConnected)
    {
        savedSSID = WiFi.SSID();
        savedPSK = WiFi.psk();
        Serial.println("Saved network: " + savedSSID);
        
        lv_refr_now(NULL);
        WiFi.disconnect(true);
        delay(300);
        lv_refr_now(NULL);
    }
    
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    delay(300);
    lv_refr_now(NULL);
    
    Serial.println("Scanning...");
    int n = -1;
    int retries = 0;
    
    while (retries < 5 && (n < 0))
    {
        lv_timer_handler();
        lv_refr_now(NULL);
        
        n = WiFi.scanNetworks();
        Serial.printf("Attempt %d: %d\n", retries + 1, n);
        
        if (n < 0)
        {
            delay(500);
            if (retries < 4)
            {
                WiFi.mode(WIFI_OFF);
                delay(300);
                WiFi.mode(WIFI_STA);
                delay(300);
            }
        }
        
        lv_timer_handler();
        lv_refr_now(NULL);
        retries++;
    }
    
    Serial.printf("Final scan result: %d networks\n", n);
    
    lv_obj_clean(uic_wificontainer);
    
    if (n <= 0)
    {
        lv_obj_t *msgLabel = lv_label_create(uic_wificontainer);
        lv_label_set_text(msgLabel, n == 0 ? "No networks found" : "Scan failed, try again");
        lv_obj_set_style_text_color(msgLabel, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
        lv_obj_align(msgLabel, LV_ALIGN_CENTER, 0, 0);
    }
    else
    {
        int maxDisplay = (n > 8) ? 8 : n;  // 🔥 قلل إلى 8 شبكات
        
        for(int i = 0; i < maxDisplay; i++)
        {
            String ssid = WiFi.SSID(i);
            if (ssid.length() > 0)
            {
                AddWifiNetwork(ssid.c_str(), i);
                Serial.printf("  %d: %s\n", i, ssid.c_str());
            }
        }
        lv_obj_set_height(uic_wificontainer, maxDisplay * 44 + 20);
        
        if (n > 8)
        {
            lv_obj_t *msgLabel = lv_label_create(uic_wificontainer);
            char msg[32];
            sprintf(msg, "+ %d more networks", n - 8);
            lv_label_set_text(msgLabel, msg);
            lv_obj_set_style_text_color(msgLabel, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
            lv_obj_align(msgLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
        }
    }
    
    WiFi.scanDelete();
    
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("Memory: %d%% used\n", mon.used_pct);
    
    if (wasConnected && savedSSID.length() > 0 && WiFi.status() != WL_CONNECTED)
    {
        Serial.print("Reconnecting to: " + savedSSID + " ");
        WiFi.begin(savedSSID.c_str(), savedPSK.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30)
        {
            delay(200);
            attempts++;
            Serial.print(".");
            
            if (attempts % 3 == 0)
            {
                lv_timer_handler();
                lv_refr_now(NULL);
            }
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("Reconnected!");
            UpdateCurrentNetwork();
            WiFi.setAutoReconnect(true);
        }
        else
        {
            Serial.println("Reconnect failed");
        }
    }
    
    playBeepDirect(50);
    delay(60);
    playBeepDirect(50);

    HideLoading();
    isScanning = false;
}

void SearchWifiEvent(lv_event_t *e)
{
    Serial.println("Search button clicked");
    lv_obj_clean(uic_wificontainer);
    PerformWifiScan();
}

void changetowifi_event(lv_event_t *e)
{
    wifiSettingsOpen = true;
    lastReconnect = millis();
}

void backtosettings_event(lv_event_t *e)
{
    wifiSettingsOpen = false;
    lv_obj_clean(uic_wificontainer);
}

void changetoempo_event(lv_event_t *e)
{
    lv_screen_load(uic_employee);
}
// ============================================================
// 🔥 دوال تعديل وحذف الموظفين
// ============================================================

int selectedEmployeeIndex = -1;  // الفهرس الحالي للموظف المحدد

// ============================================================
// 🔥 دالة حذف موظف من SD Card
// ============================================================
void deleteEmployeeFromSD(int employeeID) {
    File file = SD.open("/employees.csv");
    if (!file) {
        Serial.println("❌ employees.csv not found!");
        return;
    }

    String lines[MAX_EMPLOYEES];
    int lineCount = 0;
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            lines[lineCount++] = line;
        }
    }
    file.close();

    SD.remove("/employees.csv");
    File newFile = SD.open("/employees.csv", FILE_WRITE);
    if (!newFile) {
        Serial.println("❌ Failed to create new employees.csv");
        return;
    }

    for (int i = 0; i < lineCount; i++) {
        String line = lines[i];
        int firstComma = line.indexOf(',');
        int secondComma = line.indexOf(',', firstComma + 1);
        
        if (firstComma > 0 && secondComma > 0) {
            String idStr = line.substring(0, firstComma);
            if (idStr.toInt() != employeeID) {
                newFile.println(line);
            } else {
                // 🔥 استخراج الـ Fingerprint ID قبل الحذف
                String fpStr = line.substring(secondComma + 1);
                int fpID = fpStr.toInt();
                Serial.printf("🗑️ Deleting employee ID: %d (FP: %d)\n", employeeID, fpID);
                
                // 🔥 حذف البصمة من المستشعر
                int p = finger.deleteModel(fpID);
                if (p == FINGERPRINT_OK) {
                    Serial.printf("✅ Fingerprint #%d deleted!\n", fpID);
                } else {
                    Serial.printf("❌ Failed to delete fingerprint #%d\n", fpID);
                }
            }
        }
    }
    newFile.close();
    
    Serial.println("✅ Employee and fingerprint deleted successfully!");
}


// ============================================================
// 🔥 دالة تعديل اسم موظف في SD Card
// ============================================================
void updateEmployeeNameInSD(int employeeID, String newName) {
    File file = SD.open("/employees.csv");
    if (!file) {
        Serial.println("❌ employees.csv not found!");
        return;
    }

    String lines[MAX_EMPLOYEES];
    int lineCount = 0;
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            lines[lineCount++] = line;
        }
    }
    file.close();

    SD.remove("/employees.csv");
    File newFile = SD.open("/employees.csv", FILE_WRITE);
    if (!newFile) {
        Serial.println("❌ Failed to create new employees.csv");
        return;
    }

    for (int i = 0; i < lineCount; i++) {
        String line = lines[i];
        int firstComma = line.indexOf(',');
        int secondComma = line.indexOf(',', firstComma + 1);
        
        if (firstComma > 0 && secondComma > 0) {
            String idStr = line.substring(0, firstComma);
            if (idStr.toInt() == employeeID) {
                // تعديل الاسم
                String fpStr = line.substring(secondComma + 1);
                newFile.println(idStr + "," + newName + "," + fpStr);
                Serial.printf("✏️ Updated employee: ID=%d, New Name=%s\n", employeeID, newName.c_str());
            } else {
                newFile.println(line);
            }
        }
    }
    newFile.close();
    
    Serial.println("✅ Employee updated successfully!");
}

// ============================================================
// 🔥 حدث زر Delete - إظهار popup
// ============================================================

void deleteEmployeeAndFingerprint(int employeeID, int fingerprintID) {
    // 1️⃣ حذف البصمة من المستشعر
    Serial.printf("🗑️ Deleting fingerprint ID: %d\n", fingerprintID);
    int p = finger.deleteModel(fingerprintID);
    
    if (p == FINGERPRINT_OK) {
        Serial.printf("✅ Fingerprint #%d deleted from sensor!\n", fingerprintID);
    } else {
        Serial.printf("❌ Failed to delete fingerprint #%d (error: %d)\n", fingerprintID, p);
    }
    
    // 2️⃣ حذف الموظف من SD Card
    deleteEmployeeFromSD(employeeID);
    
    // 3️⃣ تحديث القائمة
    ShowAllEmployees();
    
    // 4️⃣ تشغيل صوت
    playBeepDirect(80);
}

void deleteEmployeeEvent(lv_event_t *e) {
    lv_obj_t *btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t *item = lv_obj_get_parent(btn);  // البطاقة التي تحتوي الزر
    
    // البحث عن الموظف المحدد
    // نبحث في البطاقة عن الـ ID
    lv_obj_t *idLabel = lv_obj_get_child(item, 1);  // الـ ID هو الطفل الثاني (0=اسم, 1=ID)
    if (idLabel != NULL) {
        const char *idText = lv_label_get_text(idLabel);
        selectedEmployeeIndex = atoi(idText);
        Serial.printf("🗑️ Delete requested for employee ID: %d\n", selectedEmployeeIndex);
    }
    
    // 🔥 إظهار uic_popup20
    if (uic_popup20 != NULL) {
        lv_obj_clear_flag(uic_popup20, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(uic_popup20);
    }
}

// ============================================================
// 🔥 حدث زر OK في popup - تأكيد الحذف
// ============================================================
void confirmDeleteEvent(lv_event_t *e) {
    Serial.printf("✅ Confirming delete for employee ID: %d\n", selectedEmployeeIndex);
    
    if (selectedEmployeeIndex > 0) {
        // 🔥 حذف الموظف والبصمة معاً
        deleteEmployeeAndFingerprint(selectedEmployeeIndex, selectedEmployeeIndex);
        
        // 🔥 إخفاء popup
        if (uic_popup20 != NULL) {
            lv_obj_add_flag(uic_popup20, LV_OBJ_FLAG_HIDDEN);
        }
        
        playBeepDirect(80);
    }
}
// ============================================================
// 🔥 حدث زر NO في popup - إلغاء الحذف
// ============================================================
void cancelDeleteEvent(lv_event_t *e) {
    Serial.println("❌ Delete cancelled");
    
    // 🔥 إخفاء popup
    if (uic_popup20 != NULL) {
        lv_obj_add_flag(uic_popup20, LV_OBJ_FLAG_HIDDEN);
    }
    
    playBeepDirect(40);
}

// ============================================================
// 🔥 حدث زر Edit - الانتقال لصفحة التعديل
// ============================================================
void editEmployeeEvent(lv_event_t *e) {
    lv_obj_t *btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t *item = lv_obj_get_parent(btn);  // البطاقة التي تحتوي الزر
    
    // البحث عن الموظف المحدد
    lv_obj_t *nameLabel = lv_obj_get_child(item, 0);  // الاسم هو الطفل الأول
    lv_obj_t *idLabel = lv_obj_get_child(item, 1);    // الـ ID هو الطفل الثاني
    
    if (nameLabel != NULL && idLabel != NULL) {
        const char *nameText = lv_label_get_text(nameLabel);
        const char *idText = lv_label_get_text(idLabel);
        selectedEmployeeIndex = atoi(idText);
        
        Serial.printf("✏️ Edit requested for: %s (ID: %d)\n", nameText, selectedEmployeeIndex);
        
        // 🔥 الانتقال لصفحة uic_editemployee
        if (uic_editemployee != NULL) {
            lv_screen_load(uic_editemployee);
        }
        
        // 🔥 تعبئة حقل الاسم بالاسم القديم
        if (uic_editemployeename != NULL) {
            lv_textarea_set_text(uic_editemployeename, nameText);
        }
    }
}

// ============================================================
// 🔥 حدث حفظ التعديل
// ============================================================
void saveEditEmployeeEvent(lv_event_t *e) {
    if (uic_editemployeename == NULL) return;
    
    String newName = String(lv_textarea_get_text(uic_editemployeename));
    newName.trim();
    
    if (newName.length() == 0) {
        Serial.println("❌ Name cannot be empty!");
        return;
    }
    
    if (selectedEmployeeIndex > 0) {
        // 🔥 تعديل الاسم في SD Card
        updateEmployeeNameInSD(selectedEmployeeIndex, newName);
        
        // 🔥 إظهار uic_okpopup10
        if (uic_okpopup10 != NULL) {
            lv_obj_clear_flag(uic_okpopup10, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(uic_okpopup10);
        }
        
        playBeepDirect(80);
       
    }

}

// ============================================================
// 🔥 حدث إخفاء okpopup10
// ============================================================
void hideOkPopupEvent(lv_event_t *e) {
    if (uic_okpopup10 != NULL) {
        lv_obj_add_flag(uic_okpopup10, LV_OBJ_FLAG_HIDDEN);
    }
    
    // العودة لصفحة الموظفين وتحديث القائمة
    if (uic_employee != NULL) {
        lv_screen_load(uic_employee);
        ShowAllEmployees();
    }
}

void AddEmployeeCard(int index) {
    if (ui_mainpanel == NULL) return;
    if (index >= employeeCount) return;
    
    int startY = 40;
    int cardY = startY + (index * 62);
    
    lv_obj_t *item = lv_obj_create(ui_mainpanel);
    lv_obj_set_size(item, 464, 58);
    lv_obj_set_pos(item, 0, cardY);
    lv_obj_set_align(item, LV_ALIGN_TOP_LEFT);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_style_radius(item, 15, LV_PART_MAIN);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x141922), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(item, 255, LV_PART_MAIN);
    lv_obj_set_style_border_color(item, lv_color_hex(0x2A303B), LV_PART_MAIN);
    lv_obj_set_style_border_opa(item, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(item, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(item, 0, LV_PART_MAIN);
    
    // 🔥 اسم الموظف
    lv_obj_t *nameLabel = lv_label_create(item);
    lv_label_set_text(nameLabel, employeeList[index].name.c_str());
    lv_obj_set_style_text_color(nameLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(nameLabel, 255, LV_PART_MAIN);
    lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(nameLabel, 20, 3);
    
    // 🔥 ID (مخفي)
    lv_obj_t *idLabel = lv_label_create(item);
    char idStr[10];
    sprintf(idStr, "%d", employeeList[index].id);
    lv_label_set_text(idLabel, idStr);
    lv_obj_set_style_text_color(idLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(idLabel, 255, LV_PART_MAIN);
    lv_obj_set_style_text_font(idLabel, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(idLabel, LV_ALIGN_TOP_RIGHT, -15, 2);
    
    // 🔥 زر Edit
    lv_obj_t *editBtn = lv_btn_create(item);
    lv_obj_set_size(editBtn, 39, 38);
    lv_obj_align(editBtn, LV_ALIGN_CENTER, 58, 0);
    lv_obj_set_style_radius(editBtn, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(editBtn, lv_color_hex(0x27EA39), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(editBtn, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(editBtn, 0, LV_PART_MAIN);
    
    lv_obj_t *editImg = lv_image_create(editBtn);
    lv_image_set_src(editImg, &ui_img_1790866911);
    lv_obj_center(editImg);
    
    // 🔥 زر Delete
    lv_obj_t *delBtn = lv_btn_create(item);
    lv_obj_set_size(delBtn, 39, 38);
    lv_obj_align(delBtn, LV_ALIGN_CENTER, 112, 0);
    lv_obj_set_style_radius(delBtn, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(delBtn, lv_color_hex(0xEA2727), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(delBtn, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(delBtn, 0, LV_PART_MAIN);
    
    lv_obj_t *delImg = lv_image_create(delBtn);
    lv_image_set_src(delImg, &ui_img_1077457577);
    lv_obj_center(delImg);
    
    // 🔥 إضافة الأحداث للزرين
    lv_obj_add_event_cb(editBtn, editEmployeeEvent, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(delBtn, deleteEmployeeEvent, LV_EVENT_CLICKED, NULL);
}

// ============================================================
// 🔥 هيكل بيانات السجل (Attendance Record)
// ============================================================
struct AttendanceRecord {
    int id;
    String name;
    int fingerprintID;
    String type;    // "IN" or "OUT"
    String time;
    String date;
};

AttendanceRecord attendanceList[MAX_EMPLOYEES * 10];  // سعة كافية
int attendanceCount = 0;

// ============================================================
// 🔥 تحميل سجل الحضور من SD Card
// ============================================================
void loadAttendanceFromSD()
{
    attendanceCount = 0;

    File file = SD.open("/attendance.csv", FILE_READ);

    if (!file)
    {
        Serial.println("❌ attendance.csv not found!");
        return;
    }

    Serial.println("================================");
    Serial.println("📂 LOADING ATTENDANCE");
    Serial.println("================================");

    bool firstLine = true;

    while (file.available() && attendanceCount < 100)
    {
        String line = file.readStringUntil('\n');

        line.trim();
        line.replace("\r", "");
        line.trim();

        if (line.length() == 0)
            continue;

        // تجاهل Header
        if (firstLine)
        {
            firstLine = false;

            if (line.startsWith("ID,"))
            {
                Serial.println("📝 Header skipped");
                continue;
            }
        }

        Serial.print("📄 RAW: [");
        Serial.print(line);
        Serial.println("]");

        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);
        int p4 = line.indexOf(',', p3 + 1);
        int p5 = line.indexOf(',', p4 + 1);

        if (p1 < 0 ||
            p2 < 0 ||
            p3 < 0 ||
            p4 < 0 ||
            p5 < 0)
        {
            Serial.println("⚠️ Invalid attendance line!");
            continue;
        }

        String idStr = line.substring(0, p1);
        String name = line.substring(p1 + 1, p2);
        String fpStr = line.substring(p2 + 1, p3);
        String type = line.substring(p3 + 1, p4);
        String time = line.substring(p4 + 1, p5);
        String date = line.substring(p5 + 1);

        idStr.trim();
        name.trim();
        fpStr.trim();
        type.trim();
        time.trim();
        date.trim();

        type.toUpperCase();

        date.replace("-", "/");

        if (name.length() == 0)
            continue;

        if (type != "IN" && type != "OUT")
        {
            Serial.printf(
                "⚠️ Invalid type: [%s]\n",
                type.c_str()
            );
            continue;
        }

        if (date.length() == 0)
            continue;

        attendanceList[attendanceCount].id =
            idStr.toInt();

        attendanceList[attendanceCount].name =
            name;

        attendanceList[attendanceCount].fingerprintID =
            fpStr.toInt();

        attendanceList[attendanceCount].type =
            type;

        attendanceList[attendanceCount].time =
            time;

        attendanceList[attendanceCount].date =
            date;

        Serial.printf(
            "✅ [%d] Name=[%s] FP=%d Type=[%s] Time=[%s] Date=[%s]\n",
            attendanceCount,
            name.c_str(),
            fpStr.toInt(),
            type.c_str(),
            time.c_str(),
            date.c_str()
        );

        attendanceCount++;
    }

    file.close();

    Serial.println("================================");
    Serial.printf(
        "✅ TOTAL ATTENDANCE: %d\n",
        attendanceCount
    );
    Serial.println("================================");
}// ============================================================
// 🔥 عرض سجل الحضور في uic_recordscreen (نسخة محسنة)
// ============================================================

// ============================================================
// 🔥 عرض سجل الحضور في uic_recordscreen (نسخة محسنة)
// ============================================================

// ============================================================
// 🔥 عرض سجل الحضور (نسخة مبسطة ونظيفة)
// ============================================================
// 🔥 عرض سجلات الحضور (مع دعم الفلترة حسب الشهر)
// ============================================================
// ============================================================
// 🔥 عرض سجلات الحضور (مع دعم الفلترة حسب الشهر)
// ============================================================
void ShowAttendanceRecords(bool filterByMonth = false, String selectedMonthYear = "")
{
    if (uic_recordscreen == NULL) {
        Serial.println("❌ uic_recordscreen is NULL!");
        return;
    }

    if (ui_mainpanel3 == NULL) {
        Serial.println("❌ ui_mainpanel3 is NULL!");
        return;
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("📋 SHOW ATTENDANCE RECORDS");
    Serial.println("========================================");

    lv_obj_clean(ui_mainpanel3);

    loadEmployeesFromSD();
    loadAttendanceFromSD();

    Serial.printf("👥 Employees : %d\n", employeeCount);
    Serial.printf("📦 Records   : %d\n", attendanceCount);

    // =========================================================
    // 🔥 فلترة حسب الشهر
    // =========================================================
    
    int selectedMonth = 0;
    int selectedYear = 0;
    bool hasFilter = false;
    
    if (filterByMonth && selectedMonthYear.length() > 0 && selectedMonthYear != "No Records") {
        selectedMonth = selectedMonthYear.substring(0, 2).toInt();
        selectedYear = selectedMonthYear.substring(3, 7).toInt();
        hasFilter = true;
        Serial.printf("📅 Filtering for: %02d/%04d\n", selectedMonth, selectedYear);
    }

    // =========================================================
    // CURRENT DATE
    // =========================================================

    updateTime();
    char today[11];
    snprintf(today, sizeof(today), "%04d/%02d/%02d", currentYear, currentMonth, currentDay);
    String currentDate = String(today);
    currentDate.trim();
    Serial.print("📅 TODAY: ");
    Serial.println(currentDate);

    // =========================================================
    // MAIN PANEL SCROLL
    // =========================================================

    const int CONTENT_WIDTH = 430;

    lv_obj_add_flag(ui_mainpanel3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_mainpanel3, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_mainpanel3, LV_SCROLLBAR_MODE_AUTO);

    // =========================================================
    // CONTENT
    // =========================================================

    lv_obj_t *content = lv_obj_create(ui_mainpanel3);
    lv_obj_set_width(content, CONTENT_WIDTH);
    lv_obj_set_height(content, 100);
    lv_obj_set_pos(content, 5, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // =========================================================
    // 🔥 GET UNIQUE DATES (مع دعم الفلترة)
    // 🔥 استخدام static لتجنب Stack Overflow
    // =========================================================

    static String dates[366];  // 🔥 static = في Heap
    int dateCount = 0;

    for (int i = 0; i < attendanceCount; i++)
    {
        String date = attendanceList[i].date;
        date.trim();
        date.replace("\r", "");
        date.replace("\n", "");
        date.replace("-", "/");

        if (date.length() == 0)
            continue;

        // 🔥 إذا كان هناك فلترة
        if (hasFilter) {
            int firstSlash = date.indexOf('/');
            int secondSlash = date.indexOf('/', firstSlash + 1);
            
            if (firstSlash > 0 && secondSlash > 0) {
                int recordMonth = 0, recordYear = 0;
                String yearPart = date.substring(0, firstSlash);
                
                if (yearPart.length() == 4) {
                    recordYear = yearPart.toInt();
                    recordMonth = date.substring(firstSlash + 1, secondSlash).toInt();
                } else {
                    String yearPart2 = date.substring(secondSlash + 1);
                    if (yearPart2.length() == 4) {
                        recordYear = yearPart2.toInt();
                        recordMonth = date.substring(firstSlash + 1, secondSlash).toInt();
                    }
                }
                
                if (recordMonth != selectedMonth || recordYear != selectedYear) {
                    continue;
                }
            }
        }

        // التحقق من التكرار
        bool exists = false;
        for (int j = 0; j < dateCount; j++)
        {
            if (dates[j] == date)
            {
                exists = true;
                break;
            }
        }

        if (!exists && dateCount < 366)
        {
            dates[dateCount] = date;
            dateCount++;
        }
    }

    // ترتيب التواريخ (الأحدث أولاً)
    for (int i = 0; i < dateCount - 1; i++)
    {
        for (int j = i + 1; j < dateCount; j++)
        {
            if (dates[j] > dates[i])
            {
                String temp = dates[i];
                dates[i] = dates[j];
                dates[j] = temp;
            }
        }
    }

    Serial.println("========== DATES ==========");
    for (int i = 0; i < dateCount; i++)
    {
        Serial.printf("%d -> [%s]\n", i, dates[i].c_str());
    }
    Serial.println("===========================");

    // إذا ما في سجلات
    if (dateCount == 0)
    {
        lv_obj_t *msg = lv_label_create(ui_mainpanel3);
        if (hasFilter) {
            lv_label_set_text(msg, "📋 No records for this month");
        } else {
            lv_label_set_text(msg, "📋 No attendance records");
        }
        lv_obj_set_style_text_color(msg, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_center(msg);
        return;
    }

    // =========================================================
    // DRAW
    // =========================================================

    int cardY = 8;

    for (int d = 0; d < dateCount; d++)
    {
        String date = dates[d];
        date.trim();

        // DATE HEADER
        lv_obj_t *dateLabel = lv_label_create(content);
        lv_label_set_text(dateLabel, date.c_str());
        lv_obj_set_width(dateLabel, CONTENT_WIDTH - 20);
        lv_obj_set_height(dateLabel, 28);
        lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_pos(dateLabel, 5, cardY);
        cardY += 30;

        // HEADER
        lv_obj_t *header = lv_obj_create(content);
        lv_obj_set_size(header, CONTENT_WIDTH - 10, 24);
        lv_obj_set_pos(header, 5, cardY);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        // HEADER NAME
        lv_obj_t *headerName = lv_label_create(header);
        lv_label_set_text(headerName, "NAME");
        lv_obj_set_width(headerName, 140);
        lv_obj_set_style_text_color(headerName, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(headerName, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(headerName, LV_ALIGN_LEFT_MID, 5, 0);

        // HEADER IN
        lv_obj_t *headerIn = lv_label_create(header);
        lv_label_set_text(headerIn, "IN");
        lv_obj_set_width(headerIn, 70);
        lv_obj_set_style_text_color(headerIn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(headerIn, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_align(headerIn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(headerIn, LV_ALIGN_LEFT_MID, 145, 0);

        // HEADER OUT
        lv_obj_t *headerOut = lv_label_create(header);
        lv_label_set_text(headerOut, "OUT");
        lv_obj_set_width(headerOut, 70);
        lv_obj_set_style_text_color(headerOut, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(headerOut, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_align(headerOut, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(headerOut, LV_ALIGN_LEFT_MID, 225, 0);

        cardY += 27;

        // عرض الموظفين
        for (int e = 0; e < employeeCount; e++)
        {
            String employeeName = employeeList[e].name;
            employeeName.trim();
            if (employeeName.length() == 0) continue;

            String inTime = "OFF";
            String outTime = "OFF";
            bool foundIN = false;
            bool foundOUT = false;

            for (int i = 0; i < attendanceCount; i++)
            {
                // فلترة
                if (hasFilter) {
                    String recordDate = attendanceList[i].date;
                    recordDate.trim();
                    recordDate.replace("-", "/");
                    
                    int firstSlash = recordDate.indexOf('/');
                    int secondSlash = recordDate.indexOf('/', firstSlash + 1);
                    
                    if (firstSlash > 0 && secondSlash > 0) {
                        int recordMonth = 0, recordYear = 0;
                        String yearPart = recordDate.substring(0, firstSlash);
                        
                        if (yearPart.length() == 4) {
                            recordYear = yearPart.toInt();
                            recordMonth = recordDate.substring(firstSlash + 1, secondSlash).toInt();
                        } else {
                            String yearPart2 = recordDate.substring(secondSlash + 1);
                            if (yearPart2.length() == 4) {
                                recordYear = yearPart2.toInt();
                                recordMonth = recordDate.substring(firstSlash + 1, secondSlash).toInt();
                            }
                        }
                        
                        if (recordMonth != selectedMonth || recordYear != selectedYear) {
                            continue;
                        }
                    }
                }
                
                if (attendanceList[i].name == employeeName && attendanceList[i].date == date)
                {
                    if (attendanceList[i].type == "IN")
                    {
                        inTime = attendanceList[i].time;
                        foundIN = true;
                    }
                    else if (attendanceList[i].type == "OUT")
                    {
                        outTime = attendanceList[i].time;
                        foundOUT = true;
                    }
                }
            }

            if (foundIN && !foundOUT)
            {
                outTime = "--:--";
            }

            // CARD
            lv_obj_t *item = lv_obj_create(content);
            lv_obj_set_size(item, CONTENT_WIDTH - 10, 46);
            lv_obj_set_pos(item, 5, cardY);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_set_style_radius(item, 8, LV_PART_MAIN);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x141922), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(item, lv_color_hex(0x2A303B), LV_PART_MAIN);
            lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
            lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN);

            // NAME
            lv_obj_t *nameLabel = lv_label_create(item);
            lv_label_set_text(nameLabel, employeeName.c_str());
            lv_obj_set_width(nameLabel, 130);
            lv_obj_set_style_text_color(nameLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
            lv_obj_align(nameLabel, LV_ALIGN_LEFT_MID, 10, 0);

            // IN
            lv_obj_t *inLabel = lv_label_create(item);
            lv_label_set_text(inLabel, inTime.c_str());
            lv_obj_set_width(inLabel, 70);
            if (inTime == "OFF")
            {
                lv_obj_set_style_text_color(inLabel, lv_color_hex(0x555555), LV_PART_MAIN);
            }
            else
            {
                lv_obj_set_style_text_color(inLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            }
            lv_obj_set_style_text_font(inLabel, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_obj_set_style_text_align(inLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_align(inLabel, LV_ALIGN_LEFT_MID, 145, 0);

            // OUT
            lv_obj_t *outLabel = lv_label_create(item);
            lv_label_set_text(outLabel, outTime.c_str());
            lv_obj_set_width(outLabel, 70);
            if (outTime == "OFF" || outTime == "--:--")
            {
                lv_obj_set_style_text_color(outLabel, lv_color_hex(0x555555), LV_PART_MAIN);
            }
            else
            {
                lv_obj_set_style_text_color(outLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            }
            lv_obj_set_style_text_font(outLabel, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_obj_set_style_text_align(outLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_align(outLabel, LV_ALIGN_LEFT_MID, 225, 0);

            cardY += 54;
        }

        cardY += 12;
    }

    lv_obj_set_height(content, cardY + 20);
    lv_obj_update_layout(content);
    lv_obj_update_layout(ui_mainpanel3);
    lv_obj_scroll_to_y(ui_mainpanel3, 0, LV_ANIM_OFF);

    Serial.println("========================================");
    Serial.printf("👥 Employees : %d\n", employeeCount);
    Serial.printf("📅 Dates     : %d\n", dateCount);
    Serial.printf("📦 Records   : %d\n", attendanceCount);
    Serial.printf("📏 Height    : %d\n", cardY + 20);
    Serial.println("========================================");
}// ============================================================
// 🔥 استخراج الأشهر والسنة الموجودة فعلاً في السجلات
// ============================================================
// ============================================================
// 🔥 تعبئة Dropdown بأسماء الموظفين
// ============================================================
void populateNameDropdown() {
    if (uic_namedropdown == NULL) {
        Serial.println("❌ uic_namedropdown is NULL!");
        return;
    }
    
    loadEmployeesFromSD();
    
    String options = "";
    for (int i = 0; i < employeeCount; i++) {
        options += employeeList[i].name;
        if (i < employeeCount - 1) options += "\n";
    }
    
    if (employeeCount == 0) {
        options = "No Employees";
        Serial.println("⚠️ No employees found!");
    }
    
    lv_dropdown_set_options(uic_namedropdown, options.c_str());
    lv_dropdown_set_selected(uic_namedropdown, 0);
    
    Serial.printf("✅ Name dropdown populated with %d employees\n", employeeCount);
}

// ============================================================
// 🔥 عرض سجلات موظف معين (كل يوم منفصل)
// ============================================================
// ============================================================
// 🔥 عرض سجلات موظف معين (مع كل يوم منفصل) - نسخة محسنة
// ============================================================
void filterAttendanceByName(String selectedName) {
    if (ui_mainpanel3 == NULL) {
        Serial.println("❌ ui_mainpanel3 is NULL!");
        return;
    }
    
    lv_obj_clean(ui_mainpanel3);
    
    selectedName.trim();
    if (selectedName == "No Employees" || selectedName.length() == 0) {
        lv_obj_t *msg = lv_label_create(ui_mainpanel3);
        lv_label_set_text(msg, "📋 No employees found");
        lv_obj_set_style_text_color(msg, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_center(msg);
        return;
    }
    
    Serial.printf("👤 Filtering for: %s\n", selectedName.c_str());
    
    // 🔥 تحميل البيانات مرة واحدة
    loadEmployeesFromSD();
    loadAttendanceFromSD();
    
    // =========================================================
    // تصفية السجلات حسب الاسم
    // =========================================================
    static AttendanceRecord filteredRecords[200];
    static int filteredCount = 0;
    filteredCount = 0;
    
    for (int i = 0; i < attendanceCount && filteredCount < 200; i++) {
        String name = attendanceList[i].name;
        name.trim();
        
        if (name == selectedName) {
            filteredRecords[filteredCount] = attendanceList[i];
            filteredCount++;
        }
    }
    
    Serial.printf("📦 Found %d records for %s\n", filteredCount, selectedName.c_str());
    
    if (filteredCount == 0) {
        lv_obj_t *msg = lv_label_create(ui_mainpanel3);
        lv_label_set_text(msg, "📋 No records for this employee");
        lv_obj_set_style_text_color(msg, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_center(msg);
        return;
    }
    
    // =========================================================
    // استخراج التواريخ الفريدة (static)
    // =========================================================
    static String uniqueDates[366];
    static int dateCount = 0;
    dateCount = 0;
    
    for (int i = 0; i < filteredCount; i++) {
        String date = filteredRecords[i].date;
        date.trim();
        date.replace("-", "/");
        
        bool exists = false;
        for (int j = 0; j < dateCount; j++) {
            if (uniqueDates[j] == date) {
                exists = true;
                break;
            }
        }
        
        if (!exists && dateCount < 366) {
            uniqueDates[dateCount] = date;
            dateCount++;
        }
    }
    
    // ترتيب التواريخ (الأحدث أولاً)
    for (int i = 0; i < dateCount - 1; i++) {
        for (int j = i + 1; j < dateCount; j++) {
            if (uniqueDates[j] > uniqueDates[i]) {
                String temp = uniqueDates[i];
                uniqueDates[i] = uniqueDates[j];
                uniqueDates[j] = temp;
            }
        }
    }
    
    // =========================================================
    // 🔥 تجهيز الـ Scroll (مرة واحدة)
    // =========================================================
    const int CONTENT_WIDTH = 430;
    
    lv_obj_add_flag(ui_mainpanel3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_mainpanel3, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_mainpanel3, LV_SCROLLBAR_MODE_AUTO);
    
    lv_obj_t *content = lv_obj_create(ui_mainpanel3);
    lv_obj_set_width(content, CONTENT_WIDTH);
    lv_obj_set_height(content, 100);
    lv_obj_set_pos(content, 5, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    
    int cardY = 8;
    
    // =========================================================
    // 🔥 عرض الأيام مع تحديث LVGL بين كل يوم
    // =========================================================
    for (int d = 0; d < dateCount; d++) {
        String date = uniqueDates[d];
        date.trim();
        
        // =====================================================
        // عنوان اليوم
        // =====================================================
        lv_obj_t *dateLabel = lv_label_create(content);
        lv_label_set_text(dateLabel, date.c_str());
        lv_obj_set_width(dateLabel, CONTENT_WIDTH - 20);
        lv_obj_set_height(dateLabel, 28);
        lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_pos(dateLabel, 5, cardY);
        cardY += 30;
        
        // =====================================================
        // HEADER (IN - OUT)
        // =====================================================
        lv_obj_t *header = lv_obj_create(content);
        lv_obj_set_size(header, CONTENT_WIDTH - 10, 24);
        lv_obj_set_pos(header, 5, cardY);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
        
        // Header IN
        lv_obj_t *headerIn = lv_label_create(header);
        lv_label_set_text(headerIn, "IN");
        lv_obj_set_width(headerIn, 100);
        lv_obj_set_style_text_color(headerIn, lv_color_hex(0x4CAF50), LV_PART_MAIN);
        lv_obj_set_style_text_font(headerIn, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_align(headerIn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(headerIn, LV_ALIGN_LEFT_MID, 50, 0);
        
        // Header OUT
        lv_obj_t *headerOut = lv_label_create(header);
        lv_label_set_text(headerOut, "OUT");
        lv_obj_set_width(headerOut, 100);
        lv_obj_set_style_text_color(headerOut, lv_color_hex(0xF44336), LV_PART_MAIN);
        lv_obj_set_style_text_font(headerOut, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_align(headerOut, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(headerOut, LV_ALIGN_LEFT_MID, 180, 0);
        
        cardY += 27;
        
        // =====================================================
        // عرض سجلات هذا اليوم للموظف المختار
        // =====================================================
        String inTime = "OFF";
        String outTime = "OFF";
        bool foundIN = false;
        bool foundOUT = false;
        
        for (int i = 0; i < filteredCount; i++) {
            if (filteredRecords[i].date == date) {
                if (filteredRecords[i].type == "IN") {
                    inTime = filteredRecords[i].time;
                    foundIN = true;
                } else if (filteredRecords[i].type == "OUT") {
                    outTime = filteredRecords[i].time;
                    foundOUT = true;
                }
            }
        }
        
        if (foundIN && !foundOUT) {
            outTime = "--:--";
        }
        
        // =====================================================
        // بطاقة اليوم
        // =====================================================
        lv_obj_t *item = lv_obj_create(content);
        lv_obj_set_size(item, CONTENT_WIDTH - 10, 50);
        lv_obj_set_pos(item, 5, cardY);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_style_radius(item, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x141922), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(item, lv_color_hex(0x2A303B), LV_PART_MAIN);
        lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN);
        
        // IN TIME
        lv_obj_t *inLabel = lv_label_create(item);
        lv_label_set_text(inLabel, inTime.c_str());
        lv_obj_set_width(inLabel, 100);
        lv_obj_set_style_text_color(inLabel, 
            (inTime != "OFF") ? lv_color_hex(0x4CAF50) : lv_color_hex(0x555555),
            LV_PART_MAIN);
        lv_obj_set_style_text_font(inLabel, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_align(inLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(inLabel, LV_ALIGN_LEFT_MID, 50, 0);
        
        // OUT TIME
        lv_obj_t *outLabel = lv_label_create(item);
        lv_label_set_text(outLabel, outTime.c_str());
        lv_obj_set_width(outLabel, 100);
        lv_obj_set_style_text_color(outLabel,
            (outTime != "OFF" && outTime != "--:--") ? lv_color_hex(0xF44336) : lv_color_hex(0x555555),
            LV_PART_MAIN);
        lv_obj_set_style_text_font(outLabel, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_align(outLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(outLabel, LV_ALIGN_LEFT_MID, 180, 0);
        
        cardY += 62;
        cardY += 12;
        
        // 🔥 مهم: تحديث LVGL كل 3 أيام لتجنب Watchdog
        if (d % 3 == 0) {
            lv_timer_handler();
            delay(1);
        }
    }
    
    lv_obj_set_height(content, cardY + 20);
    lv_obj_update_layout(content);
    lv_obj_update_layout(ui_mainpanel3);
    lv_obj_scroll_to_y(ui_mainpanel3, 0, LV_ANIM_OFF);
    
    Serial.printf("✅ Displayed %d days for %s\n", dateCount, selectedName.c_str());
}// ============================================================
// 🔥 حدث تغيير الـ Name Dropdown
// ============================================================
void namedropdown_event(lv_event_t *e) {
    if (uic_namedropdown == NULL) {
        Serial.println("❌ uic_namedropdown is NULL!");
        return;
    }
    
    char selectedName[32];
    lv_dropdown_get_selected_str(uic_namedropdown, selectedName, sizeof(selectedName));
    
    String nameStr = String(selectedName);
    nameStr.trim();
    
    Serial.printf("👤 Selected: %s\n", nameStr.c_str());
    
    // 🔥 عرض سجلات الموظف المختار
    if (nameStr != "No Employees" && nameStr.length() > 0) {
        filterAttendanceByName(nameStr);
    }
    
    // 🔥 تحديث LVGL فوراً
    lv_refr_now(NULL);
}
void populateDateDropdownFromRecords() {
    if (uic_datedropdown == NULL) {
        Serial.println("❌ uic_datedropdown is NULL!");
        return;
    }
    
    loadAttendanceFromSD();
    
    String uniqueDates[20];  // 🔥 قللنا المصفوفة
    int dateCount = 0;
    
    for (int i = 0; i < attendanceCount && i < 100; i++) {
        String date = attendanceList[i].date;
        date.trim();
        date.replace("-", "/");
        
        int firstSlash = date.indexOf('/');
        int secondSlash = date.indexOf('/', firstSlash + 1);
        
        if (firstSlash > 0 && secondSlash > 0) {
            String yearPart = date.substring(0, firstSlash);
            String monthPart = date.substring(firstSlash + 1, secondSlash);
            
            // محاولة YYYY/M/D
            if (yearPart.length() == 4) {
                if (monthPart.length() == 1) monthPart = "0" + monthPart;
                String monthYear = monthPart + "/" + yearPart;
                
                bool exists = false;
                for (int j = 0; j < dateCount; j++) {
                    if (uniqueDates[j] == monthYear) {
                        exists = true;
                        break;
                    }
                }
                
                if (!exists && dateCount < 20) {
                    uniqueDates[dateCount] = monthYear;
                    dateCount++;
                }
            } else {
                // محاولة D/M/YYYY
                String yearPart2 = date.substring(secondSlash + 1);
                if (yearPart2.length() == 4) {
                    if (monthPart.length() == 1) monthPart = "0" + monthPart;
                    String monthYear = monthPart + "/" + yearPart2;
                    
                    bool exists = false;
                    for (int j = 0; j < dateCount; j++) {
                        if (uniqueDates[j] == monthYear) {
                            exists = true;
                            break;
                        }
                    }
                    
                    if (!exists && dateCount < 20) {
                        uniqueDates[dateCount] = monthYear;
                        dateCount++;
                    }
                }
            }
        }
    }
    
    // ترتيب
    for (int i = 0; i < dateCount - 1; i++) {
        for (int j = i + 1; j < dateCount; j++) {
            int month1 = uniqueDates[i].substring(0, 2).toInt();
            int year1 = uniqueDates[i].substring(3, 7).toInt();
            int month2 = uniqueDates[j].substring(0, 2).toInt();
            int year2 = uniqueDates[j].substring(3, 7).toInt();
            
            if (year1 > year2 || (year1 == year2 && month1 > month2)) {
                String temp = uniqueDates[i];
                uniqueDates[i] = uniqueDates[j];
                uniqueDates[j] = temp;
            }
        }
    }
    
    String options = "";
    for (int i = 0; i < dateCount; i++) {
        options += uniqueDates[i];
        if (i < dateCount - 1) options += "\n";
    }
    
    if (dateCount == 0) {
        options = "No Records";
    }
    
    lv_dropdown_set_options(uic_datedropdown, options.c_str());
    lv_dropdown_set_selected(uic_datedropdown, 0);
    
    Serial.printf("✅ Date dropdown populated with %d months\n", dateCount);
}// ============================================================
// ============================================================
// ============================================================
// 🔥 تصفية سجلات الحضور (نسخة موفرة للذاكرة)
// ============================================================
// ============================================================
// 🔥 تصفية حسب الشهر مع عرض كل يوم منفصل
// ============================================================
void filterAttendanceByMonth(String selectedDate) {
    if (ui_mainpanel3 == NULL) {
        Serial.println("❌ ui_mainpanel3 is NULL!");
        return;
    }
    
    lv_obj_clean(ui_mainpanel3);
    
    selectedDate.trim();
    if (selectedDate == "No Records" || selectedDate.length() == 0) {
        lv_obj_t *msg = lv_label_create(ui_mainpanel3);
        lv_label_set_text(msg, "📋 No attendance records");
        lv_obj_set_style_text_color(msg, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_center(msg);
        return;
    }
    
    int selectedMonth = selectedDate.substring(0, 2).toInt();
    int selectedYear = selectedDate.substring(3, 7).toInt();
    
    Serial.printf("📅 Filtering for: %02d/%04d\n", selectedMonth, selectedYear);
    
    loadEmployeesFromSD();
    loadAttendanceFromSD();
    
    // =========================================================
    // تصفية السجلات حسب الشهر
    // =========================================================
    AttendanceRecord filteredRecords[200];
    int filteredCount = 0;
    
    for (int i = 0; i < attendanceCount; i++) {
        String date = attendanceList[i].date;
        date.trim();
        date.replace("-", "/");
        
        int recordMonth = 0, recordYear = 0;
        bool valid = false;
        
        int firstSlash = date.indexOf('/');
        int secondSlash = date.indexOf('/', firstSlash + 1);
        
        if (firstSlash > 0 && secondSlash > 0) {
            String yearPart = date.substring(0, firstSlash);
            if (yearPart.length() == 4) {
                recordYear = yearPart.toInt();
                recordMonth = date.substring(firstSlash + 1, secondSlash).toInt();
                valid = true;
            } else {
                String yearPart2 = date.substring(secondSlash + 1);
                if (yearPart2.length() == 4) {
                    recordYear = yearPart2.toInt();
                    recordMonth = date.substring(firstSlash + 1, secondSlash).toInt();
                    valid = true;
                }
            }
        }
        
        if (valid && recordMonth == selectedMonth && recordYear == selectedYear) {
            filteredRecords[filteredCount] = attendanceList[i];
            filteredCount++;
        }
    }
    
    Serial.printf("📦 Found %d records for %s\n", filteredCount, selectedDate.c_str());
    
    if (filteredCount == 0) {
        lv_obj_t *msg = lv_label_create(ui_mainpanel3);
        lv_label_set_text(msg, "📋 No records for this month");
        lv_obj_set_style_text_color(msg, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_center(msg);
        return;
    }
    
    // =========================================================
    // استخراج التواريخ الفريدة من السجلات المصفاة
    // =========================================================
    String uniqueDates[100];
    int dateCount = 0;
    
    for (int i = 0; i < filteredCount; i++) {
        String date = filteredRecords[i].date;
        date.trim();
        date.replace("-", "/");
        
        bool exists = false;
        for (int j = 0; j < dateCount; j++) {
            if (uniqueDates[j] == date) {
                exists = true;
                break;
            }
        }
        
        if (!exists && dateCount < 100) {
            uniqueDates[dateCount] = date;
            dateCount++;
        }
    }
    
    // ترتيب التواريخ
    for (int i = 0; i < dateCount - 1; i++) {
        for (int j = i + 1; j < dateCount; j++) {
            if (uniqueDates[j] > uniqueDates[i]) {
                String temp = uniqueDates[i];
                uniqueDates[i] = uniqueDates[j];
                uniqueDates[j] = temp;
            }
        }
    }
    
    // =========================================================
    // تجهيز الـ Scroll
    // =========================================================
    lv_obj_add_flag(ui_mainpanel3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_mainpanel3, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_mainpanel3, LV_SCROLLBAR_MODE_AUTO);
    
    lv_obj_t *content = lv_obj_create(ui_mainpanel3);
    lv_obj_set_width(content, 430);
    lv_obj_set_height(content, 100);
    lv_obj_set_pos(content, 5, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    
    int cardY = 8;
    
    // =========================================================
    // عرض كل يوم
    // =========================================================
    for (int d = 0; d < dateCount; d++) {
        String date = uniqueDates[d];
        date.trim();
        
        // =====================================================
        // عنوان اليوم
        // =====================================================
        lv_obj_t *dateLabel = lv_label_create(content);
        lv_label_set_text(dateLabel, date.c_str());
        lv_obj_set_width(dateLabel, 400);
        lv_obj_set_height(dateLabel, 28);
        lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_pos(dateLabel, 5, cardY);
        cardY += 30;
        
        // =====================================================
        // HEADER
        // =====================================================
        lv_obj_t *header = lv_obj_create(content);
        lv_obj_set_size(header, 420, 24);
        lv_obj_set_pos(header, 5, cardY);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
        
        // Header NAME
        lv_obj_t *headerName = lv_label_create(header);
        lv_label_set_text(headerName, "NAME");
        lv_obj_set_width(headerName, 130);
        lv_obj_set_style_text_color(headerName, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(headerName, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(headerName, LV_ALIGN_LEFT_MID, 10, 0);
        
        // Header IN
        lv_obj_t *headerIn = lv_label_create(header);
        lv_label_set_text(headerIn, "IN");
        lv_obj_set_width(headerIn, 70);
        lv_obj_set_style_text_color(headerIn, lv_color_hex(0x4CAF50), LV_PART_MAIN);
        lv_obj_set_style_text_font(headerIn, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_align(headerIn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(headerIn, LV_ALIGN_LEFT_MID, 150, 0);
        
        // Header OUT
        lv_obj_t *headerOut = lv_label_create(header);
        lv_label_set_text(headerOut, "OUT");
        lv_obj_set_width(headerOut, 70);
        lv_obj_set_style_text_color(headerOut, lv_color_hex(0xF44336), LV_PART_MAIN);
        lv_obj_set_style_text_font(headerOut, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_align(headerOut, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(headerOut, LV_ALIGN_LEFT_MID, 235, 0);
        
        cardY += 27;
        
        // =====================================================
        // عرض موظفي هذا اليوم (من السجلات المصفاة)
        // =====================================================
        for (int e = 0; e < employeeCount; e++) {
            String employeeName = employeeList[e].name;
            employeeName.trim();
            if (employeeName.length() == 0) continue;
            
            // البحث عن سجلات هذا الموظف في هذا اليوم
            String inTime = "OFF";
            String outTime = "OFF";
            bool foundIN = false;
            bool foundOUT = false;
            
            for (int i = 0; i < filteredCount; i++) {
                if (filteredRecords[i].name == employeeName && 
                    filteredRecords[i].date == date) {
                    if (filteredRecords[i].type == "IN") {
                        inTime = filteredRecords[i].time;
                        foundIN = true;
                    } else if (filteredRecords[i].type == "OUT") {
                        outTime = filteredRecords[i].time;
                        foundOUT = true;
                    }
                }
            }
            
            if (foundIN && !foundOUT) {
                outTime = "--:--";
            }
            
            // =================================================
            // بطاقة الموظف
            // =================================================
            lv_obj_t *item = lv_obj_create(content);
            lv_obj_set_size(item, 420, 44);
            lv_obj_set_pos(item, 5, cardY);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_set_style_radius(item, 8, LV_PART_MAIN);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x141922), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(item, lv_color_hex(0x2A303B), LV_PART_MAIN);
            lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
            lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN);
            
            // NAME
            lv_obj_t *nameLabel = lv_label_create(item);
            lv_label_set_text(nameLabel, employeeName.c_str());
            lv_obj_set_width(nameLabel, 130);
            lv_obj_set_style_text_color(nameLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
            lv_obj_align(nameLabel, LV_ALIGN_LEFT_MID, 10, 0);
            
            // IN
            lv_obj_t *inLabel = lv_label_create(item);
            lv_label_set_text(inLabel, inTime.c_str());
            lv_obj_set_width(inLabel, 70);
            lv_obj_set_style_text_color(inLabel, 
                (inTime != "OFF") ? lv_color_hex(0x4CAF50) : lv_color_hex(0x555555),
                LV_PART_MAIN);
            lv_obj_set_style_text_font(inLabel, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_obj_set_style_text_align(inLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_align(inLabel, LV_ALIGN_LEFT_MID, 150, 0);
            
            // OUT
            lv_obj_t *outLabel = lv_label_create(item);
            lv_label_set_text(outLabel, outTime.c_str());
            lv_obj_set_width(outLabel, 70);
            lv_obj_set_style_text_color(outLabel,
                (outTime != "OFF" && outTime != "--:--") ? lv_color_hex(0xF44336) : lv_color_hex(0x555555),
                LV_PART_MAIN);
            lv_obj_set_style_text_font(outLabel, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_obj_set_style_text_align(outLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_align(outLabel, LV_ALIGN_LEFT_MID, 235, 0);
            
            cardY += 52;
        }
        
        cardY += 12;
    }
    
    lv_obj_set_height(content, cardY + 20);
    lv_obj_update_layout(content);
    lv_obj_update_layout(ui_mainpanel3);
    lv_obj_scroll_to_y(ui_mainpanel3, 0, LV_ANIM_OFF);
    
    Serial.printf("✅ Displayed %d days for %s\n", dateCount, selectedDate.c_str());
}// 🔥 حدث تغيير الـ Dropdown
// ============================================================
void datedropdown_event(lv_event_t *e) {
    if (uic_datedropdown == NULL) {
        Serial.println("❌ uic_datedropdown is NULL!");
        return;
    }
    
    char selectedDate[16];
    lv_dropdown_get_selected_str(uic_datedropdown, selectedDate, sizeof(selectedDate));
    
    String dateStr = String(selectedDate);
    dateStr.trim();
    
    Serial.printf("📅 Selected: %s\n", dateStr.c_str());
    
    // 🔥 عرض السجلات مع فلترة حسب الشهر
    if (dateStr != "No Records" && dateStr.length() > 0) {
        ShowAttendanceRecords(true, dateStr);
    } else {
        ShowAttendanceRecords(false, "");
    }
}// ============================================================
// 🔥 عرض شاشة السجلات مع الـ Dropdown
// ============================================================
void go_to_record_screen_event(lv_event_t *e) {
    Serial.println("📱 Going to Record screen...");
    
    if (uic_recordscreen != NULL) {
        lv_screen_load(uic_recordscreen);
        
        // تعبئة Dropdowns
        populateDateDropdownFromRecords();
        populateNameDropdown();
        
        // عرض كل السجلات (بدون فلترة)
        ShowAttendanceRecords(false, "");
        
        playBeepDirect(40);
    }
}

// ============================================================
// 🔥 دالة تغيير كلمة السر مع Popups
// ============================================================
void changePasswordEvent(lv_event_t *e) {
    Serial.println("🔑 Changing password...");
    
    if (uic_changpassbox == NULL) {
        Serial.println("❌ uic_changpassbox is NULL!");
        return;
    }
    
    // قراءة كلمة السر الجديدة
    const char *newPass = lv_textarea_get_text(uic_changpassbox);
    
    if (newPass == NULL || strlen(newPass) == 0) {
        Serial.println("❌ Password is empty!");
        
        // 🔥 إظهار uic_fingertemp7 (خطأ)
        if (uic_fingertemp7 != NULL) {
            lv_obj_clear_flag(uic_fingertemp7, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(uic_fingertemp7, "❌ Password cannot be empty!");
            lv_obj_set_style_text_color(uic_fingertemp7, lv_color_hex(0xFF0000), LV_PART_MAIN);
            lv_obj_move_foreground(uic_fingertemp7);
            
            // إخفاء بعد 2 ثانية
            lv_timer_t *hideTimer = lv_timer_create([](lv_timer_t *t) {
                if (uic_fingertemp7 != NULL) {
                    lv_obj_add_flag(uic_fingertemp7, LV_OBJ_FLAG_HIDDEN);
                }
                lv_timer_del(t);
            }, 2000, NULL);
            lv_timer_set_repeat_count(hideTimer, 1);
        }
        
        playBeepDirect(100);
        return;
    }
    
    // التحقق من أن كلمة السر 4 أرقام
    if (strlen(newPass) != 4) {
        Serial.println("❌ Password must be 4 digits!");
        
        // 🔥 إظهار uic_fingertemp7 (خطأ)
        if (uic_fingertemp7 != NULL) {
            lv_obj_clear_flag(uic_fingertemp7, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(uic_fingertemp7, "❌ Password must be 4 digits!");
            lv_obj_set_style_text_color(uic_fingertemp7, lv_color_hex(0xFF0000), LV_PART_MAIN);
            lv_obj_move_foreground(uic_fingertemp7);
            
            // إخفاء بعد 2 ثانية
            lv_timer_t *hideTimer = lv_timer_create([](lv_timer_t *t) {
                if (uic_fingertemp7 != NULL) {
                    lv_obj_add_flag(uic_fingertemp7, LV_OBJ_FLAG_HIDDEN);
                }
                lv_timer_del(t);
            }, 2000, NULL);
            lv_timer_set_repeat_count(hideTimer, 1);
        }
        
        playBeepDirect(100);
        return;
    }
    
    // 🔥 حفظ كلمة السر الجديدة
    correctPassword = String(newPass);
    
    // حفظ في Preferences
    prefs.begin("settings", false);
    prefs.putString("password", correctPassword);
    prefs.end();
    
    Serial.printf("✅ Password changed to: %s\n", correctPassword.c_str());
    
    // 🔥 إظهار uic_okpopup2 (نجاح)
    if (uic_okpopup2 != NULL) {
        lv_obj_clear_flag(uic_okpopup2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(uic_okpopup2);
        
        // إخفاء بعد 2 ثانية
        lv_timer_t *hideTimer = lv_timer_create([](lv_timer_t *t) {
            if (uic_okpopup2 != NULL) {
                lv_obj_add_flag(uic_okpopup2, LV_OBJ_FLAG_HIDDEN);
            }
            lv_timer_del(t);
        }, 2000, NULL);
        lv_timer_set_repeat_count(hideTimer, 1);
    }
    
    // 🔥 مسح حقل الإدخال
    lv_textarea_set_text(uic_changpassbox, "");
    
    playBeepDirect(80);
}
// ========== Setup ==========
void setup()
{
    Serial.begin(115200);
    delay(1000);
    
    Serial.printf("PSRAM Found: %s\n", psramFound() ? "YES" : "NO");
Serial.printf("PSRAM Size : %u\n", ESP.getPsramSize());
Serial.printf("PSRAM Free : %u\n", ESP.getFreePsram());

    Serial.println("\n\n=== Starting Setup ===");
   
    if (!gfx->begin())
    {
        Serial.println("gfx->begin failed");
        while (1);
    }

    ledcAttach(GFX_BL, 5000, 8);
    ledcWrite(GFX_BL, 120);

    gfx->fillScreen(RGB565_BLACK);

    touchController.begin();
    touchController.setRotation(ROTATION_INVERTED);

    lv_init();
    lv_tick_set_cb(millis_cb);

    screenWidth = gfx->width();
    screenHeight = gfx->height();
    bufSize = screenWidth * 35;  // 🔥 خففنا من 40 إلى 35

    disp_draw_buf = (lv_color_t *)
    heap_caps_malloc(bufSize * 2,
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!disp_draw_buf)
    {
        disp_draw_buf = (lv_color_t *)
            heap_caps_malloc(bufSize * 2, MALLOC_CAP_8BIT);
    }

    if (!disp_draw_buf)
    {
        Serial.println("Buffer alloc failed");
        while (1);
    }

    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);

    ui_init();
    rtcWire.begin(RTC_SDA, RTC_SCL);
    rtcWire.setClock(100000);
    
   if (!rtc.begin(&rtcWire)) {
        Serial.println("⚠️ RTC not found!");
    } else {
        Serial.println("✅ RTC initialized");
        if (rtc.lostPower()) {
            Serial.println("⚡ RTC lost power, setting time!");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
    }  
    initFingerprint();
//deleteAllFingerprints();

    Serial.println("Initializing SD...");
Serial.printf("SCK=%d MISO=%d MOSI=%d CS=%d\n",
              SD_SCK, SD_MISO, SD_MOSI, SD_CS);

spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

if (!SD.begin(SD_CS, spiSD, 10000000))
{
    Serial.println("❌ SD Failed");
}
else
{
    Serial.println("✅ SD Ready");
}  
//deleteAllFiles(SD, "/");

  //deleteAllFiles(SD, "/");
 // ===== إعداد الـ Rollers =====
    lv_roller_set_options(uic_timehour, 
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12", 
        LV_ROLLER_MODE_INFINITE);
    
    String minOptions = "";
    for (int i = 0; i < 60; i++) {
        if (i < 10) minOptions += "0";
        minOptions += String(i);
        if (i < 59) minOptions += "\n";
    }
    lv_roller_set_options(uic_timemin, minOptions.c_str(), LV_ROLLER_MODE_INFINITE);
    
    lv_roller_set_options(uic_ampm, "AM\nPM", LV_ROLLER_MODE_INFINITE);
    
    String dayOptions = "";
    for (int i = 1; i <= 31; i++) {
        if (i < 10) dayOptions += "0";
        dayOptions += String(i);
        if (i < 31) dayOptions += "\n";
    }
    lv_roller_set_options(uic_day, dayOptions.c_str(), LV_ROLLER_MODE_INFINITE);
    
    String monthOptions = "";
    for (int i = 1; i <= 12; i++) {
        if (i < 10) monthOptions += "0";
        monthOptions += String(i);
        if (i < 12) monthOptions += "\n";
    }
    lv_roller_set_options(uic_month, monthOptions.c_str(), LV_ROLLER_MODE_INFINITE);
    
    String yearOptions = "";
    for (int i = 2026; i <= 2060; i++) {
        yearOptions += String(i);
        if (i < 2060) yearOptions += "\n";
    }
    lv_roller_set_options(uic_years, yearOptions.c_str(), LV_ROLLER_MODE_INFINITE);
    
    displayTime();
    
    lv_obj_add_event_cb(uic_savetime, savetime_event, LV_EVENT_CLICKED, NULL);

    lv_i18n_init(lv_i18n_language_pack);
    loadSavedLanguage();
    loadSoundSettings();
    
    AddBeepToAllButtons(uic_main);
    AddBeepToAllButtons(uic_settings);
    AddBeepToAllButtons(uic_wifiset);
    AddBeepToAllButtons(uic_connectwifi);
    AddBeepToAllButtons(uic_soundset);
    AddBeepToAllButtons(uic_langset);
    AddBeepToAllButtons(uic_date);
    AddBeepToAllButtons(uic_menu);
    AddBeepToAllButtons(uic_employee);
    AddBeepToAllButtons(uic_addemployee);
    AddBeepToAllButtons(uic_editemployee);
    AddBeepToAllButtons(uic_recordscreen);
     AddBeepToAllButtons(uic_changepassscreen);




   

   
    
    setupBuzzer();
    
    lv_obj_add_flag(uic_passpanel, LV_OBJ_FLAG_HIDDEN);
    
    // ===== أزرار التنقل =====
    lv_obj_add_event_cb(uic_btnmenu, btnmenu_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(uic_backmain, backmain_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(uic_changetoempo, go_to_employee_event, LV_EVENT_CLICKED, NULL);
    
    // 🔥 أضف حدث زر العودة إلى المينو مع التحقق
    
        
    // 🔥 أحداث popup الحذف
    if (uic_deletbtn2no != NULL) {
        lv_obj_add_event_cb(uic_deletbtn2no, cancelDeleteEvent, LV_EVENT_CLICKED, NULL);
    }
    if (uic_deletbtn2ok != NULL) {
        lv_obj_add_event_cb(uic_deletbtn2ok, confirmDeleteEvent, LV_EVENT_CLICKED, NULL);
    }
    
    // 🔥 حدث حفظ التعديل
    if (uic_okedit != NULL) {
        lv_obj_add_event_cb(uic_okedit, saveEditEmployeeEvent, LV_EVENT_CLICKED, NULL);
    }
    

if (uic_okpopup10 != NULL) {
    lv_obj_add_event_cb(uic_okpopup10, hideOkPopupEvent, LV_EVENT_CLICKED, NULL);
}

    if (uic_changetorecord != NULL) {
    lv_obj_add_event_cb(uic_changetorecord, go_to_record_screen_event, LV_EVENT_CLICKED, NULL);
}
    
    // في setup() بعد ui_init()
if (uic_datedropdown != NULL) {
    lv_obj_add_event_cb(uic_datedropdown, datedropdown_event, LV_EVENT_VALUE_CHANGED, NULL);
}

if (uic_namedropdown != NULL) {
    lv_obj_add_event_cb(uic_namedropdown, namedropdown_event, LV_EVENT_VALUE_CHANGED, NULL);
}


// 🔥 إخفاء Popups في البداية
if (uic_okpopup2 != NULL) {
    lv_obj_add_flag(uic_okpopup2, LV_OBJ_FLAG_HIDDEN);
}
if (uic_fingertemp7 != NULL) {
    lv_obj_add_flag(uic_fingertemp7, LV_OBJ_FLAG_HIDDEN);
}

// 🔥 حدث تغيير كلمة السر
if (uic_changepassbtn != NULL) {
    lv_obj_add_event_cb(uic_changepassbtn, changePasswordEvent, LV_EVENT_CLICKED, NULL);
}

// 🔥 حدث إخفاء okpopup2 عند الضغط عليه (إغلاق يدوي)
if (uic_okpopup2 != NULL) {
    lv_obj_add_event_cb(uic_okpopup2, [](lv_event_t *e) {
        if (uic_okpopup2 != NULL) {
            lv_obj_add_flag(uic_okpopup2, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_CLICKED, NULL);
}

// 🔥 حدث إخفاء fingertemp7 عند الضغط عليه (إغلاق يدوي)
if (uic_fingertemp7 != NULL) {
    lv_obj_add_event_cb(uic_fingertemp7, [](lv_event_t *e) {
        if (uic_fingertemp7 != NULL) {
            lv_obj_add_flag(uic_fingertemp7, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_CLICKED, NULL);
}
        
    
    lv_obj_add_event_cb(uic_keypasstxt, keypasstxt_event, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_keyboard_set_textarea(uic_keypass, uic_keypasstxt);
    
    lv_group_t *group = lv_group_create();
    lv_group_add_obj(group, uic_keypass);
    lv_indev_set_group(lv_indev_get_next(NULL), group);
    lv_group_set_default(group);
    
    lv_obj_add_event_cb(uic_mutetouch, mute_touch_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(uic_mute, mute_events_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(uic_soundslider, sound_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(uic_droplang, droplang_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(uic_savelang, savelang_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_add_flag(uic_correct, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uic_wrong, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uic_loadingoverlay2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uic_loading2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uic_loadingoverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uic_loading, LV_OBJ_FLAG_HIDDEN);
    

    lv_obj_add_event_cb(uic_gotoadd, gotoadd_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(uic_addemployeeaction, addemployeeaction_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(uic_backtomenu, backtomain_event, LV_EVENT_CLICKED, NULL);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    
    prefs.begin("wifi", true);
    String saved_ssid = prefs.getString("ssid", "");
    String saved_pass = prefs.getString("pass", "");
    prefs.end();
    
    if (saved_ssid.length() > 0)
    {
        Serial.println("Attempting to connect to saved network: " + saved_ssid);
        WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30)
        {
            delay(200);
            attempts++;
            Serial.print(".");
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("Connected to saved network!");
            UpdateCurrentNetwork();
        }
        else
        {
            Serial.println("Failed to connect to saved network");
            WiFi.setAutoReconnect(false);
        }
    }


    // ========== تحميل كلمة السر المحفوظة ==========
prefs.begin("settings", true);
String savedPassword = prefs.getString("password", "0000");
prefs.end();

if (savedPassword.length() == 4) {
    correctPassword = savedPassword;
    Serial.printf("🔑 Loaded password: %s\n", correctPassword.c_str());
} else {
    correctPassword = "0000";
    Serial.println("🔑 Using default password: 0000");
}
    
    lv_obj_add_event_cb(uic_searchwifi, SearchWifiEvent, LV_EVENT_CLICKED, NULL);
    lv_obj_set_layout(uic_wificontainer, LV_LAYOUT_NONE);
    lv_obj_add_event_cb(uic_connectbtn, connect_btn_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(uic_changetowifiset, changetowifi_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(uic_backtosettings, backtosettings_event, LV_EVENT_CLICKED, NULL);

    Serial.println("Setup complete!");
    
    playBeepDirect(80);
    delay(100);
    playBeepDirect(80);
    delay(100);
    playBeepDirect(80);
}

// ========== Loop ==========
void loop()
{
    static unsigned long lastTimeUpdate = 0;
    static unsigned long lastMemoryPrint = 0;
    static unsigned long lastCheck = 0;
    static unsigned long lastFingerprintCheck = 0;

    updateBeep();

    // 🔥 معالجة تسجيل البصمة (إذا كان في وضع التسجيل)
    processFingerprintEnrollment();

    // 🔥🔥🔥 فحص البصمة كل 300ms (على الشاشة الرئيسية فقط)
 if (millis() - lastFingerprintCheck > 500) {
        lastFingerprintCheck = millis();
        
        lv_obj_t *current_screen = lv_display_get_screen_active(disp);
        if (current_screen == uic_main && !isEnrolling) {
            processAttendance();
        }
    }

    // تحديث الوقت
    if (millis() - lastTimeUpdate > 10000)
    {
        lastTimeUpdate = millis();
        displayTime();
    }

    // فحص الذاكرة كل 5 ثوانٍ
    if (millis() - lastMemoryPrint > 5000)
    {
        lastMemoryPrint = millis();

        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);

        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();

        Serial.println("\n========================================");
        Serial.println("📊 MEMORY STATUS");
        Serial.println("========================================");

        Serial.printf("LVGL Used        : %u%%\n", mon.used_pct);
        Serial.printf("LVGL Fragment    : %u%%\n", mon.frag_pct);
        Serial.printf("LVGL Free Size   : %u bytes\n", (uint32_t)mon.free_size);
        Serial.printf("LVGL BiggestBlk  : %u bytes\n", (uint32_t)mon.free_biggest_size);

        Serial.printf("ESP Heap         : %u / %u bytes (%.1f%% free)\n",
                      freeHeap,
                      totalHeap,
                      freeHeap * 100.0f / totalHeap);

        Serial.printf("ESP PSRAM        : %u bytes\n", ESP.getFreePsram());

        Serial.println("========================================");

        if (mon.used_pct > 85)
            Serial.println("⚠️ LVGL memory is high!");

        if (freeHeap < 30000)
            Serial.println("⚠️ ESP Heap is low!");
    }

    // تحديث حالة الواي فاي
    if (millis() - lastCheck > 5000)
    {
        lastCheck = millis();
        UpdateCurrentNetwork();
        UpdateWifiStatus();
    }

    // إعادة الاتصال بالواي فاي
    if (!wifiSettingsOpen &&
        WiFi.status() != WL_CONNECTED &&
        millis() - lastReconnect > 5000)
    {
        lastReconnect = millis();

        prefs.begin("wifi", true);
        String ssid = prefs.getString("ssid", "");
        String pass = prefs.getString("pass", "");
        prefs.end();

        if (ssid.length() > 0)
        {
            Serial.println("Retrying WiFi...");
            WiFi.begin(ssid.c_str(), pass.c_str());
        }
    }
    lv_timer_handler();
    delay(5);
}
