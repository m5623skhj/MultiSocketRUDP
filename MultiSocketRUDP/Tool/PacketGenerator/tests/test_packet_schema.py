import copy
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

GENERATOR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(GENERATOR))
import yaml
import PacketGenerator
import PacketItemsFilePath
from PacketSchema import ParseType, ValidateSchema

FIXTURE = GENERATOR.parents[1] / "CoreTest" / "GeneratedPacketSchema"
SCHEMA = Path(__file__).with_name("Structs.yml")


def GenerateFixture(output):
    output.mkdir(parents=True, exist_ok=True)
    packets, structs = ValidateSchema(yaml.safe_load(SCHEMA.read_text(encoding="utf-8")))
    paths = {"protocolHeaderPath": str(output / "Protocol.h"),
             "protocolCppFileCppPath": str(output / "Protocol.cpp"),
             "packetTypeFilePath": str(output / "PacketIdType.h")}
    for name in ("ProtocolHeader", "ProtocolCpp"):
        destination = output / ("Protocol.h" if name == "ProtocolHeader" else "Protocol.cpp")
        shutil.copyfile(GENERATOR / (name + "Origin"), destination)
    with patch.multiple(PacketItemsFilePath, **paths):
        assert PacketGenerator.GenerateProtocolHeader(packets, structs)
        assert PacketGenerator.GenerateProtocolCpp(packets, "GeneratedSchemaPacketId")
        assert PacketGenerator.GeneratePacketType(packets, "GeneratedSchemaPacketId")
        for path in paths.values():
            os.replace(path + "_new", path)


