#include "chatbot_app.h"
#include "ui/ui_engine.h"
#include "services/wifi_manager.h"
#include "ui/notification_popup.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <algorithm>
#include <cctype>
#include <stdio.h>   // fopen/fclose
#include <time.h>    // timestamp

static const char* TAG = "Chatbot";
static std::string s_resp;

const char* ChatbotApp::HISTORY_PATH = "/sdcard/ionbot/history.txt";

static esp_err_t chatCb(esp_http_client_event_t* e){
    if(e->event_id == HTTP_EVENT_ON_DATA)
        s_resp.append((char*)e->data, e->data_len);
    return ESP_OK;
}

// ─── Offline rules (unchanged) ───────────────────────────────────────────────
struct Rule { const char* k; const char* r; };
static const Rule RULES[] = {
    {"hello","Hey! I'm IonBot. How can I help?"},
    {"hi","Hi there! Ask me anything."},
    {"help","Try asking about IonOS features, or switch to Online mode with WiFi!"},
    {"battery","Check the battery icon in the top-right corner."},
    {"wifi","Go to Settings → WiFi to connect."},
    {"music","Open the Music app to play files from /sdcard/music/"},
    {"game","Emulator app supports GB/GBC/GBA ROMs from /sdcard/roms/"},
    {"joke","Why do programmers hate nature? Too many bugs!"},
    {"spec","ESP32-S3 @ 240MHz, 240x320 display, 7 RGB LEDs, 9 buttons, WiFi!"},
    {"thanks","You're welcome! Anything else?"},
    {"bye","See you! Press B to exit."},
    {"who","I'm IonBot, the built-in AI for IonOS v1.7.0"},
};

// ─── SD card helpers ──────────────────────────────────────────────────────────
void ChatbotApp::saveToSD(const std::string& role, const std::string& text){
    // Make sure /sdcard/ionbot/ directory exists
    mkdir("/sdcard/ionbot", 0777); // safe to call even if exists

    FILE* f = fopen(HISTORY_PATH, "a"); // append mode
    if(!f){
        ESP_LOGW(TAG, "Could not open history file for writing");
        return;
    }
    // Get timestamp
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    fprintf(f, "[%s] %s: %s\n", ts, role.c_str(), text.c_str());
    fclose(f);
}

void ChatbotApp::loadHistoryFromSD(){
    FILE* f = fopen(HISTORY_PATH, "r");
    if(!f) return; // no history yet, that's fine

    char line[600];
    int loaded = 0;
    // Read last MAX_HISTORY*2 lines — simple approach: read all, keep tail
    std::vector<std::string> lines;
    while(fgets(line, sizeof(line), f)){
        lines.push_back(std::string(line));
    }
    fclose(f);

    // Parse and rebuild m_history from last N lines
    // Format: [timestamp] role: content
    int start = std::max(0, (int)lines.size() - MAX_HISTORY * 2);
    for(int i = start; i < (int)lines.size(); i++){
        const std::string& l = lines[i];
        // Find "] " after timestamp
        size_t rp = l.find("] ");
        if(rp == std::string::npos) continue;
        std::string rest = l.substr(rp + 2); // "role: content\n"
        size_t cp = rest.find(": ");
        if(cp == std::string::npos) continue;
        std::string role = rest.substr(0, cp);
        std::string content = rest.substr(cp + 2);
        // Strip trailing newline
        if(!content.empty() && content.back() == '\n') content.pop_back();
        // Map saved roles back to API roles
        if(role == "You") m_history.push_back({"user", content});
        else if(role == "IonBot") m_history.push_back({"assistant", content});
        // Render to UI
        appendMsg(content, role == "You");
    }
    if(!m_history.empty())
        appendMsg("— Previous session restored —", false);
}

