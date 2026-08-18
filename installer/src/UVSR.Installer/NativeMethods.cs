using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using Microsoft.Win32.SafeHandles;

namespace UvsrInstaller;

internal static class NativeMethods
{
    private const byte VerNtWorkstation = 1;
    private const uint TokenQuery = 0x0008;
    private const int TokenElevation = 20;
    private static readonly Guid WintrustActionGenericVerifyV2 =
        new("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct OsVersionInfoEx
    {
        internal int Size;
        internal int Major;
        internal int Minor;
        internal int Build;
        internal int PlatformId;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] internal string ServicePack;
        internal ushort ServicePackMajor;
        internal ushort ServicePackMinor;
        internal ushort SuiteMask;
        internal byte ProductType;
        internal byte Reserved;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WintrustFileInfo
    {
        internal uint Size;
        internal IntPtr FilePath;
        internal IntPtr FileHandle;
        internal IntPtr KnownSubject;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WintrustData
    {
        internal uint Size;
        internal IntPtr PolicyCallbackData;
        internal IntPtr SipClientData;
        internal uint UiChoice;
        internal uint RevocationChecks;
        internal uint UnionChoice;
        internal IntPtr FileInfo;
        internal uint StateAction;
        internal IntPtr StateData;
        internal string? UrlReference;
        internal uint ProviderFlags;
        internal uint UiContext;
        internal IntPtr SignatureSettings;
    }

    [DllImport("ntdll.dll")]
    private static extern int RtlGetVersion(ref OsVersionInfoEx versionInfo);

    [DllImport("wintrust.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern int WinVerifyTrust(
        IntPtr windowHandle,
        [MarshalAs(UnmanagedType.LPStruct)] Guid actionId,
        IntPtr trustData);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    internal static extern bool MoveFileEx(string existingFile, string? newFile, uint flags);

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool OpenProcessToken(
        IntPtr processHandle,
        uint desiredAccess,
        out SafeFileHandle tokenHandle);

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool GetTokenInformation(
        SafeFileHandle tokenHandle,
        int tokenInformationClass,
        out int tokenInformation,
        int tokenInformationLength,
        out int returnLength);

    internal static bool IsCurrentProcessElevated()
    {
        if (!OpenProcessToken(System.Diagnostics.Process.GetCurrentProcess().Handle,
                TokenQuery, out SafeFileHandle token))
            throw new InstallerException("Windows could not inspect the UVSR Launcher security token.");
        using (token)
        {
            if (!GetTokenInformation(token, TokenElevation, out int elevated,
                    sizeof(int), out _))
                throw new InstallerException("Windows could not inspect the UVSR Launcher elevation state.");
            return elevated != 0;
        }
    }

    internal static (int Build, bool IsWorkstation) GetWindowsVersion()
    {
        OsVersionInfoEx info = new() { Size = Marshal.SizeOf<OsVersionInfoEx>(), ServicePack = string.Empty };
        int result = RtlGetVersion(ref info);
        if (result != 0)
            throw new InstallerException("Windows version detection failed.");
        return (info.Build, info.ProductType == VerNtWorkstation);
    }

    internal static void VerifyMicrosoftSignature(string path)
    {
        VerifyAuthenticodeSignature(path);
        try
        {
#pragma warning disable SYSLIB0057 // Authenticode signer extraction has no loader replacement for PE files.
            using X509Certificate2 certificate = new(X509Certificate.CreateFromSignedFile(path));
#pragma warning restore SYSLIB0057
            if (!string.Equals(certificate.GetNameInfo(X509NameType.SimpleName, false),
                    "Microsoft Corporation", StringComparison.OrdinalIgnoreCase))
                throw new InstallerException("A prerequisite was signed by an unexpected publisher.");
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is CryptographicException or IOException)
        {
            throw new InstallerException("A Microsoft prerequisite signature could not be verified.", ex);
        }
    }

    internal static void VerifyAuthenticodeSignature(string path)
    {
        IntPtr filePath = IntPtr.Zero;
        IntPtr fileInfoPointer = IntPtr.Zero;
        IntPtr dataPointer = IntPtr.Zero;
        try
        {
            filePath = Marshal.StringToCoTaskMemUni(path);
            WintrustFileInfo fileInfo = new()
            {
                Size = (uint)Marshal.SizeOf<WintrustFileInfo>(),
                FilePath = filePath
            };
            fileInfoPointer = Marshal.AllocCoTaskMem(Marshal.SizeOf<WintrustFileInfo>());
            Marshal.StructureToPtr(fileInfo, fileInfoPointer, false);
            WintrustData data = new()
            {
                Size = (uint)Marshal.SizeOf<WintrustData>(),
                UiChoice = 2,
                RevocationChecks = 1,
                UnionChoice = 1,
                FileInfo = fileInfoPointer,
                StateAction = 0,
                ProviderFlags = 0x00000080,
                UiContext = 0
            };
            dataPointer = Marshal.AllocCoTaskMem(Marshal.SizeOf<WintrustData>());
            Marshal.StructureToPtr(data, dataPointer, false);
            int result = WinVerifyTrust(IntPtr.Zero, WintrustActionGenericVerifyV2, dataPointer);
            if (result != 0)
                throw new InstallerException("The downloaded program did not have a valid Windows signature.");
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is CryptographicException or IOException)
        {
            throw new InstallerException("The downloaded program signature could not be verified.", ex);
        }
        finally
        {
            if (dataPointer != IntPtr.Zero) Marshal.FreeCoTaskMem(dataPointer);
            if (fileInfoPointer != IntPtr.Zero) Marshal.FreeCoTaskMem(fileInfoPointer);
            if (filePath != IntPtr.Zero) Marshal.FreeCoTaskMem(filePath);
        }
    }

    internal static void VerifyLauncherPublisherSignature(string path)
    {
        if (!ProductConstants.HashRegex().IsMatch(
                ProductConstants.LauncherPublisherSpkiSha256))
            throw new InstallerException(
                "UVSR Launcher updates are not enabled for this unsigned preview build.");
        VerifyAuthenticodeSignature(path);
        try
        {
#pragma warning disable SYSLIB0057 // Authenticode signer extraction has no loader replacement for PE files.
            using X509Certificate2 certificate = new(X509Certificate.CreateFromSignedFile(path));
#pragma warning restore SYSLIB0057
            using AsymmetricAlgorithm key =
                (AsymmetricAlgorithm?)certificate.GetRSAPublicKey() ??
                certificate.GetECDsaPublicKey() ??
                throw new InstallerException(
                    "The launcher publisher used an unsupported signing key.");
            string actual = Convert.ToHexString(
                SHA256.HashData(key.ExportSubjectPublicKeyInfo())).ToLowerInvariant();
            if (!CryptographicOperations.FixedTimeEquals(
                    Convert.FromHexString(ProductConstants.LauncherPublisherSpkiSha256),
                    Convert.FromHexString(actual)))
                throw new InstallerException(
                    "The launcher update was signed by an unexpected publisher.");
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is CryptographicException or IOException)
        {
            throw new InstallerException(
                "The launcher publisher signature could not be verified.", ex);
        }
    }
}
