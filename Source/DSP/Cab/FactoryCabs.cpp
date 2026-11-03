#include "FactoryCabs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>

namespace kumamp::cabs
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;

    struct Bell { double hz, q, db; };

    /** Speaker + cabinet voicing. All filters are analogue prototypes evaluated on the FFT grid. */
    struct Voicing
    {
        CabInfo info;
        double lowHz, lowQ;          // 2nd-order high-pass: speaker / box resonance
        bool openBack;               // extra 6 dB/oct below lowHz (back-wave cancellation)
        double bodyHz, bodyDb;       // low shelf: low-mid body
        double highHz, highQ;        // 4th-order (2 x 2nd-order) cone roll-off
        double floorDb;              // break-up energy left above the roll-off
        std::array<Bell, 8> bells;   // box modes, presence, honk (0 dB = unused)
        double rippleDb;             // cone break-up irregularity, 1-9 kHz
        std::uint32_t seed;
        double reflectMs, reflectDb, reflectHz;   // back wave (open, inverted) / inner reflection (closed)
    };

    // clang-format off
    const std::vector<Voicing>& voicings()
    {
        static const std::vector<Voicing> v {
            { { "1x10-americano", "1x10", "Americano abierto", false },
              125, 0.80, true,  420, 2.0,  5200, 1.05, -26, { { { 1200, 1.4, 3.0 }, { 2800, 1.5, 4.0 }, { 650, 3.0, -2.5 } } }, 2.0, 11u, 3.4, -9, 900 },
            { { "2x10-americano", "2x10", "Americano abierto", false },
              115, 0.85, true,  380, 3.0,  5600, 1.00, -25, { { { 800, 1.5, 1.5 }, { 2500, 1.4, 3.5 }, { 4300, 2.5, 1.5 } } }, 2.0, 12u, 3.8, -9, 850 },
            { { "4x10-americano", "4x10", "Americano abierto", false },
              95,  0.90, true,  350, 4.0,  5800, 0.95, -24, { { { 1600, 1.4, 1.5 }, { 3000, 1.5, 3.0 }, { 420, 3.5, -2.0 } } }, 2.0, 13u, 4.2, -10, 800 },
            { { "1x12-americano", "1x12", "Americano abierto", false },
              100, 0.90, true,  400, 3.5,  5400, 1.00, -25, { { { 300, 2.0, 2.0 }, { 2200, 1.3, 3.0 }, { 4000, 2.0, 2.0 } } }, 2.2, 14u, 3.0, -8, 900 },
            { { "1x12-britanico", "1x12", "Británico cerrado", false },
              95,  1.20, false, 420, 5.0,  5300, 1.00, -24, { { { 600, 4.0, -3.0 }, { 1400, 1.5, 2.0 }, { 2600, 1.6, 3.0 } } }, 2.2, 15u, 1.1, -14, 2500 },
            { { "2x12-abierto", "2x12", "Británico abierto", false },
              100, 0.85, true,  450, 3.0,  6000, 1.05, -23, { { { 1000, 1.5, 1.0 }, { 2400, 1.4, 4.0 }, { 4500, 2.2, 2.5 } } }, 2.2, 16u, 3.2, -8, 950 },
            { { "2x12-cerrado", "2x12", "Moderno cerrado", false },
              85,  1.15, false, 430, 6.0,  5600, 1.00, -23, { { { 500, 4.5, -3.0 }, { 2300, 1.6, 2.0 }, { 3600, 2.0, 3.0 } } }, 2.4, 17u, 1.3, -14, 2500 },
            { { "4x12-vintage", "4x12", "Británico vintage", false },
              90,  1.10, false, 400, 6.0,  4300, 0.85, -24, { { { 450, 4.0, -4.0 }, { 1800, 1.2, 3.0 }, { 3200, 2.5, 1.0 } } }, 2.2, 18u, 1.5, -14, 2200 },
            // Calibrated against measured close-mic 4x12 (closed back, 30 W British speakers):
            // +7 dB body at 100-250 Hz, box notch at 500 Hz, flat 1-4 kHz with a notch at ~3.1 kHz,
            // -12 dB at 6.3 kHz and a -20 dB break-up floor above 8 kHz.
            { { "4x12-moderno", "4x12", "Británico moderno", false },
              112, 1.30, false, 700, 7.0,  4650, 1.25, -18, { { { 500, 5.0, -6.5 }, { 640, 4.0, 1.5 }, { 3150, 4.0, -5.0 }, { 2400, 2.0, 1.0 }, { 300, 0.9, 3.0 }, { 820, 2.5, 2.0 }, { 7900, 2.0, -7.0 } } }, 2.4, 19u, 1.6, -15, 2400 },
            { { "1x15-bajo", "1x15", "Bajo cerrado", true },
              45,  1.00, false, 300, 3.0,  2800, 0.80, -32, { { { 800, 1.6, 1.5 }, { 1800, 1.8, 2.0 } } }, 1.2, 20u, 1.6, -16, 1500 },
            { { "8x10-bajo", "8x10", "Bajo cerrado", true },
              55,  1.20, false, 250, 2.0,  4200, 0.90, -28, { { { 120, 1.5, 2.0 }, { 600, 2.5, -2.0 }, { 2200, 1.2, 3.0 } } }, 1.6, 21u, 2.0, -15, 1800 },
        };
        return v;
    }

    struct MicVoicing
    {
        MicInfo info;
        Bell presence;
        double lowShelfHz, lowShelfDb;     // proximity (+) / distance (-)
        double highShelfHz, highShelfDb;
        double lowPassHz;                  // 0 = none (ribbon: 6 dB/oct)
        double rippleScale;                // off-axis / ribbon smooth out the break-up
        double roomDb;                     // extra reflection level (distance)
    };

    const std::vector<MicVoicing>& micVoicings()
    {
        static const std::vector<MicVoicing> m {
            { { "din-centro",  "Dinámico · centro" }, { 5200, 1.3,  3.0 }, 200, 1.5,     0,  0.0,    0, 1.0,  0 },
            { { "din-borde",   "Dinámico · borde" },  { 5200, 1.3,  2.0 }, 200, 1.0,  2200, -5.0,    0, 0.7,  0 },
            { { "cinta",       "Cinta" },               { 1000, 1.0,  0.0 }, 180, 2.5,  6000, -2.0, 3200, 0.5,  0 },
            { { "condensador", "Condensador" },         { 8000, 0.8,  1.5 }, 150, -1.5,    0,  0.0,    0, 1.0,  4 },
            { { "mezcla",      "Dinámico + Cinta" },  { 1000, 1.0,  0.0 },   0, 0.0,     0,  0.0,    0, 1.0,  0 },   // rendered as a blend
        };
        return m;
    }
    // clang-format on

    constexpr int kBlendMic = 4;

    //== Analogue prototypes, magnitude at frequency f ==========================
    double highPass2 (double f, double f0, double q)
    {
        const double w = f / f0;
        return w * w / std::sqrt ((1 - w * w) * (1 - w * w) + (w / q) * (w / q));
    }
    double lowPass2 (double f, double f0, double q)
    {
        const double w = f / f0;
        return 1.0 / std::sqrt ((1 - w * w) * (1 - w * w) + (w / q) * (w / q));
    }
    double highPass1 (double f, double f0) { const double w = f / f0; return w / std::sqrt (1 + w * w); }
    double lowPass1 (double f, double f0)  { const double w = f / f0; return 1.0 / std::sqrt (1 + w * w); }

    double bell (double f, const Bell& b)
    {
        if (b.db == 0.0 || b.hz <= 0.0)
            return 1.0;
        const double a = std::pow (10.0, b.db / 40.0);
        const std::complex<double> s (0.0, f / b.hz);
        return std::abs ((s * s + s * (a / b.q) + 1.0) / (s * s + s / (a * b.q) + 1.0));
    }
    /** First-order shelves (gentle, 2-octave transition). */
    double lowShelf (double f, double f0, double db)
    {
        if (db == 0.0 || f0 <= 0.0) return 1.0;
        const double r = std::pow (10.0, db / 40.0);   // sqrt of the linear gain
        const std::complex<double> s (0.0, f / f0);
        return std::abs ((s + r) / (s + 1.0 / r));      // g at DC, 1 at high frequencies
    }
    double highShelf (double f, double f0, double db)
    {
        if (db == 0.0 || f0 <= 0.0) return 1.0;
        return std::pow (10.0, db / 20.0) / lowShelf (f, f0, db);
    }

    //== Small deterministic RNG (same IR on every machine) =====================
    struct Rng
    {
        std::uint32_t state;
        double next() { state = state * 1664525u + 1013904223u; return (double) (state >> 8) / 16777216.0; }
    };

    //== Radix-2 complex FFT (in place) =========================================
    void fft (std::vector<std::complex<double>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double angle = 2 * kPi / (double) len * (inverse ? 1 : -1);
            const std::complex<double> wl (std::cos (angle), std::sin (angle));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1);
                for (size_t k = 0; k < len / 2; ++k, w *= wl)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                }
            }
        }
        if (inverse)
            for (auto& x : a) x /= (double) n;
    }

    double magnitude (const Voicing& c, const MicVoicing& m, const std::vector<Bell>& ripple, double f)
    {
        double g = highPass2 (f, c.lowHz, c.lowQ) * highPass2 (f, c.lowHz * 0.8, 0.7071);   // 24 dB/oct below resonance
        if (c.openBack) g *= highPass1 (f, c.lowHz * 0.9);
        g *= lowShelf (f, c.bodyHz, c.bodyDb);
        g *= lowPass2 (f, c.highHz, c.highQ) * lowPass2 (f, c.highHz * 1.08, 0.7071);
        g = std::sqrt (g * g + std::pow (10.0, c.floorDb / 10.0) * std::pow (highPass2 (f, c.lowHz, 0.7071), 2) * lowPass1 (f, 14000.0));
        for (const auto& b : c.bells) g *= bell (f, b);
        for (const auto& b : ripple)  g *= bell (f, b);

        g *= bell (f, m.presence);
        g *= lowShelf (f, m.lowShelfHz, m.lowShelfDb);
        g *= highShelf (f, m.highShelfHz, m.highShelfDb);
        if (m.lowPassHz > 0) g *= lowPass1 (f, m.lowPassHz);
        return g;
    }

    std::vector<float> renderSingle (const Voicing& c, const MicVoicing& m, double sampleRate)
    {
        constexpr size_t kFft = 1 << 14;
        const auto length = (size_t) std::lround (0.085 * sampleRate);   // 85 ms

        // Cone break-up: narrow random bells between 1 and 9 kHz.
        std::vector<Bell> ripple;
        Rng rng { c.seed };
        for (int i = 0; i < 14; ++i)
        {
            const double hz = 1000.0 * std::pow (9.0, rng.next());
            const double db = (rng.next() * 2.0 - 1.0) * c.rippleDb * m.rippleScale;
            ripple.push_back ({ hz, 4.0 + 6.0 * rng.next(), db });
        }

        // Minimum phase from the magnitude (real cepstrum, folded).
        std::vector<std::complex<double>> spec (kFft);
        for (size_t k = 0; k <= kFft / 2; ++k)
        {
            const double f = std::max (1.0, (double) k * sampleRate / (double) kFft);
            const double mag = std::max (magnitude (c, m, ripple, f), 1e-4);   // -80 dB floor
            spec[k] = std::log (mag);
            if (k > 0 && k < kFft / 2) spec[kFft - k] = spec[k];
        }
        fft (spec, true);
        for (size_t n = 1; n < kFft / 2; ++n) { spec[n] *= 2.0; spec[kFft - n] = 0.0; }
        fft (spec, false);
        for (auto& x : spec) x = std::exp (x);
        fft (spec, true);

        std::vector<float> ir (length);
        for (size_t n = 0; n < length; ++n)
            ir[n] = (float) spec[n].real();

        // Early reflection: inverted, low-passed back wave for open backs; a dull inner bounce for closed ones.
        const double reflectDb = c.reflectDb + m.roomDb;
        const auto delay = (size_t) std::lround (c.reflectMs * 0.001 * sampleRate);
        if (delay > 0 && delay < length)
        {
            const float gain = (float) (std::pow (10.0, reflectDb / 20.0) * (c.openBack ? -1.0 : 1.0));
            const float coeff = (float) std::exp (-2 * kPi * c.reflectHz / sampleRate);
            std::vector<float> echo (length, 0.0f);
            float state = 0.0f;
            for (size_t n = 0; n + delay < length; ++n)
            {
                state = ir[n] + coeff * (state - ir[n]);
                echo[n + delay] = state * gain;
            }
            for (size_t n = 0; n < length; ++n)
                ir[n] += echo[n];
        }

        // Fade the last 30 % so the truncation does not ring.
        const size_t fadeStart = length * 7 / 10;
        for (size_t n = fadeStart; n < length; ++n)
        {
            const double t = (double) (n - fadeStart) / (double) (length - fadeStart);
            ir[n] *= (float) (0.5 + 0.5 * std::cos (kPi * t));
        }
        return ir;
    }

    void normalisePeak (std::vector<float>& ir)
    {
        float peak = 0.0f;
        for (float x : ir) peak = std::max (peak, std::abs (x));
        if (peak > 0.0f)
            for (float& x : ir) x *= 0.5f / peak;
    }
} // namespace

