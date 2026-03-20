#pragma once
#include "apps/app_manager.h"
#include "drivers/storage/sd_driver.h"
#include <string>
#include <vector>
class FileManagerApp : public IonApp {
public:
    void onCreate()  override;
    void onDestroy() override;
    void onKey(ion_key_t k, bool pressed) override;
private:
    void navigateTo(const std::string& path);
    void openSelected();
    void deleteSelected();
    const char* fileIcon(const std::string& name);
    lv_obj_t *m_list, *m_pathLbl, *m_statsLbl;
    std::string m_path;
    std::vector<FileEntry> m_entries;
    int m_sel=0;
};
