#pragma once
#include "apps/app_manager.h"
#include <string>
class BrowserApp : public IonApp {
public:
    void onCreate()  override;
    void onDestroy() override;
    void onKey(ion_key_t k, bool pressed) override;
private:
    void showBookmarks();
    void navigate(const char* url);
    void renderContent(const char* raw);
    static void fetchTask(void* arg);
    struct FetchArgs { BrowserApp* app; char url[256]; };
    lv_obj_t *m_urlLbl, *m_content, *m_statusLbl;
    bool m_loading=false, m_showingBookmarks=true;
    std::string m_inputText;
    static constexpr const char* URLS[]  = {
        "http://wttr.in/?format=3","http://api.quotable.io/random",
        "http://numbersapi.com/random","http://icanhazdadjoke.com/",
        "http://catfact.ninja/fact"};
    static constexpr const char* NAMES[] = {
        "Weather","Quote of Day","Number Fact","Dad Joke","Cat Fact"};
};
