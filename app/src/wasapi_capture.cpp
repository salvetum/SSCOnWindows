/*
 * WASAPI Loopback Audio Capture - Implementation
 *
 * Uses WASAPI in shared loopback mode to capture system audio output.
 * The captured PCM data is delivered via callback from a dedicated thread.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wasapi_capture.h"
#include <cstdio>
#include <cstring>
#include <avrt.h>
#include <timeapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <endpointvolume.h>

/* WASAPI CLSID/IID - defined here to avoid linking issues */
static const CLSID CLSID_MMDeviceEnumerator_ = __uuidof(MMDeviceEnumerator);
static const IID IID_IMMDeviceEnumerator_ = __uuidof(IMMDeviceEnumerator);
static const IID IID_IAudioClient_ = __uuidof(IAudioClient);
static const IID IID_IAudioCaptureClient_ = __uuidof(IAudioCaptureClient);
static const IID IID_IAudioEndpointVolume_ = __uuidof(IAudioEndpointVolume);

WasapiCapture::WasapiCapture() {
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    buffer_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); /* auto-reset */
}

WasapiCapture::~WasapiCapture() {
    stop();

    if (capture_client_) { capture_client_->Release(); capture_client_ = nullptr; }
    if (audio_client_) { audio_client_->Release(); audio_client_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    if (enumerator_) { enumerator_->Release(); enumerator_ = nullptr; }
    if (buffer_event_) { CloseHandle(buffer_event_); buffer_event_ = nullptr; }
    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
}

bool WasapiCapture::mute_output(bool mute) {
    if (!enumerator_) return false;

    /* Get the current default render endpoint (speakers / headphone jack).
     * Note: loopback capture grabs data pre-volume-mix, so muting the
     * speaker output via IAudioEndpointVolume does NOT silence the loopback
     * stream — the headphones keep playing. */
    IMMDevice *render_dev = nullptr;
    HRESULT hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &render_dev);
    if (FAILED(hr) || !render_dev) {
        fprintf(stderr, "WasapiCapture: mute_output: no default render endpoint (hr=0x%08lx)\n", hr);
        return false;
    }

    IAudioEndpointVolume *vol = nullptr;
    hr = render_dev->Activate(IID_IAudioEndpointVolume_, CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(&vol));
    if (FAILED(hr) || !vol) {
        fprintf(stderr, "WasapiCapture: mute_output: IAudioEndpointVolume activate failed (hr=0x%08lx)\n", hr);
        render_dev->Release();
        return false;
    }

    BOOL bMute = mute ? TRUE : FALSE;
    hr = vol->SetMute(bMute, nullptr);
    if (SUCCEEDED(hr))
        fprintf(stderr, "WasapiCapture: output %s\n", mute ? "MUTED" : "UNMUTED");
    else
        fprintf(stderr, "WasapiCapture: SetMute failed (hr=0x%08lx)\n", hr);

    vol->Release();
    render_dev->Release();
    return SUCCEEDED(hr);
}

