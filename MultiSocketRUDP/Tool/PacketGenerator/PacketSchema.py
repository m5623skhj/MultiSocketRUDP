"""Validate the complete schema before generation, then order value-type dependencies."""

import copy
import re
from dataclasses import dataclass


SCALARS = set("""bool char wchar_t short int long float double BYTE WORD DWORD UINT UINT64
    __int64 int8_t uint8_t int16_t uint16_t int32_t uint32_t int64_t uint64_t""".split())
SCALARS.update({"signed char", "unsigned char", "unsigned short", "unsigned int",
                "unsigned long", "long long", "unsigned long long", "long double"})
SCALARS.update("std::" + name for name in (
    "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t"))
STRINGS = {"std::string", "std::wstring"}
SEQUENCES = {"std::vector", "std::list"}
SETS = {"std::set", "std::unordered_set"}
MAPS = {"std::map", "std::unordered_map"}
ORDERED = {"std::set", "std::map"}
KEYWORDS = set("""alignas alignof and and_eq asm auto bitand bitor bool break case catch char
    char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast
    continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum
    explicit export extern false float for friend goto if inline int long mutable namespace new
    noexcept not not_eq nullptr operator or or_eq private protected public register reinterpret_cast
    requires return short signed sizeof static static_assert static_cast struct switch template this
    thread_local throw true try typedef typeid typename union unsigned using virtual void volatile
    wchar_t while xor xor_eq final override""".split())
RESERVED_TYPES = SCALARS | {"NetBuffer", "NetBufferCodec", "NetBufferDetail", "IPacket",
    "PacketId", "PACKET_ID", "PacketManager", "PacketHandler", "std", "Player",
    "CNetServerSerializationBuf", "SetBufferToParameters", "SetParametersToBuffer"}


@dataclass(frozen=True)
class FieldType:
    name: str
    args: tuple = ()

    def Cpp(self):
        return self.name + ("<" + ", ".join(arg.Cpp() for arg in self.args) + ">" if self.args else "")


def ParseType(text):
    """Parse only the supported C++ type grammar, including nested template arguments."""
    if not isinstance(text, str) or not text.strip():
        raise ValueError("Type must be a nonempty string")
    tokens = re.findall(r"[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)*|[<>,]|\S", text)
    position = 0

    def Parse():
        nonlocal position
        names = []
        while position < len(tokens) and re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)*", tokens[position]):
            names.append(tokens[position])
            position += 1
        if not names:
            raise ValueError(f"Invalid type: {text}")
        args = []
        if position < len(tokens) and tokens[position] == "<":
            position += 1
            args.append(Parse())
            while position < len(tokens) and tokens[position] == ",":
                position += 1
                args.append(Parse())
            if position >= len(tokens) or tokens[position] != ">":
                raise ValueError(f"Missing '>' in type: {text}")
            position += 1
        return FieldType(" ".join(names), tuple(args))

    result = Parse()
    if position != len(tokens):
        raise ValueError(f"Unsupported type syntax: {text}")
    return result


def ValidateName(name, location):
    if (not isinstance(name, str) or not re.fullmatch(r"[A-Za-z][A-Za-z_0-9]*", name)
            or name in KEYWORDS or "__" in name):
        raise ValueError(f"{location}: invalid C++ name {name!r}")


