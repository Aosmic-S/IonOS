#include "sd_driver.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "sys/stat.h"
#include "dirent.h"
#include <string.h>

static const char* TAG = "SD";
SDDriver& SDDriver::getInstance(){ static SDDriver i; return i; }

esp_err_t SDDriver::init() {
    esp_vfs_fat_sdmmc_mount_config_t mc = {};
    mc.format_if_mount_failed = false;
    mc.max_files              = SD_MAX_FILES;
    mc.allocation_unit_size   = 16*1024;

    sdmmc_card_t* card;
    sdmmc_host_t  host = SDSPI_HOST_DEFAULT();
    host.slot          = SPI3_HOST;

    spi_bus_config_t bus = {};
    bus.mosi_io_num   = PIN_SD_MOSI;
    bus.miso_io_num   = PIN_SD_MISO;
    bus.sclk_io_num   = PIN_SD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    esp_err_t r = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) return r;

    sdspi_device_config_t dc = SDSPI_DEVICE_CONFIG_DEFAULT();
    dc.host_id  = SPI3_HOST;
    dc.gpio_cs  = (gpio_num_t)PIN_SD_CS;

    r = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &dc, &mc, &card);
    if (r != ESP_OK) { ESP_LOGW(TAG,"Mount failed: %s",esp_err_to_name(r)); return r; }

    m_mounted = true;
    sdmmc_card_print_info(stdout, card);

    // Ensure default dirs exist
    for (auto d : {"/sdcard/apps","/sdcard/music","/sdcard/roms/gb",
                   "/sdcard/roms/gbc","/sdcard/roms/gba","/sdcard/data","/sdcard/photos"})
        ensureDir(d);

    return ESP_OK;
}

bool SDDriver::listDir(const char* path, std::vector<FileEntry>& out) {
    DIR* dir = opendir(path);
    if (!dir) return false;
    struct dirent* e;
    while ((e = readdir(dir)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        FileEntry fe;
        fe.name  = e->d_name;
        fe.isDir = (e->d_type == DT_DIR);
        fe.size  = 0;
        if (!fe.isDir) {
            std::string full = std::string(path) + "/" + e->d_name;
            struct stat st; if(stat(full.c_str(),&st)==0) fe.size=st.st_size;
        }
        out.push_back(fe);
    }
    closedir(dir); return true;
}

bool    SDDriver::exists(const char* p) { struct stat s; return stat(p,&s)==0; }
int64_t SDDriver::fileSize(const char* p) { struct stat s; return stat(p,&s)==0?s.st_size:-1; }
void    SDDriver::ensureDir(const char* p) { mkdir(p,0777); }

uint64_t SDDriver::freeSpace() {
    FATFS* fs; DWORD fc;
    if (f_getfree("0:", &fc, &fs)!=FR_OK) return 0;
    return (uint64_t)fc * fs->csize * 512;
}
uint64_t SDDriver::totalSpace() {
    FATFS* fs; DWORD fc;
    if (f_getfree("0:", &fc, &fs)!=FR_OK) return 0;
    return (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
}
