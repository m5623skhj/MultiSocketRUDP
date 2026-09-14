import yaml
import os
import shutil
import re
import filecmp
from typing import Dict, List, Set

import PacketItemsFilePath
import MakePacketItemsOnce
from PacketSchema import ValidateSchema, MakeDataStructs

def ToEnumName(name):
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    s2 = re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1)
    return s2.upper()

def CopyPacketFiles():
    try:
        shutil.copy(PacketItemsFilePath.packetTypeFilePath, PacketItemsFilePath.packetTypeFilePath + "_new")
        shutil.copy(PacketItemsFilePath.protocolHeaderPath, PacketItemsFilePath.protocolHeaderPath + "_new")
        shutil.copy(PacketItemsFilePath.protocolCppFileCppPath, PacketItemsFilePath.protocolCppFileCppPath + "_new")
        shutil.copy(PacketItemsFilePath.playerPacketHandlerRegisterCppFilePath, PacketItemsFilePath.playerPacketHandlerRegisterCppFilePath + "_new")
        shutil.copy(PacketItemsFilePath.playerPacketHandlerRegisterHeaderFilePath, PacketItemsFilePath.playerPacketHandlerRegisterHeaderFilePath + "_new")
        shutil.copy(PacketItemsFilePath.playerPacketHandlerCppFilePath, PacketItemsFilePath.playerPacketHandlerCppFilePath + "_new")
        shutil.copy(PacketItemsFilePath.playerPacketHandlerHeaderFilePath, PacketItemsFilePath.playerPacketHandlerHeaderFilePath + "_new")
        
    except FileNotFoundError as e:
        print(f"File not found: {e.filename}")
        return False
    except OSError as e:
        print(f"File copy failed: {e.strerror}")
        return False
    
    return True


def ReplacePacketFiled():
    try:
        ReplaceFile(PacketItemsFilePath.packetTypeFilePath, PacketItemsFilePath.packetTypeFilePath + "_new")
        ReplaceFile(PacketItemsFilePath.protocolHeaderPath, PacketItemsFilePath.protocolHeaderPath + "_new")
        ReplaceFile(PacketItemsFilePath.protocolCppFileCppPath, PacketItemsFilePath.protocolCppFileCppPath + "_new")
        ReplaceFile(PacketItemsFilePath.playerPacketHandlerRegisterCppFilePath, PacketItemsFilePath.playerPacketHandlerRegisterCppFilePath + "_new")
        ReplaceFile(PacketItemsFilePath.playerPacketHandlerRegisterHeaderFilePath, PacketItemsFilePath.playerPacketHandlerRegisterHeaderFilePath + "_new")
        ReplaceFile(PacketItemsFilePath.playerPacketHandlerCppFilePath, PacketItemsFilePath.playerPacketHandlerCppFilePath + "_new")
        ReplaceFile(PacketItemsFilePath.playerPacketHandlerHeaderFilePath, PacketItemsFilePath.playerPacketHandlerHeaderFilePath + "_new")
    except Exception as e:
        return


def ReplaceFile(originFile, newFile):
    if os.path.exists(newFile):
        try:
            if os.path.exists(originFile):
                if filecmp.cmp(originFile, newFile, shallow=False):
                    os.remove(newFile)
                    return
                
                os.remove(originFile)
            
            shutil.move(newFile, originFile)
        except Exception as e:
            os.remove(newFile)
            print(f"Error during file replacement: {e}")
    else:
        print(f"New file does not exist: {newFile}")
        
        
def CopyServerGeneratedFileToClientPath():
    CopyServerFileToClientFile(PacketItemsFilePath.packetTypeFilePath, PacketItemsFilePath.clientPacketTypeFilePath)
    CopyServerFileToClientFile(PacketItemsFilePath.protocolCppFileCppPath, PacketItemsFilePath.clientProtocolCppFilePath)
    CopyServerFileToClientFile(PacketItemsFilePath.protocolHeaderPath, PacketItemsFilePath.clientProtocolHeaderFilePath)
    print("Copy server geneareted file to client path completed")
            
            
