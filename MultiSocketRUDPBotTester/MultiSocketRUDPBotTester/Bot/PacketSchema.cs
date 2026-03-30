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
    }

    public class PacketFieldDef
    {
        public required string Name { get; init; }
        public required FieldType Type { get; init; }
        public object? DefaultValue { get; init; }
    }

    public static class PacketSchema
    {
        private static readonly Dictionary<PacketId, PacketFieldDef[]> Schemas = new()
        {
            [PacketId.Ping] = [],
            [PacketId.Pong] = [],

            [PacketId.TestStringPacketReq] =
            [
                new PacketFieldDef {Name = "testString", Type = FieldType.String, DefaultValue = ""}
            ],
            [PacketId.TestStringPacketRes] =
            [
                new PacketFieldDef {Name = "echoString", Type = FieldType.String, DefaultValue = ""}
            ],
            [PacketId.TestPacketReq] =
            [
                new PacketFieldDef {Name = "order", Type = FieldType.Int, DefaultValue = 0}
            ],
            [PacketId.TestPacketRes] =
            [
                new PacketFieldDef {Name = "order", Type = FieldType.Int, DefaultValue = 0}
            ],
        };

        public static PacketFieldDef[]? Get(PacketId id) => Schemas.GetValueOrDefault(id);
        public static void Register(PacketId id, PacketFieldDef[] fields) => Schemas[id] = fields;
    }
}