// ─── onCreate ────────────────────────────────────────────────────────────────
void ChatbotApp::onCreate(){
    buildScreen("IonBot");

    // Mode bar
    lv_obj_t* mb = lv_obj_create(m_screen);
    lv_obj_set_size(mb, 320, 24); lv_obj_set_pos(mb, 0, 44);
    lv_obj_set_style_bg_color(mb, lv_color_hex(0x131929), 0);
    lv_obj_set_style_border_width(mb, 0, 0);
    lv_obj_clear_flag(mb, LV_OBJ_FLAG_CLICKABLE);

    // Offline btn
    lv_obj_t* ob = lv_btn_create(mb); lv_obj_set_size(ob, 80, 18); lv_obj_set_pos(ob, 2, 3);
    lv_obj_set_style_bg_color(ob, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_radius(ob, 4, 0);
    lv_obj_t* ol = lv_label_create(ob); lv_label_set_text(ol, "Offline");
    lv_obj_set_style_text_font(ol, &lv_font_montserrat_12, 0); lv_obj_center(ol);
    lv_obj_add_event_cb(ob, [](lv_event_t*){
        ((ChatbotApp*)AppManager::getInstance().getCurrentApp())->m_online = false;
        NotificationPopup::getInstance().show("IonBot", "Offline mode", ION_NOTIF_INFO, 1500);
    }, LV_EVENT_CLICKED, nullptr);

    // Online btn
    lv_obj_t* onb = lv_btn_create(mb); lv_obj_set_size(onb, 100, 18); lv_obj_set_pos(onb, 90, 3);
    lv_obj_set_style_bg_color(onb, lv_color_hex(0x131929), 0);
    lv_obj_set_style_border_color(onb, lv_color_hex(0x1E2D4A), 0);
    lv_obj_set_style_border_width(onb, 1, 0);
    lv_obj_set_style_radius(onb, 4, 0);
    lv_obj_t* onl = lv_label_create(onb); lv_label_set_text(onl, LV_SYMBOL_WIFI " Online AI");
    lv_obj_set_style_text_font(onl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(onl, lv_color_hex(0x8899BB), 0); lv_obj_center(onl);
    lv_obj_add_event_cb(onb, [](lv_event_t*){
        if(!WiFiManager::getInstance().isConnected()){
            NotificationPopup::getInstance().show("IonBot","WiFi required", ION_NOTIF_WARNING, 2000); return;
        }
        ((ChatbotApp*)AppManager::getInstance().getCurrentApp())->m_online = true;
        NotificationPopup::getInstance().show("IonBot", "Online AI mode", ION_NOTIF_SUCCESS, 2000);
    }, LV_EVENT_CLICKED, nullptr);

    // Clear history btn
    lv_obj_t* cb = lv_btn_create(mb); lv_obj_set_size(cb, 80, 18); lv_obj_set_pos(cb, 200, 3);
    lv_obj_set_style_bg_color(cb, lv_color_hex(0x3A1A1A), 0);
    lv_obj_set_style_border_color(cb, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_border_width(cb, 1, 0);
    lv_obj_set_style_radius(cb, 4, 0);
    lv_obj_t* cl = lv_label_create(cb); lv_label_set_text(cl, LV_SYMBOL_TRASH " Clear");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cl, lv_color_hex(0xFF6666), 0); lv_obj_center(cl);
    lv_obj_add_event_cb(cb, [](lv_event_t*){
        ChatbotApp* s = (ChatbotApp*)AppManager::getInstance().getCurrentApp();
        if(!s) return;
        // Clear in-memory history
        s->m_history.clear();
        // Delete SD card file
        remove(ChatbotApp::HISTORY_PATH);
        // Clear chat log UI
        lv_obj_clean(s->m_log);
        s->appendMsg("History cleared.", false);
        NotificationPopup::getInstance().show("IonBot", "History cleared", ION_NOTIF_INFO, 1500);
    }, LV_EVENT_CLICKED, nullptr);

    // Chat log
    m_log = lv_obj_create(m_screen);
    lv_obj_set_size(m_log, 320, 220); lv_obj_set_pos(m_log, 0, 68);
    lv_obj_set_style_bg_color(m_log, lv_color_hex(0x0A0E1A), 0);
    lv_obj_set_style_border_width(m_log, 0, 0);
    lv_obj_set_style_pad_all(m_log, 4, 0);
    lv_obj_set_scroll_dir(m_log, LV_DIR_VER);
    lv_obj_set_flex_flow(m_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(m_log, 4, 0);

    // Input row
    lv_obj_t* row = lv_obj_create(m_screen);
    lv_obj_set_size(row, 320, 44); lv_obj_set_pos(row, 0, 290);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x131929), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1E2D4A), 0);
    lv_obj_set_style_pad_all(row, 4, 0);

    m_input = lv_textarea_create(row);
    lv_obj_set_size(m_input, 188, 34); lv_obj_set_pos(m_input, 0, 0);
    lv_textarea_set_placeholder_text(m_input, "Type a message...");
    lv_textarea_set_one_line(m_input, true);
    lv_obj_set_style_bg_color(m_input, lv_color_hex(0x0A0E1A), 0);
    lv_obj_set_style_text_color(m_input, lv_color_hex(0xEEF2FF), 0);
    lv_obj_set_style_radius(m_input, 6, 0);
    lv_obj_add_event_cb(m_input, [](lv_event_t* e){
        ChatbotApp* s = (ChatbotApp*)AppManager::getInstance().getCurrentApp();
        if(s) s->m_inputText = lv_textarea_get_text((lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    m_sendBtn = lv_btn_create(row);
    lv_obj_set_size(m_sendBtn, 36, 34); lv_obj_set_pos(m_sendBtn, 196, 0);
    lv_obj_set_style_bg_color(m_sendBtn, lv_color_hex(0x7B2FFF), 0);
    lv_obj_set_style_radius(m_sendBtn, 8, 0);
    lv_obj_t* si = lv_label_create(m_sendBtn); lv_label_set_text(si, LV_SYMBOL_RIGHT); lv_obj_center(si);
    lv_obj_add_event_cb(m_sendBtn, [](lv_event_t*){
        ChatbotApp* s = (ChatbotApp*)AppManager::getInstance().getCurrentApp();
        if(s && !s->m_inputText.empty()){
            s->sendMsg(s->m_inputText);
            lv_textarea_set_text(s->m_input, "");
            s->m_inputText.clear();
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Load previous history from SD card
    loadHistoryFromSD();
    if(m_history.empty())
        appendMsg("Hi! I'm IonBot. Ask me anything about IonOS!", false);
}

// ─── appendMsg (unchanged) ───────────────────────────────────────────────────
void ChatbotApp::appendMsg(const std::string& text, bool isUser){
    lv_obj_t* b = lv_obj_create(m_log);
    lv_obj_set_width(b, 220); lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b, lv_color_hex(isUser ? 0x7B2FFF : 0x131929), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 6, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* sl = lv_label_create(b);
    lv_label_set_text(sl, isUser ? "You" : "IonBot");
    lv_obj_set_style_text_color(sl, lv_color_hex(isUser ? 0xBB99FF : 0x00D4FF), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_12, 0);
    lv_obj_t* ml = lv_label_create(b);
    lv_label_set_text(ml, text.c_str());
    lv_obj_set_style_text_color(ml, lv_color_hex(0xEEF2FF), 0);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(ml, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ml, 206);
    lv_obj_set_style_margin_left(b, isUser ? 20 : 0, 0);
    lv_obj_scroll_to_y(m_log, LV_COORD_MAX, LV_ANIM_ON);
}

// ─── sendMsg — now pushes to m_history and saves to SD ───────────────────────
void ChatbotApp::sendMsg(const std::string& text){
    if(m_waiting || text.empty()) return;
    appendMsg(text, true);

    // Add to in-memory history
    m_history.push_back({"user", text});
    // Trim to MAX_HISTORY exchanges (2 msgs per exchange)
    while((int)m_history.size() > MAX_HISTORY * 2)
        m_history.erase(m_history.begin());

    // Save user message to SD
    saveToSD("You", text);

    m_waiting = true;
    if(m_online && WiFiManager::getInstance().isConnected()){
        ApiArg* a = new ApiArg(); a->app = this;
        strncpy(a->prompt, text.c_str(), 511);
        // Pass history snapshot into the task
        a->history = m_history; // copy current history
        xTaskCreate(apiTask, "chatbot_api", 8192, a, 4, nullptr);
    } else {
        std::string r = offlineReply(text);
        m_history.push_back({"assistant", r});
        saveToSD("IonBot", r);
        if(UIEngine::getInstance().lock(100)){
            appendMsg(r, false);
            UIEngine::getInstance().unlock();
        }
        m_waiting = false;
    }
}

// ─── offlineReply (unchanged) ─────────────────────────────────────────────────
std::string ChatbotApp::offlineReply(const std::string& in){
    std::string lo = in;
    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
    for(auto& rule : RULES)
        if(lo.find(rule.k) != std::string::npos) return rule.r;
    return "I'm in offline mode. Switch to Online mode for smarter answers!";
}

// ─── apiTask — sends full history as conversation context ────────────────────
void ChatbotApp::apiTask(void* arg){
    ApiArg* a = (ApiArg*)arg;
    s_resp.clear();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "meta-llama/llama-3.3-70b-instruct:free");
    cJSON* msgs = cJSON_AddArrayToObject(root, "messages");

    // System prompt
    cJSON* sys = cJSON_CreateObject();
    cJSON_AddStringToObject(sys, "role", "system");
    cJSON_AddStringToObject(sys, "content",
        "You are IonBot on IonOS handheld by Arham. Be brief (<150 chars). "
        "You have memory of this conversation.");
    cJSON_AddItemToArray(msgs, sys);

    // ★ Full conversation history for context
    for(auto& msg : a->history){
        cJSON* m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", msg.role.c_str());
        cJSON_AddStringToObject(m, "content", msg.content.c_str());
        cJSON_AddItemToArray(msgs, m);
    }

    cJSON_AddNumberToObject(root, "max_tokens", 80);
    char* payload = cJSON_PrintUnformatted(root); cJSON_Delete(root);

    esp_http_client_config_t cfg = {};
    cfg.url = "https://openrouter.ai/api/v1/chat/completions";
    cfg.event_handler = chatCb; cfg.timeout_ms = 20000; cfg.buffer_size = 4096;
    auto* c = esp_http_client_init(&cfg);
    esp_http_client_set_method(c, HTTP_METHOD_POST);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "Authorization", "Bearer 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");
    esp_http_client_set_header(c, "HTTP-Referer", "https://ionos.local");
    esp_http_client_set_header(c, "X-Title", "IonBot");
    esp_http_client_set_post_field(c, payload, strlen(payload));

    std::string reply = "API error. Check your connection.";
    if(esp_http_client_perform(c) == ESP_OK){
        cJSON* r = cJSON_Parse(s_resp.c_str());
        if(r){
            cJSON* ch = cJSON_GetObjectItem(r, "choices");
            if(cJSON_IsArray(ch)){
                cJSON* f = cJSON_GetArrayItem(ch, 0);
                cJSON* mc = cJSON_GetObjectItem(cJSON_GetObjectItem(f, "message"), "content");
                if(cJSON_IsString(mc)) reply = mc->valuestring;
            }
            cJSON_Delete(r);
        }
    }
    esp_http_client_cleanup(c);
    free(payload);

    // Save bot reply to SD
    a->app->saveToSD("IonBot", reply);

    if(UIEngine::getInstance().lock(200)){
        a->app->m_history.push_back({"assistant", reply});
        // Trim again after bot reply
        while((int)a->app->m_history.size() > ChatbotApp::MAX_HISTORY * 2)
            a->app->m_history.erase(a->app->m_history.begin());
        a->app->appendMsg(reply, false);
        a->app->m_waiting = false;
        UIEngine::getInstance().unlock();
    }
    delete a;
    vTaskDelete(nullptr);
}

// ─── onKey (unchanged) ───────────────────────────────────────────────────────
void ChatbotApp::onKey(ion_key_t k, bool pressed){
    if(!pressed) return;
    if(k == ION_KEY_B) AppManager::getInstance().closeCurrentApp();
    if(k == ION_KEY_X && !m_inputText.empty()){
        sendMsg(m_inputText);
        lv_textarea_set_text(m_input, "");
        m_inputText.clear();
    }
}

void ChatbotApp::onDestroy(){
    if(m_screen){ lv_obj_del(m_screen); m_screen = nullptr; }
}
