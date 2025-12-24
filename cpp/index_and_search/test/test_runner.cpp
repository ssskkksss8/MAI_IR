#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>

#if defined(_WIN32)
  #include <windows.h>
  #include <direct.h>
  #define MKDIR(path) _mkdir(path)
#else
  #include <sys/stat.h>
  #include <unistd.h>
  #define MKDIR(path) mkdir(path, 0755)
#endif

static void write_text_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot write file: " << path << "\n";
        std::exit(2);
    }
    out.write(content.data(), (std::streamsize)content.size());
}

static void require_true(bool ok, const std::string& msg) {
    if (!ok) {
        std::cerr << "TEST FAILED: " << msg << "\n";
        std::exit(1);
    }
}

static std::vector<uint32_t> parse_docids_from_searcher_output(const std::string& out) {
    std::vector<uint32_t> ids;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        std::string idstr = (tab == std::string::npos) ? line : line.substr(0, tab);
        uint32_t id = (uint32_t)std::strtoul(idstr.c_str(), nullptr, 10);
        if (id > 0) ids.push_back(id);
    }
    return ids;
}

static void require_ids_eq(const std::vector<uint32_t>& got,
                           const std::vector<uint32_t>& exp,
                           const std::string& name) {
    if (got != exp) {
        std::cerr << "TEST FAILED: " << name << "\n";
        std::cerr << "Expected: ";
        for (auto x : exp) std::cerr << x << " ";
        std::cerr << "\nGot:      ";
        for (auto x : got) std::cerr << x << " ";
        std::cerr << "\n";
        std::exit(1);
    }
}

#if defined(_WIN32)

static bool file_exists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string win_last_error_message(DWORD e) {
    char* msg = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD n = FormatMessageA(flags, NULL, e, 0, (LPSTR)&msg, 0, NULL);
    std::string s;
    if (n && msg) s.assign(msg, msg + n);
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

static std::string fullpath(const std::string& p) {
    char buf[MAX_PATH];
    DWORD n = GetFullPathNameA(p.c_str(), MAX_PATH, buf, NULL);
    if (n == 0 || n >= MAX_PATH) return p;
    return std::string(buf);
}

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w;
    w.resize((size_t)(n > 0 ? n - 1 : 0));
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

static std::wstring win_quote_arg_w(const std::wstring& a) {
    if (a.empty()) return L"\"\"";
    bool need = false;
    for (wchar_t c : a) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'"') { need = true; break; }
    }
    if (!need) return a;

    std::wstring r;
    r.push_back(L'"');
    int bs = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { bs++; continue; }
        if (c == L'"') {
            r.append((size_t)bs * 2 + 1, L'\\');
            r.push_back(L'"');
            bs = 0;
            continue;
        }
        if (bs) { r.append((size_t)bs, L'\\'); bs = 0; }
        r.push_back(c);
    }
    if (bs) r.append((size_t)bs * 2, L'\\');
    r.push_back(L'"');
    return r;
}

static std::string run_process_capture_win(const std::string& exe_abs_utf8,
                                           const std::vector<std::string>& args_utf8,
                                           int* exit_code) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        if (exit_code) *exit_code = 127;
        return "CreatePipe failed";
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};

    std::wstring exe_abs = utf8_to_wide(exe_abs_utf8);

    std::wstring cmdline = win_quote_arg_w(exe_abs);
    for (const auto& a8 : args_utf8) {
        cmdline.push_back(L' ');
        cmdline += win_quote_arg_w(utf8_to_wide(a8));
    }

    std::vector<wchar_t> cmdbuf(cmdline.begin(), cmdline.end());
    cmdbuf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        exe_abs.c_str(),
        cmdbuf.data(),
        NULL, NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    CloseHandle(hWrite);

    if (!ok) {
        DWORD e = GetLastError();
        CloseHandle(hRead);
        if (exit_code) *exit_code = 127;
        std::ostringstream oss;
        oss << win_last_error_message(e) << "\n"
            << "GetLastError=" << (unsigned long)e << "\n";
        return oss.str();
    }

    std::string out;
    char buf[4096];
    DWORD r = 0;
    while (ReadFile(hRead, buf, (DWORD)sizeof(buf), &r, NULL) && r > 0) {
        out.append(buf, buf + r);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    if (exit_code) *exit_code = (int)ec;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    return out;
}

#else

#include <cstdio>

static std::string sh_quote(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r.push_back(c);
    }
    r += "'";
    return r;
}

