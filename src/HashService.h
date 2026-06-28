#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct HashResult
{
    std::wstring sha256;
    std::wstring sha1;
    std::wstring md5;
};

class HashService
{
public:
    static HashResult ComputeAll(const std::vector<BYTE>& data);

private:
    static std::wstring ComputeOne(const std::vector<BYTE>& data, LPCWSTR algorithm);
    static std::wstring BytesToHex(const std::vector<BYTE>& bytes);
};
