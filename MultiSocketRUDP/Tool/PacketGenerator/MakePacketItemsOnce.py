import PacketItemsFilePath
import os

def MakePacketHandlerCppFile():
    if not os.path.exists(PacketItemsFilePath.packetHandlerFilePath):
        code = "#include \"PreCompile.h\"\n"
        code += "#include \"PacketHandler.h\"\n"
        code += "#include \"../MultiSocketRUDPServer/PacketHandlerUtil.h\"\n\n"
        code += "namespace ContentsPacketHandler\n{\n\t\n\tvoid Init()\n\t{\n\t}\n}"
        
        with open(PacketItemsFilePath.packetHandlerFilePath, 'w') as file:
            file.write(code)
            print("PacketHandler.cpp file created")
        

def MakePacketHandlerHeaderFile():
    if not os.path.exists(PacketItemsFilePath.packetHandlerHeaderFilePath):
        code = "#pragma once\n"
        code += "#include \"Protocol.h\"\n\n"
        code += "namespace ContentsPacketHandler\n{\n\tvoid Init();\n}"
        
        with open(PacketItemsFilePath.packetHandlerHeaderFilePath, 'w') as file:
            file.write(code)
            print("PacketHander.h file created")
        

def MakePacketIdTypeHeaderFile():
    if not os.path.exists(PacketItemsFilePath.packetTypeFilePath):
        code = "#pragma once\n\n"
        code += "enum class PACKET_ID : unsigned int\n{\n\tInvalidPacketId = 0\n};"
        
        with open(PacketItemsFilePath.packetTypeFilePath, 'w') as file:
            file.write(code)
            print("PacketIdType.h file created")


def MakeProtocolCppFile():
    if not os.path.exists(PacketItemsFilePath.protocolCppFileCppPath):
        code = "#include \"PreCompile.h\"\n"
        code += "#include \"Protocol.h\"\n\n"
        code += "#pragma region packet function\n"
        code += "#pragma endregion packet function"
        
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