#include "HashService.h"
#include <bcrypt.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")

HashResult HashService::ComputeAll(const std::vector<BYTE>& data)
{
    HashResult result;
    result.sha256 = ComputeOne(data, BCRYPT_SHA256_ALGORITHM);
    result.sha1 = ComputeOne(data, BCRYPT_SHA1_ALGORITHM);
    result.md5 = ComputeOne(data, BCRYPT_MD5_ALGORITHM);
    return result;
}

std::wstring HashService::ComputeOne(const std::vector<BYTE>& data, LPCWSTR algorithm)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD cbData = 0;

    if (BCryptOpenAlgorithmProvider(&alg, algorithm, nullptr, 0) != 0)
        return L"No disponible";

    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &cbData, 0) != 0 ||
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &cbData, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(alg, 0);
        return L"No disponible";
    }

    std::vector<BYTE> object(objectLength);
    std::vector<BYTE> digest(hashLength);

    if (BCryptCreateHash(alg, &hash, object.data(), objectLength, nullptr, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(alg, 0);
        return L"No disponible";
    }

    const ULONG chunk = 1024 * 1024;
    size_t offset = 0;
    while (offset < data.size())
    {
        ULONG part = static_cast<ULONG>((data.size() - offset) > chunk ? chunk : (data.size() - offset));
        if (BCryptHashData(hash, const_cast<PUCHAR>(data.data() + offset), part, 0) != 0)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(alg, 0);
            return L"No disponible";
        }
        offset += part;
    }

    if (BCryptFinishHash(hash, digest.data(), hashLength, 0) != 0)
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return L"No disponible";
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return BytesToHex(digest);
}

std::wstring HashService::BytesToHex(const std::vector<BYTE>& bytes)
{
    std::wstringstream ss;
    ss << std::hex << std::setfill(L'0');
    for (BYTE b : bytes)
        ss << std::setw(2) << static_cast<unsigned>(b);
    return ss.str();
}
