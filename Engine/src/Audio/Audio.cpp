// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Audio.hpp"
#include "FemboyEngine/Assets.hpp"
#include "FemboyEngine/AudioMixer.hpp"
#include "FemboyEngine/AudioOcclusion.hpp"
#include "FemboyEngine/Boy.hpp"
#include <algorithm>
#include <filesystem>

namespace Fem
{

namespace
{
// A runaway caller (a sound fired every frame by a collision) would otherwise
// grow activeSounds_ without bound until each clip finished.
constexpr size_t kMaxVoices = 64;
} // namespace

AudioSystem& AudioSystem::Instance()
{
    static AudioSystem a;
    return a;
}

AudioSystem::AudioSystem()
{
    // The mixer is constructed here (and therefore destroyed after this
    // singleton), so the callback can safely re-apply live volumes.
    AudioMixer::Instance().SetChangeCallback([this]() { ApplyMixerVolumes(); });
}

void AudioSystem::ApplyMixerVolumes()
{
    if (musicLoaded_)
    {
        music_.setVolume(musicBaseVolume_ * AudioMixer::Instance().EffectiveVolume("music"));
    }
    for (auto& s : activeSounds_)
    {
        if (s.sound)
        {
            s.sound->setVolume(s.baseVolume * AudioMixer::Instance().EffectiveVolume(s.group) * s.occlusionGain);
            s.sound->setPitch(s.pitch);
        }
    }
}

void AudioSystem::StopSounds(const std::string& id)
{
    for (auto& s : activeSounds_)
    {
        if (s.sound && s.id == id)
        {
            s.sound->stop();
        }
    }
    activeSounds_.erase(
        std::remove_if(activeSounds_.begin(), activeSounds_.end(), [&id](const ActiveSound& s) { return s.id == id; }),
        activeSounds_.end());
}

void AudioSystem::PlaySound(const std::string& id, float volume, const std::string& group)
{
    // Shared ownership: the voice keeps the buffer alive even if the asset cache
    // drops or replaces it while the sound is still playing.
    auto buffer = AssetManager::Instance().SoundShared(id);
    if (!buffer)
    {
        // soundShared() already logged the miss.
        BOY_WARN("Audio", "sound not found: " + id);
        return;
    }

    // Drop finished voices before adding a new one.
    activeSounds_.erase(std::remove_if(activeSounds_.begin(), activeSounds_.end(), [](const ActiveSound& s)
                                       { return !s.sound || s.sound->getStatus() == sf::Sound::Status::Stopped; }),
                        activeSounds_.end());

    ActiveSound entry;
    entry.id = id;
    entry.baseVolume = std::clamp(volume, 0.f, 100.f);
    entry.group = group;
    entry.buffer = buffer;
    entry.sound = std::make_unique<sf::Sound>(*buffer);
    // sf::Sound volume is 0..100; the mixer returns 0..1.
    entry.sound->setVolume(entry.baseVolume * AudioMixer::Instance().EffectiveVolume(group));
    entry.sound->play();

    // Steal the oldest voice once the cap is reached, so a loud frame cannot
    // exhaust both memory and mixer channels.
    if (activeSounds_.size() >= kMaxVoices)
    {
        activeSounds_.erase(activeSounds_.begin());
    }

    activeSounds_.push_back(std::move(entry));
}

void AudioSystem::PlaySoundAt(const std::string& id, sf::Vector2f position, float volume, const std::string& group)
{
    PlaySound(id, volume, group);
    if (activeSounds_.empty())
    {
        return;
    }

    ActiveSound& entry = activeSounds_.back();
    if (entry.id != id)
    {
        return;
    }
    entry.positional = true;
    entry.position = position;
    const OcclusionResult occlusion = AudioOcclusion::Instance().Compute(position);
    entry.occlusionGain = occlusion.gain;
    entry.pitch = occlusion.pitch;
    entry.sound->setVolume(entry.baseVolume * AudioMixer::Instance().EffectiveVolume(entry.group) *
                           entry.occlusionGain);
    entry.sound->setPitch(entry.pitch);
}

void AudioSystem::SetListenerPosition(sf::Vector2f position)
{
    AudioOcclusion::Instance().SetListener(position);
}

sf::Vector2f AudioSystem::ListenerPosition() const
{
    return AudioOcclusion::Instance().Listener();
}

void AudioSystem::PlayMusic(const std::string& id, bool loop, float volume)
{
    music_.stop();

    // Accept either an asset id (resolved through the manifest) or a direct
    // file path. Passing an id straight to openFromFile() only worked when the
    // id happened to equal the path.
    std::string path = id;
    if (!AssetManager::Instance().HasMusic(id))
    {
        if (std::filesystem::exists(id))
        {
            path = id;
        }
        else
        {
            BOY_ERROR("Audio", "music not found: " + id);
            musicLoaded_ = false;
            return;
        }
    }
    else
    {
        path = AssetManager::Instance().MusicPath(id);
    }

    musicLoaded_ = music_.openFromFile(path);
    if (!musicLoaded_)
    {
        BOY_ERROR("Audio", "cannot open music: " + path);
        return;
    }

    currentMusic_ = id;
    musicBaseVolume_ = std::clamp(volume, 0.f, 100.f);
    music_.setLooping(loop);
    // 0..100 base * 0..1 mixer factor. The old code multiplied by an extra 100.
    music_.setVolume(musicBaseVolume_ * AudioMixer::Instance().EffectiveVolume("music"));
    music_.play();
    BOY_INFO("Audio", "playing music: " + id);
}

void AudioSystem::StopMusic()
{
    music_.stop();
    currentMusic_.clear();
}

void AudioSystem::SetMasterVolume(float v)
{
    // The mixer's change callback re-applies the new level to every live voice
    // and to the music stream.
    AudioMixer::Instance().SetMasterVolume(std::clamp(v, 0.f, 1.f));
}

float AudioSystem::MasterVolume() const
{
    return AudioMixer::Instance().MasterVolume();
}

void AudioSystem::PlayVoice(const std::string& audioId)
{
    PlaySound(audioId, 100.f, "voice");
}

void AudioSystem::Update()
{
    activeSounds_.erase(std::remove_if(activeSounds_.begin(), activeSounds_.end(), [](const ActiveSound& s)
                                       { return !s.sound || s.sound->getStatus() == sf::Sound::Status::Stopped; }),
                        activeSounds_.end());

    // Positional voices re-evaluate occlusion when the listener moves. This is
    // one ray per voice against the shared visibility world, not a per-sample
    // filter: SFML has no audio callback of its own.
    for (ActiveSound& s : activeSounds_)
    {
        if (!s.positional || !s.sound)
        {
            continue;
        }
        const OcclusionResult occlusion = AudioOcclusion::Instance().Compute(s.position);
        s.occlusionGain = occlusion.gain;
        s.pitch = occlusion.pitch;
        s.sound->setVolume(s.baseVolume * AudioMixer::Instance().EffectiveVolume(s.group) * s.occlusionGain);
        s.sound->setPitch(s.pitch);
    }
}

void AudioSystem::StopAllSounds()
{
    for (auto& s : activeSounds_)
    {
        if (s.sound)
        {
            s.sound->stop();
        }
    }
    activeSounds_.clear();
}

} // namespace Fem
