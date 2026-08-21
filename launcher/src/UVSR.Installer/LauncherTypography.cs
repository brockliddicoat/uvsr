using System.Drawing.Text;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace UvsrInstaller;

internal sealed record LauncherFontResource(
    string ResourceName,
    int Length,
    string Sha256,
    ushort WeightClass,
    string SubfamilyName,
    FontStyle Style);

internal sealed record OpenTypeFontMetadata(
    string FamilyName,
    string SubfamilyName,
    ushort WeightClass);

internal static class LauncherTypography
{
    internal const string FamilyName = "Noto Sans";
    internal const string LicenseResourceName =
        "UVSR.Installer.Notices.NotoSansOfl.txt";

    internal static readonly LauncherFontResource RegularResource = new(
        "UVSR.Installer.Fonts.NotoSans.Regular.ttf",
        621572,
        "478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823",
        400,
        "Regular",
        FontStyle.Regular);

    internal static readonly LauncherFontResource BoldResource = new(
        "UVSR.Installer.Fonts.NotoSans.Bold.ttf",
        631484,
        "1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913",
        700,
        "Bold",
        FontStyle.Bold);

    private static readonly Lazy<FontState> State = new(
        LoadFonts,
        LazyThreadSafetyMode.ExecutionAndPublication);

    internal static void EnsureAvailable() => _ = State.Value;

    internal static bool TryEnsureAvailable(out string detail)
    {
        try
        {
            EnsureAvailable();
            detail = string.Empty;
            return true;
        }
        catch (Exception ex)
        {
            detail = "UVSR Launcher could not load its embedded Noto Sans fonts. " +
                "Download a fresh launcher from the official UVSR release. " +
                ex.GetBaseException().Message;
            return false;
        }
    }

    internal static Font CreateRegular(float pointSize) =>
        CreateFont(pointSize, FontStyle.Regular);

    internal static Font CreateBold(float pointSize) =>
        CreateFont(pointSize, FontStyle.Bold);

    private static Font CreateFont(float pointSize, FontStyle style)
    {
        if (!float.IsFinite(pointSize) || pointSize <= 0)
            throw new ArgumentOutOfRangeException(nameof(pointSize));
        FontState state = State.Value;
        if (!state.Family.IsStyleAvailable(style))
            throw new InstallerException(
                $"The embedded {FamilyName} {style} face is unavailable.");
        Font font = new(state.Family, pointSize, style, GraphicsUnit.Point);
        if (!string.Equals(font.FontFamily.Name, FamilyName,
                StringComparison.Ordinal) || font.Style != style)
        {
            font.Dispose();
            throw new InstallerException(
                $"Windows substituted the embedded {FamilyName} {style} face.");
        }
        return font;
    }

    internal static OpenTypeFontMetadata ValidateFontBytes(
        byte[] bytes,
        LauncherFontResource expected)
    {
        ArgumentNullException.ThrowIfNull(bytes);
        ArgumentNullException.ThrowIfNull(expected);
        if (bytes.Length != expected.Length)
            throw new InstallerException(
                $"The embedded {FamilyName} {expected.SubfamilyName} font has the wrong size.");
        string hash = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        if (!string.Equals(hash, expected.Sha256, StringComparison.Ordinal))
            throw new InstallerException(
                $"The embedded {FamilyName} {expected.SubfamilyName} font failed its integrity check.");
        OpenTypeFontMetadata metadata = ParseOpenTypeMetadata(bytes);
        if (!string.Equals(metadata.FamilyName, FamilyName, StringComparison.Ordinal) ||
            !string.Equals(metadata.SubfamilyName, expected.SubfamilyName,
                StringComparison.Ordinal) ||
            metadata.WeightClass != expected.WeightClass)
        {
            throw new InstallerException(
                $"The embedded {FamilyName} {expected.SubfamilyName} font metadata is invalid.");
        }
        return metadata;
    }

