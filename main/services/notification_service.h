#pragma once
#include "config/ion_config.h"
class NotificationService {
public:
    static NotificationService& getInstance();
    void init();
    void post(const char* title, const char* msg,
              ion_notif_level_t level=ION_NOTIF_INFO, uint32_t ms=3000);
};