bool WasapiCapture::init(uint32_t preferred_sample_rate, const wchar_t *device_id) {
    HRESULT hr;

    /* Initialize COM on this thread if not already done */
    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
        fprintf(stderr, "WasapiCapture: CoInitializeEx failed: 0x%08lx\n", hr);
        return false;
    }

    /* Create device enumerator */
    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator_, nullptr, CLSCTX_ALL,
        IID_IMMDeviceEnumerator_, reinterpret_cast<void **>(&enumerator_)
    );
    if (FAILED(hr)) {
        fprintf(stderr, "WasapiCapture: Failed to create device enumerator: 0x%08lx\n", hr);
        return false;
    }

    /* Get audio output device — either by explicit ID or system default */
    if (device_id) {
        hr = enumerator_->GetDevice(device_id, &device_);
        if (FAILED(hr)) {
            fprintf(stderr, "WasapiCapture: Failed to get device by ID: 0x%08lx\n", hr);
            return false;
        }
    } else {
        hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
        if (FAILED(hr)) {
            fprintf(stderr, "WasapiCapture: No default audio output device: 0x%08lx\n", hr);
            return false;
        }
    }

    /* Print device name */
    IPropertyStore *props = nullptr;
    hr = device_->OpenPropertyStore(STGM_READ, &props);
    if (SUCCEEDED(hr)) {
        PROPVARIANT varName;
        PropVariantInit(&varName);
        hr = props->GetValue(PKEY_Device_FriendlyName, &varName);
        if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
            fprintf(stderr, "WasapiCapture: Using device: %ls\n", varName.pwszVal);
        }
        PropVariantClear(&varName);
        props->Release();
    }

    /* Activate IAudioClient */
    hr = device_->Activate(
        IID_IAudioClient_, CLSCTX_ALL, nullptr,
        reinterpret_cast<void **>(&audio_client_)
    );
    if (FAILED(hr)) {
        fprintf(stderr, "WasapiCapture: Failed to activate audio client: 0x%08lx\n", hr);
        return false;
    }

    /* Get the mix format (system output format) */
    WAVEFORMATEX *mix_format = nullptr;
    hr = audio_client_->GetMixFormat(&mix_format);
    if (FAILED(hr)) {
        fprintf(stderr, "WasapiCapture: Failed to get mix format: 0x%08lx\n", hr);
        return false;
    }

    channels_ = mix_format->nChannels;

    /* Use container size (wBitsPerSample) for buffer calculations and callback.
     * The audio callback dispatches on 32 (float) vs 16 (int) — using valid bits
     * instead of container bits would cause 24-in-32 formats to be dropped. */
    bits_per_sample_ = mix_format->wBitsPerSample;
    if (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *ext = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(mix_format);
        uint32_t valid_bits = ext->Samples.wValidBitsPerSample;
        if (valid_bits != 0 && valid_bits != bits_per_sample_) {
            fprintf(stderr, "WasapiCapture: Note: valid bits (%u) != container bits (%u)\n",
                    valid_bits, bits_per_sample_);
        }
    }

    /* Try preferred sample rate if specified and different from system default */
    WAVEFORMATEX *init_format = mix_format;
    WAVEFORMATEX custom_format = {};
    bool use_custom = false;

    if (preferred_sample_rate > 0 && preferred_sample_rate != mix_format->nSamplesPerSec) {
        /* Build a custom format based on mix_format but with the preferred rate */
        custom_format = *mix_format;
        custom_format.nSamplesPerSec = preferred_sample_rate;
        custom_format.nAvgBytesPerSec = preferred_sample_rate * mix_format->nBlockAlign;

        WAVEFORMATEX *closest = nullptr;
        hr = audio_client_->IsFormatSupported(
            AUDCLNT_SHAREMODE_SHARED, &custom_format, &closest);
        if (hr == S_OK) {
            init_format = &custom_format;
            use_custom = true;
            fprintf(stderr, "WasapiCapture: Using preferred sample rate: %u Hz\n",
                    preferred_sample_rate);
        } else {
            fprintf(stderr, "WasapiCapture: Preferred rate %u Hz not supported, using system default\n",
                    preferred_sample_rate);
            if (closest) CoTaskMemFree(closest);
        }
    }

    sample_rate_ = init_format->nSamplesPerSec;

    fprintf(stderr, "WasapiCapture: Format: %u Hz, %u ch, %u bit (container: %u bit) tag=0x%04x\n",
           sample_rate_, channels_, bits_per_sample_, init_format->wBitsPerSample,
           mix_format->wFormatTag);
    if (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *ext = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(mix_format);
        fprintf(stderr, "WasapiCapture: Extensible subtype=%08x-... valid_bits=%u\n",
                ext->SubFormat.Data1, ext->Samples.wValidBitsPerSample);
    }

    /* Larger buffer for polling mode (event-driven keeps 10ms default) */
    REFERENCE_TIME buf_duration = static_cast<REFERENCE_TIME>(
        (event_mode_ ? BUFFER_DURATION_MS : 40)) * 10000;

    hr = audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        buf_duration,
        0,                  /* periodicity (0 = default for shared mode) */
        init_format,
        nullptr             /* session GUID */
    );

    if (FAILED(hr)) {
        /* Fallback: try event-driven loopback */
        fprintf(stderr, "WasapiCapture: Polling init failed (0x%08lx), trying event-driven\n", hr);
        hr = audio_client_->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            buf_duration,
            0,
            init_format,
            nullptr
        );
        if (SUCCEEDED(hr)) {
            /* Set the event handle for event-driven mode */
            HRESULT eh = audio_client_->SetEventHandle(buffer_event_);
            if (SUCCEEDED(eh)) {
                event_mode_ = true;
                fprintf(stderr, "WasapiCapture: Using event-driven capture\n");
            } else {
                fprintf(stderr, "WasapiCapture: SetEventHandle failed: 0x%08lx\n", eh);
            }
        }
    } else {
        event_mode_ = false;
        fprintf(stderr, "WasapiCapture: Using polling capture\n");
    }

    CoTaskMemFree(mix_format);

    if (FAILED(hr)) {
        fprintf(stderr, "WasapiCapture: Failed to initialize audio client: 0x%08lx\n", hr);
        return false;
    }

    /* Get capture client */
    hr = audio_client_->GetService(
        IID_IAudioCaptureClient_,
        reinterpret_cast<void **>(&capture_client_)
    );
    if (FAILED(hr)) {
        fprintf(stderr, "WasapiCapture: Failed to get capture client: 0x%08lx\n", hr);
        return false;
    }

    fprintf(stderr, "WasapiCapture: Initialized successfully\n");
    return true;
}

