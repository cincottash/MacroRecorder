#include "Backend.h"

namespace platform {

#ifdef Q_OS_WIN
Backend* createWindowsBackend(QObject* parent);
#else
Backend* createLinuxBackend(QObject* parent);
#endif

static Backend* (*g_testFactory)(QObject*) = nullptr;
void Backend::setFactoryForTests(Backend* (*factory)(QObject*)) { g_testFactory = factory; }

Backend* Backend::create(QObject* parent)
{
    if (g_testFactory) return g_testFactory(parent);
#ifdef Q_OS_WIN
    return createWindowsBackend(parent);
#else
    return createLinuxBackend(parent);
#endif
}

}  // namespace platform
