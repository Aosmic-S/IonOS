#pragma once
#include <functional>
class AudioManager {
public:
    static AudioManager& getInstance();
    void init();
    void play(const char* path, std::function<void()> onDone={});
    void playSystemSound(const char* name); // "click","notification","error","boot","success"
    void stop(); void pause(); void resume();
    void setVolume(uint8_t v);
    uint8_t getVolume() const;
    bool isPlaying() const;
    void streamTask();
};