    internal static OpenTypeFontMetadata ParseOpenTypeMetadata(
        ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length < 12)
            throw new InstallerException("The embedded launcher font is truncated.");
        int tableCount = ReadUInt16(bytes, 4);
        int directoryEnd = checked(12 + tableCount * 16);
        if (directoryEnd > bytes.Length)
            throw new InstallerException("The embedded launcher font table directory is invalid.");

        (int Offset, int Length)? os2 = null;
        (int Offset, int Length)? name = null;
        for (int index = 0; index < tableCount; index++)
        {
            int entry = 12 + index * 16;
            string tag = Encoding.ASCII.GetString(bytes.Slice(entry, 4));
            int offset = CheckedInt(ReadUInt32(bytes, entry + 8));
            int length = CheckedInt(ReadUInt32(bytes, entry + 12));
            ValidateRange(bytes, offset, length);
            if (tag == "OS/2")
                os2 = (offset, length);
            else if (tag == "name")
                name = (offset, length);
        }
        if (os2 is null || os2.Value.Length < 6 || name is null ||
            name.Value.Length < 6)
            throw new InstallerException("The embedded launcher font metadata is incomplete.");

        ushort weight = ReadUInt16(bytes, os2.Value.Offset + 4);
        string family = ReadPreferredName(bytes, name.Value, 16, 1);
        string subfamily = ReadPreferredName(bytes, name.Value, 17, 2);
        if (string.IsNullOrWhiteSpace(family) || string.IsNullOrWhiteSpace(subfamily))
            throw new InstallerException("The embedded launcher font names are invalid.");
        return new OpenTypeFontMetadata(family, subfamily, weight);
    }

    private static FontState LoadFonts()
    {
        PrivateFontCollection collection = new();
        List<IntPtr> buffers = new();
        List<IntPtr> registrations = new();
        try
        {
            foreach (LauncherFontResource resource in
                     new[] { RegularResource, BoldResource })
            {
                byte[] bytes = ReadResource(resource.ResourceName);
                ValidateFontBytes(bytes, resource);
                IntPtr buffer = Marshal.AllocHGlobal(bytes.Length);
                buffers.Add(buffer);
                Marshal.Copy(bytes, 0, buffer, bytes.Length);
                uint fontCount = 0;
                IntPtr registration = AddFontMemResourceEx(
                    buffer, checked((uint)bytes.Length), IntPtr.Zero, ref fontCount);
                if (registration == IntPtr.Zero || fontCount == 0)
                    throw new InstallerException(
                        $"Windows could not register the embedded {FamilyName} {resource.SubfamilyName} face.");
                registrations.Add(registration);
                collection.AddMemoryFont(buffer, bytes.Length);
            }

            FontFamily? family = collection.Families.FirstOrDefault(candidate =>
                string.Equals(candidate.Name, FamilyName, StringComparison.Ordinal));
            if (family is null ||
                !family.IsStyleAvailable(FontStyle.Regular) ||
                !family.IsStyleAvailable(FontStyle.Bold))
            {
                throw new InstallerException(
                    $"The embedded {FamilyName} Regular and Bold faces are unavailable.");
            }

            FontState state = new(collection, family, buffers, registrations);
            AppDomain.CurrentDomain.ProcessExit += (_, _) => state.Dispose();
            return state;
        }
        catch
        {
            foreach (IntPtr registration in registrations)
                _ = RemoveFontMemResourceEx(registration);
            collection.Dispose();
            foreach (IntPtr buffer in buffers)
                Marshal.FreeHGlobal(buffer);
            throw;
        }
    }

    private static byte[] ReadResource(string resourceName)
    {
        using Stream stream = typeof(LauncherTypography).Assembly
            .GetManifestResourceStream(resourceName)
            ?? throw new InstallerException(
                $"The embedded launcher font resource '{resourceName}' is missing.");
        using MemoryStream memory = new();
        stream.CopyTo(memory);
        return memory.ToArray();
    }

    private static string ReadPreferredName(
        ReadOnlySpan<byte> bytes,
        (int Offset, int Length) table,
        ushort preferredId,
        ushort fallbackId)
    {
        int count = ReadUInt16(bytes, table.Offset + 2);
        int stringStorage = checked(table.Offset + ReadUInt16(bytes, table.Offset + 4));
        int recordsEnd = checked(table.Offset + 6 + count * 12);
        if (recordsEnd > table.Offset + table.Length || stringStorage > bytes.Length)
            throw new InstallerException("The embedded launcher font name table is invalid.");

        string? best = null;
        int bestScore = int.MinValue;
        for (int index = 0; index < count; index++)
        {
            int record = table.Offset + 6 + index * 12;
            ushort platform = ReadUInt16(bytes, record);
            ushort language = ReadUInt16(bytes, record + 4);
            ushort nameId = ReadUInt16(bytes, record + 6);
            if ((nameId != preferredId && nameId != fallbackId) ||
                (platform != 0 && platform != 3))
                continue;
            int length = ReadUInt16(bytes, record + 8);
            int offset = checked(stringStorage + ReadUInt16(bytes, record + 10));
            ValidateRange(bytes, offset, length);
            if ((length & 1) != 0)
                continue;
            string value = Encoding.BigEndianUnicode
                .GetString(bytes.Slice(offset, length)).TrimEnd('\0');
            if (string.IsNullOrWhiteSpace(value))
                continue;
            int score = (nameId == preferredId ? 100 : 0) +
                (platform == 3 ? 20 : 10) +
                (language == 0x0409 ? 5 : language == 0 ? 3 : 0);
            if (score > bestScore)
            {
                best = value;
                bestScore = score;
            }
        }
        return best ?? throw new InstallerException(
            "The embedded launcher font name record is missing.");
    }

    private static ushort ReadUInt16(ReadOnlySpan<byte> bytes, int offset)
    {
        ValidateRange(bytes, offset, 2);
        return (ushort)((bytes[offset] << 8) | bytes[offset + 1]);
    }

    private static uint ReadUInt32(ReadOnlySpan<byte> bytes, int offset)
    {
        ValidateRange(bytes, offset, 4);
        return ((uint)bytes[offset] << 24) |
               ((uint)bytes[offset + 1] << 16) |
               ((uint)bytes[offset + 2] << 8) |
               bytes[offset + 3];
    }

    private static int CheckedInt(uint value) => value <= int.MaxValue
        ? (int)value
        : throw new InstallerException("The embedded launcher font table is too large.");

    private static void ValidateRange(ReadOnlySpan<byte> bytes, int offset, int length)
    {
        if (offset < 0 || length < 0 || offset > bytes.Length - length)
            throw new InstallerException("The embedded launcher font is truncated.");
    }

    [DllImport("gdi32.dll", ExactSpelling = true)]
    private static extern IntPtr AddFontMemResourceEx(
        IntPtr pbFont,
        uint cbFont,
        IntPtr pdv,
        ref uint pcFonts);

    [DllImport("gdi32.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool RemoveFontMemResourceEx(IntPtr fh);

    private sealed class FontState : IDisposable
    {
        private readonly PrivateFontCollection _collection;
        private readonly IReadOnlyList<IntPtr> _buffers;
        private readonly IReadOnlyList<IntPtr> _registrations;
        private int _disposed;

        internal FontState(
            PrivateFontCollection collection,
            FontFamily family,
            IReadOnlyList<IntPtr> buffers,
            IReadOnlyList<IntPtr> registrations)
        {
            _collection = collection;
            Family = family;
            _buffers = buffers;
            _registrations = registrations;
        }

        internal FontFamily Family { get; }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0)
                return;
            foreach (IntPtr registration in _registrations)
                _ = RemoveFontMemResourceEx(registration);
            _collection.Dispose();
            foreach (IntPtr buffer in _buffers)
                Marshal.FreeHGlobal(buffer);
        }
    }
}
