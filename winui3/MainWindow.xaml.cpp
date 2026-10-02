#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "driver_mode.h"
#include "driver_switch.h"
#include "ssc_encoder.h"

#include <thread>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace
{
    std::string WToA(hstring const& ws)
    {
        if (ws.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        std::string result(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), result.data(), len, nullptr, nullptr);
        return result;
    }

    hstring AToW(const std::string& s)
    {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring result(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), result.data(), len);
        return hstring(result);
    }

    /* Severity brush lookup from the app ResourceDictionary (App.xaml tokens).
     * Returns null for missing keys so callers can fall back to the default text color. */
    Microsoft::UI::Xaml::Media::SolidColorBrush TagBrush(const wchar_t* key)
    {
        try {
            auto resources = Application::Current().Resources();
            auto value = resources.TryLookup(winrt::box_value(key));
            if (value) return value.as<Microsoft::UI::Xaml::Media::SolidColorBrush>();
        } catch (...) {}
        return nullptr;
    }

    /* ------------------------------------------------------------------
     * User-facing copy for the status card (docs/dev/PLAN_UX_DESIGN.md Faz 1).
     *
     * A2dpService::notify_state() hands the UI localization *keys*
     * ("status.ready", "error.pairing_hint", ...), not prose. Showing them
     * verbatim meant the app said "status.connected" to the user. Every key now
     * maps to three fields:
     *   headline - the state, in one or two words
     *   detail   - the technical term PLUS a plain-language explanation
     *   action   - the single most useful next step ("do this, then that")
     * The raw key is still written to the activity log for diagnostics, so
     * nothing is lost.
     *
     * Phase copy for a healthy transition is picked by the State enum; the
     * per-key entries below only refine it (or override it for errors).
     * ------------------------------------------------------------------ */
    /* Severity of a status-card update. Mirrors implementation::LogTag but lives
     * at file scope so the copy table does not depend on the implementation
     * namespace being declared first. */
    enum class Tone { Info, Ok, Warn, Error };

    struct StateCopy {
        const char* headline;
        const char* detail;
        const char* action;   /* "" -> the action line is hidden */
        wchar_t glyph;        /* single Segoe Fluent Icons code point */
        Tone tone;
    };

    /* Segoe Fluent Icons glyphs: color is never the only signal (Faz 1).
     * Info / Sync / Accept (checkmark) / Error / Warning. */
    constexpr wchar_t kGlyphInfo    = 0xE946;
    constexpr wchar_t kGlyphSync    = 0xE895;
    constexpr wchar_t kGlyphCheck   = 0xE73E;
    constexpr wchar_t kGlyphError   = 0xE783;
    constexpr wchar_t kGlyphWarning = 0xE7BA;

    const StateCopy& CopyForKey(const std::string& key)
    {
        static const StateCopy kUnknown{
            "Working on it",
            "The app is busy setting things up.",
            "", kGlyphSync, Tone::Info };

        static const std::pair<const char*, StateCopy> kTable[] = {
            /* ---- healthy transitions ---- */
            { "status.ready",
              { "Ready", "No device connected yet.",
                "Press 'Scan Devices' to look for headsets, or 'Direct Connect' if you know the address.",
                kGlyphInfo, Tone::Info } },
            { "status.initializing",
              { "Starting up", "Bringing up the Bluetooth stack.",
                "", kGlyphSync, Tone::Info } },
            { "status.initializing_btstack",
              { "Starting the dongle",
                "Opening the USB Bluetooth dongle and loading its firmware.",
                "This takes a second. Keep the dongle plugged in.", kGlyphSync, Tone::Info } },
            { "status.connecting_device",
              { "Pairing with the headset", "Negotiating the A2DP profile.",
                "Keep the headset close, powered on and out of the case.", kGlyphSync, Tone::Info } },
            { "status.initializing_audio",
              { "Starting audio", "Opening Windows loopback capture and the audio encoder.",
                "", kGlyphSync, Tone::Info } },
            { "status.connected",
              { "Connected", "The headset accepted the connection.",
                "", kGlyphCheck, Tone::Ok } },
            { "status.reconnected",
              { "Reconnected", "The headset link came back up.",
                "", kGlyphCheck, Tone::Ok } },
            { "status.connection_lost",
              { "Connection lost", "The headset link dropped.",
                "Reconnect, or move closer to the dongle.", kGlyphWarning, Tone::Warn } },

            /* ---- actionable errors ---- */
            { "error.invalid_address",
              { "Check the address",
                "That Bluetooth address is not in the expected XX:XX:XX:XX:XX:XX form.",
                "Fix the address, then press 'Direct Connect'.", kGlyphError, Tone::Error } },
            { "error.pairing_hint",
              { "Headset not found",
                "The headset did not answer the scan. It may be switched off, already connected to "
                "another device, or out of pairing mode.",
                "Put both earbuds in the case, hold them until the LED blinks, then press "
                "'Scan Devices' again.", kGlyphError, Tone::Error } },
            { "error.no_compatible_codec",
              { "No compatible codec",
                "This headset does not offer a codec the app can use (SSC, AAC or SBC), so it cannot "
                "be streamed to.",
                "Try another codec under 'Audio settings'. If none work, this headset is not supported.",
                kGlyphError, Tone::Error } },
            { "error.btstack_init",
              { "Dongle not available",
                "The USB Bluetooth dongle could not be opened, so no connection is possible.",
                "Open 'Troubleshooting' and check the dongle shows 'WinUSB (Streaming mode)'. "
                "If not, press 'Enable Streaming (WinUSB)'.", kGlyphError, Tone::Error } },
            { "error.codec_configure",
              { "Codec rejected",
                "The headset refused the codec configuration - usually an unsupported bitrate or "
                "sample rate for this device.",
                "Set bitrate to 0 (auto), pick sample rate 48k, then reconnect.", kGlyphError, Tone::Error } },
            { "error.encoder_init",
              { "Encoder could not start",
                "The audio encoder (Samsung SSC) failed to initialise, so no audio can be produced.",
                "Check the activity log for the reason. The SSC encoder needs WSL2 (or --ssc-native "
                "Qiling) running.", kGlyphError, Tone::Error } },
            { "error.stream_start",
              { "Could not start audio",
                "The headset accepted the connection but the audio stream would not start.",
                "Disconnect and reconnect. If it repeats, unplug and replug the dongle.",
                kGlyphError, Tone::Error } },
            { "error.wasapi_init",
              { "Audio capture failed",
                "Windows loopback capture could not be opened, so there is no audio to stream.",
                "Check that the default playback device is active and its format is set to 48 kHz.",
                kGlyphError, Tone::Error } },
            { "error.encode_thread",
              { "Encoder stopped",
                "The thread that encodes and sends audio stopped unexpectedly.",
                "Read the activity log, then reconnect.", kGlyphError, Tone::Error } },
            { "error.reconnect_failed",
              { "Reconnect failed",
                "Automatic reconnection gave up after several attempts.",
                "Press 'Reconnect Last', or scan again if the headset was reset.", kGlyphError, Tone::Error } },
            { "error.btstack_crash_scan",
              { "Bluetooth stack stopped while scanning",
                "The Bluetooth stack stopped unexpectedly during the device scan.",
                "Unplug and replug the dongle, then scan again.", kGlyphError, Tone::Error } },
            { "error.btstack_crash_stream",
              { "Bluetooth stack stopped while streaming",
                "The Bluetooth stack stopped unexpectedly, so audio stopped.",
                "Unplug and replug the dongle, then reconnect.", kGlyphError, Tone::Error } },
            { "error.aac_unavailable",
              { "AAC not available",
                "This build has no AAC encoder, so the stream falls back to SBC.",
                "Prefer SSC (Samsung Scalable Codec) for the best quality on Galaxy earbuds.",
                kGlyphWarning, Tone::Warn } },
            { "error.virtual_capture_init",
              { "Virtual audio device unavailable",
                "The virtual audio endpoint needed to capture microphone input could not be created.",
                "Install the virtual output driver, or switch the capture mode to loopback.",
                kGlyphError, Tone::Error } },
        };

        for (const auto& entry : kTable) {
            if (key == entry.first) return entry.second;
        }
        return kUnknown;
    }

    /* Base copy for a State enum value; per-key text from CopyForKey()
     * overrides detail/action when the service sent a known key. */
    StateCopy BaseCopyForState(A2dpService::State state)
    {
        switch (state) {
        case A2dpService::State::Connecting:
            return { "Connecting...", "Talking to the headset.",
                     "Keep the headset close and powered on.", kGlyphSync, Tone::Info };
        case A2dpService::State::Streaming:
            return { "Streaming audio", "Windows audio is on its way to the headset.",
                     "", kGlyphCheck, Tone::Ok };
        case A2dpService::State::Reconnecting:
            return { "Reconnecting...", "The link dropped; retrying automatically.",
                     "If this keeps happening, press 'Reconnect Last'.", kGlyphSync, Tone::Warn };
        case A2dpService::State::Error:
            return { "Something went wrong", "The last operation did not finish.",
                     "See the activity log below for the raw error.", kGlyphError, Tone::Error };
        case A2dpService::State::Idle:
        default:
            return { "Ready", "Not connected to a device.",
                     "Press 'Scan Devices' to look for headsets.", kGlyphInfo, Tone::Info };
        }
    }
}

