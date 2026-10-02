#pragma once

#include "MainWindow.g.h"
#include "app_settings.h"

#include <deque>

namespace winrt::A2DPWBWinUI::implementation
{
    /* Severity tags matching the CLI tag language (INFO/OK/WARN/ERROR/DATA,
     * docs/dev/PLAN_DESIGN_CONSISTENCY.md Faz A/C). Drove the log-pane colors. */
    enum class LogTag { Info, Ok, Warn, Error, Data };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

        // Event handlers
        void OnScanClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnConnectClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDirectConnectClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnClearLogClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDeviceSelectionChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnCodecChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnQualityChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnRateChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnBitDepthChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnBitrateChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void OnVolumeChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void OnAutoMuteChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAutoConnectChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnReconnectLastClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSetupClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnEnableStreamingClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRestoreBtClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRefreshDriverClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnToggleLogClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        std::unique_ptr<A2dpService> service_;
        AppSettings settings_;
        Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{ nullptr };

        void UpdateUI(const A2dpService::State& state, const std::string& text);
        void UpdateStatusCard(const A2dpService::State& state, const std::string& key);
        void UpdateStatusDevice(const std::string& name, const std::string& mac);
        void AppendLog(const std::string& text);
        void AppendLog(LogTag tag, const std::string& text);
        void AppendLogLine(const std::wstring& text,
                           const Microsoft::UI::Xaml::Media::SolidColorBrush& brush);
        static constexpr size_t kMaxLogLines = 500;
        void RefreshDriverMode();
        void UpdateBitDepthForCodec();
        void ApplyBitrateSnap(int requestedKbps);
        void ApplyDeviceVolumePercent(int pct);
        void StartStream(const std::string& mac, const std::string& name,
                         const std::string& logText);
        void StopStream();
        void SetStreamingUi(bool streaming);
        void RememberLastDevice(const std::string& mac, const std::string& name);
        void ShowSetupGuide();
        void RunDriverSwitch(bool enable_winusb);

        A2dpService::DeviceEntry selectedDevice_{};
        bool hasSelectedDevice_{ false };
        bool uiReady_{ false };
        bool driverBusy_{ false };
        int selectedCodecIndex_{ 0 };
        int selectedQualityIndex_{ 0 };
        int selectedRateIndex_{ 0 };
        int selectedBitDepthIndex_{ 0 };
        int selectedBitrateKbps_{ 0 };
        bool isStreaming_{ false };
        bool updatingVolumeFromDevice_{ false };

        void UpdateStats(const A2dpService::StreamStats& stats);
        void UpdateFreshness();
        void UpdateDeviceCaps();
        void UpdateSparkline();

        /* Plan Faz 2: a stale number must not look live. The core pushes stats
         * ~1 Hz while streaming and nothing when stopped, so this timer ages
         * the freshness label and greys the values out. */
        Microsoft::UI::Dispatching::DispatcherQueueTimer freshness_timer_{ nullptr };
        uint64_t last_stats_tick_ = 0;
        bool stats_are_fresh_{ false };

        std::deque<float> sparkLatency_;
        std::deque<float> sparkError_;
    };
}

namespace winrt::A2DPWBWinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
