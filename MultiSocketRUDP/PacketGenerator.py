import yaml
import os
import shutil
import re

def CopyPacketFiles():
    try:
        shutil.copy(packetTypeFilePath, packetTypeFilePath + "_new")
        shutil.copy(packetFileHeaderPath, packetFileHeaderPath + "_new")
        shutil.copy(packetFileCppPath, packetFileCppPath + "_new")
        shutil.copy(packetHandlerFilePath, packetHandlerFilePath + "_new")
    except FileNotFoundError as e:
        print(f"File not found: {e.filename}")
        return False
    except OSError as e:
        print(f"File copy failed: {e.strerror}")
        return False
    
    return True


def ReplacePacketFiled():
    try:
        ReplaceFile(packetTypeFilePath, packetTypeFilePath + "_new")
        ReplaceFile(packetFileHeaderPath, packetFileHeaderPath + "_new")
        ReplaceFile(packetFileCppPath, packetFileCppPath + "_new")
        ReplaceFile(packetHandlerFilePath, packetHandlerFilePath + "_new")
    except Exception as e:
        return


def ReplaceFile(originFile, newFile):
    if os.path.exists(newFile):
        try:
            if os.path.exists(originFile):
                os.remove(originFile)
                print(f"Deleted original file: {originFile}")
            
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
        
    for value in yamlData:
        packetType = value['Type']
        packetName = value['PacketName']
        items = value.get('Items')
        
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


def GeneratePacketType(values):
    generatedCode = "#pragma once\n\n"
    generatedCode += "enum class PACKET_ID : unsigned int\n{\n\tInvalidPacketId = 0\n"
    
    for value in values:
        generatedCode += f"\t, {value['PacketName']}\n"
    
    generatedCode += "};"
    return generatedCode


def MakePacketClasss(values):
    generatedCode = ""
    for value in values:
        packetName = value['PacketName']
        items = value.get('Items')
        
        generatedCode += f"class {packetName} : public IPacket\n" + "{\npublic:\n"
        generatedCode += f"\t{packetName}() = default;\n"
        generatedCode += f"\tvirtual ~{packetName}() override = default;\n\npublic:\n"
        generatedCode += "\tvirtual PacketId GetPacketId() const override;\n"
        if items is not None:
            generatedCode += "\tvirtual void BufferToPacket(NetBuffer& buffer) override;\n"
            generatedCode += "\tvirtual void PacketToBuffer(NetBuffer& buffer) override;\n"
            generatedCode += "\npublic:\n"
            for item in items:
                generatedCode += f"\t{item['Type']} {item['Name']}\n"
        generatedCode += "};\n\n"
    
    return generatedCode


def GenerateProtocolHeader(values, targetFilePath):
    pattern = r"#pragma pack\(push, 1\)(.*?)#pragma pack\(pop\)"
    with open(targetFilePath, "r") as file:
        originCode = file.read()
    
    modifiedCode = re.sub(pattern, f"#pragma pack(push, 1)\n{MakePacketClasss(values)}#pragma pack(pop)", originCode, flags=re.DOTALL)
    
    with open(targetFilePath, 'w') as file:
        file.write(modifiedCode)


def GenerateInitInPacketHandlerCpp(packetList, originCode):
    pattern = r'void Init\(\)\n\t{(.*?)\n\t}'
    match = re.search(pattern, originCode, re.DOTALL)
    
    if not match:
        print("Init function not found in the ContentsPacketHandler namespace")
        return False, None

    targetCode = match.group(1)
    
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
    return True, modifiedCode


def GenerateProtocolCpp(packetList, targetFilePath):
    with open(targetFilePath, 'r') as file:
        originCode = file.read()
    
    state, modifiedCode = GenerateHandlePacketInPacketHandlerCpp(packetList, originCode)
    if state == False:
        return False
    
    state, modifiedCode = GenerateInitInPacketHandlerCpp(packetList, modifiedCode)
    if state == False:
        return False

    with open(targetFilePath, 'w') as file:
        file.write(modifiedCode)
    return True


def GeneratePacket(packetList):
    GenerateProtocolHeader(packetList, packetFileHeaderPath + "_new")
    return GenerateProtocolCpp(packetList, packetHandlerFilePath + "_new")


def GeneratePacketHandler(packetList):
    return True


def ProcessPacketGenerate():
    with open(ymlFilePath, 'r') as file:
        ymlData = yaml.load(file, Loader=yaml.SafeLoader)
        
    if IsValidPacketTypeInYaml(ymlData['Packet']) == False:
        print("Code genearate failed")
        exit()
        
    if CopyPacketFiles() == False:
        print("Copy packet files failed")
        exit()
        
    packetList = ymlData['Packet']
    packetTypeCode = GeneratePacketType(packetList)
    with open(packetTypeFilePath + "_new", 'w') as file:
        file.write(packetTypeCode)
        
    if GeneratePacket(packetList) == False:
        print("Generated packet failed")
        exit()
        
    if GeneratePacketHandler(packetList) == False:
        print("Generated packet handler failed")
        exit()
        
    ReplacePacketFiled()


# Write file path here
packetTypeFilePath = 'ContentsServer/PacketIdType.h'
packetFileHeaderPath = 'ContentsServer/Protocol.h'
packetFileCppPath = 'ContentsServer/Protocol.cpp'
packetHandlerFilePath = 'ContentsServer/PacketHandler.cpp'
ymlFilePath = 'PacketDefine.yml'

ProcessPacketGenerate()
print("Code generated successfully")