namespace winrt::A2DPWBWinUI::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        VersionCreditText().Text(L"v" + AToW(std::string(APP_VERSION)) + L" \u00B7 by Salvetum");

        // Enable Mica backdrop (Windows 11 translucent material)
        try {
            auto mica = Microsoft::UI::Xaml::Media::MicaBackdrop();
            this->SystemBackdrop(mica);
        } catch (...) {}

        dispatcher_ = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

        service_ = std::make_unique<A2dpService>();

        // Load settings and saved devices (mirrors wxWidgets MainFrame init)
        settings_.load();
        service_->load_saved_devices();
        service_->set_bt_chip_pid(settings_.bt_chip_pid);
        service_->set_bt_chip_fw_stem(settings_.bt_chip_fw_stem);
        service_->set_debug_mode(settings_.debug_mode);
        service_->set_auto_mute_output(true);
        AutoMuteCheck().IsChecked(true);

        // State callback (worker thread â†’ UI)
        service_->set_state_callback([this](A2dpService::State state, const std::string& text) {
            auto captured = this;
            dispatcher_.TryEnqueue([captured, state, text]() {
                captured->UpdateUI(state, text);
            });
        });

        // Stream info callback (worker thread â†’ UI)
        service_->set_stream_info_callback([this](const A2dpService::StreamInfo& info) {
            auto captured = this;
            dispatcher_.TryEnqueue([captured, info]() {
                captured->CodecText().Text(L"Codec: " + AToW(info.codec));
                captured->BitrateText().Text(L"Bitrate: " + AToW(std::to_string(info.bitrate_kbps) + " kbps"));
                captured->SampleRateText().Text(L"Sample Rate: " + AToW(std::to_string(info.sample_rate) + " Hz"));
                captured->ChannelsText().Text(L"Channels: " + AToW(std::to_string(info.channels)));

                if (info.source_sample_rate > 0) {
                    std::string src = std::to_string(info.source_sample_rate) + " Hz, "
                                    + std::to_string(info.source_channels) + " ch, "
                                    + std::to_string(info.source_bit_depth) + " bit";
                    captured->SourceInfoText().Text(L"Source: " + AToW(src));
                }
                /* Faz 1: explain what the numbers mean, not just show them. */
                std::string hint;
                if (info.sample_rate >= 96000) {
                    hint = "96 kHz UHQ is upsampled from 48 kHz loopback capture; the headset must "
                           "advertise UHQ support or you will hear silence.";
                } else if (info.source_sample_rate > 0 &&
                           info.source_sample_rate != info.sample_rate) {
                    hint = "Capture and stream rates differ, so the audio is resampled before encoding.";
                } else {
                    hint = "Loopback capture is taken before the Windows volume mixer, so the speaker "
                           "volume slider does not change what the headset plays.";
                }
                if (auto h = captured->AudioStatusHintText()) h.Text(AToW(hint));
            });
        });

        // Live stats callback (worker thread â†’ UI, ~2 s cadence)
        service_->set_stats_callback([this](const A2dpService::StreamStats& stats) {
            auto captured = this;
            dispatcher_.TryEnqueue([captured, stats]() {
                captured->UpdateStats(stats);
            });
        });

        // Device volume callback (AVRCP absolute volume, worker thread â†’ UI)
        service_->set_volume_changed_callback([this](uint8_t vol) {
            auto captured = this;
            int pct = (static_cast<int>(vol) * 100 + 63) / 127;
            dispatcher_.TryEnqueue([captured, pct]() {
                captured->ApplyDeviceVolumePercent(pct);
            });
        });

        // Scan complete callback (worker thread â†’ UI)
        service_->set_scan_complete_callback([this]() {
            auto captured = this;
            dispatcher_.TryEnqueue([captured]() {
                auto devices = captured->service_->get_devices();
                captured->ScanButton().IsEnabled(true);
                if (devices.empty()) {
                    captured->AppendLog(LogTag::Warn, "No devices found.");
                } else {
                    captured->AppendLog(LogTag::Ok, "Found " + std::to_string(devices.size()) + " device(s).");
                    for (const auto& dev : devices) {
                        captured->DeviceListView().Items().Append(
                            winrt::box_value(AToW(dev.name + " (" + dev.addr_str + ")")));
                    }
                }
            });
        });

        AppendLog(LogTag::Data, "Settings loaded. Chip PID: 0x" + [&]() {
            char buf[8]; snprintf(buf, sizeof(buf), "%04X", settings_.bt_chip_pid); return std::string(buf);
        }() + ", FW stem: " + (settings_.bt_chip_fw_stem.empty() ? "(none)" : settings_.bt_chip_fw_stem) + ". Ready.");

        // Last-device / auto-connect UI state (persisted in settings.json)
        AutoConnectCheck().IsChecked(settings_.auto_connect_on_start);
        ReconnectLastButton().IsEnabled(!settings_.last_device_mac.empty());
        UpdateStatusDevice(settings_.last_device_name, settings_.last_device_mac);

        // Detect the dongle's driver mode (WinUSB vs BTHUSB) once at startup
        RefreshDriverMode();

        // Normalize bit-depth control after UI is fully loaded
        uiReady_ = true;
        UpdateBitDepthForCodec();

        // Auto-connect (plan section 3): resume the last device at launch
        if (settings_.auto_connect_on_start && !settings_.last_device_mac.empty()) {
            auto st = detect_dongle_driver(0x2357, 0x0604);
            if (st.mode != DongleDriverMode::WinUsbStream) {
                AppendLog(LogTag::Warn, "Auto-connect skipped: dongle is not in WinUSB (Streaming) mode. "
                          "Click 'Enable Streaming (WinUSB)' in the Streaming Mode panel.");
            } else {
                AppendLog("Auto-connecting to last device " + settings_.last_device_name +
                          " [" + settings_.last_device_mac + "]...");
                StartStream(settings_.last_device_mac, settings_.last_device_name, "Auto-connect");
            }
        }
    }

    MainWindow::~MainWindow()
    {
        if (service_) {
            service_->stop_streaming();
        }
    }

    void MainWindow::OnScanClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!service_ || service_->is_scanning()) return;

        AppendLog("Scanning for Bluetooth audio devices (BTstack inquiry ~8s)...");
        ScanButton().IsEnabled(false);
        DeviceListView().Items().Clear();
        hasSelectedDevice_ = false;
        ConnectButton().IsEnabled(false);

        service_->start_scan();
        // Scan result handled by scan_complete_callback (wired in constructor)
    }

    void MainWindow::OnConnectClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!service_) return;

        if (isStreaming_) {
            StopStream();
            return;
        }

        if (!hasSelectedDevice_) {
            AppendLog(LogTag::Warn, "No device selected. Use 'Scan Devices' or 'Direct Connect'.");
            return;
        }

        ConnectionProfile profile{};
        profile.device_address = selectedDevice_.addr_str;
        profile.device_name = selectedDevice_.name;
        profile.codec = ProfileManager::index_to_codec(selectedCodecIndex_);
        profile.quality = ProfileManager::index_to_quality(selectedQualityIndex_);
        profile.bitrate_kbps = static_cast<uint32_t>(selectedBitrateKbps_);
        profile.bit_depth = ProfileManager::index_to_bit_depth(selectedBitDepthIndex_);
        profile.capture_mode = "loopback";
        profile.sample_rate = (selectedRateIndex_ == 1) ? 96000 : 0; /* 0 = auto (48k on this rig) */

        AppendLog("Connecting to " + selectedDevice_.name + " [" + profile.codec + "]...");
        RememberLastDevice(profile.device_address, profile.device_name);

        service_->start_streaming(profile);
        SetStreamingUi(true);
    }

    void MainWindow::OnDirectConnectClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!service_) return;

        if (isStreaming_) {
            StopStream();
            return;
        }

        // Read MAC from text box
        auto macHstring = MacAddressBox().Text();
        std::string mac = WToA(macHstring);
        if (mac.empty() || mac.size() < 17) {
            AppendLog(LogTag::Error, "Invalid MAC address.");
            return;
        }

        // Validate MAC format
        unsigned int a[6];
        if (sscanf_s(mac.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
                     &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
            AppendLog(LogTag::Error, "Invalid MAC format. Use XX:XX:XX:XX:XX:XX");
            return;
        }

        // Convert to uppercase
        for (auto& c : mac) c = (char)toupper((unsigned char)c);

        ConnectionProfile profile{};
        profile.device_address = mac;
        profile.device_name = "Buds3 FE";
        profile.codec = ProfileManager::index_to_codec(selectedCodecIndex_);
        profile.quality = ProfileManager::index_to_quality(selectedQualityIndex_);
        profile.bitrate_kbps = static_cast<uint32_t>(selectedBitrateKbps_);
        profile.bit_depth = ProfileManager::index_to_bit_depth(selectedBitDepthIndex_);
        profile.capture_mode = "loopback";
        profile.sample_rate = (selectedRateIndex_ == 1) ? 96000 : 0; /* 0 = auto (48k on this rig) */

        AppendLog("Direct connect to " + mac + " [" + profile.codec + "]...");
        RememberLastDevice(profile.device_address, profile.device_name);

        service_->start_streaming(profile);
        SetStreamingUi(true);
    }

    void MainWindow::OnClearLogClick(IInspectable const&, RoutedEventArgs const&)
    {
        LogLines().Children().Clear();
    }

    void MainWindow::OnDeviceSelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        if (!uiReady_ || !service_) return;
        auto devices = service_->get_devices();
        int idx = DeviceListView().SelectedIndex();

        if (idx >= 0 && idx < (int)devices.size()) {
            selectedDevice_ = devices[idx];
            hasSelectedDevice_ = true;
            ConnectButton().IsEnabled(true);
            UpdateStatusDevice(selectedDevice_.name, selectedDevice_.addr_str);
            AppendLog(LogTag::Data, "Selected: " + selectedDevice_.name);
        } else {
            hasSelectedDevice_ = false;
            ConnectButton().IsEnabled(false);
            UpdateStatusDevice(settings_.last_device_name, settings_.last_device_mac);
        }
    }

    void MainWindow::OnCodecChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        selectedCodecIndex_ = CodecCombo().SelectedIndex();
        if (uiReady_) UpdateBitDepthForCodec();
    }

    void MainWindow::OnQualityChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        selectedQualityIndex_ = QualityCombo().SelectedIndex();
    }

    void MainWindow::OnRateChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        selectedRateIndex_ = RateCombo().SelectedIndex();
        /* Re-snap the bitrate to the new mode's valid set (48k vs 96k UHQ).
         * Guarded: during XBF load IsSelected="True" fires this before the
         * BitrateSlider/BitrateValueText elements are connected. */
        if (uiReady_) ApplyBitrateSnap(selectedBitrateKbps_);
    }

    void MainWindow::OnBitDepthChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        selectedBitDepthIndex_ = BitDepthCombo().SelectedIndex();
    }

    void MainWindow::UpdateBitDepthForCodec()
    {
        // Codec-based bit-depth display (sample width is fixed per codec):
        //   SSC     : fixed int32 (daemon wire protocol)
        //   AAC, SBC: fixed int16
        switch (selectedCodecIndex_) {
        case 0: /* SSC */
            BitDepthCombo().IsEnabled(false);
            BitDepthCombo().SelectedIndex(3); /* forced 32-bit */
            BitDepthHintText().Text(L"32-bit (int32, fixed)");
            break;
        case 1: case 2: /* AAC, SBC */
            BitDepthCombo().IsEnabled(false);
            BitDepthCombo().SelectedIndex(1); /* forced 16-bit */
            BitDepthHintText().Text(L"16-bit (int16, fixed)");
            break;
        default:
            BitDepthCombo().IsEnabled(true);
            BitDepthHintText().Text(L"resolved per codec");
            break;
        }
    }

    void MainWindow::OnBitrateChanged(IInspectable const&, Controls::Primitives::RangeBaseValueChangedEventArgs const& e)
    {
        if (!uiReady_) return;
        ApplyBitrateSnap(static_cast<int>(e.NewValue()));
    }

    void MainWindow::ApplyBitrateSnap(int requestedKbps)
    {
        /* SSC bitrates are mode-gated; snap so the slider can never hand the
         * blob an unsupported value (which garbles audio). */
        uint32_t rate = (selectedRateIndex_ == 1) ? 96000 : 48000;
        int snapped = static_cast<int>(SscEncoder::snap_bitrate_kbps(
            static_cast<uint32_t>(requestedKbps), rate));
        selectedBitrateKbps_ = snapped;
        /* Null-safe: these elements may not be connected yet during XBF load. */
        if (auto slider = BitrateSlider()) {
            if (static_cast<int>(slider.Value()) != snapped)
                slider.Value(snapped);
        }
        if (auto text = BitrateValueText()) {
            text.Text(AToW(snapped == 0 ? std::string("auto")
                                        : std::to_string(snapped) + " kbps"));
        }
    }

    void MainWindow::OnVolumeChanged(IInspectable const&, Controls::Primitives::RangeBaseValueChangedEventArgs const& e)
    {
        if (!uiReady_) return;
        int pct = static_cast<int>(e.NewValue());
        if (auto text = VolumeValueText())
            text.Text(AToW(std::to_string(pct) + "%"));
        /* Ignore the echo produced while mirroring a device-initiated change. */
        if (updatingVolumeFromDevice_) return;
        if (service_)
            service_->set_device_volume(static_cast<float>(pct) / 100.0f);
    }

    void MainWindow::ApplyDeviceVolumePercent(int pct)
    {
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        updatingVolumeFromDevice_ = true;
        if (auto slider = VolumeSlider())
            slider.Value(pct);
        if (auto text = VolumeValueText())
            text.Text(AToW(std::to_string(pct) + "%"));
        updatingVolumeFromDevice_ = false;
    }

    void MainWindow::OnAutoMuteChanged(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (service_) {
            bool enabled = AutoMuteCheck().IsChecked().Value();
            service_->set_auto_mute_output(enabled);
            AppendLog(LogTag::Data, enabled ? "Auto-mute output: ON (speakers muted while streaming)"
                                            : "Auto-mute output: OFF");
        }
    }

    void MainWindow::OnAutoConnectChanged(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        settings_.auto_connect_on_start = AutoConnectCheck().IsChecked().Value();
        settings_.save();
        AppendLog(LogTag::Data, settings_.auto_connect_on_start ? "Auto-connect on start: ON"
                                                              : "Auto-connect on start: OFF");
    }

    void MainWindow::RememberLastDevice(const std::string& mac, const std::string& name)
    {
        settings_.last_device_mac = mac;
        settings_.last_device_name = name;
        settings_.save();
        ReconnectLastButton().IsEnabled(true);
        UpdateStatusDevice(name, mac);
    }

    void MainWindow::SetStreamingUi(bool streaming)
    {
        isStreaming_ = streaming;
        ConnectButton().Content(winrt::box_value(streaming ? L"Disconnect" : L"Connect"));
        DirectConnectButton().Content(winrt::box_value(streaming ? L"Disconnect" : L"Direct Connect"));
    }

    void MainWindow::StopStream()
    {
        if (!service_) return;
        service_->stop_streaming();
        SetStreamingUi(false);
        AppendLog(LogTag::Ok, "Disconnected.");
    }

    void MainWindow::StartStream(const std::string& mac, const std::string& name, const std::string& logText)
    {
        if (!service_ || isStreaming_) {
            if (isStreaming_) AppendLog(LogTag::Warn, "Disconnect first.");
            return;
        }

        ConnectionProfile profile{};
        profile.device_address = mac;
        profile.device_name = name;
        profile.codec = ProfileManager::index_to_codec(selectedCodecIndex_);
        profile.quality = ProfileManager::index_to_quality(selectedQualityIndex_);
        profile.bitrate_kbps = static_cast<uint32_t>(selectedBitrateKbps_);
        profile.bit_depth = ProfileManager::index_to_bit_depth(selectedBitDepthIndex_);
        profile.capture_mode = "loopback";
        profile.sample_rate = (selectedRateIndex_ == 1) ? 96000 : 0;

        AppendLog(logText + " -> " + name + " [" + profile.codec + "]...");
        RememberLastDevice(mac, name);

        service_->start_streaming(profile);
        SetStreamingUi(true);
    }

    void MainWindow::OnReconnectLastClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!service_) return;
        if (settings_.last_device_mac.empty()) {
            AppendLog(LogTag::Warn, "No previous device. Connect once via Scan/Direct Connect first.");
            return;
        }
        StartStream(settings_.last_device_mac, settings_.last_device_name, "Reconnecting");
    }

    void MainWindow::OnSetupClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowSetupGuide();
    }

    void MainWindow::ShowSetupGuide()
    {
        auto dialog = ContentDialog();
        dialog.Title(winrt::box_value(L"Setup & Help"));

        auto scroll = ScrollViewer();
        scroll.MaxHeight(520);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);

        auto stack = StackPanel();
        stack.Spacing(8);

        auto heading = [](const std::wstring& t) {
            auto tb = TextBlock();
            tb.Text(t);
            tb.FontWeight(winrt::Windows::UI::Text::FontWeight{ 600 });
            return tb;
        };
        auto para = [](const std::wstring& t) {
            auto tb = TextBlock();
            tb.Text(t);
            tb.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
            tb.Margin(winrt::Microsoft::UI::Xaml::Thickness{ 0.0, 0.0, 0.0, 4.0 });
            return tb;
        };

        stack.Children().Append(heading(L"How streaming works"));
        stack.Children().Append(para(L"SSC On Windows streams Windows audio over a USB Bluetooth dongle "
                                     L"(TP-Link UB500 / Realtek RTL8761B) with BTstack in USB (WinUSB) mode. "
                                     L"This bypasses the Windows Bluetooth stack entirely."));
        stack.Children().Append(para(L"Windows Bluetooth is NOT active while the dongle is in Streaming mode. "
                                     L"Use the two buttons in the 'Streaming Mode (Dongle)' panel to switch."));

        stack.Children().Append(heading(L"First-time setup"));
        stack.Children().Append(para(L"1) Connect the dongle. If Windows shows it as a Bluetooth adapter "
                                     L"(BTHUSB), click 'Enable Streaming (WinUSB)' and approve the UAC prompt."));
        stack.Children().Append(para(L"2) Put the Buds3 FE into pairing mode (hold both earbuds until the LED blinks)."));
        stack.Children().Append(para(L"3) Click 'Scan Devices' (about 8 s), then select your buds in the list."));
        stack.Children().Append(para(L"4) Pick a codec (SSC recommended for Buds3 FE; AAC/SBC fall back if unsupported) and click 'Connect'."));
        stack.Children().Append(para(L"5) Optional: tick 'Auto-connect last device on start' to resume streaming at launch."));

        stack.Children().Append(heading(L"Troubleshooting / FAQ"));
        stack.Children().Append(para(L"Q: No sound?  A: Verify the dongle state shows 'WinUSB (Streaming mode)' and a source "
                                     L"is playing. Windows output volume does not change the captured mix."));
        stack.Children().Append(para(L"Q: Reconnect fails?  A: 'Reconnect Last' reuses the saved link key. If the buds were "
                                     L"reset, delete %APPDATA%\\A2DPWB\\profiles.json so a fresh pair happens."));
        stack.Children().Append(para(L"Q: SSC bitrate?  A: 48 kHz High 229 / Standard 192 / Mobile 128 kbps; "
                                     L"'96k UHQ' upsamples to 96 kHz (250 / 442 / 584 kbps). Note: UHQ needs "
                                     L"the device to advertise the SSC UHQ capability bit - if it does not "
                                     L"(e.g. Buds3 FE), the app falls back to 48 kHz."));
        stack.Children().Append(para(L"Q: Back to normal Windows Bluetooth?  A: Click 'Restore Windows BT (BTHUSB)' "
                                     L"in the Streaming Mode panel (same PnP automation, no manual Zadig/Device Manager)."));

        scroll.Content(stack);
        dialog.Content(scroll);
        dialog.PrimaryButtonText(L"Close");
        dialog.XamlRoot(this->Content().XamlRoot());
        dialog.ShowAsync();
    }

    void MainWindow::RunDriverSwitch(bool enable_winusb)
    {
        if (driverBusy_) return;
        if (isStreaming_) {
            AppendLog(LogTag::Warn, "Stop streaming before switching the dongle driver.");
            return;
        }

        driverBusy_ = true;
        EnableStreamingButton().IsEnabled(false);
        RestoreBtButton().IsEnabled(false);
        AppendLog(enable_winusb ? "Switching dongle to WinUSB (Streaming)... a UAC prompt will appear."
                                : "Restoring dongle to BTHUSB (Windows Bluetooth)... a UAC prompt will appear.");

        std::thread([this, enable_winusb]() {
            auto rep = set_dongle_winusb(0x2357, 0x0604, enable_winusb);
            dispatcher_.TryEnqueue([this, rep, enable_winusb]() {
                driverBusy_ = false;
                const char* action = enable_winusb ? "Enable Streaming" : "Restore Windows BT";
                switch (rep.result) {
                case DongleSwitchResult::Ok:
                case DongleSwitchResult::NoChange:
                    AppendLog(LogTag::Ok, std::string(action) + ": " + rep.message);
                    break;
                default:
                    AppendLog(LogTag::Error, std::string(action) + " failed: " + rep.message);
                    break;
                }
                RefreshDriverMode();
                EnableStreamingButton().IsEnabled(true);
                RestoreBtButton().IsEnabled(true);
            });
        }).detach();
    }

    void MainWindow::OnEnableStreamingClick(IInspectable const&, RoutedEventArgs const&)
    {
        RunDriverSwitch(true);
    }

    void MainWindow::OnRestoreBtClick(IInspectable const&, RoutedEventArgs const&)
    {
        RunDriverSwitch(false);
    }

    void MainWindow::UpdateStats(const A2dpService::StreamStats& stats)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Latency: %.2f ms", stats.latency_ms);
        LatencyText().Text(AToW(buf));
        snprintf(buf, sizeof(buf), "Error Rate: %.2f%%", stats.error_rate * 100.0);
        ErrorRateText().Text(AToW(buf));
        snprintf(buf, sizeof(buf), "Loss Rate: %.2f%%", stats.loss_rate * 100.0);
        LossRateText().Text(AToW(buf));
        snprintf(buf, sizeof(buf), "Queue: %u  (sends=%llu fails=%llu)", stats.queue_depth,
                 (unsigned long long)stats.total_sends, (unsigned long long)stats.send_fails);
        QueueText().Text(AToW(buf));

        /* Severity highlighting, mirroring the CLI health rows (warn latency > 15 ms,
         * error/loss above the sparkline's 1 % visibility threshold are red). */
        LatencyText().Foreground(nullptr);
        ErrorRateText().Foreground(nullptr);
        LossRateText().Foreground(nullptr);
        if (stats.latency_ms > 15.0)
            LatencyText().Foreground(TagBrush(L"TagWarnBrush"));
        if (stats.error_rate >= 0.01f)
            ErrorRateText().Foreground(TagBrush(L"TagErrorBrush"));
        if (stats.loss_rate >= 0.01f)
            LossRateText().Foreground(TagBrush(L"TagErrorBrush"));

        // Feed history for the sparkline (~120 samples â‰ˆ 4 min at 2 Hz)
        sparkLatency_.push_back(static_cast<float>(stats.latency_ms));
        sparkError_.push_back(static_cast<float>(stats.error_rate));
        while (sparkLatency_.size() > 120) sparkLatency_.pop_front();
        while (sparkError_.size() > 120) sparkError_.pop_front();
        UpdateSparkline();
    }

    void MainWindow::UpdateSparkline()
    {
        constexpr float kPad = 2.0f;
        double w = SparklineLatency().ActualWidth();
        double h = SparklineLatency().ActualHeight();
        if (w <= 0) w = 240.0;
        if (h <= 0) h = 46.0;
        float innerW = static_cast<float>(w - 2.0 * kPad);
        float innerH = static_cast<float>(h - 2.0 * kPad);

        // Latency line: 0..25 ms vertically centered on the graph
        auto latPts = winrt::Microsoft::UI::Xaml::Media::PointCollection();
        size_t n = sparkLatency_.size();
        if (n > 1) {
            for (size_t i = 0; i < n; ++i) {
                float v = sparkLatency_[i];
                if (v < 0.0f) v = 0.0f;
                if (v > 25.0f) v = 25.0f;
                float x = kPad + static_cast<float>(i) / (n - 1) * innerW;
                float y = kPad + innerH - (v / 25.0f) * innerH;
                latPts.Append(winrt::Windows::Foundation::Point(x, y));
            }
        }
        SparklineLatency().Points(latPts);

        // Error line: 0..5% overlaid in red
        auto errPts = winrt::Microsoft::UI::Xaml::Media::PointCollection();
        if (n > 1) {
            for (size_t i = 0; i < n; ++i) {
                float v = sparkError_[i];
                if (v < 0.0f) v = 0.0f;
                if (v > 0.05f) v = 0.05f;
                float x = kPad + static_cast<float>(i) / (n - 1) * innerW;
                float y = kPad + innerH - (v / 0.05f) * innerH;
                errPts.Append(winrt::Windows::Foundation::Point(x, y));
            }
        }
        SparklineError().Points(errPts);
    }

    void MainWindow::UpdateUI(const A2dpService::State& state, const std::string& text)
    {
        UpdateStatusCard(state, text);

        switch (state) {
        case A2dpService::State::Idle:
            SetStreamingUi(false);
            ScanButton().IsEnabled(true);
            break;
        case A2dpService::State::Connecting:
            ScanButton().IsEnabled(false);
            ConnectButton().IsEnabled(false);
            break;
        case A2dpService::State::Streaming:
            ScanButton().IsEnabled(false);
            ConnectButton().IsEnabled(true);
            SetStreamingUi(true);
            break;
        case A2dpService::State::Reconnecting:
            ScanButton().IsEnabled(false);
            ConnectButton().IsEnabled(false);
            break;
        case A2dpService::State::Error:
            ScanButton().IsEnabled(true);
            SetStreamingUi(false);
            if (hasSelectedDevice_) ConnectButton().IsEnabled(true);
            break;
        }
        AppendLog(LogTag::Data, "[State] " + text);
    }

    /* Status card = the one place that answers "is it working, and what do I do
     * next?". Glyph + headline + explanation + explicit next step, so the state
     * never depends on color alone (PLAN_UX_DESIGN.md Faz 1). */
    void MainWindow::UpdateStatusCard(const A2dpService::State& state, const std::string& key)
    {
        StateCopy copy = BaseCopyForState(state);

        /* A known localization key refines (and for errors overrides) the copy.
         * status.connected while streaming should still read as streaming. */
        if (!key.empty()) {
            const StateCopy& byKey = CopyForKey(key);
            const bool isErrorKey = key.rfind("error.", 0) == 0;
            const bool keyDrivesHeadline = isErrorKey || state == A2dpService::State::Idle;
            if (keyDrivesHeadline) copy.headline = byKey.headline;
            if (byKey.detail && byKey.detail[0]) copy.detail = byKey.detail;
            if (byKey.action && byKey.action[0]) copy.action = byKey.action;
            if (isErrorKey || state != A2dpService::State::Streaming) {
                copy.glyph = byKey.glyph;
                copy.tone = byKey.tone;
            }
        }

        StatusHeadlineText().Text(AToW(copy.headline));
        StatusDetailText().Text(AToW(copy.detail));

        if (copy.action && copy.action[0]) {
            StatusActionText().Text(AToW(std::string("Next: ") + copy.action));
            StatusActionText().Visibility(Visibility::Visible);
        } else {
            StatusActionText().Text(L"");
            StatusActionText().Visibility(Visibility::Collapsed);
        }

        StatusGlyph().Glyph(hstring(std::wstring(1, copy.glyph)));

        /* Tone drives only the glyph color - the headline text carries the same
         * information, so color is never the sole signal. */
        switch (copy.tone) {
        case Tone::Ok:    StatusGlyph().Foreground(TagBrush(L"TagOkBrush"));    break;
        case Tone::Warn:  StatusGlyph().Foreground(TagBrush(L"TagWarnBrush"));  break;
        case Tone::Error: StatusGlyph().Foreground(TagBrush(L"TagErrorBrush")); break;
        default:          StatusGlyph().Foreground(TagBrush(L"TagDataBrush"));  break;
        }
    }

    void MainWindow::UpdateStatusDevice(const std::string& name, const std::string& mac)
    {
        StatusDeviceText().Text(name.empty() ? hstring(L"\u2014") : AToW(name));
        StatusDeviceMacText().Text(mac.empty() ? hstring(L"") : AToW(mac));
    }

    void MainWindow::OnToggleLogClick(IInspectable const&, RoutedEventArgs const&)
    {
        /* Collapse by hiding the panel only - the TextBlocks stay in LogLines so
         * history survives (PLAN_UX_DESIGN.md Faz 1: "keeps entries"). */
        auto panel = LogPanel();
        if (!panel) return;
        const bool collapsed = panel.Visibility() == Visibility::Collapsed;
        panel.Visibility(collapsed ? Visibility::Visible : Visibility::Collapsed);
        LogToggleButton().Content(winrt::box_value(collapsed ? L"Hide log" : L"Show log"));
        if (collapsed) {
            dispatcher_.TryEnqueue([this] {
                auto scrollable = LogScroller().ScrollableHeight();
                LogScroller().ScrollToVerticalOffset(scrollable);
            });
        }
    }

    void MainWindow::OnRefreshDriverClick(IInspectable const&, RoutedEventArgs const&)
    {
        RefreshDriverMode();
    }

    void MainWindow::RefreshDriverMode()
    {
        // TP-Link UB500: VID 0x2357, PID 0x0604 (Realtek RTL8761BU)
        auto st = detect_dongle_driver(0x2357, 0x0604);

        std::wstring state;
        std::string detail;
        switch (st.mode) {
        case DongleDriverMode::WinUsbStream:
            state = L"WinUSB (Streaming mode)";
            detail = "Service: " + st.service + " | Device: " + st.device_name;
            break;
        case DongleDriverMode::BthUsb:
            state = L"BTHUSB (Windows Bluetooth)";
            detail = "Service: " + st.service + " | Device: " + st.device_name;
            break;
        case DongleDriverMode::Other:
            state = L"Other driver";
            detail = "Service: " + st.service + " | Device: " + st.device_name;
            break;
        case DongleDriverMode::NotPresent:
        default:
            state = L"Not detected";
            detail = "No UB500 (VID 0x2357/PID 0x0604) device node found.";
            break;
        }

        DriverStateText().Text(state);
        DriverDetailText().Text(AToW(detail));
        AppendLog(LogTag::Data, "[DriverMode] " + detail);
    }

    void MainWindow::AppendLog(const std::string& text)
    {
        AppendLog(LogTag::Info, text);
    }

    void MainWindow::AppendLog(LogTag tag, const std::string& text)
    {
        Microsoft::UI::Xaml::Media::SolidColorBrush brush{ nullptr };
        switch (tag) {
        case LogTag::Ok:    brush = TagBrush(L"TagOkBrush");    break;
        case LogTag::Warn:  brush = TagBrush(L"TagWarnBrush");  break;
        case LogTag::Error: brush = TagBrush(L"TagErrorBrush"); break;
        case LogTag::Data:  brush = TagBrush(L"TagDataBrush");  break;
        default:            break;
        }
        AppendLogLine(std::wstring(AToW(text).c_str()), brush);
    }

    void MainWindow::AppendLogLine(const std::wstring& text,
                                   const Microsoft::UI::Xaml::Media::SolidColorBrush& brush)
    {
        using namespace Microsoft::UI::Xaml::Controls;

        /* One TextBlock per log line. Whole-line severity tint mirrors the CLI tag
         * language; the mono log style comes from App.xaml (MonoLogTextStyle). */
        TextBlock line;
        line.Text(hstring(text));
        if (brush) line.Foreground(brush);
        try {
            auto resources = Application::Current().Resources();
            auto styleValue = resources.TryLookup(winrt::box_value(L"MonoLogTextStyle"));
            if (styleValue) line.Style(styleValue.as<Microsoft::UI::Xaml::Style>());
        } catch (...) {}

        auto children = LogLines().Children();
        children.Append(line);
        while (children.Size() > kMaxLogLines) children.RemoveAt(0);

        /* Mirror the newest line into the collapsed-log summary. */
        if (auto summary = LogSummaryText()) summary.Text(hstring(text));

        /* Scroll after layout settles so the newest line is visible. */
        dispatcher_.TryEnqueue([this] {
            auto scrollable = LogScroller().ScrollableHeight();
            LogScroller().ScrollToVerticalOffset(scrollable);
        });
    }
}
