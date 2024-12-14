import yaml

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


def GeneratePacketType(packetTypeName, values):
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
        
    packetList = ymlData['Packet']
    packetTypeCode = GeneratePacketType('PacketType', packetList)
    with open(packetTypeFilePath, 'w') as file:
        file.write(packetTypeCode)
            
    with open(packetFileHeaderPath, 'w') as file:
        file.write(GeneratePacket(packetList))
                
    with open(packetHandlerFilePath, 'w') as file:
        file.write(GeneratePacketHandler(packetList))


# Write file path here
packetTypeFilePath = 'ContentsServer/PacketIdType.h'
packetFileHeaderPath = 'ContentsServer/Protocol.h'
packetFileCppPath = 'ContentsServer/Protocol.cpp'
packetHandlerFilePath = 'ContentsServer/PacketHandler.cpp'
ymlFilePath = 'PacketDefine.yml'

ProcessPacketGenerate()
print("Code generated successfully")