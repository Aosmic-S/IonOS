#include "music_player_app.h"
#include "ui/ui_engine.h"
#include "services/audio_manager.h"
#include "drivers/storage/sd_driver.h"
#include "drivers/rgb/ws2812_driver.h"
#include "esp_log.h"
#include <string.h>
static const char* TAG = "MusicApp";
void MusicPlayerApp::onCreate() {
    buildScreen("Music Player");
    // Album art circle
    lv_obj_t* art = lv_obj_create(m_screen);
    lv_obj_set_size(art,80,80); lv_obj_align(art,LV_ALIGN_TOP_MID,0,52);
    lv_obj_set_style_radius(art,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(art,lv_color_hex(0x1A2236),0);
    lv_obj_set_style_border_color(art,lv_color_hex(0x00D4FF),0);
    lv_obj_set_style_border_width(art,2,0);
    lv_obj_t* note=lv_label_create(art);
    lv_label_set_text(note,LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(note,lv_color_hex(0x00D4FF),0);
    lv_obj_set_style_text_font(note,&lv_font_montserrat_24,0);
    lv_obj_center(note);
    // Track name
    m_trackLbl = lv_label_create(m_screen);
    lv_label_set_text(m_trackLbl,"No tracks found");
    lv_obj_set_style_text_color(m_trackLbl,lv_color_hex(0xEEF2FF),0);
    lv_obj_set_style_text_font(m_trackLbl,&lv_font_montserrat_14,0);
    lv_label_set_long_mode(m_trackLbl,LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(m_trackLbl,220); lv_obj_align(m_trackLbl,LV_ALIGN_TOP_MID,0,140);
    // Progress bar
    m_progressBar = lv_bar_create(m_screen);
    lv_obj_set_size(m_progressBar,200,6); lv_obj_align(m_progressBar,LV_ALIGN_TOP_MID,0,162);
    lv_obj_set_style_bg_color(m_progressBar,lv_color_hex(0x1E2D4A),0);
    lv_obj_set_style_bg_color(m_progressBar,lv_color_hex(0x00D4FF),LV_PART_INDICATOR);
    lv_bar_set_range(m_progressBar,0,100);
    // Controls
    struct { const char* ico; ion_key_t k; } btns[] = {
        {LV_SYMBOL_PREV,ION_KEY_LEFT},{LV_SYMBOL_PLAY,ION_KEY_X},{LV_SYMBOL_NEXT,ION_KEY_RIGHT}
    };
    for(int i=0;i<3;i++){
        lv_obj_t* b=lv_btn_create(m_screen);
        lv_obj_set_size(b,48,40); lv_obj_set_pos(b,60+i*52,176);
        lv_obj_set_style_bg_color(b,lv_color_hex(i==1?0x00D4FF:0x131929),0);
        lv_obj_set_style_radius(b,8,0);
        lv_obj_t* l=lv_label_create(b); lv_label_set_text(l,btns[i].ico);
        lv_obj_set_style_text_font(l,&lv_font_montserrat_16,0);
        lv_obj_center(l);
        if(i==1) m_playBtn=b;
        lv_obj_set_user_data(b,(void*)(intptr_t)btns[i].k);
        lv_obj_add_event_cb(b,[](lv_event_t*e){
            MusicPlayerApp* self=(MusicPlayerApp*)AppManager::getInstance().getCurrentApp();
            if(!self) return;
            ion_key_t k=(ion_key_t)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            self->onKey(k,true);
        },LV_EVENT_CLICKED,nullptr);
    }
    // Playlist
    m_list = lv_list_create(m_screen);
    lv_obj_set_size(m_list, 320, 80); lv_obj_set_pos(m_list,0,224);
    lv_obj_set_style_bg_color(m_list,lv_color_hex(0x0A0E1A),0);
    lv_obj_set_style_border_width(m_list,0,0);
    scanMusic();
    // 1s update timer
    m_timer=lv_timer_create([](lv_timer_t* t){((MusicPlayerApp*)t->user_data)->updateUI();},1000,this);
}
void MusicPlayerApp::scanMusic() {
    m_tracks.clear(); lv_obj_clean(m_list);
    std::vector<FileEntry> files;
    SDDriver::getInstance().listDir("/sdcard/music", files);
    for(auto& f:files){
        if(f.isDir) continue;
        std::string ext=f.name.size()>4?f.name.substr(f.name.size()-4):"";
        for(char& c:ext) c=tolower(c);
        if(ext==".wav"||ext==".mp3"||ext=="flac"){
            m_tracks.push_back("/sdcard/music/"+f.name);
            lv_obj_t* btn=lv_list_add_btn(m_list,LV_SYMBOL_AUDIO,f.name.c_str());
            lv_obj_set_style_bg_color(btn,lv_color_hex(0x0A0E1A),0);
            lv_obj_set_style_bg_color(btn,lv_color_hex(0x00D4FF),LV_STATE_FOCUSED);
            lv_obj_set_style_text_font(btn,&lv_font_montserrat_12,0);
            int idx=m_tracks.size()-1;
            lv_obj_set_user_data(btn,(void*)(intptr_t)idx);
            lv_obj_add_event_cb(btn,[](lv_event_t*e){
                int i=(int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                ((MusicPlayerApp*)AppManager::getInstance().getCurrentApp())->play(i);
            },LV_EVENT_CLICKED,nullptr);
        }
    }
    if(!m_tracks.empty()) play(0);
    else lv_label_set_text(m_trackLbl,"No music on SD card");
}
void MusicPlayerApp::play(int idx) {
    if(idx<0||idx>=(int)m_tracks.size()) return;
    m_idx=idx; m_playing=true;
    const char* path=m_tracks[idx].c_str();
    const char* slash=strrchr(path,'/');
    lv_label_set_text(m_trackLbl,slash?slash+1:path);
    AudioManager::getInstance().play(path,[this](){
        if(UIEngine::getInstance().lock(100)){
            m_idx=(m_idx+1)%m_tracks.size(); play(m_idx);
            UIEngine::getInstance().unlock();
        }
    });
    WS2812Driver::getInstance().setAnimation(LEDAnim::MUSIC_BEAT);
    lv_obj_t* l=lv_obj_get_child(m_playBtn,0);
    if(l) lv_label_set_text(l,LV_SYMBOL_PAUSE);
}
void MusicPlayerApp::updateUI() {
    // Progress bar animation
    static int fake=0; fake=(fake+2)%100;
    lv_bar_set_value(m_progressBar,fake,LV_ANIM_ON);
}
void MusicPlayerApp::onKey(ion_key_t k, bool pressed) {
    if(!pressed) return;
    switch(k){
        case ION_KEY_X:
            if(m_playing){ AudioManager::getInstance().pause(); m_playing=false;
                lv_obj_t* l=lv_obj_get_child(m_playBtn,0);
                if(l) lv_label_set_text(l,LV_SYMBOL_PLAY);
                WS2812Driver::getInstance().setAnimation(LEDAnim::NONE);
            } else { AudioManager::getInstance().resume(); m_playing=true;
                lv_obj_t* l=lv_obj_get_child(m_playBtn,0);
                if(l) lv_label_set_text(l,LV_SYMBOL_PAUSE);
                WS2812Driver::getInstance().setAnimation(LEDAnim::MUSIC_BEAT);
            } break;
        case ION_KEY_RIGHT: play((m_idx+1)%m_tracks.size()); break;
        case ION_KEY_LEFT:  play((m_idx-1+m_tracks.size())%m_tracks.size()); break;
        case ION_KEY_B: AppManager::getInstance().closeCurrentApp(); break;
        default: break;
    }
}
void MusicPlayerApp::onDestroy() {
    AudioManager::getInstance().stop();
    WS2812Driver::getInstance().setAnimation(LEDAnim::NONE);
    if(m_timer){lv_timer_del(m_timer);m_timer=nullptr;}
    if(m_screen){lv_obj_del(m_screen);m_screen=nullptr;}
}