bool WasapiCapture::start(AudioCallback callback) {
    if (running_.load()) {
        fprintf(stderr, "WasapiCapture: Already running\n");
        return false;
    }
    if (!audio_client_ || !capture_client_) {
        fprintf(stderr, "WasapiCapture: Not initialized\n");
        return false;
    }

    callback_ = callback;
    ResetEvent(stop_event_);
    running_.store(true);

    /* Start the audio client */
    HRESULT hr = audio_client_->Start();
    if (FAILED(hr)) {
        fprintf(stderr, "WasapiCapture: Failed to start capture: 0x%08lx\n", hr);
        running_.store(false);
        return false;
    }

    /* Create capture thread */
    thread_handle_ = CreateThread(
        nullptr, 0, capture_thread_proc, this, 0, nullptr
    );
    if (!thread_handle_) {
        fprintf(stderr, "WasapiCapture: Failed to create capture thread\n");
        audio_client_->Stop();
        running_.store(false);
        return false;
    }

    fprintf(stderr, "WasapiCapture: Capture started\n");
    return true;
}

void WasapiCapture::stop() {
    if (!running_.load()) return;

    running_.store(false);
    SetEvent(stop_event_);

    if (thread_handle_) {
        WaitForSingleObject(thread_handle_, 5000);
        CloseHandle(thread_handle_);
        thread_handle_ = nullptr;
    }

    if (audio_client_) {
        audio_client_->Stop();
    }

    fprintf(stderr, "WasapiCapture: Capture stopped\n");
}

DWORD WINAPI WasapiCapture::capture_thread_proc(LPVOID param) {
    /* Initialize COM for this thread */
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    auto *self = static_cast<WasapiCapture *>(param);
    self->capture_loop();

    CoUninitialize();
    return 0;
}

