#pragma once
// What the bridge core expects from its host firmware: wrap-safe elapsed time and a mutex.
#include <cstdint>
#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#elif defined(NRF52_PLATFORM)
#include <Arduino.h>
#else
#include <mutex>
#endif

class Throttle
{
  public:
    static bool isWithinTimespanMsAt(uint32_t lastMs, uint32_t intervalMs, uint32_t nowMs)
    {
        return (nowMs - lastMs) < intervalMs;
    }
};

namespace concurrency
{
#if defined(ESP32) || defined(NRF52_PLATFORM)
class Lock
{
  public:
    Lock() : handle(xSemaphoreCreateMutex()) {}
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
    void lock() { xSemaphoreTake(handle, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(handle); }

  private:
    SemaphoreHandle_t handle;
};
#else
using Lock = std::mutex;
#endif

class LockGuard
{
  public:
    explicit LockGuard(Lock *lock) : held(lock) { held->lock(); }
    ~LockGuard() { held->unlock(); }
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

  private:
    Lock *held;
};
} // namespace concurrency
