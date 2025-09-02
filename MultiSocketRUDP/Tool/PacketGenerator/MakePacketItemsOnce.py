import PacketItemsFilePath
import os

def MakePlayerPacketHandlerCppFile():
    if not os.path.exists(PacketItemsFilePath.playerPacketHandlerRegisterCppFilePath):
        with open("PacketGenerator/PlayerPacketHandlerRegisterCppOrigin", 'r') as file:
            code = file.read()
        
        with open(PacketItemsFilePath.playerPacketHandlerRegisterCppFilePath, 'w') as file:
            file.write(code)
            print("PlayerPacketHandlerRegister.cpp file created")
        

def MakePlayerPacketHandlerHeaderFile():
    if not os.path.exists(PacketItemsFilePath.playerPacketHandlerRegisterHeaderFilePath):
        with open("PacketGenerator/PlayerPacketHandlerRegisterHeaderOrigin", 'r') as file:
            code = file.read()
        
        with open(PacketItemsFilePath.playerPacketHandlerRegisterHeaderFilePath, 'w') as file:
            file.write(code)
            print("PlayerPacketHandlerRegister.h file created")


def MakePlayerCppFile():
    if not os.path.exists(PacketItemsFilePath.playerPacketHandlerCppFilePath):
        code = '''#include "PreCompile.h"
#include "Player.h"

#pragma region Packet Handler
#pragma endregion Packet Handler
'''
        
        directory = os.path.dirname(PacketItemsFilePath.playerPacketHandlerCppFilePath)
        if directory and not os.path.exists(directory):
            os.makedirs(directory)
        
        with open(PacketItemsFilePath.playerPacketHandlerCppFilePath, 'w') as file:
            file.write(code)
            print("Player.cpp file created")


def MakePlayerHeaderFile():
    if not os.path.exists(PacketItemsFilePath.playerPacketHandlerHeaderFilePath):
        code = '''#pragma once
#include "RUDPSession.h"
#include "Protocol.h"

class Player final : public RUDPSession
{
public:
\tPlayer() = delete;
\t~Player() override = default;
\texplicit Player(MultiSocketRUDPCore& inCore);

private:
\tvoid OnConnected() override;
\tvoid OnDisconnected() override;

private:
\tvoid RegisterAllPacketHandler();

#pragma region Packet Handler
public:
#pragma endregion Packet Handler
};
'''
        
        directory = os.path.dirname(PacketItemsFilePath.playerPacketHandlerHeaderFilePath)
        if directory and not os.path.exists(directory):
            os.makedirs(directory)
        
        with open(PacketItemsFilePath.playerPacketHandlerHeaderFilePath, 'w') as file:
            file.write(code)
            print("Player.h file created")
        

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
    MakePlayerPacketHandlerCppFile()
    MakePlayerPacketHandlerHeaderFile()
    MakePlayerCppFile()  
    MakePlayerHeaderFile()  # 새로 추가
    MakePacketIdTypeHeaderFile()
    MakeProtocolCppFile()
    MakeProtocolHeaderFile()
    MakeYamlFile()