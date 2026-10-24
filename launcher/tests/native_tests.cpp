#include "internal.h"

#include <bcrypt.h>
#include <miniz.h>
#include <objbase.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <utility>

namespace fs = std::filesystem;
using namespace uvsr::launcher;

namespace
{
    [[noreturn]] void fail(std::string_view message)
    {
        throw std::runtime_error(std::string(message));
    }

    void expect(bool condition, std::string_view message = "assertion failed")
    {
        if (!condition)
            fail(message);
    }

    template<typename Function>
    void expect_error(Function&& function)
    {
        try
        {
            std::forward<Function>(function)();
        }
        catch (const std::exception&)
        {
            return;
        }
        fail("expected a rejected contract");
    }

    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            std::array<wchar_t, MAX_PATH + 1> temporary{};
            const DWORD length = GetTempPathW(
                static_cast<DWORD>(temporary.size()), temporary.data());
            if (length == 0 || length >= temporary.size())
                detail::throw_last_error("Windows could not locate test storage");
            m_Base = fs::path(temporary.data()) /
                L"UVSR Launcher Native Tests";
            m_Path = m_Base / widen(guid_string(detail::new_guid()));
            fs::create_directories(m_Path);
        }

        ~TemporaryDirectory()
        {
            try
            {
                if (is_strict_descendant(m_Path, m_Base))
                {
                    std::error_code error;
                    fs::remove_all(m_Path, error);
                }
            }
            catch (...)
            {
            }
        }

        [[nodiscard]] const fs::path& path() const noexcept { return m_Path; }

    private:
        fs::path m_Base;
        fs::path m_Path;
    };

    template<typename T, NTSTATUS(WINAPI* Close)(T)>
    class BCryptHandle final
    {
    public:
        BCryptHandle() = default;
        explicit BCryptHandle(T value) : m_Value(value) {}
        ~BCryptHandle()
        {
            if (m_Value)
                static_cast<void>(Close(m_Value));
        }
        BCryptHandle(const BCryptHandle&) = delete;
        BCryptHandle& operator=(const BCryptHandle&) = delete;
        [[nodiscard]] T get() const noexcept { return m_Value; }

    private:
        T m_Value = nullptr;
    };

    NTSTATUS WINAPI close_algorithm(BCRYPT_ALG_HANDLE value)
    {
        return BCryptCloseAlgorithmProvider(value, 0);
    }

    NTSTATUS WINAPI close_key(BCRYPT_KEY_HANDLE value)
    {
        return BCryptDestroyKey(value);
    }

    using Algorithm = BCryptHandle<BCRYPT_ALG_HANDLE,
        close_algorithm>;
    using Key = BCryptHandle<BCRYPT_KEY_HANDLE, close_key>;

    struct SignedEnvelope
    {
        std::vector<std::byte> envelope;
        std::vector<std::byte> publicKey;
    };

    [[nodiscard]] std::string base64(std::span<const std::byte> value)
    {
        DWORD characters = 0;
        if (!CryptBinaryToStringA(
                reinterpret_cast<const BYTE*>(value.data()),
                static_cast<DWORD>(value.size()),
                CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                nullptr, &characters) || characters == 0)
            fail("base64 sizing failed");
        std::string result(characters, '\0');
        if (!CryptBinaryToStringA(
                reinterpret_cast<const BYTE*>(value.data()),
                static_cast<DWORD>(value.size()),
                CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                result.data(), &characters))
            fail("base64 encoding failed");
        if (!result.empty() && result.back() == '\0')
            result.pop_back();
        return result;
    }

    [[nodiscard]] SignedEnvelope sign_payload(
        std::string_view payload,
        int schema)
    {
        BCRYPT_ALG_HANDLE rawAlgorithm = nullptr;
        if (BCryptOpenAlgorithmProvider(&rawAlgorithm,
                BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0)
            fail("P-256 provider could not open");
        Algorithm algorithm(rawAlgorithm);
        BCRYPT_KEY_HANDLE rawKey = nullptr;
        if (BCryptGenerateKeyPair(algorithm.get(), &rawKey, 256, 0) < 0)
            fail("P-256 test key generation failed");
        Key key(rawKey);
        if (BCryptFinalizeKeyPair(key.get(), 0) < 0)
            fail("P-256 test key finalization failed");

        std::array<unsigned char, 32> digest{};
        BCRYPT_ALG_HANDLE rawHash = nullptr;
        if (BCryptOpenAlgorithmProvider(&rawHash, BCRYPT_SHA256_ALGORITHM,
                nullptr, 0) < 0)
            fail("SHA-256 provider could not open");
        Algorithm hash(rawHash);
        if (BCryptHash(hash.get(), nullptr, 0,
                reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),
                static_cast<ULONG>(payload.size()), digest.data(),
                static_cast<ULONG>(digest.size())) < 0)
            fail("test payload hashing failed");

        ULONG signatureBytes = 0;
        if (BCryptSignHash(key.get(), nullptr, digest.data(),
                static_cast<ULONG>(digest.size()), nullptr, 0,
                &signatureBytes, 0) < 0 || signatureBytes != 64u)
            fail("P-256 signature sizing failed");
        std::vector<std::byte> signature(signatureBytes);
        if (BCryptSignHash(key.get(), nullptr, digest.data(),
                static_cast<ULONG>(digest.size()),
                reinterpret_cast<PUCHAR>(signature.data()), signatureBytes,
                &signatureBytes, 0) < 0 || signatureBytes != 64u)
            fail("P-256 signing failed");

        ULONG blobBytes = 0;
        if (BCryptExportKey(key.get(), nullptr, BCRYPT_ECCPUBLIC_BLOB,
                nullptr, 0, &blobBytes, 0) < 0 ||
            blobBytes != sizeof(BCRYPT_ECCKEY_BLOB) + 64u)
            fail("P-256 public key sizing failed");
        std::vector<std::byte> blob(blobBytes);
        if (BCryptExportKey(key.get(), nullptr, BCRYPT_ECCPUBLIC_BLOB,
                reinterpret_cast<PUCHAR>(blob.data()), blobBytes,
                &blobBytes, 0) < 0)
            fail("P-256 public key export failed");
        const auto* header = reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(
            blob.data());
        if (header->dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC ||
            header->cbKey != 32u)
            fail("P-256 test key had the wrong curve");

        constexpr std::array<unsigned char, 27> SpkiPrefix = {
            0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
            0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48,
            0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04
        };
        std::vector<std::byte> publicKey(SpkiPrefix.size() + 64u);
        std::memcpy(publicKey.data(), SpkiPrefix.data(), SpkiPrefix.size());
        std::memcpy(publicKey.data() + SpkiPrefix.size(),
            blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), 64u);

        const auto payloadBytes = std::as_bytes(std::span(payload));
        const std::string envelope =
            "{\"schemaVersion\":" + std::to_string(schema) +
            ",\"keyId\":\"test-key\",\"payloadBase64\":\"" +
            base64(payloadBytes) + "\",\"signatureBase64\":\"" +
            base64(signature) + "\"}";
        const auto bytes = std::as_bytes(std::span(envelope));
        return { { bytes.begin(), bytes.end() }, std::move(publicKey) };
    }

    [[nodiscard]] std::string launcher_payload(
        std::int64_t sequence = 16,
        std::string_view version = "1.2.0",
        std::string_view hash =
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
    {
        return "{\"schemaVersion\":2,\"productId\":\"" +
            std::string(ProductId) +
            "\",\"channel\":\"stable\",\"releaseSequence\":" +
            std::to_string(sequence) + ",\"version\":\"" +
            std::string(version) +
            "\",\"sourceCommit\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"," +
            "\"artifact\":{\"name\":\"uvsr-launcher.exe\",\"size\":1234," +
            "\"sha256\":\"" + std::string(hash) + "\"}}";
    }

    [[nodiscard]] std::string renderer_payload(
        std::uint64_t size = 1234,
        std::string_view hash =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        std::int64_t sequence = 16,
        std::string_view source =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        std::string_view settings =
            "9c50b0f1515e89d856c8ebb627b86984",
        std::string_view version = "40016.45297.20830.35288")
    {
        return "{\"schemaVersion\":1,\"productId\":\"" +
            std::string(ProductId) +
            "\",\"channel\":\"stable\",\"releaseSequence\":" +
            std::to_string(sequence) + ",\"sourceCommit\":\"" +
            std::string(source) + "\",\"settingsHash\":\"" +
            std::string(settings) + "\",\"engineVersion\":\"" +
            std::string(version) +
            "\",\"artifact\":{\"name\":\"uvsr-renderer-windows-11-x64.zip\"," +
            "\"size\":" + std::to_string(size) + ",\"sha256\":\"" +
            std::string(hash) + "\"}}";
    }

    [[nodiscard]] LauncherFeed verified_launcher_feed()
    {
        const SignedEnvelope signedFeed = sign_payload(launcher_payload(), 2);
        return verify_launcher_feed(signedFeed.envelope,
            signedFeed.publicKey, "test-key");
    }

    [[nodiscard]] RendererFeed verified_renderer_feed(
        std::uint64_t size = 1234,
        std::string_view hash =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        std::int64_t sequence = 16,
        std::string_view source =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        std::string_view settings =
            "9c50b0f1515e89d856c8ebb627b86984",
        std::string_view version = "40016.45297.20830.35288")
    {
        const SignedEnvelope signedFeed = sign_payload(renderer_payload(size,
            hash, sequence, source, settings, version), 1);
        return verify_renderer_feed(signedFeed.envelope,
            signedFeed.publicKey, "test-key");
    }

    void write_bytes(
        const fs::path& path,
        std::span<const std::byte> bytes)
    {
        fs::create_directories(path.parent_path());
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            detail::throw_last_error("Windows could not create a test file");
        DWORD written = 0;
        const bool success = bytes.size() <=
                std::numeric_limits<DWORD>::max() &&
            WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                &written, nullptr) != FALSE && written == bytes.size();
        const DWORD error = success ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (!success)
        {
            SetLastError(error);
            detail::throw_last_error("Windows could not write a test file");
        }
    }

    void write_zip(
        const fs::path& path,
        std::initializer_list<std::pair<const char*, std::string_view>> files)
    {
        mz_zip_archive archive{};
        if (!mz_zip_writer_init_heap(&archive, 0, 0))
            fail("test ZIP creation could not start");
        void* data = nullptr;
        size_t size = 0;
        try
        {
            for (const auto& [name, contents] : files)
            {
                if (!mz_zip_writer_add_mem(&archive, name, contents.data(),
                        contents.size(),
                        static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION)))
                    fail("test ZIP entry could not be added");
            }
            if (!mz_zip_writer_finalize_heap_archive(&archive, &data, &size) ||
                !data || size == 0)
                fail("test ZIP could not be finalized");
            write_bytes(path, { static_cast<const std::byte*>(data), size });
            mz_free(data);
            data = nullptr;
            static_cast<void>(mz_zip_writer_end(&archive));
        }
        catch (...)
        {
            if (data)
                mz_free(data);
            static_cast<void>(mz_zip_writer_end(&archive));
            throw;
        }
    }

    [[nodiscard]] std::int64_t parse_int64(std::wstring_view text)
    {
        const std::string value = narrow(text);
        std::int64_t result = 0;
        const auto parsed = std::from_chars(value.data(),
            value.data() + value.size(), result);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size())
            fail("integer argument was invalid");
        return result;
    }

    [[nodiscard]] std::string run_identity(const fs::path& engine)
    {
        TemporaryDirectory temporary;
        const fs::path output = temporary.path() / L"identity.json";
        HANDLE file = CreateFileW(output.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            detail::throw_last_error("Windows could not create identity output");
        SetHandleInformation(file, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = file;
        startup.hStdError = file;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        std::wstring command = L"\"" + engine.native() +
            L"\" --identity-json";
        if (!CreateProcessW(engine.c_str(), command.data(), nullptr, nullptr,
                TRUE, CREATE_NO_WINDOW, nullptr, engine.parent_path().c_str(),
                &startup, &process))
        {
            CloseHandle(file);
            detail::throw_last_error("Windows could not start the exact engine");
        }
        CloseHandle(process.hThread);
        const DWORD wait = WaitForSingleObject(process.hProcess, 30'000);
        DWORD exitCode = std::numeric_limits<DWORD>::max();
        static_cast<void>(GetExitCodeProcess(process.hProcess, &exitCode));
        CloseHandle(process.hProcess);
        FlushFileBuffers(file);
        CloseHandle(file);
        if (wait != WAIT_OBJECT_0 || exitCode != 0)
            fail("the exact engine identity process failed");
        return detail::read_text(output, 1024u * 1024u);
    }

    void engine_version_test()
    {
        expect(is_engine_version("40016.45297.20830.35288"));
        expect(!is_engine_version("52087.1234.34644.65536"));
        expect(!is_engine_version("040016.45297.20830.35288"));
        expect(!is_engine_version("40016.45297.20830"));
    }

    void signed_launcher_feed_test()
    {
        const LauncherFeed feed = verified_launcher_feed();
        expect(feed.schemaVersion == 2 && feed.releaseSequence == 16);
        expect(feed.version == "1.2.0");
        expect(feed.artifact.name == "uvsr-launcher.exe");
    }

    void signed_renderer_feed_test()
    {
        const RendererFeed feed = verified_renderer_feed();
        expect(feed.schemaVersion == 1 && feed.releaseSequence == 16);
        expect(feed.settingsHash == "9c50b0f1515e89d856c8ebb627b86984");
        expect(feed.artifact.name == "uvsr-renderer-windows-11-x64.zip");
    }

    void signed_feed_rejection_test()
    {
        SignedEnvelope signedFeed = sign_payload(launcher_payload(), 2);
        const std::string marker = "payloadBase64\":\"";
        const auto markerBytes = std::as_bytes(std::span(marker));
        auto first = std::search(signedFeed.envelope.begin(),
            signedFeed.envelope.end(), markerBytes.begin(), markerBytes.end());
        expect(first != signedFeed.envelope.end());
        first += static_cast<std::ptrdiff_t>(markerBytes.size());
        *first = *first == static_cast<std::byte>('A')
            ? static_cast<std::byte>('B') : static_cast<std::byte>('A');
        expect_error([&]
        {
            static_cast<void>(verify_launcher_feed(signedFeed.envelope,
                signedFeed.publicKey, "test-key"));
        });

        const std::string valid = launcher_payload();
        const std::string duplicate =
            "{\"schemaVersion\":2,\"schemaVersion\":2," +
            valid.substr(valid.find(',') + 1u);
        SignedEnvelope duplicateFeed = sign_payload(duplicate, 2);
        expect_error([&]
        {
            static_cast<void>(verify_launcher_feed(duplicateFeed.envelope,
                duplicateFeed.publicKey, "test-key"));
        });
    }

    void update_classification_test()
    {
        LauncherFeed launcher = verified_launcher_feed();
        expect(classify_launcher_update(15, "1.1.0",
            "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
            true, launcher) == ComponentUpdateState::UpdateAvailable);
        expect(classify_launcher_update(16, launcher.version,
            launcher.artifact.sha256, true, launcher) ==
            ComponentUpdateState::Current);
        expect(classify_launcher_update(16, launcher.version,
            launcher.artifact.sha256, false, launcher) ==
            ComponentUpdateState::RepairNeeded);
        expect_error([&]
        {
            static_cast<void>(classify_launcher_update(16, "1.2.1",
                launcher.artifact.sha256, true, launcher));
        });

        RendererFeed renderer = verified_renderer_feed();
        InstallSnapshot absent;
        expect(classify_renderer_update(absent, renderer) ==
            ComponentUpdateState::NotInstalled);
        InstallState state;
        state.installationId = detail::new_guid();
        state.activeVersionId =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-20260825000000-aaaaaaaa";
        state.releaseSequence = renderer.releaseSequence;
        state.commit = renderer.sourceCommit;
        state.settingsHash = renderer.settingsHash;
        state.engineVersion = renderer.engineVersion;
        state.artifactSha256 = renderer.artifact.sha256;
        state.executableSha256 =
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        state.installedUtc = "2026-08-25T00:00:00.000Z";
        InstallSnapshot installed{ true, true, state.installationId, state,
            {}, "installed", false };
        expect(classify_renderer_update(installed, renderer) ==
            ComponentUpdateState::Current);
        installed.damaged = true;
        expect(classify_renderer_update(installed, renderer) ==
            ComponentUpdateState::RepairNeeded);
        installed.damaged = false;
        renderer.sourceCommit =
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        expect_error([&]
        {
            static_cast<void>(classify_renderer_update(installed, renderer));
        });
    }

    void state_and_path_test()
    {
        TemporaryDirectory temporary;
        const InstallerPaths paths = InstallerPaths::create(
            temporary.path() / L"local", temporary.path() / L"desktop",
            temporary.path() / L"programs");
        expect(paths.programRoot.filename() == L"UVSR");
        expect(paths.launcher_executable(
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
                .filename() == LauncherExecutableName);
        expect(paths.version_executable(
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-20260825000000-aaaaaaaa")
                .filename() ==
            EngineExecutableName);
        expect(is_strict_descendant(paths.programRoot, temporary.path()));
        expect(!is_strict_descendant(temporary.path(), paths.programRoot));
        expect_error([&]
        {
            static_cast<void>(paths.version_executable("../escape"));
        });

        OwnerMarker owner;
        owner.installationId = detail::new_guid();
        const std::string serialized = detail::serialize_owner(owner);
        const OwnerMarker parsed = detail::parse_owner(serialized);
        expect(guid_equal(owner.installationId, parsed.installationId));
        std::string unknown = serialized;
        const std::size_t end = unknown.rfind('}');
        unknown.insert(end, ",\"unknown\":true");
        expect_error([&]
        {
            static_cast<void>(detail::parse_owner(unknown));
        });

        constexpr std::string_view CsharpState = R"({
  "schemaVersion": 1,
  "installationId": "11111111-2222-3333-4444-555555555555",
  "activeVersionId": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-20260825000000-aaaaaaaa",
  "releaseSequence": 16,
  "commit": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "settingsHash": "9c50b0f1515e89d856c8ebb627b86984",
  "engineVersion": "40016.45297.20830.35288",
  "artifactSha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "executableSha256": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
  "desktopShortcut": true,
  "installedUtc": "2026-08-25T00:00:00+00:00"
})";
        const InstallState compatible = detail::parse_install_state(CsharpState);
        detail::validate_install_state(compatible,
            parse_guid("11111111-2222-3333-4444-555555555555"));
        const InstallState roundTrip = detail::parse_install_state(
            detail::serialize_install_state(compatible));
        expect(roundTrip.activeVersionId == compatible.activeVersionId &&
            roundTrip.installedUtc == compatible.installedUtc);
        expect_error([]
        {
            const detail::Json unsafe = detail::parse_json("9007199254740992");
            static_cast<void>(detail::integer_value(unsafe, "unsafe integer"));
        });
    }

    void ui_state_test()
    {
        expect(reconcile_renderer_click(RendererButtonIntent::Launch,
            ProcessState::NotRunning) == RendererClickAction::Launch);
        expect(reconcile_renderer_click(RendererButtonIntent::Close,
            ProcessState::Running) == RendererClickAction::Close);
        expect(reconcile_renderer_click(RendererButtonIntent::Launch,
            ProcessState::Running) == RendererClickAction::None);
        expect(renderer_active_state(RendererBusyAction::Launching).marquee);
        expect(renderer_success_state(RendererBusyAction::Closing).progress ==
            100);
        expect(update_terminal_state(UpdateFlowExit::NoSelection).phase ==
            L"Ready");
        InstallSnapshot snapshot;
        expect(install_button_text(snapshot) == L"Install");
        snapshot.installed = true;
        expect(install_button_text(snapshot) == L"Installed");
        snapshot.damaged = true;
        expect(install_button_text(snapshot) == L"Install");
    }

    void process_and_shortcut_test()
    {
        const fs::path current = detail::current_executable();
        const ExactProcessInspection processes =
            inspect_processes_by_executable(current);
        const auto match = std::find_if(processes.matches.begin(),
            processes.matches.end(), [](const ExactProcessIdentity& value)
            {
                return value.processId == GetCurrentProcessId();
            });
        expect(processes.state == ProcessState::Running &&
            match != processes.matches.end());
        expect(inspect_exact_process(*match) == ProcessState::Running);
        ExactProcessIdentity stale = *match;
        ++stale.creationTime;
        expect(inspect_exact_process(stale) == ProcessState::NotRunning);

        TemporaryDirectory temporary;
        const InstallerPaths paths = InstallerPaths::create(
            temporary.path() / L"local", temporary.path() / L"desktop",
            temporary.path() / L"programs");
        const GUID installation = detail::new_guid();
        OwnerMarker owner;
        owner.installationId = installation;
        fs::create_directories(paths.programRoot);
        detail::write_atomic(paths.program_marker(),
            detail::serialize_owner(owner));
        const std::string hash = sha256(current);
        const fs::path installed = paths.launcher_executable(hash);
        fs::create_directories(installed.parent_path());
        fs::copy_file(current, installed, fs::copy_options::none);
        const fs::path shortcut = temporary.path() / L"test.lnk";
        detail::ShellIntegration::create_shortcut(shortcut, installed, L"",
            installed.parent_path(), L"UVSR native shortcut test");
        const detail::ShortcutInfo value =
            detail::ShellIntegration::read_shortcut(shortcut);
        expect(detail::path_equal(value.target, installed));
        expect(value.arguments.empty());
        expect(detail::ShellIntegration(paths).shortcut_owned(
            value, installation));
    }

    void unsafe_archive_test()
    {
        TemporaryDirectory temporary;
        constexpr std::array UnsafePaths = {
            "../escape.txt",
            "bin/licenses/CON.txt",
            "bin/licenses/readme. ",
            "bin/licenses/LPT1"
        };
        for (std::size_t index = 0; index < UnsafePaths.size(); ++index)
        {
            const fs::path archive = temporary.path() /
                (std::to_wstring(index) + L"-" +
                    std::wstring(RendererArtifactName));
            write_zip(archive, { { UnsafePaths[index], "unsafe" } });
            const RendererFeed feed = verified_renderer_feed(
                fs::file_size(archive), sha256(archive));
            expect_error([&]
            {
                static_cast<void>(stage_renderer_archive_for_test(archive,
                    temporary.path() /
                        (L"package-" + std::to_wstring(index)), feed));
            });
        }
        expect(!fs::exists(temporary.path().parent_path() / L"escape.txt"));
    }

    struct NamedTest
    {
        std::string_view name;
        void (*body)();
    };

    constexpr std::array Tests = {
        NamedTest{ "engine version syntax", engine_version_test },
        NamedTest{ "signed launcher feed", signed_launcher_feed_test },
        NamedTest{ "signed renderer feed", signed_renderer_feed_test },
        NamedTest{ "signed feed rejection", signed_feed_rejection_test },
        NamedTest{ "update classification", update_classification_test },
        NamedTest{ "state and path contracts", state_and_path_test },
        NamedTest{ "frontend state contracts", ui_state_test },
        NamedTest{ "process and shortcut services", process_and_shortcut_test },
        NamedTest{ "unsafe package path rejection", unsafe_archive_test }
    };
}

namespace uvsr::launcher
{
    int verify_renderer_archive_command(
        const std::vector<std::wstring>& arguments)
    {
        if (arguments.size() != 6u ||
            arguments[0] != L"--verify-renderer-archive")
        {
            std::cerr << "usage: --verify-renderer-archive "
                "<uvsr-renderer-windows-11-x64.zip> <source-commit> "
                "<settings-hash> <engine-version> <release-sequence>\n";
            return 2;
        }
        try
        {
            const fs::path archive = fs::absolute(arguments[1]);
            if (!fs::is_regular_file(archive) ||
                archive.filename() != RendererArtifactName)
                fail("the exact canonical renderer archive was not supplied");
            const std::int64_t sequence = parse_int64(arguments[5]);
            const std::uint64_t size = fs::file_size(archive);
            const std::string hash = sha256(archive);
            const RendererFeed feed = verified_renderer_feed(size, hash,
                sequence, narrow(arguments[2]), narrow(arguments[3]),
                narrow(arguments[4]));
            TemporaryDirectory temporary;
            const PackageManifest manifest = stage_renderer_archive_for_test(
                archive, temporary.path() / L"package", feed);
            expect(manifest.sourceCommit == feed.sourceCommit);
            expect(manifest.settingsHash == feed.settingsHash);
            expect(manifest.engineVersion == feed.engineVersion);
            expect(manifest.releaseSequence == feed.releaseSequence);
            std::cout << "PASS exact renderer archive completed the native "
                "signed stage and validation path at sequence " << sequence << "\n";
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "FAIL exact renderer archive: " << error.what() << "\n";
            return 1;
        }
    }

    int verify_production_services_command(
        const std::vector<std::wstring>& arguments)
    {
        if (arguments.size() != 3u ||
            arguments[0] != L"--verify-production-services")
        {
            std::cerr << "usage: --verify-production-services "
                "<uvsr-engine.exe> <uvsr-launcher.exe>\n";
            return 2;
        }
        try
        {
            const fs::path engine = fs::absolute(arguments[1]);
            const fs::path launcher = fs::absolute(arguments[2]);
            expect(engine.filename() == EngineExecutableName &&
                fs::is_regular_file(engine));
            expect(launcher.filename() == LauncherExecutableName &&
                fs::is_regular_file(launcher));
            const fs::path packageRoot = engine.parent_path().parent_path();
            const PackageManifest manifest = read_package_manifest(packageRoot);
            validate_renderer_package(packageRoot, manifest);

            const ExactProcessInspection current =
                inspect_processes_by_executable(detail::current_executable());
            expect(current.state == ProcessState::Running &&
                std::ranges::any_of(current.matches,
                    [](const ExactProcessIdentity& value)
                    {
                        return value.processId == GetCurrentProcessId();
                    }));

            const detail::Json identity = detail::parse_json(
                run_identity(engine));
            expect(detail::string_value(detail::member(identity,
                "executable"), "engine executable") == "uvsr-engine.exe");
            expect(detail::string_value(detail::member(identity,
                "settings_hash"), "engine settings") ==
                manifest.settingsHash);
            expect(detail::string_value(detail::member(identity,
                "engine_version"), "engine version") ==
                manifest.engineVersion);
            expect(detail::string_value(detail::member(identity,
                "source_commit"), "engine source") ==
                manifest.sourceCommit);
            expect(detail::boolean_value(detail::member(identity,
                "production"), "production identity"));
            expect(detail::string_value(detail::member(identity,
                "configuration"), "engine configuration") == "Release");

            TemporaryDirectory temporary;
            const InstallerPaths paths = InstallerPaths::create(
                temporary.path() / L"local",
                temporary.path() / L"desktop",
                temporary.path() / L"programs");
            const GUID installation = detail::new_guid();
            OwnerMarker owner;
            owner.installationId = installation;
            fs::create_directories(paths.programRoot);
            detail::write_atomic(paths.program_marker(),
                detail::serialize_owner(owner));
            const fs::path installed = paths.launcher_executable(
                sha256(launcher));
            fs::create_directories(installed.parent_path());
            fs::copy_file(launcher, installed, fs::copy_options::none);
            detail::ShellIntegration::create_shortcut(
                paths.start_menu_shortcut(), installed, L"",
                installed.parent_path(), L"UVSR production shortcut test");
            const detail::ShortcutInfo shortcut =
                detail::ShellIntegration::read_shortcut(
                    paths.start_menu_shortcut());
            expect(detail::path_equal(shortcut.target, installed));
            expect(detail::ShellIntegration(paths).shortcut_owned(
                shortcut, installation));
            std::cout << "PASS production engine process, package, and COM "
                "shortcut services\n";
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "FAIL production services: " << error.what() << "\n";
            return 1;
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    const HRESULT initialized = CoInitializeEx(nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized))
    {
        std::cerr << "FAIL COM initialization\n";
        return 1;
    }
    const auto uninitialize = [] { CoUninitialize(); };
    try
    {
        std::vector<std::wstring> arguments;
        arguments.reserve(static_cast<std::size_t>((std::max)(0, argc - 1)));
        for (int index = 1; index < argc; ++index)
            arguments.emplace_back(argv[index]);
        if (!arguments.empty() &&
            arguments.front() == L"--verify-renderer-archive")
        {
            const int result = verify_renderer_archive_command(arguments);
            uninitialize();
            return result;
        }
        if (!arguments.empty() &&
            arguments.front() == L"--verify-production-services")
        {
            const int result = verify_production_services_command(arguments);
            uninitialize();
            return result;
        }
        if (!arguments.empty())
            fail("unsupported test command");

        int failures = 0;
        for (const NamedTest& test : Tests)
        {
            try
            {
                test.body();
                std::cout << "PASS " << test.name << "\n";
            }
            catch (const std::exception& error)
            {
                ++failures;
                std::cerr << "FAIL " << test.name << ": " << error.what()
                    << "\n";
            }
        }
        std::cout << Tests.size() - static_cast<std::size_t>(failures) << "/"
            << Tests.size() << " launcher contract tests passed.\n";
        uninitialize();
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL launcher test command: " << error.what() << "\n";
        uninitialize();
        return 1;
    }
}
