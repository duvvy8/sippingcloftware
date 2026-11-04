#include <QtCore>
#include <windows.h>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto args = app.arguments();
    if (args.size() != 3)
        return 2;
    auto name = args[2].toStdWString();
    HANDLE release = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
    if (!release)
        return 3;
    auto close = qScopeGuard([&] { CloseHandle(release); });
    HWND window = CreateWindowExW(0, L"STATIC", L"Clipboard verification", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window)
        return 4;
    auto destroy = qScopeGuard([&] { DestroyWindow(window); });
    bool opened = false;
    for (int i = 0; i < 50 && !opened; ++i) {
        opened = OpenClipboard(window) != FALSE;
        if (!opened)
            Sleep(50);
    }
    if (!opened)
        return 5;
    auto unlock = qScopeGuard([] { CloseClipboard(); });
    QSaveFile ready(args[1]);
    if (!ready.open(QIODevice::WriteOnly))
        return 6;
    ready.write("{\"locked\":true}");
    if (!ready.commit())
        return 7;
    return WaitForSingleObject(release, 30000) == WAIT_OBJECT_0 ? 0 : 8;
}
