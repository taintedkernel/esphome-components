// https://gist.github.com/mcleary/b0bf4fa88830ff7c882d

#pragma once

#include <chrono>
#include <ctime>
#include <cmath>

typedef std::chrono::duration<int> seconds;
typedef std::chrono::duration<int,std::milli> milliseconds;

class CountdownTimer
{
private:
    std::chrono::time_point<std::chrono::steady_clock> m_start;
    std::chrono::time_point<std::chrono::steady_clock> m_end;
    std::chrono::duration<int,std::milli> m_length;
    std::function<void()> m_func;
    std::function<void(void*)> m_func_data;
    void* m_data = NULL;
    bool m_running = false;
    int m_count;

public:
    void start(std::chrono::duration<int,std::milli> length,
        std::function<void()> func, int count)
    {
        m_length = length;
        m_func = func;
        m_count = count;
        m_start = std::chrono::steady_clock::now();
        m_running = true;
        // TODO: Test for negative count
    }

    void start(std::chrono::duration<int,std::milli> length,
        std::function<void(void*)> func, void* data, int count)
    {
        m_length = length;
        m_func_data = func;
        m_data = data;
        m_count = count;
        m_start = std::chrono::steady_clock::now();
        m_running = true;
        // TODO: Test for negative count
    }

    void stop()
    {
        m_end = std::chrono::steady_clock::now();
        m_running = false;
    }

    bool expired()
    {
        if (std::chrono::steady_clock::now() > m_start + m_length) {
            return true;
        } else {
            return false;
        }
    }

    bool runIfExpired()
    {
        if (!m_running || !expired())
            return false;

        if (m_data != NULL) {
            m_func_data(m_data);
        } else {
            m_func();
        }

        // If single count, stop and return
        // true to flag timer removal
        if (m_count == 1) {
            stop();
            return true;
        }

        // If multiple count, decrement count
        if (m_count > 1) {
            m_count--;
        }

        // Reset counter
        m_start = std::chrono::steady_clock::now();

        return false;
    }

    double elapsedMilliseconds()
    {
        std::chrono::time_point<std::chrono::steady_clock> endTime;

        if(m_running) {
            endTime = std::chrono::steady_clock::now();
        } else {
            endTime = m_end;
        }

        return std::chrono::duration_cast<std::chrono::milliseconds>(endTime - m_start).count();
    }

    double elapsedSeconds()
    {
        return elapsedMilliseconds() / 1000.0;
    }
};