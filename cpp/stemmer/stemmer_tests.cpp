#include <iostream>
#include <vector>
#include <string>

#ifdef _WIN32
  #include <windows.h>
#endif

#define STEMMER_NO_MAIN
#include "stemmer_ru.cpp"

struct Case { std::string in, expected; };

static bool run_case(const Case& tc) {
    std::string got = stem_utf8_ru(tc.in);
    if (got != tc.expected) {
        std::cerr << "[FAIL] \"" << tc.in << "\" -> \"" << got
                  << "\" (expected \"" << tc.expected << "\")\n";
        return false;
    }
    std::cout << "[PASS] \"" << tc.in << "\" -> \"" << got << "\"\n";
    return true;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::vector<Case> tests = {
        {"СТОЛЫ", "стол"},
        {"Стола", "стол"},
        {"столом", "стол"},
        {"столе", "стол"},
        {"столах", "стол"},
        {"столами", "стол"},
        {"книги", "книг"},
        {"книгой", "книг"},
        {"машины", "машин"},
        {"машиной", "машин"},
        {"рыба", "рыб"},
        {"рыбы", "рыб"},
        {"красивый", "красив"},
        {"красивого", "красив"},
        {"синий", "син"},
        {"синего", "син"},
        {"ёлка", "елк"},
        {"елка", "елк"},
        {"test", "test"},
        {"12345", "12345"},
        {"зая", "зая"}
    };

    int ok = 0, bad = 0;
    for (const auto& tc : tests) (run_case(tc) ? ++ok : ++bad);

    std::cout << "\nTOTAL: " << ok << " passed, " << bad << " failed\n";
    return bad == 0 ? 0 : 1;
}
