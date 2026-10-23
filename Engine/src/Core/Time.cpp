// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Copyright (c) 2026 Razraboychik.
#include "FemboyEngine/Time.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace Fem
{
namespace
{

// Number of days in a month for a Gregorian year. February is the only month
// that cares about the leap rule, so it is the only one that asks.
int DaysInMonth(int year, int month)
{
    static const int table[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
    {
        return 31;
    }
    if (month == 2)
    {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return table[month - 1];
}

void AppendNumber(std::string& out, int value, int digits)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%0*d", digits, value);
    out += buffer;
}

} // namespace

bool DateTime::Valid() const
{
    if (month < 1 || month > 12)
    {
        return false;
    }
    if (day < 1 || day > DaysInMonth(year, month))
    {
        return false;
    }
    if (hour < 0 || hour > 23)
    {
        return false;
    }
    if (minute < 0 || minute > 59)
    {
        return false;
    }
    if (second < 0 || second > 60) // 60: leap second, tolerated
    {
        return false;
    }
    if (millisecond < 0 || millisecond > 999)
    {
        return false;
    }
    return year >= 1970 && year <= 9999;
}

std::int64_t DateTime::ToUnix() const
{
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = std::min(second, 59);
#if defined(_WIN32)
    const std::time_t value = _mkgmtime(&tm);
#else
    const std::time_t value = timegm(&tm);
#endif
    return static_cast<std::int64_t>(value);
}

DateTime DateTime::FromUnix(std::int64_t seconds)
{
    const std::time_t value = static_cast<std::time_t>(seconds);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    DateTime out;
    out.year = tm.tm_year + 1900;
    out.month = tm.tm_mon + 1;
    out.day = tm.tm_mday;
    out.hour = tm.tm_hour;
    out.minute = tm.tm_min;
    out.second = tm.tm_sec;
    return out;
}

DateTime DateTime::Now()
{
    const std::time_t value = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &value);
#else
    localtime_r(&value, &tm);
#endif
    DateTime out;
    out.year = tm.tm_year + 1900;
    out.month = tm.tm_mon + 1;
    out.day = tm.tm_mday;
    out.hour = tm.tm_hour;
    out.minute = tm.tm_min;
    out.second = tm.tm_sec;
    return out;
}

DateTime DateTime::UtcNow()
{
    const std::time_t value = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    DateTime out;
    out.year = tm.tm_year + 1900;
    out.month = tm.tm_mon + 1;
    out.day = tm.tm_mday;
    out.hour = tm.tm_hour;
    out.minute = tm.tm_min;
    out.second = tm.tm_sec;
    return out;
}

bool operator==(const DateTime& a, const DateTime& b)
{
    return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour && a.minute == b.minute &&
           a.second == b.second && a.millisecond == b.millisecond;
}

bool operator<(const DateTime& a, const DateTime& b)
{
    if (a.year != b.year)
    {
        return a.year < b.year;
    }
    if (a.month != b.month)
    {
        return a.month < b.month;
    }
    if (a.day != b.day)
    {
        return a.day < b.day;
    }
    if (a.hour != b.hour)
    {
        return a.hour < b.hour;
    }
    if (a.minute != b.minute)
    {
        return a.minute < b.minute;
    }
    if (a.second != b.second)
    {
        return a.second < b.second;
    }
    return a.millisecond < b.millisecond;
}

bool operator<=(const DateTime& a, const DateTime& b)
{
    return !(b < a);
}

bool operator>(const DateTime& a, const DateTime& b)
{
    return b < a;
}

bool operator>=(const DateTime& a, const DateTime& b)
{
    return !(a < b);
}

std::string Format(const DateTime& value, const std::string& pattern)
{
    std::string out;
    out.reserve(pattern.size() + 8);
    const size_t size = pattern.size();
    for (size_t i = 0; i < size;)
    {
        // Longest token first, so YYYY is never eaten by YY.
        const std::string rest = pattern.substr(i, 4);
        if (rest.rfind("YYYY", 0) == 0)
        {
            AppendNumber(out, value.year, 4);
            i += 4;
        }
        else if (rest.rfind("YY", 0) == 0)
        {
            AppendNumber(out, value.year % 100, 2);
            i += 2;
        }
        else if (rest.rfind("MM", 0) == 0)
        {
            AppendNumber(out, value.month, 2);
            i += 2;
        }
        else if (rest.rfind("DD", 0) == 0)
        {
            AppendNumber(out, value.day, 2);
            i += 2;
        }
        else if (rest.rfind("hh", 0) == 0)
        {
            AppendNumber(out, value.hour, 2);
            i += 2;
        }
        else if (rest.rfind("mm", 0) == 0)
        {
            AppendNumber(out, value.minute, 2);
            i += 2;
        }
        else if (rest.rfind("ss", 0) == 0)
        {
            AppendNumber(out, value.second, 2);
            i += 2;
        }
        else if (pattern.compare(i, 3, "SSS") == 0)
        {
            AppendNumber(out, value.millisecond, 3);
            i += 3;
        }
        else if (pattern[i] == 'M')
        {
            // Single-letter tokens print without leading zeros.
            AppendNumber(out, value.month, 0);
            ++i;
        }
        else if (pattern[i] == 'D')
        {
            AppendNumber(out, value.day, 1);
            ++i;
        }
        else if (pattern[i] == 'h')
        {
            AppendNumber(out, value.hour, 1);
            ++i;
        }
        else if (pattern[i] == 'm')
        {
            AppendNumber(out, value.minute, 1);
            ++i;
        }
        else if (pattern[i] == 's')
        {
            AppendNumber(out, value.second, 1);
            ++i;
        }
        else
        {
            out += pattern[i];
            ++i;
        }
    }
    return out;
}

std::string ToIso8601(const DateTime& value)
{
    return Format(value, "YYYY-MM-DDThh:mm:ss");
}

bool ParseIso8601(const std::string& text, DateTime& out)
{
    DateTime value;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millis = 0;
    const int fields =
        std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second);
    if (fields < 3)
    {
        return false;
    }
    if (fields < 6)
    {
        hour = 0;
        minute = 0;
        second = 0;
    }
    // Optional fractional seconds: \"...:12.345Z\"
    const size_t dot = text.find('.');
    if (dot != std::string::npos)
    {
        int scale = 100;
        size_t i = dot + 1;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && scale > 0)
        {
            millis += (text[i] - '0') * scale;
            scale /= 10;
            ++i;
        }
    }
    value.year = year;
    value.month = month;
    value.day = day;
    value.hour = hour;
    value.minute = minute;
    value.second = second;
    value.millisecond = millis;
    if (!value.Valid())
    {
        return false;
    }
    out = value;
    return true;
}

