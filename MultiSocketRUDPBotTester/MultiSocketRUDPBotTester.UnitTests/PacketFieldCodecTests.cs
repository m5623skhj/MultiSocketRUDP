using MultiSocketRUDPBotTester.Bot;
using Xunit;

namespace MultiSocketRUDPBotTester.UnitTests;

public class PacketFieldCodecTests
{
    private static PacketFieldDef IntElement => new() { Name = "", Type = FieldType.Int };
    private static byte[] Encode(PacketFieldDef field, object value)
        => SendPacketNode.BuildFromSchema([field], new Dictionary<string, object> { [field.Name] = value })
            .GetPacketBuffer().AsSpan(5).ToArray();

    [Theory]
    [InlineData(FieldType.Vector, "020000000300000001000000")]
    [InlineData(FieldType.List, "02000000000000000300000001000000")]
    [InlineData(FieldType.Set, "00020000000100000003000000")]
    [InlineData(FieldType.UnorderedSet, "020000000300000001000000")]
    public void ContainerCountAndOrderingMatchNative(FieldType type, string expected)
    {
        var field = new PacketFieldDef { Name = "items", Type = type, Element = IntElement };
        Assert.Equal(Convert.FromHexString(expected), Encode(field, "[3,1]"));
    }

    [Fact]
    public void DescendingMapSortsKeysAndPreservesValues()
    {
        var field = new PacketFieldDef
        {
            Name = "items", Type = FieldType.Map, Descending = true, Element = IntElement,
            Value = new() { Name = "", Type = FieldType.String }
        };
        Assert.Equal(Convert.FromHexString("01020000000300000001006201000000010061"),
            Encode(field, "[[1, \"a\"], [3, \"b\"]]"));
    }

    [Theory]
    [InlineData(FieldType.Set, "[1,1]")]
    [InlineData(FieldType.UnorderedSet, "[1,1]")]
    [InlineData(FieldType.Map, "[[1,2],[1,3]]")]
    [InlineData(FieldType.UnorderedMap, "[[1,2],[1,3]]")]
    [InlineData(FieldType.Map, "[[1]]")]
    [InlineData(FieldType.Vector, "{}")]
    public void MalformedContainersAreRejected(FieldType type, string input)
    {
        var field = new PacketFieldDef { Name = "items", Type = type, Element = IntElement, Value = IntElement };
        Assert.Throws<ArgumentException>(() => Encode(field, input));
    }

    [Fact]
    public void NestedStructDefaultsAreFreshAndUnknownMembersFail()
    {
        var field = new PacketFieldDef
        {
            Name = "record", Type = FieldType.Struct,
            Members = [
                new() { Name = "count", Type = FieldType.Int, DefaultValue = 0 },
                new() { Name = "values", Type = FieldType.List, Element = IntElement, DefaultValue = "[]" }
            ]
        };
        Assert.Equal(new byte[12], Encode(field, "{}"));
        Assert.Throws<ArgumentException>(() => Encode(field, "{\"typo\":1}"));
        Assert.Throws<ArgumentException>(() => Encode(field, "{\"count\":1,\"count\":2}"));
        Assert.Throws<ArgumentException>(() => Encode(field, "{\"values\":null}"));
        Assert.Equal(new byte[12], Encode(field, "{}"));
    }

    [Theory]
    [InlineData(FieldType.Sbyte, "-128", "80")]
    [InlineData(FieldType.Short, "-32768", "0080")]
    [InlineData(FieldType.Long, "-9223372036854775808", "0000000000000080")]
    [InlineData(FieldType.Ulong, "18446744073709551615", "ffffffffffffffff")]
    [InlineData(FieldType.Float, "1.25", "0000a03f")]
    [InlineData(FieldType.Double, "-2.5", "00000000000004c0")]
    [InlineData(FieldType.Bool, "true", "01")]
    [InlineData(FieldType.Wstring, "한", "02005cd5")]
    public void ExtendedScalarsMatchWindowsEncoding(FieldType type, string value, string expected)
    {
        Assert.Equal(Convert.FromHexString(expected),
            Encode(new() { Name = "value", Type = type }, value));
    }

    [Fact]
    public void ExcessiveContainerCountAndPayloadAreRejected()
    {
        var field = new PacketFieldDef { Name = "items", Type = FieldType.Vector, Element = IntElement };
        Assert.Throws<ArgumentOutOfRangeException>(() => Encode(field, new int[16385]));
        Assert.ThrowsAny<Exception>(() => Encode(field, new int[5000]));
    }

    [Fact]
    public void StructArraysCanBeSerializedConcurrentlyWithoutSharedValueState()
    {
        var field = new PacketFieldDef
        {
            Name = "items", Type = FieldType.Vector,
            Element = new() { Name = "", Type = FieldType.Struct, Members = [new() { Name = "value", Type = FieldType.Int }] }
        };
        Parallel.For(0, 100, index =>
        {
            var bytes = Encode(field, new[] { new { value = index } });
            Assert.Equal(1u, System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(bytes));
            Assert.Equal(index, System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(4)));
        });
    }
}
