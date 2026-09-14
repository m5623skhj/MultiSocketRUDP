using System.Globalization;
using System.Text;
using System.Text.Json;
using MultiSocketRUDPBotTester.Buffer;

namespace MultiSocketRUDPBotTester.Bot;

/// <summary>Serializes generated schemas using the Windows x64 NetBuffer wire contract.</summary>
internal static class PacketFieldCodec
{
    private const int MaxContainerCount = 16384;
    private static readonly CultureInfo WireCulture = CultureInfo.InvariantCulture;

    internal static void Write(NetBuffer buffer, PacketFieldDef field, object? value)
    {
        value ??= field.DefaultValue;
        if (field.Type == FieldType.Struct)
        {
            var record = JsonValue(value, "{}");
            if (record.ValueKind != JsonValueKind.Object)
                throw new ArgumentException($"Field {field.Name} requires a JSON object.");
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in record.EnumerateObject())
                if (!seen.Add(property.Name) || !field.Members.Any(member => member.Name == property.Name))
                    throw new ArgumentException($"Unknown or duplicate member: {property.Name}");
            foreach (var member in field.Members)
                Write(buffer, member, record.TryGetProperty(member.Name, out var item) ? item : member.DefaultValue);
            return;
        }
        if (field.Type is FieldType.Vector or FieldType.List or FieldType.Set
            or FieldType.UnorderedSet or FieldType.Map or FieldType.UnorderedMap)
        {
            WriteContainer(buffer, field, JsonValue(value, "[]"));
            return;
        }

