import yaml
import os
import shutil

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


def GeneratePacket(values):
    return


def GeneratePacketHandler(values):
    return


def ProcessPacketGenerate():
    with open(ymlFilePath, 'r') as file:
        ymlData = yaml.load(file, Loader=yaml.SafeLoader)
        
    if IsValidPacketTypeInYaml(ymlData['Packet']) == False:
        print("Code genearate failed")
        exit()
        
    if CopyPacketFiles() == False:
        print("CopyPacketFiles() failed")
        exit()
        
    packetList = ymlData['Packet']
    packetTypeCode = GeneratePacketType(packetList)
    with open(packetTypeFilePath + "_new", 'w') as file:
        file.write(packetTypeCode)
            
    with open(packetFileHeaderPath + "_new", 'w') as file:
        file.write(GeneratePacket(packetList))
                
    with open(packetHandlerFilePath + "_new", 'w') as file:
        file.write(GeneratePacketHandler(packetList))

    ReplacePacketFiled()


# Write file path here
packetTypeFilePath = 'ContentsServer/PacketIdType.h'
packetFileHeaderPath = 'ContentsServer/Protocol.h'
packetFileCppPath = 'ContentsServer/Protocol.cpp'
packetHandlerFilePath = 'ContentsServer/PacketHandler.cpp'
ymlFilePath = 'PacketDefine.yml'

ProcessPacketGenerate()
print("Code generated successfully")