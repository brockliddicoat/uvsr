using System.Security.Cryptography;
using System.Text.Json;

namespace UvsrInstaller;

internal static class LauncherUpdateFeedVerifier
{
    private const int P256SignatureBytes = 64;
    private const string NistP256CurveOid = "1.2.840.10045.3.1.7";

    internal static LauncherFeed VerifyAndParse(byte[] envelopeData) =>
        VerifyAndParse(envelopeData,
            Convert.FromBase64String(
                ProductConstants.LauncherUpdateFeedPublicKeySpkiBase64),
            ProductConstants.LauncherUpdateFeedKeyId);

    internal static LauncherFeed VerifyAndParse(
        byte[] envelopeData,
        byte[] trustedPublicKeySpki,
        string trustedKeyId)
    {
        if (envelopeData.LongLength is <= 0 or > ProductConstants.MaximumLauncherFeedBytes)
            throw new InstallerException(
                "The launcher update feed envelope was empty or too large.");
        if (trustedPublicKeySpki is null || trustedPublicKeySpki.Length == 0 ||
            string.IsNullOrEmpty(trustedKeyId))
            throw new InstallerException(
                "The launcher update feed trust configuration was invalid.");

        LauncherUpdateFeedEnvelope envelope = DeserializeExact<LauncherUpdateFeedEnvelope>(
            envelopeData, "The launcher update feed envelope");
        if (envelope.SchemaVersion != ProductConstants.LauncherUpdateFeedSchemaVersion)
            throw new InstallerException(
                $"The launcher update feed envelope schema version was " +
                $"{envelope.SchemaVersion}; expected " +
                $"{ProductConstants.LauncherUpdateFeedSchemaVersion}.");
        if (!string.Equals(envelope.KeyId, trustedKeyId, StringComparison.Ordinal))
            throw new InstallerException(
                $"The launcher update feed key identity '{envelope.KeyId}' was not trusted.");

        byte[] payload = DecodeCanonicalBase64(envelope.PayloadBase64,
            "payload");
        if (payload.LongLength is <= 0 or >
            ProductConstants.MaximumLauncherFeedPayloadBytes)
            throw new InstallerException(
                "The launcher update feed payload was empty or too large.");
        byte[] signature = DecodeCanonicalBase64(envelope.SignatureBase64,
            "signature");
        if (signature.Length != P256SignatureBytes)
            throw new InstallerException(
                "The launcher update feed signature did not have the required P-256 size.");

        VerifySignature(payload, signature, trustedPublicKeySpki);
        return DeserializeExact<LauncherFeed>(payload,
            "The authenticated launcher update feed payload");
    }

    private static void VerifySignature(
        byte[] payload,
        byte[] signature,
        byte[] trustedPublicKeySpki)
    {
        try
        {
            using ECDsa verifier = ECDsa.Create();
            verifier.ImportSubjectPublicKeyInfo(trustedPublicKeySpki,
                out int bytesRead);
            ECParameters parameters = verifier.ExportParameters(
                includePrivateParameters: false);
            if (bytesRead != trustedPublicKeySpki.Length || verifier.KeySize != 256 ||
                !string.Equals(parameters.Curve.Oid.Value, NistP256CurveOid,
                    StringComparison.Ordinal))
                throw new InstallerException(
                    "The launcher update feed signing key was not the required P-256 key.");
            if (!verifier.VerifyData(payload, signature, HashAlgorithmName.SHA256,
                    DSASignatureFormat.IeeeP1363FixedFieldConcatenation))
                throw new InstallerException(
                    "The launcher update feed signature was invalid. No launcher update was trusted.");
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is CryptographicException or ArgumentException)
        {
            throw new InstallerException(
                "The launcher update feed signing key could not be verified.", ex);
        }
    }

    private static byte[] DecodeCanonicalBase64(string value, string field)
    {
        if (string.IsNullOrEmpty(value))
            throw new InstallerException(
                $"The launcher update feed {field} was empty.");
        try
        {
            byte[] decoded = Convert.FromBase64String(value);
            if (!string.Equals(Convert.ToBase64String(decoded), value,
                    StringComparison.Ordinal))
                throw new InstallerException(
                    $"The launcher update feed {field} was not canonical Base64.");
            return decoded;
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (FormatException ex)
        {
            throw new InstallerException(
                $"The launcher update feed {field} was not valid Base64.", ex);
        }
    }

    private static T DeserializeExact<T>(byte[] data, string description)
    {
        try
        {
            RejectDuplicateJsonProperties(data);
            return JsonSerializer.Deserialize<T>(data, JsonStore.Options)
                ?? throw new JsonException("The JSON value was empty.");
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException)
        {
            throw new InstallerException($"{description} was invalid.", ex);
        }
    }

    internal static void RejectDuplicateJsonProperties(ReadOnlySpan<byte> data)
    {
        Utf8JsonReader reader = new(data, new JsonReaderOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 32
        });
        Stack<HashSet<string>?> scopes = new();
        while (reader.Read())
        {
            switch (reader.TokenType)
            {
                case JsonTokenType.StartObject:
                    scopes.Push(new HashSet<string>(StringComparer.Ordinal));
                    break;
                case JsonTokenType.StartArray:
                    scopes.Push(null);
                    break;
                case JsonTokenType.EndObject:
                case JsonTokenType.EndArray:
                    if (scopes.Count == 0)
                        throw new JsonException("Unbalanced JSON scope.");
                    scopes.Pop();
                    break;
                case JsonTokenType.PropertyName:
                    if (scopes.Count == 0 || scopes.Peek() is not HashSet<string> names ||
                        !names.Add(reader.GetString() ?? string.Empty))
                        throw new JsonException("Duplicate JSON property.");
                    break;
            }
        }
        if (scopes.Count != 0)
            throw new JsonException("Incomplete JSON document.");
    }
}
