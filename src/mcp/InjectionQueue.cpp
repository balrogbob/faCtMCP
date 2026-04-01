#include "InjectionQueue.h"

#include <algorithm>
#include <mutex>

namespace {

std::mutex& injection_queue_mutex() {
    static std::mutex mutex;
    return mutex;
}

bool& injection_queue_enabled() {
    static bool enabled = true;
    return enabled;
}

int& injection_queue_frequency() {
    static int frequency = 1;
    return frequency;
}

int& injection_queue_call_counter() {
    static int call_counter = 0;
    return call_counter;
}

std::vector<std::string>& injection_queue_items() {
    static std::vector<std::string> items;
    return items;
}

} // namespace

void InjectionQueue::SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(injection_queue_mutex());
    injection_queue_enabled() = enabled;
}

bool InjectionQueue::IsEnabled() {
    std::lock_guard<std::mutex> lock(injection_queue_mutex());
    return injection_queue_enabled();
}

void InjectionQueue::SetFrequency(int frequency) {
    std::lock_guard<std::mutex> lock(injection_queue_mutex());
    injection_queue_frequency() = std::max(1, frequency);
}

int InjectionQueue::GetFrequency() {
    std::lock_guard<std::mutex> lock(injection_queue_mutex());
    return injection_queue_frequency();
}

void InjectionQueue::SetItems(const std::vector<std::string>& items) {
    std::lock_guard<std::mutex> lock(injection_queue_mutex());
    injection_queue_items() = items;
}

std::vector<std::string> InjectionQueue::GetItems() {
    std::lock_guard<std::mutex> lock(injection_queue_mutex());
    return injection_queue_items();
}

void InjectionQueue::ResetCounter() {
    std::lock_guard<std::mutex> lock(injection_queue_mutex());
    injection_queue_call_counter() = 0;
}

int InjectionQueue::GetCallCounter() {
    std::lock_guard<std::mutex> lock(injection_queue_mutex());
    return injection_queue_call_counter();
}

std::string InjectionQueue::ConsumeIfDue() {
    std::lock_guard<std::mutex> lock(injection_queue_mutex());

    ++injection_queue_call_counter();

    if (!injection_queue_enabled()) {
        return "";
    }

    if (injection_queue_items().empty()) {
        return "";
    }

    const int frequency = std::max(1, injection_queue_frequency());
    if ((injection_queue_call_counter() % frequency) != 0) {
        return "";
    }

    const std::string next_item = injection_queue_items().front();
    injection_queue_items().erase(injection_queue_items().begin());
    return next_item;
}
