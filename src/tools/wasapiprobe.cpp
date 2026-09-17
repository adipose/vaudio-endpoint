// wasapiprobe -- a WASAPI client for exercising a render endpoint from a test.
//
//   wasapiprobe formats [--device <substring>]
//       The shared-mode mix format, and for a matrix of PCM formats whether the endpoint accepts each in
//       exclusive mode. JSON on stdout.
//
//   wasapiprobe play [--device <substring>] [--exclusive] --rate N --bits 16|24|32 --channels N
//                    [--seconds S] [--base HZ] [--step HZ]
//       Plays a sine on every channel, channel i at base + i*step Hz (default 400 + 300*i), so that what arrives
//       at the other end says which channel it was. JSON on stdout: the format actually opened, frames written.
//
// Exit code 0 on success, 1 on failure, 2 on bad usage. It needs a desktop session in the way any audio client
// does, which in practice means running as the logged-on user.

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static const double Pi = 3.14159265358979323846;

template <class T> struct Com
{
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
    T** operator&() { return &p; }
    operator bool() const { return p != nullptr; }
};

[[noreturn]] static void Fail(const char* What, HRESULT hr)
{
    fprintf(stderr, "wasapiprobe: %s (0x%08lx)\n", What, (unsigned long)hr);
    exit(1);
}

[[noreturn]] static void Usage()
{
    fprintf(stderr,
        "usage:\n"
        "  wasapiprobe formats [--device NAME]\n"
        "  wasapiprobe play [--device NAME] [--exclusive] --rate N --bits 16|24|32 --channels N\n"
        "                   [--seconds S] [--base HZ] [--step HZ]\n");
    exit(2);
}

static DWORD ChannelMaskFor(WORD Channels)
{
    switch (Channels)
    {
    case 1: return KSAUDIO_SPEAKER_MONO;
    case 2: return KSAUDIO_SPEAKER_STEREO;
    case 4: return KSAUDIO_SPEAKER_QUAD;
    case 6: return KSAUDIO_SPEAKER_5POINT1;          // back pair, as analog 5.1 outputs describe themselves
    case 8: return KSAUDIO_SPEAKER_7POINT1_SURROUND;
    default: return 0;
    }
}

static WAVEFORMATEXTENSIBLE MakeFormat(DWORD Rate, WORD Bits, WORD Channels)
{
    WAVEFORMATEXTENSIBLE f = {};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.Format.nChannels = Channels;
    f.Format.nSamplesPerSec = Rate;
    f.Format.wBitsPerSample = Bits;
    f.Format.nBlockAlign = (WORD)(Channels * Bits / 8);
    f.Format.nAvgBytesPerSec = Rate * f.Format.nBlockAlign;
    f.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f.Samples.wValidBitsPerSample = Bits;
    f.dwChannelMask = ChannelMaskFor(Channels);
    f.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    return f;
}

static std::string Narrow(const wchar_t* Wide)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, Wide, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, Wide, -1, &s[0], n, nullptr, nullptr);
    return s;
}

static std::string JsonEscape(const std::string& In)
{
    std::string Out;
    for (char c : In) { if (c == '"' || c == '\\') Out += '\\'; Out += c; }
    return Out;
}

// The default render endpoint, or the first active one whose friendly name contains Match.
static void OpenDevice(const std::string& Match, IMMDevice** Device, std::string& Name)
{
    Com<IMMDeviceEnumerator> Enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&Enumerator));
    if (FAILED(hr)) Fail("creating the device enumerator", hr);

    auto NameOf = [](IMMDevice* d)
    {
        std::string Result;
        Com<IPropertyStore> Store;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &Store)))
        {
            PROPVARIANT v; PropVariantInit(&v);
            if (SUCCEEDED(Store->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) Result = Narrow(v.pwszVal);
            PropVariantClear(&v);
        }
        return Result;
    };

    if (Match.empty())
    {
        hr = Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, Device);
        if (FAILED(hr)) Fail("no default render endpoint", hr);
        Name = NameOf(*Device);
        return;
    }

    Com<IMMDeviceCollection> All;
    hr = Enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &All);
    if (FAILED(hr)) Fail("enumerating render endpoints", hr);
    UINT Count = 0; All->GetCount(&Count);
    for (UINT i = 0; i < Count; i++)
    {
        IMMDevice* d = nullptr;
        if (FAILED(All->Item(i, &d))) continue;
        std::string n = NameOf(d);
        if (n.find(Match) != std::string::npos) { *Device = d; Name = n; return; }
        d->Release();
    }
    Fail("no active render endpoint matches --device", E_NOTFOUND);
}