void Timer::Set(float seconds)
{
    duration_ = std::max(0.f, seconds);
    remaining_ = duration_;
    running_ = duration_ > 0.f;
    finished_ = duration_ <= 0.f;
}

void Timer::Start()
{
    if (remaining_ <= 0.f)
    {
        remaining_ = duration_;
    }
    running_ = true;
    finished_ = false;
}

void Timer::Stop()
{
    running_ = false;
}

void Timer::Restart()
{
    remaining_ = duration_;
    running_ = duration_ > 0.f;
    finished_ = duration_ <= 0.f;
}

bool Timer::Tick(float dt)
{
    if (!running_ || dt <= 0.f)
    {
        return false;
    }
    remaining_ -= dt;
    if (remaining_ > 0.f)
    {
        return false;
    }
    // Fire exactly once, on the frame the timer crosses zero, no matter how
    // large the delta was.
    remaining_ = 0.f;
    running_ = false;
    finished_ = true;
    return true;
}

float Timer::Progress() const
{
    if (duration_ <= 0.f)
    {
        return 1.f;
    }
    return std::clamp(1.f - remaining_ / duration_, 0.f, 1.f);
}

bool PollAlarm(Alarm& alarm, const DateTime& now)
{
    std::tm tm{};
    tm.tm_year = now.year - 1900;
    tm.tm_mon = now.month - 1;
    tm.tm_mday = now.day;
    tm.tm_hour = 12; // noon avoids any DST edge when only the weekday is wanted
    const std::time_t stamp = std::mktime(&tm);
    if (stamp == static_cast<std::time_t>(-1))
    {
        return false;
    }
    const int weekday = tm.tm_wday;

    if (!alarm.Allows(weekday))
    {
        return false;
    }

    const int nowSeconds = now.hour * 3600 + now.minute * 60 + now.second;
    const int alarmSeconds = alarm.hour * 3600 + alarm.minute * 60 + alarm.second;
    if (nowSeconds < alarmSeconds)
    {
        return false;
    }

    const int dayKey = now.year * 1000 + now.month * 40 + now.day;
    if (alarm.lastFiredDay == dayKey)
    {
        return false;
    }

    alarm.lastFiredDay = dayKey;
    return true;
}

std::string FormatDuration(float seconds)
{
    if (seconds < 0.f || !std::isfinite(seconds))
    {
        seconds = 0.f;
    }
    const int total = static_cast<int>(seconds + 0.5f);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;
    std::string out;
    if (hours > 0)
    {
        AppendNumber(out, hours, 1);
        out += ':';
    }
    AppendNumber(out, minutes, hours > 0 ? 2 : 1);
    out += ':';
    AppendNumber(out, secs, 2);
    return out;
}

// GameClock

void GameClock::Tick(float dt)
{
    if (paused_ || !std::isfinite(dt) || dt <= 0.f)
    {
        return;
    }
    seconds_ += static_cast<double>(dt) * static_cast<double>(timeScale_);
}

void GameClock::AddSeconds(double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0)
    {
        return;
    }
    seconds_ += seconds;
}

std::string GameClock::Format() const
{
    // formatDuration takes seconds as a float; play time is a double because a
    // long save must not lose seconds to float precision. Split the integer part
    // so the conversion is exact up to the displayed second.
    const double clamped = seconds_ < 0.0 ? 0.0 : seconds_;
    const double whole = std::floor(clamped);
    const double fraction = clamped - whole;
    const float display = static_cast<float>(whole) + static_cast<float>(fraction);
    return FormatDuration(display);
}

