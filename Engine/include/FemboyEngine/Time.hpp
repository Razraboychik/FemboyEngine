// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Fem
{

// A civil date and time. The engine deliberately does not model time zones:
// gameplay needs "what does the clock on the wall say" (Now()) and "which
// instant is this" (toUnix()), and both are unambiguous without a zone object.
struct DateTime
{
    int year = 1970;
    int month = 1; // 1..12
    int day = 1;   // 1..31
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;

    bool Valid() const;
    // Seconds since the Unix epoch, reading the fields as UTC.
    std::int64_t ToUnix() const;
    static DateTime FromUnix(std::int64_t seconds);
    // Local wall clock and UTC wall clock.
    static DateTime Now();
    static DateTime UtcNow();
};

bool operator==(const DateTime& a, const DateTime& b);
bool operator<(const DateTime& a, const DateTime& b);
bool operator<=(const DateTime& a, const DateTime& b);
bool operator>(const DateTime& a, const DateTime& b);
bool operator>=(const DateTime& a, const DateTime& b);

// Deterministic formatter with explicit tokens, so a date looks the same on
// every platform and locale instead of following strftime's local rules:
//
//   YYYY  2026        MM  03        DD  07
//   YY    26          M   3         D   7
//   hh    09          mm  05        ss  42
//   SSS   007         h   9         m   5
//
// Everything else is copied through, so "YYYY-MM-DD hh:mm:ss" just works.
std::string Format(const DateTime& value, const std::string& pattern);

// ISO 8601 round trip: "2026-10-20T18:30:00" and the same with ".123" or "Z".
std::string ToIso8601(const DateTime& value);
bool ParseIso8601(const std::string& text, DateTime& out);

// A closed-open interval [from, to) of wall-clock time. Used by events that run
// "from 20:00 until midnight" or by a seasonal window.
struct TimeRange
{
    DateTime from{};
    DateTime to{};

    bool Valid() const
    {
        return !(to < from);
    }
    bool Contains(const DateTime& value) const
    {
        return Valid() && !(value < from) && value < to;
    }
};

// Frame-driven countdown. It advances by the delta the game gives it, not by the
// wall clock, so pausing the game pauses every timer and a long frame cannot
// skip an event. Tick() returns true exactly once, on the frame it reaches zero.
class Timer
{
public:
    void Set(float seconds); // sets the duration and restarts
    void Start();
    void Stop();
    void Restart();
    bool Tick(float dt);

    bool Running() const
    {
        return running_;
    }
    bool Finished() const
    {
        return finished_;
    }
    float Duration() const
    {
        return duration_;
    }
    float Remaining() const
    {
        return remaining_;
    }
    float Elapsed() const
    {
        return duration_ - remaining_;
    }
    float Progress() const;

private:
    float duration_ = 0.f;
    float remaining_ = 0.f;
    bool running_ = false;
    bool finished_ = true;
};

// Wall-clock alarm: "18:30 on weekdays". Games poll it once per frame against
// DateTime::Now(); it fires once per day and then waits for the next one.
struct Alarm
{
    int hour = 0;
    int minute = 0;
    int second = 0;
    // Bit 0 = Sunday, bit 6 = Saturday. Default is every day.
    unsigned daysMask = 0x7F;
    // Day key (year*1000 + month*40 + day) of the last day this alarm fired.
    // Persist it with the alarm if the event must survive a restart that day.
    int lastFiredDay = -1;

    bool Allows(int weekday) const
    {
        return (daysMask & (1u << static_cast<unsigned>(weekday))) != 0u;
    }
};

// True once, on the first poll at or after today's alarm time on an allowed day.
bool PollAlarm(Alarm& alarm, const DateTime& now);

// "05:07" under an hour, "1:05:07" above. Negative values clamp to zero.
std::string FormatDuration(float seconds);

// Total play time, as opposed to wall time. The game feeds it the same delta it
// feeds gameplay, so pausing actually stops the stopwatch; the value is a
// double because a save that carries hundreds of hours should not lose seconds
// to float precision.
//
// This is the companion to DateTime::Now(): DateTime answers "when is it",
// GameClock answers "how long has the player been here". Saves store both, and
// offline progress is the difference between the saved wall clock and now.
class GameClock
{
public:
    // dt is the real frame delta; the clock applies its own scale and pause.
    void Tick(float dt);
    void AddSeconds(double seconds);

    void SetPaused(bool paused)
    {
        paused_ = paused;
    }
    bool Paused() const
    {
        return paused_;
    }
    void SetTimeScale(float scale)
    {
        timeScale_ = scale < 0.f ? 0.f : scale;
    }
    float TimeScale() const
    {
        return timeScale_;
    }

    double Seconds() const
    {
        return seconds_;
    }
    void SetSeconds(double seconds)
    {
        seconds_ = seconds < 0.0 ? 0.0 : seconds;
    }
    // "1:23:45" for the HUD/save summary.
    std::string Format() const;

private:
    double seconds_ = 0.0;
    float timeScale_ = 1.f;
    bool paused_ = false;
};

// Wall-clock seconds between two saved instants, clamped into [0, cap] so a
// machine with a rolled-back clock cannot hand the player negative progress and
// a save left for a year cannot grant a year of idle rewards. capSeconds <= 0
// means "no cap".
double OfflineSeconds(const DateTime& lastSeen, const DateTime& now, double capSeconds = 0.0);

// Wall-clock events on a schedule: daily bonus at 20:00, energy refill every
// 30 minutes, shop restock at midnight, seasonal windows.
//
// The scheduler stores absolute times, so it survives a restart, and poll()
// returns only the events that just became due, which keeps a game from
// awarding the same bonus on every frame. Repeating events advance by whole
// intervals from their due time, so a save that was offline for three days
// fires once and lands in the present rather than firing 72 times at startup.
class Scheduler
{
public:
    struct Event
    {
        std::string name;
        DateTime due{};             // next due instant
        double intervalSeconds = 0; // 0 = one-shot
        bool repeat = false;

        bool operator==(const Event& other) const
        {
            return name == other.name && due == other.due;
        }
    };

    void Schedule(const std::string& name, const DateTime& due, double intervalSeconds = 0.0, bool repeat = false);
    void ScheduleIn(const std::string& name, const DateTime& now, double delaySeconds, double intervalSeconds = 0.0,
                    bool repeat = false);
    // Schedules the next occurrence of a wall-clock time of day: if today's has
    // already passed, tomorrow's is used.
    void ScheduleDaily(const std::string& name, const DateTime& now, int hour, int minute, int second = 0);

    bool Cancel(const std::string& name);
    void Clear();
    bool Pending(const std::string& name) const;
    const Event* Find(const std::string& name) const;

    // Seconds until the named event, or -1 when it is not scheduled.
    double SecondsUntil(const std::string& name, const DateTime& now) const;

    // All events due at or before `now`, in due order. Repeating events advance
    // to their next future occurrence (one poll = one fire per event).
    std::vector<std::string> Poll(const DateTime& now);

    const std::vector<Event>& Events() const
    {
        return events_;
    }
    // For loading a save: replaces the whole schedule.
    void SetEvents(std::vector<Event> events)
    {
        events_ = std::move(events);
    }

private:
    std::vector<Event> events_;
};

} // namespace Fem
