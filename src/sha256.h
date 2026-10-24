#pragma once

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr
{
    enum class Sha256Stage
    {
        None,
        OpenFile,
        ReadFile,
        OpenProvider,
        ReadProviderObjectSize,
        CreateHash,
        HashData,
        FinishHash
    };

    struct Sha256Fault
    {
        Sha256Stage stage = Sha256Stage::None;
        std::size_t occurrence = 1u;
    };

    class Sha256Error final : public std::runtime_error
    {
    public:
        explicit Sha256Error(Sha256Stage stage)
            : std::runtime_error("SHA-256 operation failed"), m_Stage(stage)
        {
        }

        [[nodiscard]] Sha256Stage Stage() const noexcept { return m_Stage; }

    private:
        Sha256Stage m_Stage;
    };

    namespace detail
    {
        struct Sha256FaultState
        {
            Sha256Fault fault;
            std::size_t occurrences = 0u;

            void Check(Sha256Stage stage)
            {
                if (stage == fault.stage &&
                    ++occurrences == fault.occurrence)
                {
                    throw Sha256Error(stage);
                }
            }
        };

        struct CloseAlgorithm
        {
            void operator()(void* handle) const noexcept
            {
                BCryptCloseAlgorithmProvider(handle, 0u);
            }
        };

        struct DestroyHash
        {
            void operator()(void* handle) const noexcept
            {
                BCryptDestroyHash(handle);
            }
        };

        class Sha256State
        {
        public:
            explicit Sha256State(Sha256FaultState& fault) : m_Fault(fault)
            {
                BCRYPT_ALG_HANDLE algorithm = nullptr;
                m_Fault.Check(Sha256Stage::OpenProvider);
                const NTSTATUS openStatus = BCryptOpenAlgorithmProvider(
                    &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0u);
                m_Algorithm.reset(algorithm);
                Require(openStatus, Sha256Stage::OpenProvider);

                DWORD objectSize = 0u;
                DWORD resultSize = 0u;
                m_Fault.Check(Sha256Stage::ReadProviderObjectSize);
                Require(BCryptGetProperty(m_Algorithm.get(),
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectSize),
                    sizeof(objectSize), &resultSize, 0u),
                    Sha256Stage::ReadProviderObjectSize);
                m_Object.resize(objectSize);

                BCRYPT_HASH_HANDLE hash = nullptr;
                m_Fault.Check(Sha256Stage::CreateHash);
                const NTSTATUS createStatus = BCryptCreateHash(
                    m_Algorithm.get(), &hash, m_Object.data(),
                    static_cast<ULONG>(m_Object.size()), nullptr, 0u, 0u);
                m_Hash.reset(hash);
                Require(createStatus, Sha256Stage::CreateHash);
            }

            void Update(const char* data, std::size_t size)
            {
                while (size != 0u)
                {
                    const std::size_t chunk = std::min<std::size_t>(size,
                        (std::numeric_limits<ULONG>::max)());
                    m_Fault.Check(Sha256Stage::HashData);
                    Require(BCryptHashData(m_Hash.get(),
                        reinterpret_cast<PUCHAR>(const_cast<char*>(data)),
                        static_cast<ULONG>(chunk), 0u),
                        Sha256Stage::HashData);
                    data += chunk;
                    size -= chunk;
                }
            }

            [[nodiscard]] std::string Finish()
            {
                std::array<unsigned char, 32u> digest{};
                m_Fault.Check(Sha256Stage::FinishHash);
                Require(BCryptFinishHash(m_Hash.get(), digest.data(),
                    static_cast<ULONG>(digest.size()), 0u),
                    Sha256Stage::FinishHash);
                constexpr char Hex[] = "0123456789abcdef";
                std::string result;
                result.reserve(digest.size() * 2u);
                for (const unsigned char byte : digest)
                {
                    result.push_back(Hex[byte >> 4u]);
                    result.push_back(Hex[byte & 0x0fu]);
                }
                return result;
            }

        private:
            static void Require(NTSTATUS status, Sha256Stage stage)
            {
                if (status < 0)
                    throw Sha256Error(stage);
            }

            Sha256FaultState& m_Fault;
            std::unique_ptr<void, CloseAlgorithm> m_Algorithm;
            std::unique_ptr<void, DestroyHash> m_Hash;
            std::vector<unsigned char> m_Object;
        };
    }

    [[nodiscard]] inline std::string Sha256(
        std::string_view input,
        Sha256Fault fault = {})
    {
        detail::Sha256FaultState faultState{ fault };
        detail::Sha256State state(faultState);
        state.Update(input.data(), input.size());
        return state.Finish();
    }

    [[nodiscard]] inline std::string Sha256File(
        const std::filesystem::path& path,
        Sha256Fault fault = {})
    {
        detail::Sha256FaultState faultState{ fault };
        faultState.Check(Sha256Stage::OpenFile);
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw Sha256Error(Sha256Stage::OpenFile);

        detail::Sha256State state(faultState);
        std::vector<char> buffer(1024u * 1024u);
        while (input)
        {
            faultState.Check(Sha256Stage::ReadFile);
            input.read(buffer.data(),
                static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0)
                state.Update(buffer.data(), static_cast<std::size_t>(count));
        }
        if (!input.eof())
            throw Sha256Error(Sha256Stage::ReadFile);
        return state.Finish();
    }
}