// Offline progress

double OfflineSeconds(const DateTime& lastSeen, const DateTime& now, double capSeconds)
{
    const double elapsed = static_cast<double>(now.ToUnix() - lastSeen.ToUnix());
    if (!std::isfinite(elapsed) || elapsed <= 0.0)
    {
        return 0.0;
    }
    if (capSeconds > 0.0)
    {
        return std::min(elapsed, capSeconds);
    }
    return elapsed;
}

// Scheduler

void Scheduler::Schedule(const std::string& name, const DateTime& due, double intervalSeconds, bool repeat)
{
    if (name.empty())
    {
        return;
    }

    Event event;
    event.name = name;
    event.due = due;
    event.intervalSeconds = std::max(0.0, intervalSeconds);
    // A repeating event without an interval has no meaningful next occurrence.
    // Treat it as one-shot instead of letting it fire on every poll.
    event.repeat = repeat && event.intervalSeconds > 0.0;

    for (Event& existing : events_)
    {
        if (existing.name == name)
        {
            existing = std::move(event);
            return;
        }
    }
    events_.push_back(std::move(event));
}

void Scheduler::ScheduleIn(const std::string& name, const DateTime& now, double delaySeconds, double intervalSeconds,
                           bool repeat)
{
    const double delay = std::max(0.0, delaySeconds);
    const auto base = static_cast<std::int64_t>(now.ToUnix());
    const auto offset = static_cast<std::int64_t>(std::llround(delay));
    Schedule(name, DateTime::FromUnix(base + offset), intervalSeconds, repeat);
}

void Scheduler::ScheduleDaily(const std::string& name, const DateTime& now, int hour, int minute, int second)
{
    DateTime today = now;
    today.hour = std::clamp(hour, 0, 23);
    today.minute = std::clamp(minute, 0, 59);
    today.second = std::clamp(second, 0, 59);
    today.millisecond = 0;

    if (!(now < today))
    {
        today = DateTime::FromUnix(today.ToUnix() + 86400);
    }

    Schedule(name, today, 86400.0, true);
}

bool Scheduler::Cancel(const std::string& name)
{
    const auto it = std::remove_if(events_.begin(), events_.end(), [&name](const Event& e) { return e.name == name; });
    if (it == events_.end())
    {
        return false;
    }
    events_.erase(it, events_.end());
    return true;
}

void Scheduler::Clear()
{
    events_.clear();
}

bool Scheduler::Pending(const std::string& name) const
{
    return Find(name) != nullptr;
}

const Scheduler::Event* Scheduler::Find(const std::string& name) const
{
    for (const Event& e : events_)
    {
        if (e.name == name)
        {
            return &e;
        }
    }
    return nullptr;
}

double Scheduler::SecondsUntil(const std::string& name, const DateTime& now) const
{
    const Event* e = Find(name);
    if (!e)
    {
        return -1.0;
    }
    return std::max(0.0, static_cast<double>(e->due.ToUnix() - now.ToUnix()));
}

std::vector<std::string> Scheduler::Poll(const DateTime& now)
{
    std::vector<std::string> fired;

    // Collect due events in due order, but keep the container stable while
    // advancing them.
    std::vector<size_t> dueIndices;
    for (size_t i = 0; i < events_.size(); ++i)
    {
        if (events_[i].due.ToUnix() <= now.ToUnix())
        {
            dueIndices.push_back(i);
        }
    }

    std::sort(dueIndices.begin(), dueIndices.end(),
              [this](size_t a, size_t b)
              {
                  const auto& ea = events_[a];
                  const auto& eb = events_[b];
                  if (ea.due.ToUnix() != eb.due.ToUnix())
                  {
                      return ea.due.ToUnix() < eb.due.ToUnix();
                  }
                  return ea.name < eb.name;
              });

    // Repeating events advance to the first occurrence strictly after `now`.
    // One poll therefore fires once per event even after a long offline period,
    // which is what an "online" event wants: no 72 daily bonuses at startup.
    for (size_t index : dueIndices)
    {
        Event& e = events_[index];
        fired.push_back(e.name);
        if (e.repeat && e.intervalSeconds > 0.0)
        {
            const double behind = static_cast<double>(now.ToUnix() - e.due.ToUnix());
            const double steps = std::floor(behind / e.intervalSeconds) + 1.0;
            const auto next = static_cast<std::int64_t>(e.due.ToUnix() + steps * e.intervalSeconds);
            e.due = DateTime::FromUnix(next);
        }
    }

    // One-shot events are removed after the pass. Erasing by name keeps the
    // indices above valid until the loop is done.
    events_.erase(std::remove_if(events_.begin(), events_.end(),
                                 [&fired](const Event& e)
                                 {
                                     if (e.repeat)
                                     {
                                         return false;
                                     }
                                     return std::find(fired.begin(), fired.end(), e.name) != fired.end();
                                 }),
                  events_.end());

    return fired;
}

} // namespace Fem
