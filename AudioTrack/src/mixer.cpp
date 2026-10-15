#include "mixer.hpp"

#include "track.hpp"

#include <Geode/Geode.hpp>

#include <Windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace geode::prelude;

namespace at {
namespace {

constexpr int kRate = 48000;

// PKEY_Device_FriendlyName, spelled out so no INITGUID juggling is needed.
const PROPERTYKEY kFriendlyNameKey = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// Initialises COM on the current thread for the lifetime of the object.
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

std::string toUtf8(std::wstring const& text) {
    if (text.empty()) return {};
    int const size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string friendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    std::string name;
    if (SUCCEEDED(store->GetValue(kFriendlyNameKey, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
        name = toUtf8(value.pwszVal);
    PropVariantClear(&value);
    return name;
}

// The virtual cable devices (VB-Cable and our own driver): never a real microphone,
// reading them as the microphone would feed the output back into itself.
bool isVirtualName(std::string const& lowerName) {
    return lowerName.find("audiotrack") != std::string::npos || lowerName.find("vb-audio") != std::string::npos ||
           lowerName.find("cable output") != std::string::npos || lowerName.find("cable input") != std::string::npos;
}

// First active endpoint whose friendly name contains `needle` (case-insensitive).
// With `skipAudiotrack` the virtual cable devices themselves are never returned.
ComPtr<IMMDevice> findDevice(IMMDeviceEnumerator* enumerator, EDataFlow flow, std::string const& needle,
                             bool skipAudiotrack) {
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices))) return nullptr;
    UINT count = 0;
    devices->GetCount(&count);
    auto const wanted = lower(needle);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(i, &device))) continue;
        auto const name = lower(friendlyName(device.Get()));
        if (skipAudiotrack && isVirtualName(name)) continue;
        if (!wanted.empty() && name.find(wanted) == std::string::npos) continue;
        return device;
    }
    return nullptr;
}

WAVEFORMATEX floatStereoFormat() {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 2;
    format.nSamplesPerSec = kRate;
    format.wBitsPerSample = 32;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    return format;
}

// Low-latency shared mode (Windows 10+): when the device's engine format already is
// 48 kHz stereo float, ask the engine for its smallest period (a few ms instead of 10+).
bool openLowLatency(IMMDevice* device, ComPtr<IAudioClient>& client, HANDLE event, char const* what) {
    ComPtr<IAudioClient3> client3;
    if (FAILED(device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(client3.GetAddressOf()))))
        return false;
    WAVEFORMATEX* mix = nullptr;
    if (FAILED(client3->GetMixFormat(&mix)) || !mix) return false;

    bool const floatFormat =
        mix->wBitsPerSample == 32 &&
        (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
         (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
          reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT));
    UINT32 defaultPeriod = 0, fundamental = 0, minPeriod = 0, maxPeriod = 0;
    bool ok = floatFormat && mix->nChannels == 2 && mix->nSamplesPerSec == kRate &&
              SUCCEEDED(client3->GetSharedModeEnginePeriod(mix, &defaultPeriod, &fundamental, &minPeriod, &maxPeriod)) &&
              SUCCEEDED(client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPeriod, mix, nullptr)) &&
              SUCCEEDED(client3->SetEventHandle(event));
    if (!ok) log::info("Audiotrack mixer: {} is not in low-latency form (engine format {} Hz, {} ch, {} bit)", what,
                       mix->nSamplesPerSec, mix->nChannels, mix->wBitsPerSample);
    else log::info("Audiotrack mixer: {} runs with a {:.1f} ms period", what, minPeriod * 1000.0 / kRate);
    CoTaskMemFree(mix);
    if (!ok) return false;
    return SUCCEEDED(client3.As(&client));
}

// Opens `device` for shared-mode, event-driven streaming in 48 kHz stereo float. The low-latency
// path is tried first; otherwise Windows converts to and from the device format.
bool openClient(IMMDevice* device, ComPtr<IAudioClient>& client, HANDLE event, char const* what) {
    if (openLowLatency(device, client, event, what)) return true;
    client.Reset();
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()))))
        return false;
    auto format = floatStereoFormat();
    DWORD const flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format, nullptr))) return false;
    return SUCCEEDED(client->SetEventHandle(event));
}

} // namespace

Mixer& Mixer::get() {
    static Mixer instance;
    return instance;
}

Mixer::~Mixer() {
    // At process exit Windows has already killed the worker threads; a joinable
    // std::thread would call std::terminate here. They are never touched again.
    m_stopRequested = true;
    if (m_render.joinable()) m_render.detach();
    if (m_capture.joinable()) m_capture.detach();
}

// The playback device the mixer writes to: the cable's input (VB-Cable's "CABLE Input"),
// or Audiotrack's own driver when that is what is installed.
static ComPtr<IMMDevice> findCableOutput(IMMDeviceEnumerator* enumerator, std::string const& name) {
    ComPtr<IMMDevice> device;
    if (!name.empty()) device = findDevice(enumerator, eRender, name, false);
    if (!device) device = findDevice(enumerator, eRender, "cable input", false);
    if (!device) device = findDevice(enumerator, eRender, "audiotrack out", false);
    return device;
}