static std::string run_process_capture_posix(const std::string& exe,
                                             const std::vector<std::string>& args,
                                             int* exit_code) {
    std::string cmd = sh_quote(exe);
    for (auto& a : args) {
        cmd.push_back(' ');
        cmd += sh_quote(a);
    }
    cmd += " 2>&1";

    FILE* p = popen(cmd.c_str(), "r");
    if (!p) {
        if (exit_code) *exit_code = 127;
        return "popen failed\n";
    }
    std::string out;
    char buf[4096];
    while (true) {
        size_t n = std::fread(buf, 1, sizeof(buf), p);
        if (n) out.append(buf, buf + n);
        if (n < sizeof(buf)) break;
    }
    int st = pclose(p);
    if (exit_code) *exit_code = st;
    return out;
}

#endif

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <indexer_bin> <searcher_bin>\n";
        return 2;
    }

    std::string indexer = argv[1];
    std::string searcher = argv[2];

#if defined(_WIN32)
    std::string indexer_abs = fullpath(indexer);
    std::string searcher_abs = fullpath(searcher);
    require_true(file_exists(indexer_abs), "indexer not found: " + indexer_abs);
    require_true(file_exists(searcher_abs), "searcher not found: " + searcher_abs);

    std::string dir = "test_tmp";
    std::string corpus_path = dir + "\\corpus.txt";
    std::string outdir = dir + "\\idx";
#else
    std::string dir = "test_tmp";
    std::string corpus_path = dir + "/corpus.txt";
    std::string outdir = dir + "/idx";
#endif

    (void)MKDIR(dir.c_str());
    (void)MKDIR(outdir.c_str());

    std::string corpus =
        "ЭсМинЕц конвой эсминец\n"
        "конвой конвой фрегат\n"
        "лес море корабль\n"
        "эсминец фрегат\n"
        "конвой корабль\n";

    write_text_file(corpus_path, corpus);

    {
        int ec = -1;
#if defined(_WIN32)
        std::string out = run_process_capture_win(indexer_abs, {corpus_path, outdir}, &ec);
#else
        std::string out = run_process_capture_posix(indexer, {corpus_path, outdir}, &ec);
#endif
        require_true(ec == 0, "indexer exit code != 0, output:\n" + out);
        require_true(out.find("indexed docs: 5") != std::string::npos,
                     "indexer must print 'indexed docs: 5', got:\n" + out);
    }

    {
        int ec = -1;
#if defined(_WIN32)
        std::string out = run_process_capture_win(searcher_abs, {outdir, "эсминец && конвой"}, &ec);
#else
        std::string out = run_process_capture_posix(searcher, {outdir, "эсминец && конвой"}, &ec);
#endif
        require_true(ec == 0, "searcher exit code != 0, output:\n" + out);
        auto ids = parse_docids_from_searcher_output(out);
        require_ids_eq(ids, {1}, "AND query");
    }

    {
        int ec = -1;
#if defined(_WIN32)
        std::string out = run_process_capture_win(searcher_abs, {outdir, "эсминец || конвой"}, &ec);
#else
        std::string out = run_process_capture_posix(searcher, {outdir, "эсминец || конвой"}, &ec);
#endif
        require_true(ec == 0, "searcher exit code != 0, output:\n" + out);
        auto ids = parse_docids_from_searcher_output(out);
        require_ids_eq(ids, {1, 2, 4, 5}, "OR query + TF-IDF order");
    }

    {
        int ec = -1;
#if defined(_WIN32)
        std::string out = run_process_capture_win(searcher_abs, {outdir, "!конвой"}, &ec);
#else
        std::string out = run_process_capture_posix(searcher, {outdir, "!конвой"}, &ec);
#endif
        require_true(ec == 0, "searcher exit code != 0, output:\n" + out);
        auto ids = parse_docids_from_searcher_output(out);
        require_ids_eq(ids, {3, 4}, "NOT query");
    }

    {
        int ec = -1;
#if defined(_WIN32)
        std::string out = run_process_capture_win(searcher_abs, {outdir, "эсминец && (конвой || фрегат)"}, &ec);
#else
        std::string out = run_process_capture_posix(searcher, {outdir, "эсминец && (конвой || фрегат)"}, &ec);
#endif
        require_true(ec == 0, "searcher exit code != 0, output:\n" + out);
        auto ids = parse_docids_from_searcher_output(out);
        require_ids_eq(ids, {1, 4}, "parentheses query");
    }

    {
        int ec = -1;
#if defined(_WIN32)
        std::string out = run_process_capture_win(searcher_abs, {outdir, "эсминец конвой"}, &ec);
#else
        std::string out = run_process_capture_posix(searcher, {outdir, "эсминец конвой"}, &ec);
#endif
        require_true(ec == 0, "searcher exit code != 0, output:\n" + out);
        auto ids = parse_docids_from_searcher_output(out);
        require_ids_eq(ids, {1, 2, 4, 5}, "FUZZY query (no operators) must behave like OR + TF-IDF");
    }

    std::cout << "ALL TESTS PASSED\n";
    return 0;
}