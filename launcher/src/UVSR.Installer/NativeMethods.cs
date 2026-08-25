using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace UvsrInstaller;

internal static class NativeMethods
{
    private const byte VerNtWorkstation = 1;
    private const uint TokenQuery = 0x0008;
    private const int TokenElevation = 20;
    private const ushort Pe32OptionalHeaderMagic = 0x010B;
    private const ushort Pe32PlusOptionalHeaderMagic = 0x020B;
    private const uint PeSignature = 0x00004550;
    private const uint MinimumSecurityDirectoryCount = 5;
    private const uint MaximumSupportedDataDirectoryCount = 16;
    internal const int TrustENoSignature = unchecked((int)0x800B0100);
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

    internal static void VerifyLauncherAuthenticodePolicy(string path)
    {
        int result = GetAuthenticodeTrustStatus(path);
        if (result == 0)
            return;
        if (result != TrustENoSignature)
            throw new InstallerException(
                $"The launcher update executable had an invalid Windows " +
                $"signature status (HRESULT = 0x{unchecked((uint)result):X8}).");
        VerifyNoPeCertificateTable(path);
    }

    internal static void VerifyNoPeCertificateTable(string path)
    {
        InspectUnsignedLauncherCertificateTable(path);
    }

    private static void InspectUnsignedLauncherCertificateTable(string path)
    {
        try
        {
            using FileStream stream = new(path, FileMode.Open, FileAccess.Read,
                FileShare.Read);
            VerifyNoPeCertificateTable(stream);
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or
                                   EndOfStreamException)
        {
            throw new InstallerException(
                "The launcher update executable could not be inspected as a complete Windows PE file.",
                ex);
        }
    }

    private static void VerifyNoPeCertificateTable(FileStream stream)
    {
        if (stream.Length < 0x40)
            throw new InstallerException(
                "The launcher update executable had a truncated DOS header.");
        using BinaryReader reader = new(stream, System.Text.Encoding.UTF8,
            leaveOpen: true);
        if (reader.ReadUInt16() != 0x5A4D)
            throw new InstallerException(
                "The launcher update executable did not have a valid DOS header.");
        stream.Position = 0x3C;
        int peOffset = reader.ReadInt32();
        long optionalHeaderStart = (long)peOffset + 24;
        if (peOffset < 0x40 || optionalHeaderStart > stream.Length)
            throw new InstallerException(
                "The launcher update executable had an invalid PE header offset.");

        stream.Position = peOffset;
        if (reader.ReadUInt32() != PeSignature)
            throw new InstallerException(
                "The launcher update executable did not have a valid PE signature.");
        stream.Position = (long)peOffset + 20;
        ushort optionalHeaderSize = reader.ReadUInt16();
        long optionalHeaderEnd = optionalHeaderStart + optionalHeaderSize;
        if (optionalHeaderEnd < optionalHeaderStart ||
            optionalHeaderEnd > stream.Length)
            throw new InstallerException(
                "The launcher update executable had a truncated optional header.");

        stream.Position = optionalHeaderStart;
        ushort magic = reader.ReadUInt16();
        int directoryCountOffset;
        int dataDirectoriesOffset;
        int minimumOptionalHeaderSize;
        switch (magic)
        {
            case Pe32OptionalHeaderMagic:
                directoryCountOffset = 0x5C;
                dataDirectoriesOffset = 0x60;
                minimumOptionalHeaderSize = 0x88;
                break;
            case Pe32PlusOptionalHeaderMagic:
                directoryCountOffset = 0x6C;
                dataDirectoriesOffset = 0x70;
                minimumOptionalHeaderSize = 0x98;
                break;
            default:
                throw new InstallerException(
                    $"The launcher update executable used unsupported PE optional-header " +
                    $"magic 0x{magic:X4}.");
        }
        if (optionalHeaderSize < minimumOptionalHeaderSize)
            throw new InstallerException(
                "The launcher update executable optional header was too small for its Certificate Table.");

        stream.Position = optionalHeaderStart + directoryCountOffset;
        uint directoryCount = reader.ReadUInt32();
        if (directoryCount is < MinimumSecurityDirectoryCount or
            > MaximumSupportedDataDirectoryCount)
            throw new InstallerException(
                $"The launcher update executable declared an unsupported PE data-directory count " +
                $"of {directoryCount}.");
        long requiredOptionalHeaderSize = dataDirectoriesOffset +
                                          directoryCount * 8L;
        if (requiredOptionalHeaderSize > optionalHeaderSize)
            throw new InstallerException(
                "The launcher update executable data directories exceeded its optional header.");

        const int securityDirectoryIndex = 4;
        stream.Position = optionalHeaderStart + dataDirectoriesOffset +
                          securityDirectoryIndex * 8L;
        uint certificateFileOffset = reader.ReadUInt32();
        uint certificateSize = reader.ReadUInt32();
        if (certificateFileOffset != 0 || certificateSize != 0)
            throw new InstallerException(
                "The launcher update executable contained Certificate Table metadata. " +
                "An unsigned launcher must have a zero Certificate Table offset and size.");
    }

    private static int GetAuthenticodeTrustStatus(string path)
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
            return WinVerifyTrust(IntPtr.Zero, WintrustActionGenericVerifyV2,
                dataPointer);
        }
        finally
        {
            if (dataPointer != IntPtr.Zero) Marshal.FreeCoTaskMem(dataPointer);
            if (fileInfoPointer != IntPtr.Zero) Marshal.FreeCoTaskMem(fileInfoPointer);
            if (filePath != IntPtr.Zero) Marshal.FreeCoTaskMem(filePath);
        }
    }
}
