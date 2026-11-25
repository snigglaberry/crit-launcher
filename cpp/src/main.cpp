// Crit Launcher Native — entry point.
#include "app/app.h"
#include "platform/platform.h"

#ifdef _WIN32
#include <windows.h>
#include <shellscalingapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    crit::App app;
    int code = app.Run();
    return code;
}
#else
int main(int argc, char** argv) {
    crit::App app;
    return app.Run();
}
#endif
