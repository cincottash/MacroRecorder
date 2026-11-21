#include "Backend.h"

namespace platform {

#ifdef Q_OS_WIN
Backend* createWindowsBackend(QObject* parent);
#else
Backend* createLinuxBackend(QObject* parent);
#endif

Backend* Backend::create(QObject* parent)
{
#ifdef Q_OS_WIN
    return createWindowsBackend(parent);
#else
    return createLinuxBackend(parent);
#endif
}

}  // namespace platform
