#include "resource_loader.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "Resources";

ResourceLoader& ResourceLoader::getInstance() {
    static ResourceLoader inst; return inst;
}

void ResourceLoader::init() {
    if (m_inited) return;
    m_inited = true;
    ESP_LOGI(TAG, "Resource loader init");
    ESP_LOGI(TAG, "  Icons:       %d × 32×32 RGB565", ION_ICON_COUNT);
    ESP_LOGI(TAG, "  Boot frames: %d × 120×80 RGB565", ION_BOOT_FRAME_COUNT);
    ESP_LOGI(TAG, "  Font:        7×10 bitmap, 95 glyphs");
    logSizes();
}

const lv_img_dsc_t* ResourceLoader::icon(ion_icon_id_t id) {
    return ion_get_icon(id);
}

lv_obj_t* ResourceLoader::makeIconImg(lv_obj_t* parent, ion_icon_id_t id) {
    lv_obj_t* img = lv_img_create(parent);
    lv_img_set_src(img, ion_get_icon(id));
    lv_obj_set_size(img, ION_ICON_W, ION_ICON_H);
    return img;
}

const ion_sound_t* ResourceLoader::sound(const char* name) {
    if (strcmp(name, "click")        == 0) return &ion_sound_click;
    if (strcmp(name, "notification") == 0) return &ion_sound_notification;
    if (strcmp(name, "error")        == 0) return &ion_sound_error;
    if (strcmp(name, "boot")         == 0) return &ion_sound_boot;
    if (strcmp(name, "success")      == 0) return &ion_sound_success;
    ESP_LOGW(TAG, "Unknown sound: %s", name);
    return &ion_sound_click;
}

const lv_img_dsc_t* ResourceLoader::bootFrame(int idx) {
    if (idx < 0 || idx >= ION_BOOT_FRAME_COUNT) return nullptr;
    return ion_boot_frames[idx];
}

void ResourceLoader::logSizes() const {
    size_t icon_bytes  = (size_t)ION_ICON_COUNT * ION_ICON_W * ION_ICON_H * 2;
    size_t frame_bytes = (size_t)ION_BOOT_FRAME_COUNT * ION_BOOT_FRAME_W * ION_BOOT_FRAME_H * 2;
    ESP_LOGI(TAG, "  Icon data:   %zuKB", icon_bytes/1024);
    ESP_LOGI(TAG, "  Frame data:  %zuKB", frame_bytes/1024);
}