static void PrintFormat(const WAVEFORMATEX* f)
{
    DWORD Mask = 0; WORD Valid = f->wBitsPerSample; const char* Kind = "pcm";
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        auto* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
        Mask = x->dwChannelMask; Valid = x->Samples.wValidBitsPerSample;
        if (x->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) Kind = "float";
    }
    else if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) Kind = "float";
    printf("{\"rate\":%lu,\"bits\":%u,\"validBits\":%u,\"channels\":%u,\"channelMask\":\"0x%lx\",\"kind\":\"%s\"}",
           f->nSamplesPerSec, f->wBitsPerSample, Valid, f->nChannels, Mask, Kind);
}

static int CmdFormats(const std::string& DeviceMatch)
{
    Com<IMMDevice> Device; std::string Name;
    OpenDevice(DeviceMatch, &Device, Name);

    Com<IAudioClient> Client;
    HRESULT hr = Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&Client));
    if (FAILED(hr)) Fail("activating IAudioClient", hr);

    printf("{\"device\":\"%s\",\"mixFormat\":", JsonEscape(Name).c_str());
    WAVEFORMATEX* Mix = nullptr;
    if (SUCCEEDED(Client->GetMixFormat(&Mix)) && Mix) { PrintFormat(Mix); CoTaskMemFree(Mix); } else printf("null");

    const DWORD Rates[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
    const WORD Bits[] = { 16, 24, 32 };
    const WORD Channels[] = { 2, 6, 8 };

    printf(",\"exclusive\":[");
    bool First = true;
    for (WORD c : Channels) for (DWORD r : Rates) for (WORD b : Bits)
    {
        WAVEFORMATEXTENSIBLE f = MakeFormat(r, b, c);
        hr = Client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &f.Format, nullptr);
        printf("%s{\"rate\":%lu,\"bits\":%u,\"channels\":%u,\"supported\":%s}", First ? "" : ",", r, b, c, hr == S_OK ? "true" : "false");
        First = false;
    }
    printf("]}\n");
    return 0;
}

static void WriteSample(BYTE* Out, WORD Bits, double Value)
{
    if (Bits == 16)
    {
        short s = (short)(Value * 32767.0);
        memcpy(Out, &s, 2);
    }
    else if (Bits == 24)
    {
        int s = (int)(Value * 8388607.0);
        Out[0] = (BYTE)s; Out[1] = (BYTE)(s >> 8); Out[2] = (BYTE)(s >> 16);
    }
    else
    {
        int s = (int)(Value * 2147483647.0);
        memcpy(Out, &s, 4);
    }
}

