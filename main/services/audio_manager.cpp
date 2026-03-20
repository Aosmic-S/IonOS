#include "audio_manager.h"
#include "drivers/audio/audio_driver.h"
#include "resources/resource_loader.h"
AudioManager& AudioManager::getInstance(){ static AudioManager i; return i; }
void AudioManager::init() { AudioDriver::getInstance().init(); }
void AudioManager::play(const char* path, std::function<void()> done) {
    AudioDriver::getInstance().setDoneCallback(done);
    AudioDriver::getInstance().playFile(path);
}
void AudioManager::playSystemSound(const char* name) {
    const ion_sound_t* s = ResourceLoader::getInstance().sound(name);
    if (s) AudioDriver::getInstance().play((const uint8_t*)s->data, s->len*2);
}
void AudioManager::stop()   { AudioDriver::getInstance().stop(); }
void AudioManager::pause()  { AudioDriver::getInstance().pause(); }
void AudioManager::resume() { AudioDriver::getInstance().resume(); }
void AudioManager::setVolume(uint8_t v) { AudioDriver::getInstance().setVolume(v); }
uint8_t AudioManager::getVolume() const { return AudioDriver::getInstance().getVolume(); }
bool AudioManager::isPlaying() const { return AudioDriver::getInstance().isPlaying(); }
void AudioManager::streamTask() { AudioDriver::getInstance().streamTask(); }