namespace MultiSocketRUDPBotTester.Bot
{
    public enum FieldType
    {
        Byte,
        Ushort,
        Int,
        Uint,
        Ulong,
        String,
        Sbyte,
        Short,
        Long,
        Float,
        Double,
        Bool,
        Wstring,
        Struct,
        Vector,
        List,
        Set,
        UnorderedSet,
        Map,
        UnorderedMap,
    }

    public class PacketFieldDef
    {
        public required string Name { get; init; }
        public required FieldType Type { get; init; }
        public object? DefaultValue { get; init; }
        public PacketFieldDef[] Members { get; init; } = [];
        public PacketFieldDef? Element { get; init; }
        public PacketFieldDef? Value { get; init; }
        public bool Descending { get; init; }
    }

    public static partial class PacketSchema
    {
        private static readonly Dictionary<PacketId, PacketFieldDef[]> Schemas = CreateGeneratedSchemas();

        public static PacketFieldDef[]? Get(PacketId id) => Schemas.GetValueOrDefault(id);
        public static void Register(PacketId id, PacketFieldDef[] fields) => Schemas[id] = fields;
    }
}