def CopyServerFileToClientFile(serverFilePath, clientFilePath):
    if os.path.exists(clientFilePath) and filecmp.cmp(serverFilePath, clientFilePath, shallow=False):
        return

    shutil.copy2(serverFilePath, clientFilePath)
            

def DuplicateCheckAndAdd(packetDuplicateCheckerContainer, checkTarget):
    if checkTarget in packetDuplicateCheckerContainer:
        return False
    
    packetDuplicateCheckerContainer.add(checkTarget)
    return True


def DuplicateCheckPacketItems(items, packetName):
    returnValue = True
    packetItems = set()
    for item in items:
        if DuplicateCheckAndAdd(packetItems, item['Name']) == False:
            print(packetName + " : " + item['Name'] + " is duplicated")
            returnValue = False
            
    return returnValue


def IsValidPacketTypeInYaml(yamlData):
    returnValue = True
    checkedInvalidUniqueType = 0
    uniqueTypePacketName = ''
    packetDuplicateChecker = set()
        
    for data in yamlData:
        packetType = data['Type']
        packetName = data['PacketName']
        items = data.get('Items')
        
        if packetType == 'Unique':
            if checkedInvalidUniqueType == 0:
                checkedInvalidUniqueType += 1
                uniqueTypePacketName = packetName
                packetDuplicateChecker.add(packetName)
                continue
            else:
                checkedInvalidUniqueType += 1
                print("Duplicated Unique type " + uniqueTypePacketName + " and " + packetName)
                returnValue = False
        
        if packetType != 'RequestPacket' and packetType != 'ReplyPacket':                
            print("Invalid packet type : PacketName " + packetName + " / Type : " + {packetType})
            returnValue = False
            continue
            
        if DuplicateCheckAndAdd(packetDuplicateChecker, packetName) == False:
            print("Duplicate packet name : " + packetName)
            returnValue = False
            continue
        
        if items is not None:
            if DuplicateCheckPacketItems(items, packetName) == False:
                returnValue = False
                continue
    
    return returnValue


def GeneratePacketType(packetList, enumName="PACKET_ID"):
    generatedCode = "#pragma once\n\n"
    generatedCode += f"enum class {enumName} : unsigned int\n{{\n\tINVALID_PACKET_ID = 0\n"
    
    for packet in packetList:
        generatedCode += f"\t, {ToEnumName(packet['PacketName'])}\n"
    
    generatedCode += "};\n"
    
    with open(PacketItemsFilePath.packetTypeFilePath + "_new", 'w') as file:
        file.write(generatedCode)
    
    return True


def MakePacketClasss(packetList):
    generatedCode = ""
    for packet in packetList:
        packetName = packet['PacketName']
        items = packet.get('Items')
        
        generatedCode += f"class {packetName} final : public IPacket\n" + "{\npublic:\n"
        generatedCode += f"\t{packetName}() = default;\n"
        generatedCode += f"\t~{packetName}() override = default;\n\npublic:\n"
        generatedCode += "\t[[nodiscard]]\n\tPacketId GetPacketId() const override;\n"
        if items:
            generatedCode += "\tvoid BufferToPacket(NetBuffer& buffer) override;\n"
            generatedCode += "\tvoid PacketToBuffer(NetBuffer& buffer) override;\n"
            generatedCode += "\npublic:\n"
            for item in items:
                generatedCode += f"\t{item['Type']} {item['Name']}{{}};\n"
        generatedCode += "};\n\n"
    
    return generatedCode


def GenerateProtocolHeader(packetList, structList=None):
    with open(PacketItemsFilePath.protocolHeaderPath, "r") as file:
        originCode = file.read()
    modifiedCode = RenderProtocolHeader(originCode, packetList, structList)
    with open(PacketItemsFilePath.protocolHeaderPath + "_new", 'w') as file:
        file.write(modifiedCode)
    return True