bool Mixer::cableAvailable() const {
    ComScope com;
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE) return false;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return false;
    return findCableOutput(enumerator.Get(), cableName()) != nullptr;
}

void Mixer::setCableName(std::string name) {
    std::lock_guard lock(m_mutex);
    m_cableName = std::move(name);
}

std::string Mixer::cableName() const {
    std::lock_guard lock(m_mutex);
    return m_cableName;
}

std::string Mixer::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

void Mixer::setStatus(std::string text) {
    std::lock_guard lock(m_mutex);
    m_status = std::move(text);
}

void Mixer::setMicName(std::string name) {
    std::lock_guard lock(m_mutex);
    m_micName = std::move(name);
}

bool Mixer::start() {
    if (m_running) return true;
    if (m_render.joinable()) m_render.join(); // a previous run ended on its own
    if (m_capture.joinable()) m_capture.join();

    if (!cableAvailable()) {
        setStatus("virtual cable not found (install VB-Cable)");
        return false;
    }
    std::string micName;
    {
        std::lock_guard lock(m_mutex);
        micName = m_micName;
    }
    m_stopRequested = false;
    m_micWritten = 0;
    m_micRead = 0;
    m_micPrimed = false;
    m_running = true;
    setStatus("starting");
    m_render = std::thread([this] { renderThread(); });
    m_capture = std::thread([this, micName] { captureThread(micName); });
    return true;
}

void Mixer::stop() {
    m_stopRequested = true;
    if (m_render.joinable()) m_render.join();
    if (m_capture.joinable()) m_capture.join();
    m_running = false;
    std::lock_guard lock(m_mutex);
    m_voices.clear();
    m_status = "stopped";
}

// ---- voices ------------------------------------------------------------------------------------

bool Mixer::prepare(std::string const& wavUtf8, std::string& error) {
    {
        std::lock_guard lock(m_mutex);
        if (m_clip && m_clipPath == wavUtf8) return true;
    }
    auto clip = loadStereoClip(pathFromUtf8(wavUtf8), kRate, error); // decode outside the lock
    if (!clip) return false;
    std::lock_guard lock(m_mutex);
    m_clip = std::move(clip);
    m_clipPath = wavUtf8;
    return true;
}

Mixer::VoiceId Mixer::play(std::string const& wavUtf8, double fromSeconds, float volume, double endSeconds) {
    if (!m_running) return 0;
    std::string error;
    if (!prepare(wavUtf8, error)) {
        log::warn("Audiotrack mixer: cannot decode {}: {}", wavUtf8, error);
        return 0;
    }
    std::lock_guard lock(m_mutex);
    Voice voice;
    voice.id = m_nextId++;
    voice.clip = m_clip;
    auto const total = static_cast<double>(voice.clip->frames());
    voice.position = std::max(0.0, fromSeconds) * kRate;
    voice.endFrame = endSeconds > 0.0 ? std::min(total, endSeconds * kRate) : total;
    voice.volume = volume;
    if (m_voices.size() >= 8) m_voices.erase(m_voices.begin());
    m_voices.push_back(std::move(voice));
    return m_voices.back().id;
}

void Mixer::stopVoice(VoiceId id) {
    if (id == 0) return;
    std::lock_guard lock(m_mutex);
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [id](Voice const& v) { return v.id == id; }),
                   m_voices.end());
}

void Mixer::stopAll() {
    std::lock_guard lock(m_mutex);
    m_voices.clear();
}

void Mixer::setTransport(bool paused, float rate) {
    m_paused = paused;
    m_rate = std::clamp(rate, 0.05f, 8.f);
}

// ---- audio threads -----------------------------------------------------------------------------

void Mixer::captureThread(std::string micName) {
    ComScope com;
    DWORD mmTask = 0;
    HANDLE const mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmTask); // steady scheduling for audio
    struct MmcssGuard {
        HANDLE handle;
        ~MmcssGuard() {
            if (handle) AvRevertMmThreadCharacteristics(handle);
        }
    } mmcssGuard{mmcss};
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return;

    ComPtr<IMMDevice> device;
    if (!micName.empty()) device = findDevice(enumerator.Get(), eCapture, micName, true);
    if (!device) {
        enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
        // The default must not be a virtual cable, that would feed the output back into itself.
        if (device && isVirtualName(lower(friendlyName(device.Get()))))
            device = findDevice(enumerator.Get(), eCapture, "", true);
    }
    if (!device) {
        log::warn("Audiotrack mixer: no real microphone found, only the wav is sent");
        return;
    }
    log::info("Audiotrack mixer: microphone is '{}'", friendlyName(device.Get()));

    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    if (!openClient(device.Get(), client, event, "the microphone") ||
        FAILED(client->GetService(IID_PPV_ARGS(&capture))) || FAILED(client->Start())) {
        log::warn("Audiotrack mixer: cannot open the microphone");
        CloseHandle(event);
        return;
    }

    while (!m_stopRequested) {
        if (WaitForSingleObject(event, 100) == WAIT_FAILED) break;
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;

            auto const written = m_micWritten.load(std::memory_order_relaxed);
            auto const read = m_micRead.load(std::memory_order_acquire);
            if (m_passthrough && written - read + frames <= kMicRingFrames) {
                auto const* in = reinterpret_cast<float const*>(data);
                bool const silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                for (UINT32 i = 0; i < frames; ++i) {
                    auto const slot = static_cast<std::size_t>((written + i) % kMicRingFrames) * 2;
                    m_micRing[slot] = silent ? 0.f : in[i * 2];
                    m_micRing[slot + 1] = silent ? 0.f : in[i * 2 + 1];
                }
                m_micWritten.store(written + frames, std::memory_order_release);
            }
            capture->ReleaseBuffer(frames);
        }
    }
    client->Stop();
    CloseHandle(event);
}

