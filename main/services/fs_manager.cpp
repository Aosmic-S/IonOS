#include "fs_manager.h"
#include "esp_log.h"
#include <stdio.h>
FSManager& FSManager::getInstance(){ static FSManager i; return i; }
void FSManager::init() {}
bool FSManager::isMounted() const { return SDDriver::getInstance().isMounted(); }
bool FSManager::readFile(const char* p, std::string& out) {
    FILE* f=fopen(p,"rb"); if(!f) return false;
    fseek(f,0,SEEK_END); size_t sz=ftell(f); fseek(f,0,SEEK_SET);
    out.resize(sz); fread(&out[0],1,sz,f); fclose(f); return true;
}
bool FSManager::writeFile(const char* p, const char* d, size_t n) {
    FILE* f=fopen(p,"wb"); if(!f) return false;
    fwrite(d,1,n,f); fclose(f); return true;
}
bool FSManager::deleteFile(const char* p) { return remove(p)==0; }
bool FSManager::listDir(const char* p, std::vector<FileEntry>& o) { return SDDriver::getInstance().listDir(p,o); }
uint64_t FSManager::freeSpace()  { return SDDriver::getInstance().freeSpace(); }
uint64_t FSManager::totalSpace() { return SDDriver::getInstance().totalSpace(); }