def RenderProtocolHeader(originCode, packetList, structList=None):
    # Migrate legacy pack markers once. Field serialization does not depend on object packing.
    pattern = r"// BEGIN GENERATED PACKET TYPES.*?// END GENERATED PACKET TYPES"
    if not re.search(pattern, originCode, re.DOTALL):
        pattern = r"#pragma pack\(push, 1\)(.*?)#pragma pack\(pop\)"
    generatedCode = "// BEGIN GENERATED PACKET TYPES\n"
    generatedCode += MakeDataStructs(structList or []) + "\n" + MakePacketClasss(packetList)
    generatedCode += "// END GENERATED PACKET TYPES"
    modifiedCode, count = re.subn(pattern, lambda _: generatedCode, originCode, flags=re.DOTALL)
    if count != 1:
        raise ValueError("Expected exactly one generated packet type region")
    return modifiedCode


def GenerateInitInPacketHandlerCpp(packetList, originCode):
    pattern = r'void Init\(\)\n\t{(.*?)\n\t}'
    match = re.search(pattern, originCode, re.DOTALL)
    
    if not match:
        print("Init function not found in the ContentsPacketHandler namespace")
        return False, None

    targetCode = ""
    
    for packet in packetList:
        if packet['Type'] == 'ReplyPacket':
            continue
        
        candidateCode = f"PacketHandlerUtil::RegisterPacket<{packet['PacketName']}>();"
        if candidateCode not in targetCode:
            targetCode += f"\n\t\t{candidateCode}"

    modifiedCode = re.sub(pattern, f"void Init()\n\t{{{targetCode}\n\t}}", originCode, flags=re.DOTALL)
    return True, modifiedCode


def GenerateProtocolCpp(packetList, enumName="PACKET_ID"):
    with open(PacketItemsFilePath.protocolCppFileCppPath, 'r') as file:
        originCode = file.read()
    modifiedCode = RenderProtocolCpp(originCode, packetList, enumName)
    with open(PacketItemsFilePath.protocolCppFileCppPath + "_new", 'w') as file:
        file.write(modifiedCode)
    return True


def RenderProtocolCpp(originCode, packetList, enumName="PACKET_ID"):
    
    pattern = r'#pragma region packet function\n(.*?)#pragma endregion packet function'
    match = re.search(pattern, originCode, re.DOTALL)

    if not match:
        raise ValueError("Pragma region not found in the file")

    modifiedCode = ""
    for packet in packetList:
        
        packetName = packet['PacketName']
        candidateCode = f"PacketId {packetName}::GetPacketId() const\n"
        if candidateCode not in modifiedCode:
            modifiedCode += f"{candidateCode}{{\n\treturn static_cast<PacketId>({enumName}::{ToEnumName(packetName)});\n}}\n"
        
        bufferToPacketCode = f"void {packetName}::BufferToPacket(NetBuffer& buffer)\n"
        packetToBufferCode = f"void {packetName}::PacketToBuffer(NetBuffer& buffer)\n"
        
        items = packet.get('Items')
        if items:
            if bufferToPacketCode not in modifiedCode:
                modifiedCode += bufferToPacketCode
                modifiedCode += "{\n"
                modifiedCode += f"\t{packetName} temporary{{}};\n"
                for item in items:
                    modifiedCode += f"\tbuffer.ReadValue(temporary.{item['Name']});\n"
                for item in items:
                    modifiedCode += f"\tthis->{item['Name']} = std::move(temporary.{item['Name']});\n"
                modifiedCode += "}\n"
            if packetToBufferCode not in modifiedCode:
                modifiedCode += packetToBufferCode
                modifiedCode += "{\n"
                for item in items:
                    modifiedCode += f"\tbuffer.WriteValue(this->{item['Name']});\n"
                modifiedCode += "}\n"

    modifiedCode = re.sub(pattern, lambda _: "#pragma region packet function\n" + modifiedCode + "#pragma endregion packet function", originCode, flags=re.DOTALL)
    return modifiedCode