def ValidateSchema(data):
    """Return normalized packets and topologically sorted structs; do not write files."""
    if not isinstance(data, dict):
        raise ValueError("YAML root must be a mapping")
    if not ({"Structs", "Packet"} & data.keys()):
        raise ValueError("YAML must define Structs or Packet")
    data = copy.deepcopy(data)
    structs = data.get("Structs", [])
    packets = data.get("Packet", [])
    structs = [] if structs is None else structs
    packets = [] if packets is None else packets
    if not isinstance(structs, list) or not isinstance(packets, list):
        raise ValueError("Structs and Packet must be lists")
    names = {}
    packetNames = set()
    enumNames = {"INVALID_PACKET_ID"}
    uniqueCount = 0
    for definitions, key, kind in ((structs, "Name", "Structs"), (packets, "PacketName", "Packet")):
        for definition in definitions:
            if not isinstance(definition, dict):
                raise ValueError(f"{kind}: each definition must be a mapping")
            name = definition.get(key)
            ValidateName(name, kind)
            if name in names or name in RESERVED_TYPES:
                raise ValueError(f"{kind}: duplicate or reserved type name {name}")
            names[name] = definition
            if kind == "Packet":
                packetNames.add(name)
                if definition.get("Type") not in {"RequestPacket", "ReplyPacket", "Unique"}:
                    raise ValueError(f"{name}: invalid packet Type")
                uniqueCount += definition["Type"] == "Unique"
                enumName = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2",
                    re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)).upper()
                if enumName in enumNames:
                    raise ValueError(f"{name}: duplicate packet ID name {enumName}")
                enumNames.add(enumName)
    if uniqueCount > 1:
        raise ValueError("Only one Unique packet is allowed")

    dependencies = {definition["Name"]: [] for definition in structs}

    def CheckType(fieldType, location):
        name, args = fieldType.name, fieldType.args
        if name in SCALARS | STRINGS:
            if args:
                raise ValueError(f"{location}: {name} cannot have template arguments")
            return []
        if name in packetNames:
            raise ValueError(f"{location}: packet type {name} cannot be used as a data field")
        if name in dependencies:
            if args:
                raise ValueError(f"{location}: struct {name} cannot have template arguments")
            return [name]
        if name not in SEQUENCES | SETS | MAPS:
            raise ValueError(f"{location}: undefined or unsupported type {name}")
        arity = 2 if name in MAPS else 1
        if len(args) not in ({arity, arity + 1} if name in ORDERED else {arity}):
            raise ValueError(f"{location}: invalid template argument count for {name}")
        result = []
        for arg in args[:arity]:
            result.extend(CheckType(arg, location))
        if name in SETS | MAPS and (args[0].args or args[0].name not in SCALARS | STRINGS):
            raise ValueError(f"{location}: map keys and set elements must be scalar or string types")
        if len(args) > arity:
            comparator = args[-1]
            if comparator.name not in {"std::less", "std::greater"} or comparator.args != (args[0],):
                raise ValueError(f"{location}: comparator must be std::less<Key> or std::greater<Key>")
        return result

    for name, definition in names.items():
        items = definition.get("Items")
        if items is None:
            items = []
        if not isinstance(items, list):
            raise ValueError(f"{name}: Items must be a list")
        fields = set()
        for item in items:
            if not isinstance(item, dict):
                raise ValueError(f"{name}: each field must be a mapping")
            field = item.get("Name")
            ValidateName(field, name)
            if field in fields or field == name or (name in packetNames and field in {
                    "GetPacketId", "BufferToPacket", "PacketToBuffer"}):
                raise ValueError(f"{name}.{field}: duplicate or conflicting field name")
            fields.add(field)
            try:
                parsed = ParseType(item.get("Type"))
            except ValueError as error:
                raise ValueError(f"{name}.{field}: {error}") from error
            referenced = CheckType(parsed, f"{name}.{field}")
            item["Type"] = parsed.Cpp()
            if name in dependencies:
                dependencies[name].extend(referenced)
        definition["Items"] = items

    ordered, visited, active = [], set(), []

    def Visit(name):
        if name in active:
            raise ValueError("Struct dependency cycle: " + " -> ".join(active[active.index(name):] + [name]))
        if name in visited:
            return
        active.append(name)
        for dependency in dependencies[name]:
            Visit(dependency)
        active.pop()
        visited.add(name)
        ordered.append(names[name])

    for name in dependencies:
        Visit(name)
    return packets, ordered


def MakeDataStructs(structs):
    """Emit complete data types, all codec declarations, then codec bodies."""
    lines = []
    for definition in structs:
        name = definition["Name"]
        lines.extend([f"struct {name}", "{"])
        lines.extend(f"\t{item['Type']} {item['Name']}{{}};" for item in definition["Items"])
        lines.extend(["};", ""])
    for definition in structs:
        name = definition["Name"]
        size = "0" + "".join(f" + NetBuffer::MinimumValueSize<{item['Type']}>()" for item in definition["Items"])
        lines.extend(["template<>", f"struct NetBufferCodec<{name}>", "{",
            "\tstatic constexpr bool SUPPORTED = true;",
            f"\tstatic constexpr size_t MIN_WIRE_SIZE = {size};",
            f"\tstatic void Write(NetBuffer& buffer, const {name}& value);",
            f"\tstatic void Read(NetBuffer& buffer, {name}& value);", "};", ""])
    for definition in structs:
        name = definition["Name"]
        lines.extend([f"inline void NetBufferCodec<{name}>::Write(NetBuffer& buffer, const {name}& value)", "{"])
        lines.extend(f"\tbuffer.WriteValue(value.{item['Name']});" for item in definition["Items"])
        if not definition["Items"]:
            lines.append("\t(void)buffer; (void)value;")
        lines.extend(["}", "", f"inline void NetBufferCodec<{name}>::Read(NetBuffer& buffer, {name}& value)", "{",
            f"\t{name} temporary{{}};"])
        lines.extend(f"\tbuffer.ReadValue(temporary.{item['Name']});" for item in definition["Items"])
        if not definition["Items"]:
            lines.append("\t(void)buffer;")
        lines.extend(["\tvalue = std::move(temporary);", "}", ""])
    return "\n".join(lines)