        var scalar = Scalar(field.Type, value);
        switch (field.Type)
        {
            case FieldType.Byte: buffer.WriteByte((byte)scalar); break;
            case FieldType.Sbyte: buffer.WriteByte(unchecked((byte)(sbyte)scalar)); break;
            case FieldType.Ushort: buffer.WriteUShort((ushort)scalar); break;
            case FieldType.Short: buffer.WriteUShort(unchecked((ushort)(short)scalar)); break;
            case FieldType.Int: buffer.WriteInt((int)scalar); break;
            case FieldType.Uint: buffer.WriteUInt((uint)scalar); break;
            case FieldType.Long: buffer.WriteULong(unchecked((ulong)(long)scalar)); break;
            case FieldType.Ulong: buffer.WriteULong((ulong)scalar); break;
            case FieldType.Float: buffer.WriteUInt(BitConverter.SingleToUInt32Bits((float)scalar)); break;
            case FieldType.Double: buffer.WriteULong(BitConverter.DoubleToUInt64Bits((double)scalar)); break;
            case FieldType.Bool: buffer.WriteByte((bool)scalar ? (byte)1 : (byte)0); break;
            case FieldType.String: buffer.WriteString((string)scalar); break;
            case FieldType.Wstring:
                var bytes = Encoding.Unicode.GetBytes((string)scalar);
                if (bytes.Length > ushort.MaxValue)
                    throw new ArgumentOutOfRangeException(nameof(value), "UTF-16 byte length exceeds the wire limit.");
                buffer.WriteUShort((ushort)bytes.Length);
                buffer.WriteBytes(bytes);
                break;
            default: throw new ArgumentException($"Unsupported field type: {field.Type}");
        }
    }

    private static JsonElement JsonValue(object? value, string fallback)
    {
        if (value is JsonElement element)
            return element;
        if (value is string text)
        {
            using var document = JsonDocument.Parse(text);
            return document.RootElement.Clone();
        }
        return value is null ? JsonValue(fallback, fallback) : JsonSerializer.SerializeToElement(value);
    }

    private static object Scalar(FieldType type, object? value)
    {
        if (value is JsonElement element)
        {
            if (element.ValueKind is JsonValueKind.Object or JsonValueKind.Array or JsonValueKind.Null)
                throw new ArgumentException("Expected a non-null scalar value.");
            value = element.ValueKind == JsonValueKind.String ? element.GetString() : element.GetRawText();
        }
        return type switch
        {
            FieldType.Byte => Convert.ToByte(value, WireCulture),
            FieldType.Sbyte => Convert.ToSByte(value, WireCulture),
            FieldType.Short => Convert.ToInt16(value, WireCulture),
            FieldType.Ushort => Convert.ToUInt16(value, WireCulture),
            FieldType.Int => Convert.ToInt32(value, WireCulture),
            FieldType.Uint => Convert.ToUInt32(value, WireCulture),
            FieldType.Long => Convert.ToInt64(value, WireCulture),
            FieldType.Ulong => Convert.ToUInt64(value, WireCulture),
            FieldType.Float => Convert.ToSingle(value, WireCulture),
            FieldType.Double => Convert.ToDouble(value, WireCulture),
            FieldType.Bool => Convert.ToBoolean(value, WireCulture),
            FieldType.String or FieldType.Wstring => Convert.ToString(value, WireCulture) ?? "",
            _ => throw new ArgumentException($"Invalid scalar/key type: {type}")
        };
    }

    private static int Compare(PacketFieldDef field, JsonElement left, JsonElement right)
    {
        var a = Scalar(field.Type, left);
        var b = Scalar(field.Type, right);
        if (field.Type == FieldType.String)
            return Encoding.UTF8.GetBytes((string)a).AsSpan().SequenceCompareTo(Encoding.UTF8.GetBytes((string)b));
        if (field.Type == FieldType.Wstring)
            return StringComparer.Ordinal.Compare((string)a, (string)b);
        return ((IComparable)a).CompareTo(b);
    }

    private static void WriteContainer(NetBuffer buffer, PacketFieldDef field, JsonElement input)
    {
        if (input.ValueKind != JsonValueKind.Array)
            throw new ArgumentException($"Field {field.Name} requires a JSON array.");
        if (input.GetArrayLength() > MaxContainerCount)
            throw new ArgumentOutOfRangeException(nameof(input), "Container element count exceeds the native limit.");
        var element = field.Element ?? throw new ArgumentException("Missing container element schema.");
        var map = field.Type is FieldType.Map or FieldType.UnorderedMap;
        var unique = map || field.Type is FieldType.Set or FieldType.UnorderedSet;
        var ordered = field.Type is FieldType.Map or FieldType.Set;
        var items = input.EnumerateArray().ToArray();
        if (map && (field.Value is null || items.Any(item => item.ValueKind != JsonValueKind.Array || item.GetArrayLength() != 2)))
            throw new ArgumentException("Map entries must be [key, value] pairs.");
        JsonElement Key(JsonElement item) => map ? item[0] : item;
        if (unique)
        {
            // Reject ambiguous NaN keys rather than diverging from C++ comparator semantics.
            foreach (var item in items)
            {
                var key = Scalar(element.Type, Key(item));
                if (key is float f && !float.IsFinite(f) || key is double d && !double.IsFinite(d))
                    throw new ArgumentException("Set/map keys must be finite.");
            }
            var sorted = items.ToArray();
            Array.Sort(sorted, (a, b) => Compare(element, Key(a), Key(b)));
            for (var index = 1; index < sorted.Length; index++)
                if (Compare(element, Key(sorted[index - 1]), Key(sorted[index])) == 0)
                    throw new ArgumentException("Duplicate set element or map key.");
            if (ordered)
            {
                if (field.Descending) Array.Reverse(sorted);
                items = sorted;
            }
        }
        if (ordered) buffer.WriteByte(field.Descending ? (byte)1 : (byte)0);
        if (field.Type == FieldType.List) buffer.WriteULong((ulong)items.Length);
        else buffer.WriteUInt((uint)items.Length);
        foreach (var item in items)
        {
            Write(buffer, element, Key(item));
            if (map) Write(buffer, field.Value!, item[1]);
        }
    }
}
