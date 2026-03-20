#include "file_manager_app.h"
#include "ui/ui_engine.h"
#include "ui/notification_popup.h"
#include "esp_log.h"
#include <algorithm>
static const char* TAG="FileMgr";
void FileManagerApp::onCreate(){
    buildScreen("File Manager");
    lv_obj_t* pb=lv_obj_create(m_screen);
    lv_obj_set_size(pb, 320, 18); lv_obj_set_pos(pb,0,44);
    lv_obj_set_style_bg_color(pb,lv_color_hex(0x131929),0);
    lv_obj_set_style_border_width(pb,0,0); lv_obj_clear_flag(pb,LV_OBJ_FLAG_CLICKABLE);
    m_pathLbl=lv_label_create(pb);
    lv_label_set_text(m_pathLbl,"/sdcard");
    lv_obj_set_style_text_color(m_pathLbl,lv_color_hex(0x00D4FF),0);
    lv_obj_set_style_text_font(m_pathLbl,&lv_font_montserrat_12,0);
    lv_obj_align(m_pathLbl,LV_ALIGN_LEFT_MID,4,0);
    m_list=lv_list_create(m_screen);
    lv_obj_set_size(m_list, 320, 256); lv_obj_set_pos(m_list,0,62);
    lv_obj_set_style_bg_color(m_list,lv_color_hex(0x0A0E1A),0);
    lv_obj_set_style_border_width(m_list,0,0);
    lv_obj_t* sb=lv_obj_create(m_screen);
    lv_obj_set_size(sb, 320, 18); lv_obj_set_pos(sb,0,318);
    lv_obj_set_style_bg_color(sb,lv_color_hex(0x090D17),0);
    lv_obj_set_style_border_width(sb,0,0); lv_obj_clear_flag(sb,LV_OBJ_FLAG_CLICKABLE);
    m_statsLbl=lv_label_create(sb);
    lv_label_set_text(m_statsLbl,"A:Open  X:Del  B:Back");
    lv_obj_set_style_text_color(m_statsLbl,lv_color_hex(0x4A5568),0);
    lv_obj_set_style_text_font(m_statsLbl,&lv_font_montserrat_12,0);
    navigateTo("/sdcard");
}
void FileManagerApp::navigateTo(const std::string& p){
    m_path=p; m_sel=0; m_entries.clear(); lv_obj_clean(m_list);
    lv_label_set_text(m_pathLbl,p.c_str());
    if(p!="/sdcard") m_entries.push_back({"..",true,0});
    SDDriver::getInstance().listDir(p.c_str(),m_entries);
    if(m_entries.empty()){ lv_list_add_text(m_list,"(empty)"); return; }
    for(int i=0;i<(int)m_entries.size();i++){
        auto& fe=m_entries[i];
        char disp[128]; snprintf(disp,sizeof(disp),fe.isDir?"%s/":fe.size<1024?"%s (%zuB)":fe.size<1048576?"%s (%.1fKB)":"%s (%.1fMB)",
            fe.name.c_str(), fe.isDir?0:fe.size<1024?(size_t)fe.size:0);
        if(!fe.isDir) {
            if(fe.size<1024) snprintf(disp,sizeof(disp),"%s (%zuB)",fe.name.c_str(),fe.size);
            else if(fe.size<1048576) snprintf(disp,sizeof(disp),"%s (%.1fKB)",fe.name.c_str(),fe.size/1024.0f);
            else snprintf(disp,sizeof(disp),"%s (%.1fMB)",fe.name.c_str(),fe.size/1048576.0f);
        } else snprintf(disp,sizeof(disp),"%s/",fe.name.c_str());
        lv_obj_t* btn=lv_list_add_btn(m_list,fileIcon(fe.name),disp);
        lv_obj_set_style_bg_color(btn,lv_color_hex(0x0A0E1A),0);
        lv_obj_set_style_bg_color(btn,lv_color_hex(0xFFB800),LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(btn,0,0); lv_obj_set_height(btn,26);
        lv_obj_set_style_text_font(btn,&lv_font_montserrat_12,0);
        lv_obj_set_user_data(btn,(void*)(intptr_t)i);
        lv_obj_add_event_cb(btn,[](lv_event_t* e){
            int idx=(int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            FileManagerApp* s=(FileManagerApp*)AppManager::getInstance().getCurrentApp();
            if(s){s->m_sel=idx;s->openSelected();}
        },LV_EVENT_CLICKED,nullptr);
    }
    char stats[48]; snprintf(stats,sizeof(stats),"%d items | %.0fMB free",
        (int)m_entries.size(), SDDriver::getInstance().freeSpace()/1048576.0);
    lv_label_set_text(m_statsLbl,stats);
}
void FileManagerApp::openSelected(){
    if(m_sel<0||m_sel>=(int)m_entries.size()) return;
    auto& fe=m_entries[m_sel];
    if(fe.name==".."){
        size_t sl=m_path.rfind('/');
        if(sl!=std::string::npos) navigateTo(m_path.substr(0,sl));
    } else if(fe.isDir){
        navigateTo(m_path+"/"+fe.name);
    } else {
        char msg[64]; snprintf(msg,sizeof(msg),"%.1fKB",(float)fe.size/1024);
        NotificationPopup::getInstance().show(fe.name.c_str(),msg,ION_NOTIF_INFO,2000);
    }
}
void FileManagerApp::deleteSelected(){
    if(m_sel<0||m_sel>=(int)m_entries.size()) return;
    auto& fe=m_entries[m_sel];
    if(fe.name=="..") return;
    std::string full=m_path+"/"+fe.name;
    if(remove(full.c_str())==0){ NotificationPopup::getInstance().show("Deleted",fe.name.c_str(),ION_NOTIF_SUCCESS,2000); navigateTo(m_path); }
    else NotificationPopup::getInstance().show("Error","Cannot delete",ION_NOTIF_ERROR,2000);
}
const char* FileManagerApp::fileIcon(const std::string& n){
    if(n=="..") return LV_SYMBOL_LEFT;
    if(n.size()<4) return LV_SYMBOL_FILE;
    std::string ext=n.substr(n.size()-3); for(char& c:ext)c=tolower(c);
    if(ext==".gb"||ext=="gbc"||ext=="gba") return LV_SYMBOL_PLAY;
    if(ext=="wav"||ext=="mp3") return LV_SYMBOL_AUDIO;
    if(ext=="txt"||ext=="log") return LV_SYMBOL_LIST;
    return LV_SYMBOL_FILE;
}
void FileManagerApp::onKey(ion_key_t k, bool pressed){
    if(!pressed) return;
    if(k==ION_KEY_X) openSelected();
    if(k==ION_KEY_A) deleteSelected();
    if(k==ION_KEY_B){
        if(m_path!="/sdcard"){size_t sl=m_path.rfind('/'); if(sl!=std::string::npos) navigateTo(m_path.substr(0,sl));}
        else AppManager::getInstance().closeCurrentApp();
    }
}
void FileManagerApp::onDestroy(){ if(m_screen){lv_obj_del(m_screen);m_screen=nullptr;} }