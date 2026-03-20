#include "browser_app.h"
#include "ui/ui_engine.h"
#include "ui/notification_popup.h"
#include "services/wifi_manager.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <string>
static const char* TAG="Browser";
static std::string s_buf;
static esp_err_t httpCb(esp_http_client_event_t* e){
    if(e->event_id==HTTP_EVENT_ON_DATA&&e->data_len>0){
        s_buf.append((char*)e->data,e->data_len);
        if(s_buf.size()>4096) s_buf.resize(4096);
    } return ESP_OK;
}
void BrowserApp::onCreate(){
    buildScreen("Browser");
    lv_obj_t* urlBar=lv_obj_create(m_screen);
    lv_obj_set_size(urlBar, 320, 22); lv_obj_set_pos(urlBar,0,44);
    lv_obj_set_style_bg_color(urlBar,lv_color_hex(0x131929),0);
    lv_obj_set_style_border_width(urlBar,0,0);
    lv_obj_clear_flag(urlBar,LV_OBJ_FLAG_CLICKABLE);
    m_urlLbl=lv_label_create(urlBar);
    lv_label_set_text(m_urlLbl,"Select a bookmark");
    lv_obj_set_style_text_color(m_urlLbl,lv_color_hex(0x8899BB),0);
    lv_obj_set_style_text_font(m_urlLbl,&lv_font_montserrat_12,0);
    lv_obj_align(m_urlLbl,LV_ALIGN_LEFT_MID,4,0);
    m_content=lv_obj_create(m_screen);
    lv_obj_set_size(m_content, 320, 252); lv_obj_set_pos(m_content,0,66);
    lv_obj_set_style_bg_color(m_content,lv_color_hex(0x0A0E1A),0);
    lv_obj_set_style_border_width(m_content,0,0);
    lv_obj_set_style_pad_all(m_content,4,0);
    lv_obj_set_scroll_dir(m_content,LV_DIR_VER);
    lv_obj_set_flex_flow(m_content,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(m_content,3,0);
    lv_obj_t* sbar=lv_obj_create(m_screen);
    lv_obj_set_size(sbar, 320, 14); lv_obj_set_pos(sbar,0,318);
    lv_obj_set_style_bg_color(sbar,lv_color_hex(0x090D17),0);
    lv_obj_set_style_border_width(sbar,0,0);
    lv_obj_clear_flag(sbar,LV_OBJ_FLAG_CLICKABLE);
    m_statusLbl=lv_label_create(sbar);
    lv_label_set_text(m_statusLbl,"A:Open  B:Back  UP/DN:Scroll");
    lv_obj_set_style_text_color(m_statusLbl,lv_color_hex(0x4A5568),0);
    lv_obj_set_style_text_font(m_statusLbl,&lv_font_montserrat_12,0);
    showBookmarks();
}
void BrowserApp::showBookmarks(){
    lv_obj_clean(m_content); m_showingBookmarks=true;
    for(int i=0;i<5;i++){
        lv_obj_t* btn=lv_obj_create(m_content);
        lv_obj_set_width(btn,228); lv_obj_set_height(btn,34);
        lv_obj_set_style_bg_color(btn,lv_color_hex(0x131929),0);
        lv_obj_set_style_bg_color(btn,lv_color_hex(0x7B2FFF),LV_STATE_FOCUSED);
        lv_obj_set_style_radius(btn,6,0);
        lv_obj_set_style_border_color(btn,lv_color_hex(0x1E2D4A),0);
        lv_obj_set_style_border_width(btn,1,0);
        lv_obj_add_flag(btn,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* nl=lv_label_create(btn);
        lv_label_set_text(nl,NAMES[i]);
        lv_obj_set_style_text_color(nl,lv_color_hex(0xEEF2FF),0);
        lv_obj_set_style_text_font(nl,&lv_font_montserrat_14,0);
        lv_obj_align(nl,LV_ALIGN_LEFT_MID,4,-6);
        lv_obj_t* ul=lv_label_create(btn);
        lv_label_set_text(ul,URLS[i]);
        lv_obj_set_style_text_color(ul,lv_color_hex(0x8899BB),0);
        lv_obj_set_style_text_font(ul,&lv_font_montserrat_12,0);
        lv_label_set_long_mode(ul,LV_LABEL_LONG_CLIP);
        lv_obj_set_width(ul,220); lv_obj_align(ul,LV_ALIGN_LEFT_MID,4,6);
        lv_obj_set_user_data(btn,(void*)URLS[i]);
        lv_obj_add_event_cb(btn,[](lv_event_t* e){
            const char* url=(const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            BrowserApp* self=(BrowserApp*)AppManager::getInstance().getCurrentApp();
            if(self) self->navigate(url);
        },LV_EVENT_CLICKED,nullptr);
    }
}
void BrowserApp::navigate(const char* url){
    if(m_loading) return;
    if(!WiFiManager::getInstance().isConnected()){
        NotificationPopup::getInstance().show("Browser","No WiFi",ION_NOTIF_ERROR,2000); return;
    }
    m_loading=true; lv_label_set_text(m_urlLbl,url);
    lv_obj_clean(m_content);
    lv_obj_t* sp=lv_spinner_create(m_content,1000,60);
    lv_obj_set_size(sp,40,40); lv_obj_center(sp);
    lv_obj_set_style_arc_color(sp,lv_color_hex(0x7B2FFF),LV_PART_INDICATOR);
    FetchArgs* a=new FetchArgs(); a->app=this; strncpy(a->url,url,255);
    xTaskCreate(fetchTask,"fetch",8192,a,4,nullptr);
}
void BrowserApp::fetchTask(void* arg){
    FetchArgs* a=(FetchArgs*)arg; s_buf.clear();
    esp_http_client_config_t cfg={}; cfg.url=a->url;
    cfg.event_handler=httpCb; cfg.timeout_ms=8000;
    auto* c=esp_http_client_init(&cfg);
    esp_err_t r=esp_http_client_perform(c);
    int st=esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if(UIEngine::getInstance().lock(200)){
        if(r==ESP_OK&&!s_buf.empty()) a->app->renderContent(s_buf.c_str());
        else { lv_obj_clean(a->app->m_content);
               lv_obj_t* l=lv_label_create(a->app->m_content);
               lv_label_set_text(l,LV_SYMBOL_WARNING " Page load failed");
               lv_obj_set_style_text_color(l,lv_color_hex(0xFF3366),0); }
        a->app->m_loading=false; UIEngine::getInstance().unlock();
    }
    delete a; vTaskDelete(nullptr);
}
void BrowserApp::renderContent(const char* raw){
    lv_obj_clean(m_content);
    std::string text; bool inTag=false;
    for(const char* p=raw;*p;p++){
        if(*p=='<'){inTag=true;continue;}
        if(*p=='>'){inTag=false;continue;}
        if(!inTag) text+=*p;
    }
    lv_obj_t* l=lv_label_create(m_content);
    lv_label_set_text(l,text.empty()?"(empty)":text.c_str());
    lv_obj_set_style_text_color(l,lv_color_hex(0xEEF2FF),0);
    lv_obj_set_style_text_font(l,&lv_font_montserrat_12,0);
    lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l,228);
}
void BrowserApp::onKey(ion_key_t k, bool pressed){
    if(!pressed) return;
    if(k==ION_KEY_B){ if(!m_showingBookmarks){showBookmarks();lv_label_set_text(m_urlLbl,"Select a bookmark");}
                      else AppManager::getInstance().closeCurrentApp(); }
    if(k==ION_KEY_UP)   lv_obj_scroll_by(m_content,0,-40,LV_ANIM_ON);
    if(k==ION_KEY_DOWN) lv_obj_scroll_by(m_content,0, 40,LV_ANIM_ON);
}
void BrowserApp::onDestroy(){ if(m_screen){lv_obj_del(m_screen);m_screen=nullptr;} }