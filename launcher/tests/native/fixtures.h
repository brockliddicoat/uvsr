#pragma once
#include "installer_internal.h"
#include <bcrypt.h>

namespace test
{
    using namespace uvsr::launcher;
    inline constexpr char Commit[] = "1111111111111111111111111111111111111111";
    struct SigningKey
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_KEY_HANDLE value = nullptr;
        std::string publicKey;
        SigningKey();
        ~SigningKey();
        std::string Sign(const Feed& feed, bool canonical = true) const;
        std::string SignPayload(std::string_view bytes, Component component) const;
    };
    void Zip(const fs::path& root, const fs::path& destination, std::optional<std::string> badPath = {},
        bool deflate = true, std::string_view engineDeflate = {});
    Feed MakePackage(const fs::path& root, int64_t sequence = 16, bool legacy = false);
    Json InstallOldLauncher(const Paths& paths, std::string_view owner, bool oldName = false);
    void VerifyProductionServices(const fs::path& engine, const fs::path& launcher);
    void VerifyExactArchive(const fs::path& archive, Feed expected);
    struct Fixture
    {
        fs::path root;
        Paths paths;
        SigningKey signing;
        Services services;
        std::wstring registry;
        std::string suffix;
        Feed renderer, launcher;
        fs::path archive;
        bool failDownload = false;
        bool shellUsed = false;
        Fixture();
        ~Fixture();
        Installer Make();
        void Package(int64_t sequence = 16);
    };
    template<class Fn> void Throws(Fn&& fn)
    {
        try { fn(); } catch (const std::exception&) { return; }
        throw std::runtime_error("expected rejection did not occur");
    }
}
