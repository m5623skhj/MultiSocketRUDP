import contextlib
import fnmatch
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import yaml
import PacketGenerator as generator


class PacketGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for relative in [
            'MultiSocketRUDP/Tool/PacketDefine.yml',
            'MultiSocketRUDP/Tool/PacketGenerator/ProtocolHeaderOrigin',
            'MultiSocketRUDP/Tool/PacketGenerator/ProtocolCppOrigin',
            'MultiSocketRUDP/Tool/PacketGenerator/tests/Structs.yml',
            'MultiSocketRUDP/ContentsServer/Player.h',
            'MultiSocketRUDP/ContentsServer/PlayerPacketHandler.cpp',
        ]:
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((generator.ROOT / relative).read_bytes())
        self.definition = self.root / 'MultiSocketRUDP/Tool/PacketDefine.yml'
        self.data = yaml.safe_load(self.definition.read_text(encoding='utf-8'))

    def Save(self):
        self.definition.write_text(yaml.safe_dump(self.data, sort_keys=False), encoding='utf-8')

    def Generate(self, check=False):
        with contextlib.redirect_stdout(io.StringIO()):
            return generator.Generate(self.root, check)

    def Snapshot(self):
        return {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}

    def test_existing_ids_and_manual_handlers_are_preserved(self):
        body = (self.root / generator.SERVER / 'PlayerPacketHandler.cpp').read_bytes()
        self.Generate()
        ids = (self.root / generator.BOT / 'Generated/PacketId.g.cs').read_text()
        self.assertIn('TestPacketRes = 6', ids)
        self.assertIn('ChannelEchoReq = 7', ids)
        self.assertIn('ChannelEchoRes = 8', ids)
        self.assertEqual(body, (self.root / generator.SERVER / 'PlayerPacketHandler.cpp').read_bytes())
        registration = (self.root / generator.SERVER / 'PacketHandlerRegister.cpp').read_text()
        self.assertIn('&Player::OnChannelEcho', registration)

    def test_check_is_read_only_and_generation_is_idempotent(self):
        before = self.Snapshot()
        self.assertFalse(self.Generate(check=True))
        self.assertEqual(before, self.Snapshot())
        self.Generate()
        before = self.Snapshot()
        self.assertTrue(self.Generate(check=True))
        self.Generate()
        self.assertEqual(before, self.Snapshot())
        (self.root / generator.BOT / 'Generated/PacketId.g.cs').write_text('stale')
        before = self.Snapshot()
        self.assertFalse(self.Generate(check=True))
        self.assertEqual(before, self.Snapshot())

    def test_new_packets_generate_both_languages_and_preserve_custom_code(self):
        self.data['Packet'] += [
            {'Type': 'RequestPacket', 'PacketName': 'InventoryReq', 'Items': [{'Type': 'uint32_t', 'Name': 'count'}]},
            {'Type': 'ReplyPacket', 'PacketName': 'InventoryRes'},
        ]
        self.Save()
        self.Generate()
        registration = (self.root / generator.BOT / 'Generated/PacketRegister.g.cs').read_text()
        self.assertIn('PacketId.InventoryRes', registration)
        self.assertNotIn('PacketId.InventoryReq', registration)
        handlers = self.root / generator.SERVER / 'PlayerPacketHandler.cpp'
        handlers.write_text(handlers.read_text().replace('// TODO: implement application behavior.', '/* keep custom behavior */'), encoding='utf-8')
        self.Generate()
        self.assertIn('/* keep custom behavior */', handlers.read_text())
        self.assertEqual(handlers.read_text().count('void Player::OnInventoryReq('), 1)

    def test_field_change_updates_schema_and_payload_checks(self):
        self.Generate()
        self.data['Packet'][2]['Items'].append({'Type': 'int', 'Name': 'extra'})
        self.Save()
        self.assertFalse(self.Generate(check=True))
        self.Generate()
        self.assertIn('Name = "extra"', (self.root / generator.BOT / 'Generated/PacketSchema.g.cs').read_text())
        self.assertIn('packet.extra', (self.root / 'MultiSocketRUDP/CoreTest/GeneratedPacketPayloadTest.cpp').read_text())

    def test_nested_structs_and_containers_generate_both_languages(self):
        self.data['Structs'] = [
            {'Name': 'User', 'Items': [{'Name': 'position', 'Type': 'Position'}]},
            {'Name': 'Position', 'Items': [{'Name': 'x', 'Type': 'float'}]},
        ]
        self.data['Packet'][2]['Items'].append({'Name': 'users', 'Type': 'std::map<int, std::vector<User>, std::greater<int>>'})
        self.Save()
        self.Generate()
        header = (self.root / generator.SERVER / 'Protocol.h').read_text()
        self.assertLess(header.index('struct Position'), header.index('struct User'))
        self.assertIn('NetBufferCodec<User>', header)
        schema = (self.root / generator.BOT / 'Generated/PacketSchema.g.cs').read_text()
        for feature in ('FieldType.Map', 'FieldType.Vector', 'FieldType.Struct', 'FieldType.Float', 'Descending = true'):
            self.assertIn(feature, schema)
        self.assertTrue(self.Generate(check=True))

    def test_invalid_nested_schema_preserves_all_outputs(self):
        self.Generate()
        self.data['Structs'] = [{'Name': 'Cycle', 'Items': [{'Name': 'items', 'Type': 'std::vector<Cycle>'}]}]
        self.Save()
        before = self.Snapshot()
        with self.assertRaisesRegex(ValueError, 'cycle'):
            self.Generate()
        self.assertEqual(before, self.Snapshot())

    def test_deleted_reply_is_removed_from_generated_files(self):
        self.Generate()
        self.data['Packet'].pop()
        self.Save()
        self.Generate()
        self.assertNotIn('ChannelEchoRes', (self.root / generator.BOT / 'Generated/PacketHandlers.g.cs').read_text())

    def test_request_deletion_requires_explicit_manual_handler_cleanup(self):
        self.data['Packet'] = [p for p in self.data['Packet'] if p['PacketName'] != 'Ping']
        self.Save()
        before = self.Snapshot()
        with self.assertRaisesRegex(ValueError, 'manual Player handlers'):
            self.Generate()
        self.assertEqual(before, self.Snapshot())

    def test_invalid_definitions_never_modify_outputs(self):
        for mutation in ('duplicate', 'unsupported', 'direction', 'field', 'id'):
            with self.subTest(mutation=mutation):
                self.data = yaml.safe_load((generator.ROOT / 'MultiSocketRUDP/Tool/PacketDefine.yml').read_text())
                if mutation == 'duplicate': self.data['Packet'].append(self.data['Packet'][0])
                if mutation == 'unsupported': self.data['Packet'][2]['Items'][0]['Type'] = 'std::vector<Missing>'
                if mutation == 'direction': self.data['Packet'][0]['Type'] = 'invalid'
                if mutation == 'field': self.data['Packet'][2]['Items'].append(self.data['Packet'][2]['Items'][0])
                if mutation == 'id': self.data['Packet'][0]['Id'] = 44
                self.Save()
                before = self.Snapshot()
                with self.assertRaises(ValueError): self.Generate()
                self.assertEqual(before, self.Snapshot())

    def test_replacement_failure_rolls_back(self):
        self.Generate()
        target = self.root / generator.SERVER / 'PacketIdType.h'
        target.write_text('outdated')
        (self.root / generator.BOT / 'Generated/PacketId.g.cs').write_text('outdated')
        before = self.Snapshot()
        replace = generator.os.replace
        count = 0
        def FailSecond(source, destination):
            nonlocal count
            count += 1
            if count == 2: raise OSError('simulated replacement failure')
            replace(source, destination)
        with patch.object(generator.os, 'replace', side_effect=FailSecond):
            with self.assertRaises(OSError): self.Generate()
        self.assertEqual(before, self.Snapshot())

    def test_ci_protocol_paths_require_both_languages_and_sync(self):
        workflow = yaml.safe_load((generator.ROOT / '.github/workflows/CI.yml').read_text())
        classifier = workflow['jobs']['classify_changes']
        filters = yaml.safe_load(classifier['steps'][0]['with']['filters'])
        for path in (
            'MultiSocketRUDP/Tool/PacketDefine.yml',
            'MultiSocketRUDP/ContentsServer/Protocol.cpp',
            'MultiSocketRUDP/ContentsServer/PacketHandlerRegister.cpp',
            'MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Generated/PacketId.g.cs',
            'MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Buffer/NetBuffer.cs',
            'MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/ClientCore/CryptoHelper.cs',
        ):
            self.assertTrue(any(fnmatch.fnmatchcase(path, pattern) for pattern in filters['protocol']), path)
        ui = 'MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/UI/NodeConfigPanels.cs'
        self.assertFalse(any(fnmatch.fnmatchcase(ui, pattern) for pattern in filters['protocol']))
        for output in ('server', 'bot_tester'):
            self.assertIn("steps.filter.outputs.protocol == 'true'", classifier['outputs'][output])
        self.assertIn('protocol_sync', workflow['jobs']['build-and-test']['needs'])


if __name__ == '__main__':
    unittest.main()
