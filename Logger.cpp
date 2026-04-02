#include "Logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

#include "Config.h"

namespace WBNG {
    namespace Log {

        namespace {
            std::mutex g_logMutex;
            std::ofstream g_logFile;
            bool g_initialized = false;

            std::string CurrentTimestamp() {
                const auto now = std::chrono::system_clock::now();
                const auto timeT = std::chrono::system_clock::to_time_t(now);

                std::tm localTime{};
#ifdef _WIN32
                localtime_s(&localTime, &timeT);
#else
                localtime_r(&timeT, &localTime);
#endif

                std::ostringstream oss;
                oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
                return oss.str();
            }

            void Write(std::string_view level, std::string_view message) {
                if (!g_config.enableLogging || !g_initialized || !g_logFile.is_open()) {
                    return;
                }

                std::lock_guard<std::mutex> lock(g_logMutex);
                g_logFile << "[" << CurrentTimestamp() << "] "
                          << "[" << level << "] " << message << '\n';
                g_logFile.flush();
            }
        }  // namespace

        void Init() {
            if (g_initialized) {
                return;
            }

            try {
                std::filesystem::create_directories("Data\\SKSE\\Plugins");
                g_logFile.open("Data\\SKSE\\Plugins\\WeatherBehaviorNG.log", std::ios::out | std::ios::trunc);
                g_initialized = g_logFile.is_open();
            } catch (...) {
                g_initialized = false;
            }

            if (g_initialized && g_config.enableLogging) {
                Write("INFO", "Logging initialized");
            }
        }

        void Shutdown() {
            if (!g_initialized) {
                return;
            }

            if (g_config.enableLogging) {
                Write("INFO", "Logging shutdown");
            }

            std::lock_guard<std::mutex> lock(g_logMutex);
            if (g_logFile.is_open()) {
                g_logFile.close();
            }
            g_initialized = false;
        }

        void Info(std::string_view message) { Write("INFO", message); }

        void Warn(std::string_view message) { Write("WARN", message); }

        void Error(std::string_view message) { Write("ERROR", message); }

        void Debug(std::string_view message) { Write("DEBUG", message); }

        bool IsEnabled() { return g_config.enableLogging && g_initialized; }

    }  // namespace Log
}  // namespace WBNG