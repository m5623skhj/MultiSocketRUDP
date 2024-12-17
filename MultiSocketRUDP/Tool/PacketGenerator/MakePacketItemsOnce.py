import PacketItemsFilePath
import os

def MakePacketHandlerCppFile():
    if not os.path.exists(PacketItemsFilePath.packetHandlerFilePath):
        with open("PacketGenerator/PacketHandlerCppOrigin", 'r') as file:
            code = file.read()
        
        with open(PacketItemsFilePath.packetHandlerFilePath, 'w') as file:
            file.write(code)
            print("PacketHandler.cpp file created")
        

def MakePacketHandlerHeaderFile():
    if not os.path.exists(PacketItemsFilePath.packetHandlerHeaderFilePath):
        with open("PacketGenerator/PacketHandlerHeaderOrigin", 'r') as file:
            code = file.read()
        
        with open(PacketItemsFilePath.packetHandlerHeaderFilePath, 'w') as file:
            file.write(code)
            print("PacketHander.h file created")
        

def MakePacketIdTypeHeaderFile():
    if not os.path.exists(PacketItemsFilePath.packetTypeFilePath):
        with open("PacketGenerator/PacketIdTypeHeaderOrigin", 'r') as file:
            code = file.read()
        
        with open(PacketItemsFilePath.packetTypeFilePath, 'w') as file:
            file.write(code)
            print("PacketIdType.h file created")


def MakeProtocolCppFile():
    if not os.path.exists(PacketItemsFilePath.protocolCppFileCppPath):
        with open("PacketGenerator/ProtocolCppOrigin", 'r') as file:
            code = file.read()
        
        with open(PacketItemsFilePath.protocolCppFileCppPath, 'w') as file:
            file.write(code)
            print("Protocl.cpp file created")


def MakeProtocolHeaderFile():
    if not os.path.exists(PacketItemsFilePath.protocolHeaderPath):
        with open("PacketGenerator/ProtocolHeaderOrigin", 'r') as file:
            code = file.read()
        with open(PacketItemsFilePath.protocolHeaderPath, 'w') as file:
            file.write(code)
            print("Protocol.h file created")


def MakeYamlFile():
    if not os.path.exists(PacketItemsFilePath.ymlFilePath):
        code = "Packet:"
        with open(PacketItemsFilePath.ymlFilePath, 'w') as file:
            file.write(code)
            print("PacketDefine.yml file created")

def MakePacketItemsOnce():
    MakePacketHandlerCppFile()
    MakePacketHandlerHeaderFile()
    MakePacketIdTypeHeaderFile()
    MakeProtocolCppFile()
    MakeProtocolHeaderFile()
    MakeYamlFile()