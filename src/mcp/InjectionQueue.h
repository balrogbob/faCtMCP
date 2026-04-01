#pragma once

#include <string>
#include <vector>

class InjectionQueue {
public:
    static void SetEnabled(bool enabled);
    static bool IsEnabled();

    static void SetFrequency(int frequency);
    static int GetFrequency();

    static void SetItems(const std::vector<std::string>& items);
    static std::vector<std::string> GetItems();

    static void ResetCounter();
    static int GetCallCounter();
    static std::string ConsumeIfDue();

private:
    InjectionQueue() = delete;
};
