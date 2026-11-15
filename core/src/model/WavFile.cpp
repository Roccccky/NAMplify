#include "namplify/model/WavFile.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nmp
{

std::vector<float> WavData::toMono() const
{
    std::vector<float> out;
    if (channels.empty()) return out;

    const int n = numFrames();
    out.assign ((size_t) n, 0.0f);

    const float g = 1.0f / (float) numChannels();
    for (const auto& ch : channels)
        for (int i = 0; i < n; ++i)
            out[(size_t) i] += ch[(size_t) i] * g;

    return out;
}

namespace
{
    struct Reader
    {
        const std::vector<char>& d;
        size_t pos = 0;

        bool need (size_t n) const { return pos + n <= d.size(); }

        std::uint32_t u32() { std::uint32_t v = 0; std::memcpy (&v, d.data() + pos, 4); pos += 4; return v; }
        std::uint16_t u16() { std::uint16_t v = 0; std::memcpy (&v, d.data() + pos, 2); pos += 2; return v; }
        void skip (size_t n) { pos += n; }
        std::string tag()   { std::string s (d.data() + pos, 4); pos += 4; return s; }
    };

    inline float readSample (const char* p, int bits, bool isFloat)
    {
        if (isFloat)
        {
            if (bits == 32) { float f;  std::memcpy (&f, p, 4); return f; }
            if (bits == 64) { double f; std::memcpy (&f, p, 8); return (float) f; }
            return 0.0f;
        }

        switch (bits)
        {
            case 8:  return ((float) (unsigned char) *p - 128.0f) / 128.0f;
            case 16: { std::int16_t v; std::memcpy (&v, p, 2); return (float) v / 32768.0f; }
            case 24:
            {
                const std::int32_t v = (std::int32_t) ((unsigned char) p[0]
                                     | ((unsigned char) p[1] << 8)
                                     | ((signed char)   p[2] << 16));
                return (float) v / 8388608.0f;
            }
            case 32: { std::int32_t v; std::memcpy (&v, p, 4); return (float) v / 2147483648.0f; }
            default: return 0.0f;
        }
    }
}

bool readWav (const std::filesystem::path& path, WavData& out, std::string& error)
{
    std::error_code ec;
    if (! std::filesystem::exists (path, ec))
    {
        error = "File not found";
        return false;
    }

    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        error = "File could not be opened";
        return false;
    }

    std::vector<char> data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (data.size() < 44)
    {
        error = "File is too short to be a WAV";
        return false;
    }

    Reader r { data };
    if (r.tag() != "RIFF") { error = "not a RIFF file"; return false; }
    r.skip (4);
    if (r.tag() != "WAVE") { error = "not a WAVE file"; return false; }

    int  bits = 0, channels = 0;
    bool isFloat = false;
    double rate = 0.0;
    size_t dataPos = 0, dataLen = 0;

    while (r.need (8))
    {
        const std::string id = r.tag();
        const std::uint32_t len = r.u32();

        if (id == "fmt ")
        {
            if (! r.need (16)) { error = "fmt chunk is incomplete"; return false; }
            const size_t start = r.pos;

            std::uint16_t format = r.u16();
            channels = r.u16();
            rate     = (double) r.u32();
            r.skip (4);                 // Bytes pro Sekunde
            r.skip (2);                 // Blockausrichtung
            bits = r.u16();

            if (format == 0xFFFE && len >= 40 && r.need (start + 40 - r.pos))
            {
                // WAVE_FORMAT_EXTENSIBLE: der eigentliche Formatcode steht in
                // der GUID.
                r.pos = start + 24;
                const std::uint16_t sub = r.u16();
                format = sub;
            }

            isFloat = (format == 3);
            if (format != 1 && format != 3)
            {
                error = "Unsupported WAV format (PCM and float only)";
                return false;
            }

            r.pos = start + len + (len & 1);
        }
        else if (id == "data")
        {
            dataPos = r.pos;
            dataLen = std::min ((size_t) len, data.size() - r.pos);
            r.pos += len + (len & 1);
        }
        else
        {
            r.pos += len + (len & 1);
        }

        if (r.pos > data.size()) break;
    }

    if (channels <= 0 || bits <= 0 || dataLen == 0)
    {
        error = "WAV holds no usable audio";
        return false;
    }

    const int bytes = bits / 8;
    const int frames = (int) (dataLen / (size_t) (bytes * channels));
    if (frames <= 0) { error = "WAV is empty"; return false; }

    out.sampleRate = rate;
    out.channels.assign ((size_t) channels, std::vector<float> ((size_t) frames, 0.0f));

    const char* p = data.data() + dataPos;
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            out.channels[(size_t) c][(size_t) i] = readSample (p + (size_t) (i * channels + c) * bytes, bits, isFloat);

    return true;
}

bool writeWav24 (const std::filesystem::path& path,
                 const float* left, const float* right, int numFrames,
                 double sampleRate, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories (path.parent_path(), ec);

    std::ofstream out (path, std::ios::binary);
    if (! out) { error = "File could not be created"; return false; }

    const int channels = 2, bits = 24, bytes = 3;
    const std::uint32_t dataLen = (std::uint32_t) numFrames * (std::uint32_t) (channels * bytes);

    auto w32 = [&] (std::uint32_t v) { out.write ((const char*) &v, 4); };
    auto w16 = [&] (std::uint16_t v) { out.write ((const char*) &v, 2); };

    out.write ("RIFF", 4);
    w32 (36 + dataLen);
    out.write ("WAVE", 4);
    out.write ("fmt ", 4);
    w32 (16);
    w16 (1);                                          // PCM
    w16 ((std::uint16_t) channels);
    w32 ((std::uint32_t) std::lround (sampleRate));
    w32 ((std::uint32_t) std::lround (sampleRate) * (std::uint32_t) (channels * bytes));
    w16 ((std::uint16_t) (channels * bytes));
    w16 ((std::uint16_t) bits);
    out.write ("data", 4);
    w32 (dataLen);

    std::vector<char> buf ((size_t) dataLen);
    size_t k = 0;
    for (int i = 0; i < numFrames; ++i)
    {
        for (int c = 0; c < 2; ++c)
        {
            const float f = clamp (c == 0 ? left[i] : right[i], -1.0f, 1.0f);
            const std::int32_t v = (std::int32_t) std::lround ((double) f * 8388607.0);
            buf[k++] = (char) (v & 0xFF);
            buf[k++] = (char) ((v >> 8) & 0xFF);
            buf[k++] = (char) ((v >> 16) & 0xFF);
        }
    }
    out.write (buf.data(), (std::streamsize) buf.size());

    if (! out) { error = "Writing failed"; return false; }
    return true;
}

} // namespace nmp
