#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::A2DPWBWinUI::implementation
{
    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        // Redirect stderr to log file for BTstack/debug output
        std::string logPath = []() {
            char* appdata = nullptr;
            size_t len = 0;
            if (_dupenv_s(&appdata, &len, "APPDATA") != 0 || !appdata) return std::string("debug.log");
            std::string path = std::string(appdata) + "\\A2DPWB\\winui_debug.log";
            free(appdata);
            return path;
        }();
        freopen(logPath.c_str(), "w", stderr);

        window = make<MainWindow>();

        // Reasonable default size so the right-hand panels aren't clipped
        try {
            auto appWindow = window.AppWindow();
            appWindow.Resize({ 1100, 760 });
        } catch (...) {}

        window.Activate();
    }
}
