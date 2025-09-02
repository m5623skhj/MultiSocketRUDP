import yaml
import os
import shutil
import re
import filecmp
from typing import Dict, List, Set

import PacketItemsFilePath
import MakePacketItemsOnce

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


def GeneratePacketType(packetList):
    generatedCode = "#pragma once\n\n"
    generatedCode += "enum class PACKET_ID : unsigned int\n{\n\tINVALID_PACKET_ID = 0\n"
    
    for packet in packetList:
        generatedCode += f"\t, {ToEnumName(packet['PacketName'])}\n"
    
    generatedCode += "};"
    
    with open(PacketItemsFilePath.packetTypeFilePath + "_new", 'w') as file:
        file.write(generatedCode)
    
    return True


def MakePacketClasss(packetList):
    generatedCode = ""
    for packet in packetList:
        packetName = packet['PacketName']
        items = packet.get('Items')
        
        generatedCode += f"class {packetName} : public IPacket\n" + "{\npublic:\n"
        generatedCode += f"\t{packetName}() = default;\n"
        generatedCode += f"\t~{packetName}() override = default;\n\npublic:\n"
        generatedCode += "\t[[nodiscard]]\n\tPacketId GetPacketId() const override;\n"
        if items is not None:
            generatedCode += "\tvoid BufferToPacket(NetBuffer& buffer) override;\n"
            generatedCode += "\tvoid PacketToBuffer(NetBuffer& buffer) override;\n"
            generatedCode += "\npublic:\n"
            for item in items:
                generatedCode += f"\t{item['Type']} {item['Name']};\n"
        generatedCode += "};\n\n"
    
    return generatedCode


def GenerateProtocolHeader(packetList):
    pattern = r"#pragma pack\(push, 1\)(.*?)#pragma pack\(pop\)"
    with open(PacketItemsFilePath.protocolHeaderPath, "r") as file:
        originCode = file.read()
    
    modifiedCode = re.sub(pattern, f"#pragma pack(push, 1)\n{MakePacketClasss(packetList)}#pragma pack(pop)", originCode, flags=re.DOTALL)
    
    targetFilePath = PacketItemsFilePath.protocolHeaderPath + "_new"
    with open(targetFilePath, 'w') as file:
        file.write(modifiedCode)
        
    return True


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


def GenerateProtocolCpp(packetList):
    with open(PacketItemsFilePath.protocolCppFileCppPath, 'r') as file:
        originCode = file.read()
    
    pattern = r'#pragma region packet function\n(.*?)#pragma endregion packet function'
    match = re.search(pattern, originCode, re.DOTALL)

    if not match:
        print("Pragma region not found in the file")
        return False

    needWrite = False
    modifiedCode = ""
    for packet in packetList:
        
        packetName = packet['PacketName']
        candidateCode = f"PacketId {packetName}::GetPacketId() const\n"
        if candidateCode not in modifiedCode:
            modifiedCode += f"{candidateCode}{{\n\treturn static_cast<PacketId>(PACKET_ID::{ToEnumName(packetName)});\n}}\n"
            needWrite = True
        
        bufferToPacketCode = f"void {packetName}::BufferToPacket(NetBuffer& buffer)\n"
        packetToBufferCode = f"void {packetName}::PacketToBuffer(NetBuffer& buffer)\n"
        
        items = packet.get('Items')
        parameters = "buffer"
        if items is not None:
            for item in items:
                parameters += f", {item['Name']}"
        
            if bufferToPacketCode not in modifiedCode:
                modifiedCode += bufferToPacketCode
                modifiedCode += "{\n"
                modifiedCode += f"\tSetBufferToParameters({parameters});\n"
                modifiedCode += "}\n"
                needWrite = True
            if packetToBufferCode not in modifiedCode:
                modifiedCode += packetToBufferCode
                modifiedCode += "{\n"
                modifiedCode += f"\tSetParametersToBuffer({parameters});\n"
                modifiedCode += "}\n"
                needWrite = True

    if needWrite == True:
        modifiedCode = re.sub(pattern, "#pragma region packet function\n" + modifiedCode + "#pragma endregion packet function", originCode, flags=re.DOTALL)
        targetFilePath = PacketItemsFilePath.protocolCppFileCppPath + "_new"
        with open(targetFilePath, 'w') as file:
            file.write(modifiedCode)
    return True


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
    MakePacketItemsOnce.MakePacketItemsOnce()
    
    with open(PacketItemsFilePath.ymlFilePath, 'r') as file:
        ymlData = yaml.load(file, Loader=yaml.SafeLoader)
        if ymlData['Packet'] == None:
            print("ymlData is empty")
            exit()
        
    if IsValidPacketTypeInYaml(ymlData['Packet']) == False:
        print("Code genearate failed")
        exit()
        
    if CopyPacketFiles() == False:
        print("Copy packet files failed")
        exit()
        
    packetList = ymlData['Packet']
    if GeneratePacketType(packetList) == False:
        print("Generate packet type failed")
        exit()
        
    if GenerateProtocolHeader(packetList) == False:
        print("Generated protocol header failed")
        exit()

    if GenerateProtocolCpp(packetList) == False:
        print("Generated protocol cpp failed")
        exit()

    if GeneratePacketHandlerCpp(packetList) == False:
        print("Generate packet handler falied")
        exit()
        
    if GeneratePlayerPacketHandlers(packetList) == False:
        print("Generate player packet handlers failed")
        exit()
        
    if GeneratePlayerPacketHandlerDeclarations(packetList) == False:
        print("Generate player packet handler declarations failed")
        exit()
        
    ReplacePacketFiled()
    CopyServerGeneratedFileToClientPath()

ProcessPacketGenerate()
print("Code generated successfully")