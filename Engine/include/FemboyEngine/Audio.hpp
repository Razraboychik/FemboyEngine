// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <string>
#include <SFML/Audio.hpp>
#include <SFML/System/Vector2.hpp>
#include <vector>
#include <memory>

namespace Fem
{

class AudioSystem
{
public:
    static AudioSystem& Instance();

    // volume is in the usual 0..100 range and is further scaled by the mixer.
    void PlaySound(const std::string& id, float volume = 100.f, const std::string& group = "sfx");
    // Positional variant: the voice is attenuated and occluded against the
    // VisibilityWorld registered with AudioOcclusion. The sound keeps its world
    // position, so a moving listener gets updated every Update().
    void PlaySoundAt(const std::string& id, sf::Vector2f position, float volume = 100.f,
                     const std::string& group = "sfx");
    void SetListenerPosition(sf::Vector2f position);
    sf::Vector2f ListenerPosition() const;
    void PlayMusic(const std::string& id, bool loop = true, float volume = 100.f);
    void StopMusic();
    void SetMasterVolume(float v);
    float MasterVolume() const;

    // voice acting playback (decoupled)
    void PlayVoice(const std::string& audioId);

    void Update(); // clean up finished sounds
    size_t ActiveSoundCount() const
    {
        return activeSounds_.size();
    }
    void StopAllSounds();

    // Stop every voice playing the given asset id. Used before a hot-reloaded
    // buffer is swapped out, so no sf::Sound keeps a dangling buffer pointer.
    void StopSounds(const std::string& id);

private:
    AudioSystem();
    void ApplyMixerVolumes();

    struct ActiveSound
    {
        std::unique_ptr<sf::Sound> sound;
        // Keeps the buffer alive while the voice plays, independently of the
        // asset cache (hot reload / clear can drop the cache entry safely).
        std::shared_ptr<sf::SoundBuffer> buffer;
        std::string id; // asset id, used to stop voices on demand
        float baseVolume = 100.f;
        std::string group = "sfx";
        bool positional = false;
        sf::Vector2f position{0.f, 0.f};
        float occlusionGain = 1.f;
        float pitch = 1.f;
    };

    std::vector<ActiveSound> activeSounds_;
    sf::Music music_;
    std::string currentMusic_;
    float musicBaseVolume_ = 100.f;
    bool musicLoaded_ = false;
};

} // namespace Fem
