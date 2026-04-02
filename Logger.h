#pragma once

#include <sstream>
#include <string_view>

namespace WBNG {
    namespace Log {

        void Init();
        void Shutdown();

        void Info(std::string_view message);
        void Warn(std::string_view message);
        void Error(std::string_view message);
        void Debug(std::string_view message);

        bool IsEnabled();

    }  // namespace Log
}  // namespace WBNG

#define WBNG_LOG_INFO(expr)              \
    do {                                 \
        std::ostringstream _wbng_os;     \
        _wbng_os << expr;                \
        WBNG::Log::Info(_wbng_os.str()); \
    } while (false)

#define WBNG_LOG_WARN(expr)              \
    do {                                 \
        std::ostringstream _wbng_os;     \
        _wbng_os << expr;                \
        WBNG::Log::Warn(_wbng_os.str()); \
    } while (false)

#define WBNG_LOG_ERROR(expr)              \
    do {                                  \
        std::ostringstream _wbng_os;      \
        _wbng_os << expr;                 \
        WBNG::Log::Error(_wbng_os.str()); \
    } while (false)

#define WBNG_LOG_DEBUG(expr)              \
    do {                                  \
        std::ostringstream _wbng_os;      \
        _wbng_os << expr;                 \
        WBNG::Log::Debug(_wbng_os.str()); \
    } while (false)