void WasapiCapture::capture_loop() {
    /* Set this thread to multimedia priority for low-latency audio */
    DWORD task_index = 0;
    HANDLE avrt_handle = AvSetMmThreadCharacteristicsW(L"Audio", &task_index);
    if (!avrt_handle) {
        fprintf(stderr, "WasapiCapture: MMCSS registration failed, using timeBeginPeriod\n");
    }
    /* Ensure 1ms timer resolution as safety net */
    timeBeginPeriod(1);

    /* Wait on buffer_event (event-driven) + stop_event, or poll as fallback */
    HANDLE wait_handles[2] = { stop_event_, buffer_event_ };
    int handle_count = event_mode_ ? 2 : 1;

    while (running_.load()) {
        DWORD wait_result;
        if (handle_count == 2) {
            /* Event-driven: WASAPI signals buffer_event_ when data is ready */
            wait_result = WaitForMultipleObjects(2, wait_handles, FALSE, 100);
        } else {
            /* Polling fallback */
            wait_result = WaitForSingleObject(stop_event_, 5);
        }
        if (wait_result == WAIT_OBJECT_0) {
            break; /* stop_event_ signaled */
        }

        /* Get available captured data */
        UINT32 packet_length = 0;
        HRESULT hr = capture_client_->GetNextPacketSize(&packet_length);
        if (FAILED(hr)) {
            fprintf(stderr, "WasapiCapture: GetNextPacketSize failed: 0x%08lx\n", hr);
            break;
        }

        while (packet_length > 0) {
            BYTE *data = nullptr;
            UINT32 num_frames = 0;
            DWORD flags = 0;
            UINT64 device_position = 0;
            UINT64 qpc_position = 0;

            hr = capture_client_->GetBuffer(
                &data, &num_frames, &flags,
                &device_position, &qpc_position
            );
            if (FAILED(hr)) {
                fprintf(stderr, "WasapiCapture: GetBuffer failed: 0x%08lx\n", hr);
                break;
            }

            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                static uint32_t disc_count = 0;
                static DWORD disc_tick = 0;
                disc_count++;
                DWORD now = GetTickCount();
                if (now - disc_tick >= 2000 || disc_count == 1) {
                    fprintf(stderr, "WasapiCapture: Data discontinuity (count=%u)\n", disc_count);
                    disc_tick = now;
                }
            }

            static uint64_t last_device_pos = 0;
            static uint64_t last_qpc_pos = 0;
            static DWORD last_clock_tick = 0;
            static uint64_t dbg_silent_frames = 0;
            static uint64_t dbg_total_frames = 0;
            dbg_total_frames += num_frames;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                dbg_silent_frames += num_frames;
            }
            DWORD now = GetTickCount();
            if (now - last_clock_tick >= 2000) {
                double dev_hz = 0.0;
                double dur_ms = (double)(now - last_clock_tick);
                if (last_clock_tick != 0 && dur_ms > 0 && (uint64_t)device_position >= last_device_pos) {
                    dev_hz = (double)(device_position - last_device_pos) * 1000.0 / dur_ms;
                }
                double silent_pct = dbg_total_frames
                    ? (100.0 * (double)dbg_silent_frames / (double)dbg_total_frames) : 0.0;
                fprintf(stderr, "WasapiCapture: CLK dev=%llu qpc=%llu dev_hz=%.0f silent=%.1f%% frames=%llu\n",
                        (unsigned long long)device_position, (unsigned long long)qpc_position,
                        dev_hz, silent_pct, (unsigned long long)dbg_total_frames);
                last_device_pos = device_position;
                last_qpc_pos = qpc_position;
                last_clock_tick = now;
                dbg_silent_frames = 0;
                dbg_total_frames = 0;
            }

            if (num_frames > 0 && callback_) {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    /* Buffer contains silence — use thread-local zero buffer
                     * to avoid heap allocation in the hot path. */
                    uint32_t bytes = num_frames * channels_ *
                                     (bits_per_sample_ / 8);
                    static thread_local std::vector<uint8_t> silence;
                    if (silence.size() < bytes) silence.resize(bytes, 0);
                    callback_(silence.data(), num_frames,
                              channels_, sample_rate_, bits_per_sample_);
                } else {
                    callback_(data, num_frames,
                              channels_, sample_rate_, bits_per_sample_);
                }
            }

            capture_client_->ReleaseBuffer(num_frames);

            hr = capture_client_->GetNextPacketSize(&packet_length);
            if (FAILED(hr)) break;
        }
    }

    timeEndPeriod(1);
    if (avrt_handle) {
        AvRevertMmThreadCharacteristics(avrt_handle);
    }
}