const std::vector<CabInfo>& cabinets()
{
    static const std::vector<CabInfo> list = []
    {
        std::vector<CabInfo> out;
        for (const auto& v : voicings()) out.push_back (v.info);
        return out;
    }();
    return list;
}

const std::vector<MicInfo>& mics()
{
    static const std::vector<MicInfo> list = []
    {
        std::vector<MicInfo> out;
        for (const auto& m : micVoicings()) out.push_back (m.info);
        return out;
    }();
    return list;
}

bool isFactoryPath (const std::string& path)
{
    return path.rfind (kPathPrefix, 0) == 0;
}

std::string pathFor (int cab, int mic)
{
    if (cab < 0 || cab >= (int) voicings().size() || mic < 0 || mic >= (int) micVoicings().size())
        return {};
    return std::string (kPathPrefix) + voicings()[(size_t) cab].info.id + "/" + micVoicings()[(size_t) mic].info.id;
}

std::string pathFor (const std::string& cabId, const std::string& micId)
{
    int cab, mic;
    const auto path = std::string (kPathPrefix) + cabId + "/" + micId;
    return parsePath (path, cab, mic) ? path : std::string {};
}

bool parsePath (const std::string& path, int& cab, int& mic)
{
    if (! isFactoryPath (path))
        return false;
    const auto rest = path.substr (std::strlen (kPathPrefix));
    const auto slash = rest.find ('/');
    if (slash == std::string::npos)
        return false;
    const auto cabId = rest.substr (0, slash), micId = rest.substr (slash + 1);
    cab = mic = -1;
    for (size_t i = 0; i < voicings().size(); ++i)
        if (cabId == voicings()[i].info.id) cab = (int) i;
    for (size_t i = 0; i < micVoicings().size(); ++i)
        if (micId == micVoicings()[i].info.id) mic = (int) i;
    return cab >= 0 && mic >= 0;
}

std::string displayName (const std::string& path)
{
    int cab, mic;
    if (! parsePath (path, cab, mic))
        return {};
    const auto& c = voicings()[(size_t) cab].info;
    return std::string (c.size) + " " + c.name + " — " + micVoicings()[(size_t) mic].info.name;
}

std::vector<float> render (int cab, int mic, double sampleRate)
{
    if (cab < 0 || cab >= (int) voicings().size() || mic < 0 || mic >= (int) micVoicings().size() || sampleRate < 8000.0)
        return {};
    const auto& c = voicings()[(size_t) cab];
    std::vector<float> ir;
    if (mic == kBlendMic)
    {
        // Two mics on the same cone: both minimum phase, so they sum without combing.
        ir = renderSingle (c, micVoicings()[0], sampleRate);
        const auto ribbon = renderSingle (c, micVoicings()[2], sampleRate);
        for (size_t n = 0; n < ir.size(); ++n)
            ir[n] = 0.5f * ir[n] + 0.6f * ribbon[n];
    }
    else
    {
        ir = renderSingle (c, micVoicings()[(size_t) mic], sampleRate);
    }
    normalisePeak (ir);
    return ir;
}
} // namespace kumamp::cabs
