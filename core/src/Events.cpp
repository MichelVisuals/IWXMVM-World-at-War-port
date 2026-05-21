#include "StdInclude.hpp"
#include "Events.hpp"
#include "Logger.hpp"

namespace IWXMVM::Events
{
    std::map<EventType, std::vector<std::function<void()>>> listeners;

    void Invoke(const EventType eventType)
    {
        // Per-listener try/catch so one listener's throw doesn't abort the
        // remaining listeners. Without this, on T4 a single unwired dvar
        // crash in VisualsMenu's PostDemoLoad listener silently prevents
        // DemoParser::Run (also a PostDemoLoad listener) from ever running.
        std::size_t idx = 0;
        for (const auto& function : listeners[eventType])
        {
            try
            {
                function();
            }
            catch (const std::exception& e)
            {
                static std::map<std::pair<int, std::size_t>, bool> logged;
                auto key = std::make_pair((int)eventType, idx);
                if (!logged[key])
                {
                    logged[key] = true;
                    LOG_ERROR("Events::Invoke({}) listener[{}] threw std::exception: {} (further silenced)",
                              (int)eventType, idx, e.what());
                }
            }
            catch (...)
            {
                static std::map<std::pair<int, std::size_t>, bool> logged;
                auto key = std::make_pair((int)eventType, idx);
                if (!logged[key])
                {
                    logged[key] = true;
                    LOG_ERROR("Events::Invoke({}) listener[{}] threw non-std exception (further silenced)",
                              (int)eventType, idx);
                }
            }
            ++idx;
        }
    }

    void RegisterListener(const EventType eventType, const std::function<void()> function)
    {
        listeners[eventType].push_back(function);
    }
}  // namespace IWXMVM::Events