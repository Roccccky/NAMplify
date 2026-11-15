#pragma once
// Kleiner Testrahmen ohne Fremdbibliothek, damit die Tests überall laufen, wo
// der Kern baut.

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

namespace nmptest
{

struct Failure { std::string where, what; };

class Registry
{
public:
    struct Entry { std::string name; std::function<void()> fn; };

    static Registry& instance() { static Registry r; return r; }

    void add (const std::string& name, std::function<void()> fn)
    {
        entries.push_back ({ name, std::move (fn) });
    }

    std::vector<Entry> entries;
    std::vector<Failure> failures;
    std::string current;
    int checks = 0;
    int skipped = 0;
    std::vector<std::string> skipNotes;
};

struct Adder
{
    Adder (const char* name, std::function<void()> fn) { Registry::instance().add (name, std::move (fn)); }
};

inline void fail (const std::string& where, const std::string& what)
{
    Registry::instance().failures.push_back ({ where, what });
    std::printf ("      FEHLER  %s\n              %s\n", where.c_str(), what.c_str());
}

inline void skip (const std::string& why)
{
    Registry::instance().skipped++;
    Registry::instance().skipNotes.push_back (Registry::instance().current + ": " + why);
    std::printf ("      ÜBERSPRUNGEN  %s\n", why.c_str());
}

inline std::string fmt (double v)
{
    char b[64];
    std::snprintf (b, sizeof b, "%.6g", v);
    return b;
}

} // namespace nmptest

#define NMP_TEST(name)                                                              \
    static void name();                                                             \
    static ::nmptest::Adder name##_adder (#name, name);                             \
    static void name()

#define NMP_WHERE (std::string (__FILE__) + ":" + std::to_string (__LINE__))

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++::nmptest::Registry::instance().checks;                                    \
        if (! (cond)) ::nmptest::fail (NMP_WHERE, "erwartet: " #cond);               \
    } while (0)

#define CHECK_MSG(cond, msg)                                                        \
    do {                                                                            \
        ++::nmptest::Registry::instance().checks;                                    \
        if (! (cond)) ::nmptest::fail (NMP_WHERE, std::string (msg) + "  [" #cond "]"); \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                       \
    do {                                                                            \
        ++::nmptest::Registry::instance().checks;                                    \
        const double va = (double) (a), vb = (double) (b), vt = (double) (tol);      \
        if (! (std::abs (va - vb) <= vt))                                            \
            ::nmptest::fail (NMP_WHERE, std::string (#a) + " = " + ::nmptest::fmt (va) \
                             + ", erwartet " + ::nmptest::fmt (vb)                    \
                             + " ± " + ::nmptest::fmt (vt)                            \
                             + "  (Abweichung " + ::nmptest::fmt (std::abs (va - vb)) + ")"); \
    } while (0)

#define CHECK_LT(a, b)                                                              \
    do {                                                                            \
        ++::nmptest::Registry::instance().checks;                                    \
        const double va = (double) (a), vb = (double) (b);                           \
        if (! (va < vb))                                                             \
            ::nmptest::fail (NMP_WHERE, std::string (#a) + " = " + ::nmptest::fmt (va) \
                             + " sollte kleiner sein als " + #b + " = " + ::nmptest::fmt (vb)); \
    } while (0)

#define CHECK_GT(a, b)                                                              \
    do {                                                                            \
        ++::nmptest::Registry::instance().checks;                                    \
        const double va = (double) (a), vb = (double) (b);                           \
        if (! (va > vb))                                                             \
            ::nmptest::fail (NMP_WHERE, std::string (#a) + " = " + ::nmptest::fmt (va) \
                             + " sollte größer sein als " + #b + " = " + ::nmptest::fmt (vb)); \
    } while (0)

#define SKIP(why) do { ::nmptest::skip (why); return; } while (0)
