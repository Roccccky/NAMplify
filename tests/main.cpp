#include "TestFramework.h"
#include <algorithm>
#include <cstdio>
#include <string>

int main (int argc, char** argv)
{
    auto& reg = nmptest::Registry::instance();

    std::string filter = argc > 1 ? argv[1] : std::string();

    std::sort (reg.entries.begin(), reg.entries.end(),
               [] (const auto& a, const auto& b) { return a.name < b.name; });

    std::printf ("\nNAMplify Tests\n\n");

    int ran = 0;
    for (auto& e : reg.entries)
    {
        if (! filter.empty() && e.name.find (filter) == std::string::npos)
            continue;

        const size_t before = reg.failures.size();
        reg.current = e.name;
        std::printf ("  %s\n", e.name.c_str());
        e.fn();
        ++ran;
        if (reg.failures.size() == before)
            std::printf ("      ok\n");
    }

    std::printf ("\n--------------------------------------------------\n");
    std::printf ("  %d Prüfungen, %d Zusicherungen, %d Fehler",
                 ran, reg.checks, (int) reg.failures.size());
    if (reg.skipped > 0) std::printf (", %d übersprungen", reg.skipped);
    std::printf ("\n");

    if (! reg.skipNotes.empty())
    {
        std::printf ("\n  Übersprungen:\n");
        for (auto& s : reg.skipNotes) std::printf ("    · %s\n", s.c_str());
    }

    if (! reg.failures.empty())
    {
        std::printf ("\n  Fehler:\n");
        for (auto& f : reg.failures)
            std::printf ("    · %s\n      %s\n", f.where.c_str(), f.what.c_str());
        std::printf ("\n");
        return 1;
    }

    std::printf ("\n");
    return 0;
}