void Mixer::mixInto(float* out, std::uint32_t frames) {
    std::memset(out, 0, static_cast<std::size_t>(frames) * 2 * sizeof(float));

    // Real microphone: prime for 20 ms, keep at most 100 ms queued (jump to 40 ms).
    auto const written = m_micWritten.load(std::memory_order_acquire);
    auto read = m_micRead.load(std::memory_order_relaxed);
    std::uint64_t available = written - read;
    if (!m_micPrimed && available >= kRate / 50) m_micPrimed = true;
    if (m_micPrimed) {
        if (available > kRate / 10) {
            read = written - kRate / 25;
            available = kRate / 25;
        }
        auto const take = static_cast<std::uint32_t>(std::min<std::uint64_t>(available, frames));
        for (std::uint32_t i = 0; i < take; ++i) {
            auto const slot = static_cast<std::size_t>((read + i) % kMicRingFrames) * 2;
            out[i * 2] = m_micRing[slot];
            out[i * 2 + 1] = m_micRing[slot + 1];
        }
        m_micRead.store(read + take, std::memory_order_release);
        if (take < frames) m_micPrimed = false; // underrun: wait for the queue to refill
    }

    // Click voices.
    if (!m_paused) {
        double const rate = m_rate;
        std::lock_guard lock(m_mutex);
        for (auto it = m_voices.begin(); it != m_voices.end();) {
            auto& voice = *it;
            auto const* samples = voice.clip->samples.data();
            auto const total = voice.clip->frames();
            bool finished = false;
            for (std::uint32_t f = 0; f < frames; ++f) {
                if (voice.position >= voice.endFrame || static_cast<std::size_t>(voice.position) + 1 >= total) {
                    finished = true;
                    break;
                }
                auto const i0 = static_cast<std::size_t>(voice.position);
                auto const t = static_cast<float>(voice.position - static_cast<double>(i0));
                for (int c = 0; c < 2; ++c) {
                    float const a = samples[i0 * 2 + c];
                    float const b = samples[(i0 + 1) * 2 + c];
                    out[f * 2 + c] += (a + (b - a) * t) * voice.volume;
                }
                voice.position += rate;
            }
            it = finished ? m_voices.erase(it) : it + 1;
        }
    }

    for (std::uint32_t i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.f, 1.f);
}

void Mixer::renderThread() {
    ComScope com;
    DWORD mmTask = 0;
    HANDLE const mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmTask);
    struct MmcssGuard {
        HANDLE handle;
        ~MmcssGuard() {
            if (handle) AvRevertMmThreadCharacteristics(handle);
        }
    } mmcssGuard{mmcss};
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    UINT32 bufferFrames = 0;

    bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
    if (ok) device = findCableOutput(enumerator.Get(), cableName());
    ok = ok && device && openClient(device.Get(), client, event, "the cable output") &&
         SUCCEEDED(client->GetBufferSize(&bufferFrames)) && SUCCEEDED(client->GetService(IID_PPV_ARGS(&render)));
    if (ok) log::info("Audiotrack mixer: output is '{}', buffer {} frames", friendlyName(device.Get()), bufferFrames);
    if (ok) {
        // Start with a buffer of silence.
        BYTE* data = nullptr;
        if (SUCCEEDED(render->GetBuffer(bufferFrames, &data))) render->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        ok = SUCCEEDED(client->Start());
    }
    if (!ok) {
        setStatus("cannot open the virtual cable");
        log::error("Audiotrack mixer: cannot open the virtual cable's playback device");
        CloseHandle(event);
        m_running = false;
        return;
    }
    setStatus("running");
    log::info("Audiotrack mixer: running");

    while (!m_stopRequested) {
        if (WaitForSingleObject(event, 100) == WAIT_FAILED) break;
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding))) break;
        UINT32 const frames = bufferFrames - padding;
        if (frames == 0) continue;
        BYTE* data = nullptr;
        if (FAILED(render->GetBuffer(frames, &data))) break;
        mixInto(reinterpret_cast<float*>(data), frames);
        render->ReleaseBuffer(frames, 0);
    }
    client->Stop();
    CloseHandle(event);
    m_running = false;
    if (!m_stopRequested) setStatus("stopped (audio device lost)");
}

} // namespace at
