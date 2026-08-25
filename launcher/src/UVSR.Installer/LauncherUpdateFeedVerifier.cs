using System.Security.Cryptography;
using System.Text.Json;

namespace UvsrInstaller;

internal static class LauncherUpdateFeedVerifier
{
    private const int P256SignatureBytes = 64;
    private const string NistP256CurveOid = "1.2.840.10045.3.1.7";

    internal static LauncherFeed VerifyAndParse(byte[] envelopeData) =>
        VerifyAndParse(envelopeData,
            Convert.FromBase64String(ProductConstants.UpdateFeedPublicKeySpkiBase64),
            ProductConstants.UpdateFeedKeyId);

    internal static LauncherFeed VerifyAndParse(
        byte[] envelopeData,
        byte[] trustedPublicKeySpki,
        string trustedKeyId) =>
        VerifyAndParse<LauncherFeed>(envelopeData, trustedPublicKeySpki,
            trustedKeyId, ProductConstants.LauncherUpdateFeedSchemaVersion,
            "launcher");

    internal static RendererFeed VerifyRendererAndParse(byte[] envelopeData) =>
        VerifyRendererAndParse(envelopeData,
            Convert.FromBase64String(ProductConstants.UpdateFeedPublicKeySpkiBase64),
            ProductConstants.UpdateFeedKeyId);

    internal static RendererFeed VerifyRendererAndParse(
        byte[] envelopeData,
        byte[] trustedPublicKeySpki,
        string trustedKeyId) =>
        VerifyAndParse<RendererFeed>(envelopeData, trustedPublicKeySpki,
            trustedKeyId, ProductConstants.RendererUpdateFeedSchemaVersion,
            "renderer");

    private static T VerifyAndParse<T>(
        byte[] envelopeData,
        byte[] trustedPublicKeySpki,
        string trustedKeyId,
        int expectedSchemaVersion,
        string component)
    {
        if (envelopeData.LongLength is <= 0 or > ProductConstants.MaximumUpdateFeedBytes)
            throw new InstallerException(
                $"The {component} update feed envelope was empty or too large.");
        if (trustedPublicKeySpki is null || trustedPublicKeySpki.Length == 0 ||
            string.IsNullOrEmpty(trustedKeyId))
            throw new InstallerException(
                $"The {component} update feed trust configuration was invalid.");

        SignedFeedEnvelope envelope = DeserializeExact<SignedFeedEnvelope>(
            envelopeData, $"The {component} update feed envelope");
        if (envelope.SchemaVersion != expectedSchemaVersion)
            throw new InstallerException(
                $"The {component} update feed envelope schema version was " +
                $"{envelope.SchemaVersion}; expected {expectedSchemaVersion}.");
        if (!string.Equals(envelope.KeyId, trustedKeyId, StringComparison.Ordinal))
            throw new InstallerException(
                $"The {component} update feed key identity '{envelope.KeyId}' was not trusted.");

        byte[] payload = DecodeCanonicalBase64(envelope.PayloadBase64,
            component, "payload");
        if (payload.LongLength is <= 0 or > ProductConstants.MaximumUpdateFeedPayloadBytes)
            throw new InstallerException(
                $"The {component} update feed payload was empty or too large.");
        byte[] signature = DecodeCanonicalBase64(envelope.SignatureBase64,
            component, "signature");
        if (signature.Length != P256SignatureBytes)
            throw new InstallerException(
                $"The {component} update feed signature did not have the required P-256 size.");

        VerifySignature(payload, signature, trustedPublicKeySpki, component);
        return DeserializeExact<T>(payload,
            $"The authenticated {component} update feed payload");
    }

    private static void VerifySignature(
        byte[] payload,
        byte[] signature,
        byte[] trustedPublicKeySpki,
        string component)
    {
        try
        {
            using ECDsa verifier = ECDsa.Create();
            verifier.ImportSubjectPublicKeyInfo(trustedPublicKeySpki, out int bytesRead);
            ECParameters parameters = verifier.ExportParameters(false);
            if (bytesRead != trustedPublicKeySpki.Length || verifier.KeySize != 256 ||
                !string.Equals(parameters.Curve.Oid.Value, NistP256CurveOid,
                    StringComparison.Ordinal))
                throw new InstallerException(
                    $"The {component} update feed signing key was not the required P-256 key.");
            if (!verifier.VerifyData(payload, signature, HashAlgorithmName.SHA256,
                    DSASignatureFormat.IeeeP1363FixedFieldConcatenation))
                throw new InstallerException(
                    $"The {component} update feed signature was invalid. No update was trusted.");
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is CryptographicException or ArgumentException)
        {
            throw new InstallerException(
                $"The {component} update feed signing key could not be verified.", ex);
        }
    }

    private static byte[] DecodeCanonicalBase64(
        string value,
        string component,
        string field)
    {
        if (string.IsNullOrEmpty(value))
            throw new InstallerException(
                $"The {component} update feed {field} was empty.");
        try
        {
            byte[] decoded = Convert.FromBase64String(value);
            if (!string.Equals(Convert.ToBase64String(decoded), value,
                    StringComparison.Ordinal))
                throw new InstallerException(
                    $"The {component} update feed {field} was not canonical Base64.");
            return decoded;
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (FormatException ex)
        {
            throw new InstallerException(
                $"The {component} update feed {field} was not valid Base64.", ex);
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
