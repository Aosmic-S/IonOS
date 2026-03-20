#pragma once

#include "apps/app_manager.h"
#include <string>
#include <vector>

class ChatbotApp : public IonApp {
public:
    void onCreate()  override;
    void onDestroy() override;
    void onKey(ion_key_t k, bool pressed) override;

    static const char* HISTORY_PATH;
    static const int   MAX_HISTORY = 10;

private:
    // ── Message struct ────────────────────────────────────────────────────────
    struct ChatMsg {
        std::string role;     // "user" or "assistant"
        std::string content;
    };

    // ── API task arg ──────────────────────────────────────────────────────────
    struct ApiArg {
        ChatbotApp*           app;
        char                  prompt[512];
        std::vector<ChatMsg>  history;   // snapshot of m_history at send time
    };

    // ── UI objects ────────────────────────────────────────────────────────────
    lv_obj_t* m_log     = nullptr;
    lv_obj_t* m_input   = nullptr;
    lv_obj_t* m_sendBtn = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    std::string            m_inputText;
    bool                   m_waiting = false;
    bool                   m_online  = false;
    std::vector<ChatMsg>   m_history;

    // ── Methods ───────────────────────────────────────────────────────────────
    void        appendMsg(const std::string& text, bool isUser);
    void        sendMsg(const std::string& text);
    std::string offlineReply(const std::string& in);
    void        saveToSD(const std::string& role, const std::string& text);
    void        loadHistoryFromSD();
    static void apiTask(void* arg);
};