import yaml
import os
import shutil
import re

import PacketItemsFilePath
import MakePacketItemsOnce

def CopyPacketFiles():
    try:
        shutil.copy(PacketItemsFilePath.packetTypeFilePath, PacketItemsFilePath.packetTypeFilePath + "_new")
        shutil.copy(PacketItemsFilePath.protocolHeaderPath, PacketItemsFilePath.protocolHeaderPath + "_new")
        shutil.copy(PacketItemsFilePath.protocolCppFileCppPath, PacketItemsFilePath.protocolCppFileCppPath + "_new")
        shutil.copy(PacketItemsFilePath.packetHandlerFilePath, PacketItemsFilePath.packetHandlerFilePath + "_new")
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
        ReplaceFile(PacketItemsFilePath.packetHandlerFilePath, PacketItemsFilePath.packetHandlerFilePath + "_new")
    except Exception as e:
        return


def ReplaceFile(originFile, newFile):
    if os.path.exists(newFile):
        try:
            if os.path.exists(originFile):
                os.remove(originFile)
            
            shutil.move(newFile, originFile)
        except Exception as e:
            os.remove(newFile)
            print(f"Error during file replacement: {e}")
    else:
        print(f"New file does not exist: {newFile}")


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
            print("Invalid packet type : PacketName " + packetName + " / Type : " + value['Type'])
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
    generatedCode += "enum class PACKET_ID : unsigned int\n{\n\tInvalidPacketId = 0\n"
    
    for packet in packetList:
        generatedCode += f"\t, {packet['PacketName']}\n"
    
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
        generatedCode += f"\tvirtual ~{packetName}() override = default;\n\npublic:\n"
        generatedCode += "\tvirtual PacketId GetPacketId() const override;\n"
        if items is not None:
            generatedCode += "\tvirtual void BufferToPacket(NetBuffer& buffer) override;\n"
            generatedCode += "\tvirtual void PacketToBuffer(NetBuffer& buffer) override;\n"
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
        
        candidateCode = f"PacketHandlerUtil::RegisterPacket<{packet['PacketName']}>(HandlePacket);"
        if candidateCode not in targetCode:
            targetCode += f"\n\t\t{candidateCode}"

    modifiedCode = re.sub(pattern, f"void Init()\n\t{{{targetCode}\n\t}}", originCode, flags=re.DOTALL)
    return True, modifiedCode


def GenerateHandlePacketInPacketHandlerCpp(packetList, originCode):
    namespacePattern = r'namespace ContentsPacketHandler\n{(.*?)\n\tvoid Init()'
    match = re.search(namespacePattern, originCode, re.DOTALL)

    if not match:
        print("ContentsPacketHandler namespace not found in the file")
        return False, None

    handlerCode = "\n\t" + match.group(1).strip()
    
    for packet in packetList:
        if packet['Type'] == 'ReplyPacket':
            continue
        
        candidateCode = f"bool HandlePacket(RUDPSession& session, {packet['PacketName']}& packet)"
        if candidateCode not in handlerCode:
            if handlerCode:
                handlerCode += f"\n\n\t{candidateCode}"
            else:
                handlerCode += f"\n\t{candidateCode}"
            handlerCode += "\n\t{\n\t\treturn true;\n\t}"
    
    handlerCode += "\n\n\tvoid Init"
   
    modifiedCode = re.sub(namespacePattern, "namespace ContentsPacketHandler\n{" + handlerCode, originCode, flags=re.DOTALL)
    
    cleanCodePattern = "namespace ContentsPacketHandler\n\{\n\t\n"
    match = re.search(cleanCodePattern, modifiedCode, re.DOTALL)
    if match:
        modifiedCode = re.sub(cleanCodePattern, "namespace ContentsPacketHandler\n{", modifiedCode, re.DOTALL)
    
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
            modifiedCode += f"{candidateCode}{{\n\treturn static_cast<PacketId>(PACKET_ID::{packetName});\n}}\n"
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
    with open(PacketItemsFilePath.packetHandlerFilePath, 'r') as file:
        originCode = file.read()
    
    state, modifiedCode = GenerateHandlePacketInPacketHandlerCpp(packetList, originCode)
    if state == False:
        return False
    
    state, modifiedCode = GenerateInitInPacketHandlerCpp(packetList, modifiedCode)
    if state == False:
        return False

    targetFilePath = PacketItemsFilePath.packetHandlerFilePath + "_new"
    with open(targetFilePath, 'w') as file:
        file.write(modifiedCode)
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
        
    ReplacePacketFiled()

ProcessPacketGenerate()
print("Code generated successfully")