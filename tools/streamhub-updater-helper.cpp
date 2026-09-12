#ifdef _WIN32

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {

struct Options {
    DWORD pid = 0;
    std::wstring source;
    std::wstring target;
    std::wstring sha256;
    std::wstring restart;
};

bool ParseDword(const std::wstring &value, DWORD *result)
{
    if (value.empty() || !result)
        return false;
    wchar_t *end = nullptr;
    const unsigned long parsed = wcstoul(value.c_str(), &end, 10);
    if (!end || *end != L'\0' || parsed == 0 || parsed > MAXDWORD)
        return false;
    *result = static_cast<DWORD>(parsed);
    return true;
}

bool ParseArguments(int argc, wchar_t **argv, Options *options)
{
    if (!options)
        return false;
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::wstring key = argv[index];
        const std::wstring value = argv[index + 1];
        if (key == L"--pid") {
            if (!ParseDword(value, &options->pid))
                return false;
        } else if (key == L"--source") {
            options->source = value;
        } else if (key == L"--target") {
            options->target = value;
        } else if (key == L"--sha256") {
            options->sha256 = value;
        } else if (key == L"--restart") {
            options->restart = value;
        } else {
            return false;
        }
    }
    return argc == 11 && options->pid != 0 && !options->source.empty() &&
           !options->target.empty() && options->sha256.size() == 64 &&
           !options->restart.empty();
}

bool IsSha256(const std::wstring &value)
{
    if (value.size() != 64)
        return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t character) {
        return (character >= L'0' && character <= L'9') ||
               (character >= L'a' && character <= L'f') ||
               (character >= L'A' && character <= L'F');
    });
}

std::wstring Lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(towlower(character));
    });
    return value;
}

bool HashFile(const std::wstring &path, std::wstring *hash)
{
    if (!hash)
        return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                                &resultLength, 0) == 0;
    std::vector<UCHAR> object(objectLength);
    std::vector<UCHAR> digest(32);
    if (ok)
        ok = BCryptCreateHash(algorithm, &hashHandle, object.data(), objectLength, nullptr, 0, 0) == 0;

    std::vector<UCHAR> buffer(1024 * 1024);
    while (ok) {
        DWORD bytesRead = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            ok = false;
            break;
        }
        if (bytesRead == 0)
            break;
        ok = BCryptHashData(hashHandle, buffer.data(), bytesRead, 0) == 0;
    }
    if (ok)
        ok = BCryptFinishHash(hashHandle, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0;

    if (hashHandle)
        BCryptDestroyHash(hashHandle);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);

    if (!ok)
        return false;

    static constexpr wchar_t digits[] = L"0123456789abcdef";
    hash->clear();
    hash->reserve(digest.size() * 2);
    for (const UCHAR byte : digest) {
        hash->push_back(digits[byte >> 4]);
        hash->push_back(digits[byte & 0x0f]);
    }
    return true;
}

bool WaitForProcess(DWORD pid)
{
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return GetLastError() == ERROR_INVALID_HANDLE || GetLastError() == ERROR_NOT_FOUND;
    const DWORD result = WaitForSingleObject(process, 60000);
    CloseHandle(process);
    return result == WAIT_OBJECT_0;
}

bool RestoreBackup(const std::wstring &backup, const std::wstring &target)
{
    if (GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;
    DeleteFileW(target.c_str());
    return MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool ReplaceDll(const Options &options)
{
    if (!IsSha256(options.sha256) || options.source == options.target)
        return false;

    std::wstring actualHash;
    if (!HashFile(options.source, &actualHash) || Lower(actualHash) != Lower(options.sha256))
        return false;

    const DWORD targetAttributes = GetFileAttributesW(options.target.c_str());
    const bool targetExists = targetAttributes != INVALID_FILE_ATTRIBUTES;
    const std::wstring backup = options.target + L".before-update";
    const std::wstring staged = options.target + L".streamhub-new";
    DeleteFileW(staged.c_str());
    if (!CopyFileW(options.source.c_str(), staged.c_str(), FALSE))
        return false;

    std::wstring stagedHash;
    if (!HashFile(staged, &stagedHash) || Lower(stagedHash) != Lower(options.sha256)) {
        DeleteFileW(staged.c_str());
        return false;
    }

    if (targetExists) {
        DeleteFileW(backup.c_str());
        if (!CopyFileW(options.target.c_str(), backup.c_str(), FALSE)) {
            DeleteFileW(staged.c_str());
            return false;
        }
    }

    if (MoveFileExW(staged.c_str(), options.target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;

    DeleteFileW(staged.c_str());
    if (targetExists)
        RestoreBackup(backup, options.target);
    return false;
}

bool RestartProcess(const std::wstring &path)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool started = CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE,
                                        0, nullptr, nullptr, &startup, &process) != 0;
    if (started) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return started;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 2;

    Options options;
    const bool valid = ParseArguments(argc, argv, &options);
    LocalFree(argv);
    if (!valid)
        return 2;

    if (!WaitForProcess(options.pid))
        return 3;
    if (!ReplaceDll(options))
        return 4;
    return RestartProcess(options.restart) ? 0 : 5;
}

#else
int main()
{
    return 1;
}
#endif
