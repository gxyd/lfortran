#ifdef _WIN32
#include <windows.h>
#endif

#include <fstream>

#include <bin/tpl/whereami/whereami.h>

#include <lfortran/exception.h>
#include <lfortran/utils.h>
#include <lfortran/string_utils.h>

namespace LFortran {

void get_executable_path(std::string &executable_path, int &dirname_length)
{
    int length;

    length = wai_getExecutablePath(NULL, 0, &dirname_length);
    if (length > 0) {
        std::string path(length+1, '\0');
        wai_getExecutablePath(&path[0], length, &dirname_length);
        executable_path = path;
        if (executable_path[executable_path.size()-1] == '\0') {
            executable_path = executable_path.substr(0,executable_path.size()-1);
        }
    } else {
        throw LFortranException("Cannot determine executable path.");
    }
}

std::string get_runtime_library_dir()
{
    char *env_p = std::getenv("LFORTRAN_RUNTIME_LIBRARY_DIR");
    if (env_p) return env_p;

    std::string path;
    int dirname_length;
    get_executable_path(path, dirname_length);
    std::string dirname = path.substr(0,dirname_length);
    if (   endswith(dirname, "src/bin")
        || endswith(dirname, "src\\bin")
        || endswith(dirname, "SRC\\BIN")) {
        // Development version
        return dirname + "/../runtime";
    } else if (endswith(dirname, "src/lfortran/tests") ||
               endswith(to_lower(dirname), "src\\lfortran\\tests")) {
        // CTest Tests
        return dirname + "/../../runtime";
    } else {
        // Installed version
        return dirname + "/../share/lfortran/lib";
    }
}

std::string get_runtime_library_header_dir()
{
    char *env_p = std::getenv("LFORTRAN_RUNTIME_LIBRARY_HEADER_DIR");
    if (env_p) return env_p;

    return get_runtime_library_dir() + "/impure";
}

bool read_file(const std::string &filename, std::string &text)
{
    std::ifstream ifs(filename.c_str(), std::ios::in | std::ios::binary
            | std::ios::ate);

    std::ifstream::pos_type filesize = ifs.tellg();
    if (filesize < 0) return false;

    ifs.seekg(0, std::ios::beg);

    std::vector<char> bytes(filesize);
    ifs.read(&bytes[0], filesize);

    text = std::string(&bytes[0], filesize);
    return true;
}

bool present(Vec<char*> &v, const char* name) {
    for (auto &a : v) {
        if (std::string(a) == std::string(name)) {
            return true;
        }
    }
    return false;
}

Platform get_platform()
{
#ifdef _WIN32
    return Platform::Windows;
#else
#    ifdef __APPLE__
#        ifdef __aarch64__
    return Platform::macOS_ARM;
#        else
    return Platform::macOS_Intel;
#        endif
#    else
#        ifdef __FreeBSD__
    return Platform::FreeBSD;
#        else
    return Platform::Linux;
#        endif
#    endif
#endif
}

// Platform-specific initialization
// On Windows, enable colors in terminal. On other systems, does nothing.
// Return value: 0 on success, negative number on failure.
int initialize()
{
#ifdef _WIN32
    HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h_stdout == INVALID_HANDLE_VALUE)
        return -1;

    DWORD mode;
    if (! GetConsoleMode(h_stdout, &mode))
        return -2;

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (! SetConsoleMode(h_stdout, mode))
        return -3;

    return 0;
#else
    return 0;
#endif
}

}
