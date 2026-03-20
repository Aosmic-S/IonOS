#include "ws2812_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char* TAG = "WS2812";
WS2812Driver& WS2812Driver::getInstance(){ static WS2812Driver i; return i; }

esp_err_t WS2812Driver::init() {
    led_strip_config_t cfg = {};
    cfg.strip_gpio_num     = PIN_LED_DATA;
    cfg.max_leds           = LED_COUNT;
    cfg.led_pixel_format   = LED_PIXEL_FORMAT_GRB;
    cfg.led_model          = LED_MODEL_WS2812;
    led_strip_rmt_config_t rmt = {};
    rmt.resolution_hz      = LED_RMT_CLK_HZ;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt, &m_strip));
    clear(); show();
    ESP_LOGI(TAG, "%d LEDs @ GPIO%d", LED_COUNT, PIN_LED_DATA);
    return ESP_OK;
}

void WS2812Driver::setPixel(int i, RGBColor c) {
    if (i<0||i>=LED_COUNT) return;
    float s = m_brightness/255.0f;
    led_strip_set_pixel(m_strip, i, (uint8_t)(c.r*s),(uint8_t)(c.g*s),(uint8_t)(c.b*s));
}
void WS2812Driver::setAll(RGBColor c) { for(int i=0;i<LED_COUNT;i++) setPixel(i,c); }
void WS2812Driver::clear() { led_strip_clear(m_strip); }
void WS2812Driver::show()  { led_strip_refresh(m_strip); }
void WS2812Driver::setAnimation(LEDAnim a) { m_anim = a; m_animStep = 0; }

RGBColor WS2812Driver::hsv(float h, float s, float v) {
    h = fmodf(h,360.f); if(h<0)h+=360.f;
    float c=v*s, x=c*(1-fabsf(fmodf(h/60,2)-1)), m=v-c;
    float r,g,b;
    if(h<60){r=c;g=x;b=0;}else if(h<120){r=x;g=c;b=0;}
    else if(h<180){r=0;g=c;b=x;}else if(h<240){r=0;g=x;b=c;}
    else if(h<300){r=x;g=0;b=c;}else{r=c;g=0;b=x;}
    return {(uint8_t)((r+m)*255),(uint8_t)((g+m)*255),(uint8_t)((b+m)*255)};
}

void WS2812Driver::bootSweep() {
    for(int i=0;i<LED_COUNT;i++){
        setPixel(i,RGBColor::CYAN()); show();
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    clear(); show();
}

void WS2812Driver::notificationFlash(RGBColor c, int times) {
    for(int t=0;t<times;t++){
        setAll(c); show(); vTaskDelay(pdMS_TO_TICKS(80));
        clear();   show(); vTaskDelay(pdMS_TO_TICKS(80));
    }
}

void WS2812Driver::animTask() {
    while(true) {
        switch(m_anim) {
            case LEDAnim::RAINBOW:
                for(int i=0;i<LED_COUNT;i++)
                    setPixel(i, hsv((m_animStep*5 + i*360/LED_COUNT) % 360, 1.0f, 0.6f));
                show(); m_animStep++;
                vTaskDelay(pdMS_TO_TICKS(30));
                break;
            case LEDAnim::PULSE: {
                float v = 0.3f + 0.5f * (sinf(m_animStep * 0.08f) * 0.5f + 0.5f);
                for(int i=0;i<LED_COUNT;i++) setPixel(i, hsv(195,1.0f,v));
                show(); m_animStep++;
                vTaskDelay(pdMS_TO_TICKS(20));
                break;
            }
            case LEDAnim::WIFI_BLINK:
                setAll((m_animStep%20)<10 ? RGBColor{0,80,120} : RGBColor::BLACK());
                show(); m_animStep++;
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            case LEDAnim::BATT_LOW:
                setAll((m_animStep%10)<5 ? RGBColor{255,50,0} : RGBColor::BLACK());
                show(); m_animStep++;
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case LEDAnim::CHARGING: {
                int lit = (m_animStep/3) % (LED_COUNT+1);
                for(int i=0;i<LED_COUNT;i++)
                    setPixel(i, i<lit ? RGBColor{0,220,80} : RGBColor::BLACK());
                show(); m_animStep++;
                vTaskDelay(pdMS_TO_TICKS(120));
                break;
            }
            case LEDAnim::MUSIC_BEAT: {
                float v = 0.1f + 0.8f*(sinf(m_animStep*0.2f)*0.5f+0.5f);
                for(int i=0;i<LED_COUNT;i++) setPixel(i, hsv(280,1.0f,v));
                show(); m_animStep++;
                vTaskDelay(pdMS_TO_TICKS(16));
                break;
            }
            default:
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
        }
    }
}
