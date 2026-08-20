#include "app/Application.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    fam::Application app;
    return app.Run();
}
#else
int main(int, char**) {
    fam::Application app;
    return app.Run();
}
#endif
