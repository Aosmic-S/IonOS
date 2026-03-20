#pragma once
#include "apps/app_manager.h"
#include <vector>
#include <string>
class MusicPlayerApp : public IonApp {
public:
    void onCreate()  override;
    void onDestroy() override;
    void onKey(ion_key_t k, bool pressed) override;
private:
    void scanMusic();
    void play(int idx);
    void updateUI();
    lv_obj_t *m_trackLbl, *m_progressBar, *m_timeLbl, *m_playBtn, *m_list;
    lv_timer_t* m_timer = nullptr;
    std::vector<std::string> m_tracks;
    int m_idx = 0;
    bool m_playing = false;
};
