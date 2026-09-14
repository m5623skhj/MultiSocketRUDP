"""BotTester schema and deterministic Windows/MSVC wire samples for validated C++ types."""
import json
import struct

from PacketSchema import ParseType, MAPS

# The native project targets Windows: long is 32 bits, wchar_t is 16 bits,
# long double is 64 bits, and the x64 list count is size_t (64 bits).
SCALARS = {}
for names, kind, fmt, sample in (
    ("BYTE uint8_t", "Byte", "B", 173),
    ("char int8_t", "Sbyte", "b", -23),
    ("short int16_t", "Short", "h", -1234),
    ("wchar_t WORD uint16_t", "Ushort", "H", 4660),
    ("int long int32_t", "Int", "i", -1234567),
    ("DWORD UINT uint32_t", "Uint", "I", 2309737967),
    ("__int64 int64_t", "Long", "q", -81985529216486895),
    ("UINT64 uint64_t", "Ulong", "Q", 81985529216486895),
    ("float", "Float", "f", 1.25),
    ("double", "Double", "d", -2.5),
    ("bool", "Bool", "?", True),
):
    for name in names.split():
        SCALARS[name] = kind, fmt, sample
for alias, target in {
    "signed char": "char", "unsigned char": "BYTE", "unsigned short": "WORD",
    "unsigned int": "UINT", "unsigned long": "DWORD", "long long": "int64_t",
    "unsigned long long": "uint64_t", "long double": "double",
}.items():
    SCALARS[alias] = SCALARS[target]
for name in ("int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t"):
    SCALARS["std::" + name] = SCALARS[name]


def CsString(value):
    return json.dumps(value, ensure_ascii=True)


def Descriptor(typeName, structs, fieldName=""):
    """Emit a recursive, immutable-at-use descriptor; values are supplied separately."""
    parsed = ParseType(typeName)
    name = parsed.name
    properties = [f'Name = {CsString(fieldName)}']
    if name in SCALARS:
        kind = SCALARS[name][0]
        default = 'false' if kind == 'Bool' else '0'
    elif name in {"std::string", "std::wstring"}:
        kind = "String" if name == "std::string" else "Wstring"
        default = '""'
    elif name in structs:
        kind, default = "Struct", '"{}"'
        members = ", ".join(Descriptor(f["Type"], structs, f["Name"]) for f in structs[name]["Items"])
        properties.append(f'Members = [{members}]')
    else:
        kind = {"std::vector": "Vector", "std::list": "List", "std::set": "Set",
                "std::unordered_set": "UnorderedSet", "std::map": "Map",
                "std::unordered_map": "UnorderedMap"}[name]
        default = '"[]"'
        properties.append('Element = ' + Descriptor(parsed.args[0].Cpp(), structs))
        if name in MAPS:
            properties.append('Value = ' + Descriptor(parsed.args[1].Cpp(), structs))
        if name in {"std::set", "std::map"} and parsed.args[-1].name == "std::greater":
            properties.append("Descending = true")
    properties.extend([f'Type = FieldType.{kind}', f'DefaultValue = {default}'])
    return "new PacketFieldDef { " + ", ".join(properties) + " }"


def Sample(typeName, structs, index=0):
    """Return C++ initializer, JSON-compatible input, and independently encoded wire bytes."""
    parsed = ParseType(typeName)
    name = parsed.name
    if name in SCALARS:
        kind, fmt, sample = SCALARS[name]
        if kind == "Bool":
            sample = index % 2 == 0
        if kind != "Bool":
            sample += index
            if fmt in "BHIQ":
                sample %= 1 << (8 * struct.calcsize(fmt))
        literal = str(sample).lower()
        literal += {"Ulong": "ULL", "Long": "LL", "Uint": "U", "Float": "f"}.get(kind, "")
        return literal, sample, struct.pack("<" + fmt, sample)
    if name in {"std::string", "std::wstring"}:
        value = f"packet-한글-{index}"
        raw = value.encode("utf-8" if name == "std::string" else "utf-16-le")
        literal = ('"' + ''.join(f'\\x{b:02x}' for b in raw) + '"') if name == "std::string" else 'L"packet-\\uD55C\\uAE00-' + str(index) + '"'
        return literal, value, struct.pack("<H", len(raw)) + raw
    if name in structs:
        values = [(field, Sample(field["Type"], structs, index + i)) for i, field in enumerate(structs[name]["Items"])]
        return ("{" + ", ".join(v[0] for _, v in values) + "}",
                {field["Name"]: v[1] for field, v in values}, b"".join(v[2] for _, v in values))
    # One element for unordered collections: iteration order is not a wire contract.
    count = 1 if name.startswith("std::unordered") else 2
    values = [Sample(parsed.args[0].Cpp(), structs, index + i) for i in range(count)]
    isMap = name in MAPS
    if isMap:
        mapped = [Sample(parsed.args[1].Cpp(), structs, index + i + 2) for i in range(count)]
        values = [("{" + key[0] + ", " + val[0] + "}", [key[1], val[1]], key[2] + val[2])
                  for key, val in zip(values, mapped)]
    ordered = name in {"std::map", "std::set"}
    descending = ordered and parsed.args[-1].name == "std::greater"
    if ordered:
        values.sort(key=lambda v: v[1][0] if isMap else v[1], reverse=descending)
    prefix = bytes([int(descending)]) if ordered else b""
    prefix += struct.pack("<Q" if name == "std::list" else "<I", count)
    return "{" + ", ".join(v[0] for v in values) + "}", [v[1] for v in values], prefix + b"".join(v[2] for v in values)