static int CmdPlay(const std::string& DeviceMatch, bool Exclusive, DWORD Rate, WORD Bits, WORD Channels, double Seconds, double Base, double Step)
{
    Com<IMMDevice> Device; std::string Name;
    OpenDevice(DeviceMatch, &Device, Name);

    Com<IAudioClient> Client;
    HRESULT hr = Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&Client));
    if (FAILED(hr)) Fail("activating IAudioClient", hr);

    WAVEFORMATEXTENSIBLE Format = MakeFormat(Rate, Bits, Channels);
    if (Format.dwChannelMask == 0) Usage();

    // Exclusive mode opens the device in exactly this format or not at all. Shared mode is asked to convert, so the
    // same command line plays through the engine whatever the mix format is.
    REFERENCE_TIME Period = 0;
    Client->GetDevicePeriod(&Period, nullptr);
    DWORD Flags = Exclusive ? 0 : (AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
    REFERENCE_TIME Buffer = Exclusive ? Period * 4 : 2000000;   // 200 ms in shared mode
    hr = Client->Initialize(Exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED, Flags, Buffer, Exclusive ? Period : 0, &Format.Format, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
    {
        // The documented dance: ask what the aligned size is, and try again with exactly that.
        UINT32 Frames = 0;
        Client->GetBufferSize(&Frames);
        Buffer = (REFERENCE_TIME)(10000000.0 * Frames / Rate + 0.5);
        Client.p->Release(); Client.p = nullptr;
        hr = Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&Client));
        if (SUCCEEDED(hr)) hr = Client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, Flags, Buffer, Buffer, &Format.Format, nullptr);
    }
    if (FAILED(hr)) Fail(Exclusive ? "opening the endpoint in exclusive mode with this format" : "opening the endpoint in shared mode", hr);

    UINT32 BufferFrames = 0;
    Client->GetBufferSize(&BufferFrames);
    Com<IAudioRenderClient> Render;
    hr = Client->GetService(IID_PPV_ARGS(&Render));
    if (FAILED(hr)) Fail("getting IAudioRenderClient", hr);

    const UINT64 Total = (UINT64)(Seconds * Rate);
    UINT64 Written = 0;
    const WORD Bytes = Bits / 8;
    std::vector<double> Phase(Channels, 0.0), Increment(Channels);
    for (WORD c = 0; c < Channels; c++) Increment[c] = 2.0 * Pi * (Base + Step * c) / Rate;

    auto Fill = [&](UINT32 Frames)
    {
        BYTE* Data = nullptr;
        HRESULT r = Render->GetBuffer(Frames, &Data);
        if (FAILED(r)) Fail("IAudioRenderClient::GetBuffer", r);
        for (UINT32 i = 0; i < Frames; i++)
        {
            for (WORD c = 0; c < Channels; c++)
            {
                // A quarter of full scale: loud enough to find, quiet enough that a downmix cannot clip.
                double v = Written + i < Total ? 0.25 * sin(Phase[c]) : 0.0;
                Phase[c] += Increment[c];
                WriteSample(Data + ((size_t)i * Channels + c) * Bytes, Bits, v);
            }
        }
        Render->ReleaseBuffer(Frames, 0);
        Written += Frames;
    };

    Fill(BufferFrames);
    hr = Client->Start();
    if (FAILED(hr)) Fail("IAudioClient::Start", hr);

    DWORD SleepMs = (DWORD)(1000.0 * BufferFrames / Rate / 2);
    if (SleepMs < 1) SleepMs = 1;
    while (Written < Total + BufferFrames)   // run one buffer of silence past the end so the tail is played out
    {
        Sleep(SleepMs);
        UINT32 Padding = 0;
        if (Exclusive)
        {
            // Timer-driven exclusive mode: refill whatever has been consumed.
            if (FAILED(Client->GetCurrentPadding(&Padding))) break;
        }
        else
        {
            Client->GetCurrentPadding(&Padding);
        }
        UINT32 Free = BufferFrames - Padding;
        if (Free > 0) Fill(Free);
    }
    Sleep((DWORD)(1000.0 * BufferFrames / Rate) + 50);
    Client->Stop();

    printf("{\"device\":\"%s\",\"mode\":\"%s\",\"format\":", JsonEscape(Name).c_str(), Exclusive ? "exclusive" : "shared");
    PrintFormat(&Format.Format);
    printf(",\"bufferFrames\":%u,\"framesWritten\":%llu,\"toneFrames\":%llu,\"baseHz\":%.1f,\"stepHz\":%.1f}\n",
           BufferFrames, Written, Total, Base, Step);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) Usage();
    std::string Command = argv[1], DeviceMatch;
    bool Exclusive = false;
    DWORD Rate = 0; WORD Bits = 0, Channels = 0;
    double Seconds = 3.0, Base = 400.0, Step = 300.0;

    for (int i = 2; i < argc; i++)
    {
        std::string a = argv[i];
        bool v = i + 1 < argc;
        if (a == "--device" && v) DeviceMatch = argv[++i];
        else if (a == "--exclusive") Exclusive = true;
        else if (a == "--rate" && v) Rate = strtoul(argv[++i], nullptr, 10);
        else if (a == "--bits" && v) Bits = (WORD)strtoul(argv[++i], nullptr, 10);
        else if (a == "--channels" && v) Channels = (WORD)strtoul(argv[++i], nullptr, 10);
        else if (a == "--seconds" && v) Seconds = atof(argv[++i]);
        else if (a == "--base" && v) Base = atof(argv[++i]);
        else if (a == "--step" && v) Step = atof(argv[++i]);
        else Usage();
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) Fail("CoInitializeEx", hr);

    if (Command == "formats") return CmdFormats(DeviceMatch);
    if (Command == "play")
    {
        if (Rate == 0 || (Bits != 16 && Bits != 24 && Bits != 32) || Channels == 0) Usage();
        return CmdPlay(DeviceMatch, Exclusive, Rate, Bits, Channels, Seconds, Base, Step);
    }
    Usage();
}