class PacketSchemaTest(unittest.TestCase):
    def test_forward_references_are_sorted_without_reordering_packets(self):
        data = yaml.safe_load(SCHEMA.read_text(encoding="utf-8"))
        packets, structs = ValidateSchema(data)
        self.assertEqual([s["Name"] for s in structs], ["GeneratedPosition", "GeneratedUser", "GeneratedEmpty"])
        self.assertEqual([p["PacketName"] for p in packets], ["GeneratedEnvelopeReq", "GeneratedEmptyRes"])
        self.assertEqual(data["Structs"][0]["Name"], "GeneratedUser")

    def test_generated_fixture_matches_generator(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            GenerateFixture(output)
            for name in ("Protocol.h", "Protocol.cpp", "PacketIdType.h"):
                self.assertEqual((output / name).read_text(), (FIXTURE / name).read_text(), name)
            header = (output / "Protocol.h").read_text()
            self.assertLess(header.index("struct GeneratedPosition"), header.index("struct GeneratedUser"))
            self.assertLess(header.index("struct NetBufferCodec<GeneratedEmpty>"), header.index("inline void"))
            self.assertNotIn("#pragma pack", header)
            ids = (output / "PacketIdType.h").read_text()
            self.assertNotIn("GENERATED_USER", ids)

    def test_repeated_generation_is_identical(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            GenerateFixture(output)
            paths = {"protocolHeaderPath": str(output / "Protocol.h"),
                     "protocolCppFileCppPath": str(output / "Protocol.cpp")}
            packets, structs = ValidateSchema(yaml.safe_load(SCHEMA.read_text()))
            with patch.multiple(PacketItemsFilePath, **paths):
                self.assertTrue(PacketGenerator.GenerateProtocolHeader(packets, structs))
                self.assertTrue(PacketGenerator.GenerateProtocolCpp(packets, "GeneratedSchemaPacketId"))
                for path in paths.values():
                    self.assertEqual(Path(path).read_text(), Path(path + "_new").read_text())

    def test_legacy_schema_and_empty_fields(self):
        packets, structs = ValidateSchema(yaml.safe_load((GENERATOR.parent / "PacketDefine.yml").read_text()))
        self.assertEqual(len(packets), 8)
        self.assertEqual(structs, [])
        self.assertNotIn("BufferToPacket", PacketGenerator.MakePacketClasss([packets[0]]))
        packets, _ = ValidateSchema({"Packet": [{"Type": "ReplyPacket", "PacketName": "Empty", "Items": []}]})
        self.assertNotIn("BufferToPacket", PacketGenerator.MakePacketClasss(packets))

    def test_legacy_pack_marker_migration(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Protocol.h"
            path.write_text("// prefix\n#pragma pack(push, 1)\nold\n#pragma pack(pop)\n// suffix\n")
            with patch.object(PacketItemsFilePath, "protocolHeaderPath", str(path)):
                self.assertTrue(PacketGenerator.GenerateProtocolHeader([], []))
            result = Path(str(path) + "_new").read_text()
            self.assertIn("// prefix", result)
            self.assertIn("// suffix", result)
            self.assertNotIn("#pragma pack", result)

    def test_nested_type_parser(self):
        value = "std::map<int,std::vector<std::list<User>>,std::greater<int>>"
        self.assertEqual(ParseType(value).Cpp(), "std::map<int, std::vector<std::list<User>>, std::greater<int>>")

    def test_struct_only_regeneration_removes_old_packet_functions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Protocol.cpp"
            path.write_text("// prefix\n#pragma region packet function\nold packet code\n#pragma endregion packet function\n")
            with patch.object(PacketItemsFilePath, "protocolCppFileCppPath", str(path)):
                self.assertTrue(PacketGenerator.GenerateProtocolCpp([]))
            self.assertNotIn("old packet code", Path(str(path) + "_new").read_text())
        packets, structs = ValidateSchema({"Structs": [{"Name": "Data"}]})
        self.assertEqual(packets, [])
        self.assertEqual(structs[0]["Items"], [])

    def test_invalid_types_and_fields(self):
        base = {"Structs": [{"Name": "Data", "Items": [{"Name": "value", "Type": "int"}]}]}
        for fieldType in ("Missing", "int*", "int&", "std::vector<int", "std::vector<int, float>",
                          "std::set<Data>", "std::map<Data, int>", "std::unordered_map<Data, int>",
                          "std::map<int, int, std::less<float>>", "std::less<int>", "int; malicious"):
            with self.subTest(fieldType=fieldType):
                data = copy.deepcopy(base)
                data["Structs"][0]["Items"][0]["Type"] = fieldType
                with self.assertRaisesRegex(ValueError, "Data.value"):
                    ValidateSchema(data)
        for name in ("class", "__reserved", "not-valid"):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    ValidateSchema({"Structs": [{"Name": name}]})
        with self.assertRaisesRegex(ValueError, "duplicate"):
            ValidateSchema({"Structs": [{"Name": "Data"}, {"Name": "Data"}]})
        with self.assertRaisesRegex(ValueError, "duplicate"):
            ValidateSchema({"Structs": [{"Name": "Data", "Items": [{"Name": "value", "Type": "int"}] * 2}]})

    def test_direct_and_container_cycles(self):
        for aType, bType in (("B", "A"), ("std::vector<B>", "std::map<int, A>")):
            with self.subTest(aType=aType):
                with self.assertRaisesRegex(ValueError, "A -> B -> A"):
                    ValidateSchema({"Structs": [
                        {"Name": "A", "Items": [{"Type": aType, "Name": "value"}]},
                        {"Name": "B", "Items": [{"Type": bType, "Name": "value"}]}]})
        with self.assertRaisesRegex(ValueError, "A -> A"):
            ValidateSchema({"Structs": [{"Name": "A", "Items": [{"Type": "A", "Name": "value"}]}]})

    def test_packet_cannot_be_a_field_type(self):
        with self.assertRaisesRegex(ValueError, "packet type"):
            ValidateSchema({"Packet": [{"Type": "ReplyPacket", "PacketName": "Response"}],
                            "Structs": [{"Name": "Data", "Items": [{"Type": "Response", "Name": "value"}]}]})

    def test_invalid_schema_does_not_create_or_replace_files(self):
        with tempfile.TemporaryDirectory() as directory:
            schema = Path(directory) / "PacketDefine.yml"
            schema.write_text("Structs:\n - Name: Data\n   Items:\n    - Type: Missing\n      Name: value\n")
            with patch.object(PacketItemsFilePath, "ymlFilePath", str(schema)), \
                 patch.object(PacketGenerator.MakePacketItemsOnce, "MakePacketItemsOnce") as make, \
                 patch.object(PacketGenerator, "CopyPacketFiles") as copyFiles:
                self.assertFalse(PacketGenerator.ProcessPacketGenerate())
                make.assert_not_called()
                copyFiles.assert_not_called()
                self.assertEqual(list(Path(directory).iterdir()), [schema])

    def test_complete_pipeline_copies_struct_code_and_preserves_outputs_on_invalid_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / "Tool"
            tool.mkdir()
            server, client = root / "ContentsServer", root / "ContentsClient"
            server.mkdir()
            client.mkdir()
            shutil.copytree(GENERATOR, tool / "PacketGenerator", ignore=shutil.ignore_patterns("__pycache__"))
            schema = tool / "PacketDefine.yml"
            shutil.copyfile(SCHEMA, schema)
            paths = {
                "ymlFilePath": str(schema),
                "packetTypeFilePath": str(server / "PacketIdType.h"),
                "protocolHeaderPath": str(server / "Protocol.h"),
                "protocolCppFileCppPath": str(server / "Protocol.cpp"),
                "playerPacketHandlerRegisterCppFilePath": str(server / "PlayerPacketHandlerRegister.cpp"),
                "playerPacketHandlerRegisterHeaderFilePath": str(server / "PlayerPacketHandlerRegister.h"),
                "playerPacketHandlerCppFilePath": str(server / "PlayerPacketHandler.cpp"),
                "playerPacketHandlerHeaderFilePath": str(server / "Player.h"),
                "clientPacketTypeFilePath": str(client / "PacketIdType.h"),
                "clientProtocolHeaderFilePath": str(client / "Protocol.h"),
                "clientProtocolCppFilePath": str(client / "Protocol.cpp")}
            previousDirectory = Path.cwd()
            try:
                os.chdir(tool)
                with patch.multiple(PacketItemsFilePath, **paths):
                    self.assertTrue(PacketGenerator.ProcessPacketGenerate())
                    for name in ("Protocol.h", "Protocol.cpp", "PacketIdType.h"):
                        self.assertEqual((server / name).read_bytes(), (client / name).read_bytes())
                    registered = (server / "PlayerPacketHandlerRegister.cpp").read_text()
                    self.assertIn("RegisterPacket<GeneratedEnvelopeReq>", registered)
                    self.assertNotIn("RegisterPacket<GeneratedUser>", registered)
                    snapshot = {path: path.read_bytes() for path in (*server.iterdir(), *client.iterdir())}
                    self.assertTrue(PacketGenerator.ProcessPacketGenerate())
                    self.assertEqual(snapshot, {path: path.read_bytes() for path in snapshot})
                    schema.write_text("Structs:\n - Name: Invalid\n   Items:\n    - Type: Missing\n      Name: value\n")
                    self.assertFalse(PacketGenerator.ProcessPacketGenerate())
                    self.assertEqual(snapshot, {path: path.read_bytes() for path in snapshot})
                    self.assertFalse(list(server.glob("*_new")))
            finally:
                os.chdir(previousDirectory)


if __name__ == "__main__":
    if "--write-fixture" in sys.argv:
        GenerateFixture(FIXTURE)
    else:
        unittest.main()