def GeneratePacketHandlerCpp(packetList):
    with open(PacketItemsFilePath.playerPacketHandlerRegisterCppFilePath, 'r') as file:
        originCode = file.read()
    
    state, modifiedCode = GenerateInitInPacketHandlerCpp(packetList, originCode)
    if state == False:
        return False

    targetFilePath = PacketItemsFilePath.playerPacketHandlerRegisterCppFilePath + "_new"
    with open(targetFilePath, 'w') as file:
        file.write(modifiedCode)
    return True


def ExtractExistingPlayerHandlers(player_cpp_path: str) -> Set[str]:
    if not os.path.exists(player_cpp_path):
        return set()
    
    try:
        with open(player_cpp_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        pattern = r'void\s+Player::On(\w+)\s*\('
        matches = re.findall(pattern, content)
        return set(matches)
    except Exception as e:
        print(f"Error reading ExtractExistingPlayerHandlers : {e}")
        return set()


def ExtractExistingPlayerHandlers(player_cpp_path: str) -> Set[str]:
    if not os.path.exists(player_cpp_path):
        return set()
    
    try:
        with open(player_cpp_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        pattern = r'void\s+Player::On(\w+)\s*\('
        matches = re.findall(pattern, content)
        return set(matches)
    except Exception as e:
        print(f"Error reading Player.cpp: {e}")
        return set()


def ExtractExistingPlayerHandlerDeclarations(player_header_path: str) -> Set[str]:
    if not os.path.exists(player_header_path):
        return set()
    
    try:
        with open(player_header_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        pattern = r'void\s+On(\w+)\s*\(const\s+\w+&\s+packet\)\s*;'
        matches = re.findall(pattern, content)
        return set(matches)
    except Exception as e:
        print(f"Error reading Player.h: {e}")
        return set()


def GetReplyPacketName(request_name: str, packets: List[Dict]) -> str:
    if request_name.endswith('Req'):
        reply_name = request_name[:-3] + 'Res'
    elif request_name.endswith('Request'):
        reply_name = request_name[:-7] + 'Response'
    else:
        for packet in packets:
            if packet.get('Type') == 'ReplyPacket':
                if request_name == 'Ping' and packet.get('PacketName') == 'Pong':
                    return 'Pong'
        reply_name = request_name + 'Response'
    
    return reply_name


def GeneratePlayerHandlerCode(packet: Dict, packets: List[Dict]) -> str:
    packet_name = packet.get('PacketName', '')
    code = f"void Player::On{packet_name}(const {packet_name}& packet)\n{{\n\n}}\n"
    
    return code


def GeneratePlayerPacketHandlerDeclarations(packetList):
    existing_declarations = ExtractExistingPlayerHandlerDeclarations(PacketItemsFilePath.playerPacketHandlerHeaderFilePath)
    
    new_declarations = []
    for packet in packetList:
        if packet.get('Type') == 'RequestPacket':
            packet_name = packet.get('PacketName', '')
            if packet_name and packet_name not in existing_declarations:
                declaration = f"\tvoid On{packet_name}(const {packet_name}& packet);"
                new_declarations.append(declaration)
    
    if not new_declarations:
        return True
    
    pragma_start = "#pragma region Packet Handler"
    pragma_end = "#pragma endregion Packet Handler"
    
    if os.path.exists(PacketItemsFilePath.playerPacketHandlerHeaderFilePath):
        try:
            with open(PacketItemsFilePath.playerPacketHandlerHeaderFilePath, 'r', encoding='utf-8') as f:
                content = f.read()
            
            pragma_start_pos = content.find(pragma_start)
            pragma_end_pos = content.find(pragma_end)
            
            if pragma_start_pos != -1 and pragma_end_pos != -1:
                before = content[:pragma_end_pos]
                after = content[pragma_end_pos:]
                
                new_content = before + '\n'.join(new_declarations) + '\n' + after
            else:
                new_content = content + '\n' + pragma_start + '\npublic:\n'
                new_content += '\n'.join(new_declarations) + '\n'
                new_content += pragma_end + '\n'
            
            with open(PacketItemsFilePath.playerPacketHandlerHeaderFilePath + "_new", 'w', encoding='utf-8') as f:
                f.write(new_content)
            
        except Exception as e:
            print(f"Error updating Player.h: {e}")
            return False
    
    return True


def GeneratePlayerPacketHandlers(packetList):
    existing_handlers = ExtractExistingPlayerHandlers(PacketItemsFilePath.playerPacketHandlerCppFilePath)
    
    new_handlers = []
    for packet in packetList:
        if packet.get('Type') == 'RequestPacket':
            packet_name = packet.get('PacketName', '')
            if packet_name and packet_name not in existing_handlers:
                handler_code = GeneratePlayerHandlerCode(packet, packetList)
                new_handlers.append(handler_code)
    
    if not new_handlers:
        return True
    
    pragma_start = "#pragma region Packet Handler"
    pragma_end = "#pragma endregion Packet Handler"
    
    if os.path.exists(PacketItemsFilePath.playerPacketHandlerCppFilePath):
        try:
            with open(PacketItemsFilePath.playerPacketHandlerCppFilePath, 'r', encoding='utf-8') as f:
                content = f.read()
            
            pragma_start_pos = content.find(pragma_start)
            pragma_end_pos = content.find(pragma_end)
            
            if pragma_start_pos != -1 and pragma_end_pos != -1:
                before = content[:pragma_end_pos]
                after = content[pragma_end_pos:]
                
                new_content = before + '\n'.join(new_handlers) + '\n' + after
            else:
                new_content = content + '\n' + pragma_start + '\n'
                new_content += '\n'.join(new_handlers) + '\n'
                new_content += pragma_end + '\n'
            
            with open(PacketItemsFilePath.playerPacketHandlerCppFilePath + "_new", 'w', encoding='utf-8') as f:
                f.write(new_content)
            
        except Exception as e:
            print(f"GeneratePlayerPacketHandlers : {e}")
            return False
    else:
        try:
            new_content = '#include "PreCompile.h"\n#include "Player.h"\n\n'
            new_content += pragma_start + '\n'
            new_content += '\n'.join(new_handlers) + '\n'
            new_content += pragma_end + '\n'
            
            with open(PacketItemsFilePath.playerPacketHandlerCppFilePath + "_new", 'w', encoding='utf-8') as f:
                f.write(new_content)
            
        except Exception as e:
            print(f"GeneratePlayerPacketHandlers : {e}")
            return False
    
    return True


def ProcessPacketGenerate():
    # Validate the whole schema before creating templates, staging files, or replacing outputs.
    try:
        with open(PacketItemsFilePath.ymlFilePath, 'r', encoding='utf-8-sig') as file:
            packetList, structList = ValidateSchema(yaml.safe_load(file))
    except (OSError, ValueError, yaml.YAMLError) as error:
        print(f"Packet schema validation failed: {error}")
        return False

    MakePacketItemsOnce.MakePacketItemsOnce()
        
    if CopyPacketFiles() == False:
        print("Copy packet files failed")
        return False
        
    if GeneratePacketType(packetList) == False:
        print("Generate packet type failed")
        return False
        
    if GenerateProtocolHeader(packetList, structList) == False:
        print("Generated protocol header failed")
        return False

    if GenerateProtocolCpp(packetList) == False:
        print("Generated protocol cpp failed")
        return False

    if GeneratePacketHandlerCpp(packetList) == False:
        print("Generate packet handler falied")
        return False
        
    if GeneratePlayerPacketHandlers(packetList) == False:
        print("Generate player packet handlers failed")
        return False
        
    if GeneratePlayerPacketHandlerDeclarations(packetList) == False:
        print("Generate player packet handler declarations failed")
        return False
        
    ReplacePacketFiled()
    CopyServerGeneratedFileToClientPath()
    return True



import argparse
import json
from pathlib import Path
import sys
import tempfile

from PacketWire import Descriptor, Sample, CsString

ROOT = Path(__file__).resolve().parents[3]
BOT = Path('MultiSocketRUDPBotTester/MultiSocketRUDPBotTester')
SERVER = Path('MultiSocketRUDP/ContentsServer')


def ReplaceRegion(text, start, end, body):
    if text.count(start) != 1 or text.count(end) != 1:
        raise ValueError(f'Expected exactly one region: {start}')
    before, tail = text.split(start)
    _, after = tail.split(end)
    return before + start + '\n' + body + end + after


def Read(root, path):
    return (root / path).read_text(encoding='utf-8-sig')


def LoadSchema(path):
    packets, structs = ValidateSchema(yaml.safe_load(path.read_text(encoding='utf-8-sig')))
    for packet in packets:
        if 'Id' in packet:
            raise ValueError('IDs are assigned by list order; do not specify Id.')
    return packets, structs


def PacketIds(packets):
    return ("#pragma once\n\n"
            "enum class PACKET_ID : unsigned int\n{\n\tINVALID_PACKET_ID = 0\n"
            + "".join(f"\t, {ToEnumName(p['PacketName'])}\n" for p in packets) + "};")


def PayloadTests(packets, structs, fixture=False):
    """Exercise real generated native packets and the BotTester sender with the same vectors."""
    group = 'GeneratedPacketSchemaPayload' if fixture else 'GeneratedPacketPayload'
    include = 'GeneratedPacketSchema/Protocol.h' if fixture else '../ContentsServer/Protocol.h'
    cpp = f'// Generated from PacketDefine.yml / tests/Structs.yml.\n#include "PreCompile.h"\n#include <gtest/gtest.h>\n#include <array>\n#include "{include}"\n\n'
    cs = '// <auto-generated />\nusing MultiSocketRUDPBotTester.Bot;\nusing Xunit;\n\nnamespace MultiSocketRUDPBotTester.UnitTests;\n\n'
    cs += f'public class {group}Tests\n{{\n'
    for packetId, packet in enumerate(packets, 1):
        name = packet['PacketName']
        expected, assignments, csFields = b'', '', ''
        for index, field in enumerate(packet['Items']):
            initializer, value, wire = Sample(field['Type'], structs, index)
            expected += wire
            assignments += f'\tpacket.{field["Name"]} = {initializer};\n'
            # JSON handles nested values and preserves full 64-bit integers.
            valueText = json.dumps(value, ensure_ascii=True)
            csFields += f'                ["{field["Name"]}"] = System.Text.Json.JsonSerializer.Deserialize<System.Text.Json.JsonElement>({CsString(valueText)}),\n'
        cpp += f'TEST({group}Test, {name})\n{{\n\t{name} packet;\n{assignments}\tEXPECT_EQ(packet.GetPacketId(), {packetId}u);\n\tNetBuffer buffer;\n\tpacket.PacketToBuffer(buffer);\n'
        cpp += f'\tconst std::array<unsigned char, {len(expected)}> expected = {{{", ".join(str(b) for b in expected)}}};\n\tASSERT_EQ(buffer.GetUseSize(), expected.size());\n\tfor (size_t index = 0; index < expected.size(); ++index)\n\t\tEXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);\n'
        cpp += f'\t{name} decoded;\n\tdecoded.BufferToPacket(buffer);\n\tEXPECT_EQ(buffer.GetUseSize(), 0);\n\tNetBuffer roundTrip;\n\tdecoded.PacketToBuffer(roundTrip);\n\tASSERT_EQ(roundTrip.GetUseSize(), expected.size());\n\tfor (size_t index = 0; index < expected.size(); ++index)\n\t\tEXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);\n}}\n\n'
        cs += f'    [Fact]\n    public void {name}MatchesWirePayload()\n    {{\n'
        if fixture:
            fields = ', '.join(Descriptor(f['Type'], structs, f['Name']) for f in packet['Items'])
            cs += f'        PacketFieldDef[] schema = [{fields}];\n        var buffer = SendPacketNode.BuildFromSchema(schema, new Dictionary<string, object>\n        {{\n{csFields}        }});\n'
        else:
            cs += f'        Assert.Equal({packetId}u, (uint)PacketId.{name});\n        var node = new SendPacketNode\n        {{\n            PacketId = PacketId.{name},\n            FieldValues = new()\n            {{\n{csFields}            }}\n        }};\n        var buffer = node.BuildFromSchema();\n'
        cs += f'        Assert.NotNull(buffer);\n        Assert.Equal(Convert.FromHexString("{expected.hex()}"), buffer.GetPacketBuffer().AsSpan(5).ToArray());\n    }}\n'
    return cpp.rstrip() + '\n', cs + '}\n'


def BuildOutputs(root, packets, structList):
    """Prepare all outputs before writes, sharing main's C++ schema/serialization implementation."""
    structs = {s['Name']: s for s in structList}
    outputs = {}
    playerHeader = Read(root, SERVER / 'Player.h')
    playerBody = Read(root, SERVER / 'PlayerPacketHandler.cpp')
    handlers = dict((packet, method) for method, packet in re.findall(r'void\s+(On\w+)\s*\(const\s+(\w+)&\s+\w+\s*\)\s*;', playerHeader))
    requestNames = {p['PacketName'] for p in packets if p['Type'] == 'RequestPacket'}
    stale = set(handlers) - requestNames
    if stale:
        raise ValueError('Removed/renamed requests still have manual Player handlers; remove or migrate them first: ' + ', '.join(sorted(stale)))
    declarations, stubs, factory, registration = '', '', '', ''
    csIds = '// <auto-generated />\npublic enum PacketId : uint\n{\n    InvalidPacketId = 0,\n'
    schema = '// <auto-generated />\nnamespace MultiSocketRUDPBotTester.Bot;\n\npublic static partial class PacketSchema\n{\n    private static Dictionary<PacketId, PacketFieldDef[]> CreateGeneratedSchemas() => new()\n    {\n'
    csRegister = '// <auto-generated />\nnamespace MultiSocketRUDPBotTester.Contents.Client\n{\n    public partial class Client\n    {\n        private void RegisterGeneratedPacketHandlers()\n        {\n'
    csHandlers = '// <auto-generated />\nusing MultiSocketRUDPBotTester.Buffer;\n\nnamespace MultiSocketRUDPBotTester.Contents.Client.Action;\n'
    for packetId, packet in enumerate(packets, 1):
        name, items = packet['PacketName'], packet['Items']
        csIds += f'    {name} = {packetId},\n'
        if packet['Type'] != 'ReplyPacket':
            factory += f'\t\tPacketHandlerUtil::RegisterPacket<{name}>();\n'
        if packet['Type'] == 'RequestPacket':
            method = handlers.get(name, 'On' + name)
            if name not in handlers:
                declarations += f'\tvoid {method}(const {name}& packet);\n'
            if not re.search(r'void\s+Player::' + re.escape(method) + r'\s*\(', playerBody):
                stubs += f'void Player::{method}(const {name}& packet)\n{{\n\t// TODO: implement application behavior.\n}}\n\n'
            registration += f'\tRegisterPacketHandler<Player, {name}>(static_cast<PacketId>(PACKET_ID::{ToEnumName(name)}), &Player::{method});\n'
        elif packet['Type'] == 'ReplyPacket':
            csRegister += f'            packetHandlerDictionary[PacketId.{name}] = new Action.{name}Handler();\n'
            csHandlers += f'\npublic partial class {name}Handler : ActionBase\n{{\n    public override void Execute(NetBuffer buffer) => OnPacket(buffer);\n    // Implement in a separate partial class. Preserve the buffer read position.\n    partial void OnPacket(NetBuffer buffer);\n}}\n'
        schema += f'        [PacketId.{name}] =\n        [\n'
        for field in items:
            schema += '            ' + Descriptor(field['Type'], structs, field['Name']) + ',\n'
        schema += '        ],\n'
    headerTemplate = Read(root, 'MultiSocketRUDP/Tool/PacketGenerator/ProtocolHeaderOrigin')
    cppTemplate = Read(root, 'MultiSocketRUDP/Tool/PacketGenerator/ProtocolCppOrigin').replace('"PacketId.h"', '"PacketIdType.h"')
    for directory in (SERVER, Path('MultiSocketRUDP/ContentsClient')):
        outputs[directory / 'PacketIdType.h'] = PacketIds(packets)
        outputs[directory / 'Protocol.h'] = RenderProtocolHeader(headerTemplate, packets, structList)
        outputs[directory / 'Protocol.cpp'] = RenderProtocolCpp(cppTemplate, packets)
    outputs[SERVER / 'PlayerPacketHandlerRegister.cpp'] = '#include "PreCompile.h"\n#include "Protocol.h"\n#include "PacketHandlerUtil.h"\n#include "PlayerPacketHandlerRegister.h"\n\nnamespace ContentsPacketRegister\n{\n\tvoid Init()\n\t{\n' + factory + '\t}\n}\n'
    outputs[SERVER / 'PacketHandlerRegister.cpp'] = '#include "PreCompile.h"\n#include "PacketIdType.h"\n#include "Player.h"\n\nvoid Player::RegisterAllPacketHandler()\n{\n' + registration + '}\n'
    marker = '#pragma endregion Packet Handler'
    for path, text, addition in ((SERVER / 'Player.h', playerHeader, declarations), (SERVER / 'PlayerPacketHandler.cpp', playerBody, stubs)):
        if text.count(marker) != 1:
            raise ValueError(f'Missing/ambiguous handler marker: {path}')
        outputs[path] = text.replace(marker, addition + marker)
    outputs[BOT / 'Generated/PacketId.g.cs'] = csIds + '}\n'
    outputs[BOT / 'Generated/PacketSchema.g.cs'] = schema + '    };\n}\n'
    outputs[BOT / 'Generated/PacketHandlers.g.cs'] = csHandlers
    outputs[BOT / 'Generated/PacketRegister.g.cs'] = csRegister + '        }\n    }\n}\n'
    cppTests, csTests = PayloadTests(packets, structs)
    outputs[Path('MultiSocketRUDP/CoreTest/GeneratedPacketPayloadTest.cpp')] = cppTests
    outputs[Path('MultiSocketRUDPBotTester/MultiSocketRUDPBotTester.UnitTests/GeneratedPacketPayloadTests.g.cs')] = csTests
    fixturePackets, fixtureStructList = LoadSchema(root / 'MultiSocketRUDP/Tool/PacketGenerator/tests/Structs.yml')
    fixtureStructs = {s['Name']: s for s in fixtureStructList}
    cppTests, csTests = PayloadTests(fixturePackets, fixtureStructs, fixture=True)
    outputs[Path('MultiSocketRUDP/CoreTest/GeneratedPacketSchemaPayloadTest.cpp')] = cppTests
    outputs[Path('MultiSocketRUDPBotTester/MultiSocketRUDPBotTester.UnitTests/GeneratedPacketSchemaPayloadTests.g.cs')] = csTests
    return outputs


def Generate(root, check=False):
    packets, structs = LoadSchema(root / 'MultiSocketRUDP/Tool/PacketDefine.yml')
    outputs = BuildOutputs(root, packets, structs)
    changed = {path: content for path, content in outputs.items() if not (root / path).exists() or Read(root, path) != content}
    if check:
        for path in changed:
            print(f'Out of date: {path.as_posix()}')
        return not changed
    staged, originals = {}, {}
    try:
        for path, content in changed.items():
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            originals[path] = target.read_bytes() if target.exists() else None
            with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as stream:
                stream.write(content.encode('utf-8'))
                staged[path] = Path(stream.name)
        installed = []
        try:
            for path, temporary in staged.items():
                os.replace(temporary, root / path)
                installed.append(path)
        except OSError:
            for path in installed:
                if originals[path] is None:
                    (root / path).unlink()
                else:
                    (root / path).write_bytes(originals[path])
            raise
    finally:
        for temporary in staged.values():
            temporary.unlink(missing_ok=True)
    print(f'Generated C++ and C# packets: {len(changed)} changed files.')
    return True


def Main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('legacy', nargs='?', choices=['nopause'], help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        return 0 if Generate(ROOT, args.check) else 1
    except (OSError, ValueError, TypeError, KeyError, yaml.YAMLError) as error:
        print(f'Packet generation failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(Main())
