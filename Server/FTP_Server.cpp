#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <thread>
#include <fstream>
#include <filesystem>

#pragma comment (lib, "Ws2_32.lib")

#define COMMANDPORT         "27000"
#define DATAPORT            "27001"
#define BASEACK              300
#define CommandBufferSize    128
#define BUFFERSIZE           2048

static int CommandSocket;
static int DataSocket;
static auto curWorkPath = std::filesystem::current_path();

struct CommandStruct
{
    std::string command;
    std::vector<std::string> aruguments;
    std::string option;
};

struct Queue
{
    std::vector<CommandStruct*> items;

    void enqueue(CommandStruct* val)
    {
        items.push_back(val);
    }

    CommandStruct* dequeue()
    {
        CommandStruct* toReturn = items[0];
        items.erase(items.begin());
        return toReturn;
    }

    int size() const
    {
        return items.size();
    }
};
static std::unordered_map<int, Queue> commands;

struct FullFileInfo // than hopefully cast it into FileInfo
{
    char name[256];           // Server\FTP_Server.cpp
    bool isDir;               // 0 / 1
    uint64_t size;            // 328 246

    char absolutePath[256];   // C:\Users\Lenovo\source\repos\FTP_Client_Server\Server\FTP_Server.cpp
    long long lastEditTime;
    long long creationTime;

    void setSize(uint64_t val)
    {
        size = htonll(val);
    }

    void setLastEditTime(time_t val)
    {
        lastEditTime = htonll(val);
    }

    void setCreationTime(struct tm &val)
    {
        creationTime = htonll(mktime(&val));
    }
 
    void Print()
    {
        std::cout << "-------------------------------" << std::endl;
        std::cout << "| Filename: " << name << std::endl;
        std::cout << "| Path: " << absolutePath << std::endl;
        std::cout << "| Type: " << (isDir ? "<DIR>" : "<FILE>") << std::endl;
        std::cout << "| File size: " << ntohll(size) << std::endl;
        std::cout << "| Last edit: " << ntohll(lastEditTime) << std::endl;
        std::cout << "| Creation time: " << ntohll(creationTime) << std::endl;
        std::cout << "-------------------------------" << std::endl;
    }
};

struct FileInfo // client Recives this
{
    char name[256] = "";
    bool isDir;
    uint64_t size;
};

struct Packet
{
    char buffer[BUFFERSIZE];
    int bufferSize = 0;

    void AddItem(uint32_t size, char& data)
    {
        uint32_t ntwSize = htonl(size);
        memcpy(buffer + bufferSize, &ntwSize, 4);
        bufferSize += 4;

        memcpy(buffer + bufferSize, &data, size);
        bufferSize += size;
    }

    void AddSize(uint32_t size)
    {
        bufferSize += size;
    }

    int size() const
    {
        return bufferSize;
    }

    char* getBuffer(int pos = 0)
    {
        return buffer + pos;
    }

    void clear() // could clear just the bufferSize
    {
        ZeroMemory(buffer, BUFFERSIZE);
        bufferSize = 0;
    }
};

int InitWSADATA(WSADATA& wsaData)
{
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        printf("WSAStartup failed with error: %d\n", iResult);
        return -1;
    }
    return 1;
}

int CreateSocket(int& socketToCreate, const char* port)
{
    std::cout << "Creating Socket on port: " << port << std::endl;

    addrinfo hints, *serverAdrr;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int getAdrrResult = getaddrinfo(NULL, port, &hints, &serverAdrr);
    if (getAdrrResult == -1)
    {
        return -1;
    }

    for (; serverAdrr != NULL; serverAdrr = serverAdrr->ai_next)
    {
        socketToCreate = socket(serverAdrr->ai_family, serverAdrr->ai_socktype, serverAdrr->ai_protocol);
        if (socketToCreate == -1)
        {
            continue;
        }

        char yes = '1';
        int setProtocolResult = setsockopt(socketToCreate, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (setProtocolResult == -1)
        {
            return -1;
        }

        int bindSocketResult = bind(socketToCreate, serverAdrr->ai_addr, serverAdrr->ai_addrlen);
        if (bindSocketResult == -1)
        {
            printf("bind failed with error: %d\n", WSAGetLastError());
            closesocket(socketToCreate);
            continue;
        }
        break;
    }
    
    if (serverAdrr == NULL)
    {
        return -1;
    }

    freeaddrinfo(serverAdrr);
    return 1;
}

int MakeSocketsListen(int sock)
{
    if (listen(sock, 10) == -1)
    {
        std::cout << "Make Sockets Listen Error" << std::endl;
        return -1;
    }
    return 1;
}

int ReciveSYN(int socket)
{
    std::cout << "Recieving SYN" << std::endl;

    uint32_t SYNMessage;
    int recvRes = recv(socket, reinterpret_cast<char*>(&SYNMessage), 4, 0);

    if (recvRes == -1)
    {
        std::cout << "Recive SYN Error" << std::endl;
        return -1;
    }
    std::cout << "Recived SYN: " << ntohl(SYNMessage) << std::endl;

    return SYNMessage;
}

int SendSYNACK(int socket, uint32_t &SYN)
{
    std::cout << "Sending SYN" << std::endl;
    int sendResSYN = send(socket, reinterpret_cast<char*>(&SYN), 4, 0);

    if (sendResSYN == -1)
    {
        std::cout << "Sending SYN Error" << std::endl;
        return -1;
    }

    std::cout << "Sending ACK: " << BASEACK << std::endl;
    uint32_t ACK = htonl(BASEACK);
    int sendResACK = send(socket, reinterpret_cast<char*>(&ACK), 4, 0);

    if (sendResACK == -1)
    {
        std::cout << "Send ACK Error" << std::endl;
        return -1;
    }

    return 1;
}

int ReciveACK(int socket)
{
    std::cout << "Recieving ACK" << std::endl;

    uint32_t ACKMessage;
    int recvRes = recv(socket, reinterpret_cast<char*>(&ACKMessage), 4, 0);

    if (recvRes == -1 || ntohl(ACKMessage) != BASEACK)
    {
        std::cout << "Recive ACK Error" << std::endl;
        return -1;
    }
    std::cout << "Recived ACK: " << ntohl(ACKMessage) << std::endl;

    return 1;
}

bool ThreeWayHandshake(int socket)
{
    uint32_t SYN = ReciveSYN(socket);
    if (SYN == -1)                     { return false; }
    if (SendSYNACK(socket, SYN) == -1) { return false; }
    if (ReciveACK(socket) == -1)       { return false; }

    std::cout << "----- 3 Way Handshake Succesfull -----" << std::endl;
    return true;
}

int SendPortToClient(int socket)
{
    std::cout << "Sending Port to Client: " << DATAPORT << std::endl;

    int sendRes = send(socket, DATAPORT, sizeof(DATAPORT), 0);
    if (sendRes == -1)
    {
        std::cout << "Send Port Error" << std::endl;
        return -1;
    }

    return 1;
}

CommandStruct* ReadCommand(char* buffer)
{
    CommandStruct* commandStruct = new CommandStruct();

    std::string currentStr;
    for (int i = 0; i < CommandBufferSize; i++)
    {
        if (buffer[i] == '\0')
        {
            if (commandStruct->command.empty())
            {
                commandStruct->command = currentStr;
            }
            else if (currentStr[0] == '-')
            {
                commandStruct->option = currentStr;
            }
            else
            {
                commandStruct->aruguments.push_back(currentStr);
            }

            return commandStruct;
        }
        else if (buffer[i] == ' ')
        {
            if (commandStruct->command.empty())
            {
                commandStruct->command = currentStr;
            }
            else if (currentStr[0] == '-')
            {
                commandStruct->option = currentStr;
            }
            else
            {
                commandStruct->aruguments.push_back(currentStr);
            }
            currentStr = "";
        }
        else
        {
            currentStr += buffer[i];
        }
    }
}

void GetFileInfo(std::filesystem::path FilePath, std::filesystem::path originPath, FullFileInfo& fileInfo)
{
    ZeroMemory(&fileInfo, sizeof(FullFileInfo));

    std::string relativePath;
    try
    {
        relativePath = std::filesystem::relative(FilePath, originPath).string();
    }
    catch (const std::exception&)
    {
        relativePath = FilePath.string().substr(originPath.string().length() - 1); //<---
    }

    struct stat attrib;
    stat(FilePath.string().c_str(), &attrib);
    const time_t rawTime = attrib.st_ctime;

    memcpy(fileInfo.name, relativePath.c_str(), relativePath.size());
    fileInfo.isDir = std::filesystem::is_directory(FilePath);
    fileInfo.setSize(std::filesystem::file_size(FilePath));
    memcpy(fileInfo.absolutePath, FilePath.string().data(), FilePath.string().size());

    auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(FilePath));
    fileInfo.setLastEditTime(std::chrono::system_clock::to_time_t(systemTime));

    struct tm creationTime;
    localtime_s(&creationTime, &rawTime);
    fileInfo.setCreationTime(creationTime);

    /*
    std::cout << "FilePath: " + FilePath.string() << std::endl;
    std::cout << "originPath: " + originPath.string() << std::endl;
    std::cout << "RelativePath: " + relativePath << std::endl;
    std::cout << fileInfo.name << std::endl;
    std::cout << relativePath.size() << std::endl;
    std::cout << std::endl;
    */
}

void GetDirectoryFileInfo(std::filesystem::path originalPath, std::filesystem::path path, std::vector<FullFileInfo>& FileInfoVector, bool recursive = false)
{
    for (auto& val : std::filesystem::directory_iterator(path))
    {
        FullFileInfo currFileInfo;
        GetFileInfo(val, originalPath, currFileInfo);
        FileInfoVector.push_back(currFileInfo);

        if (recursive == true && val.is_directory() == 1)
        {
            GetDirectoryFileInfo(originalPath, val.path().string(), FileInfoVector, true);
        }
    }
}

bool SendPacket(int socket, Packet& packet) // check for partial sends
{
    uint32_t sizePacket = htonl(packet.size());
    int sendRes = send(socket, reinterpret_cast<char*>(&sizePacket), 4, 0);

    std::cout << "(size) send: " << sendRes << " Bytes" << std::endl;
    std::this_thread::sleep_for(std::chrono::microseconds(1));

    uint32_t totalSend = 0;
    uint32_t totalToSend = packet.size();
    while (totalSend != totalToSend)
    {
        sendRes = send(socket, packet.getBuffer(totalSend), totalToSend, 0);

        if (sendRes == 0)
        {
            std::cout << "Send Packet send res == 0" << std::endl;
            return false;
        }
        else if (sendRes == -1)
        {
            std::cout << "Send Packet Error" << std::endl;
        }

        std::cout << "(data) send: " << sendRes << " Bytes" << std::endl;
        totalSend += sendRes;
    }

    
    packet.clear();
    return true;
}

bool SendFileInfo(int socket, FullFileInfo& fullFileInfo)
{
    Packet packet;
    FileInfo* fileInfo = reinterpret_cast<FileInfo*>(&fullFileInfo);
    packet.AddItem(sizeof(FileInfo), reinterpret_cast<char&>(fileInfo[0]));
    return SendPacket(socket, packet);

}

bool sendFile(int socket, FullFileInfo& fullFileInfo)
{
    std::cout << "Sending: " << fullFileInfo.name << std::endl;

    SendFileInfo(socket, fullFileInfo);

    std::ifstream file(fullFileInfo.absolutePath, std::ios::binary);
    Packet packet;

    uint64_t totalSend = ntohll(fullFileInfo.size);
    while (totalSend != 0)
    {
        uint32_t readSize = __min(totalSend, BUFFERSIZE);

        packet.AddSize(readSize);
        file.read(packet.buffer, readSize);
        
        if (!SendPacket(socket, packet)) // sending file data
        {
            return false;
        }
        
        totalSend -= readSize;

        if (totalSend < 0)
        {
            return false; // if file send fails
        }

        std::cout << totalSend << std::endl;
    }
    
    return true;
}

bool SendCWD(int socket)
{
    Packet packet;
    std::string pathToSend = curWorkPath.string();
    packet.AddItem(pathToSend.size(), reinterpret_cast<char&>(pathToSend[0]));
    SendPacket(socket, packet);
    return true;
}

// Changes current working directory
void cdCommand(int socket, CommandStruct* command)
{
    if (command->aruguments.size() == 0 || command->aruguments.size() > 1)
    {
        return;
    }

    if (command->aruguments[0] == "..")
    {
        curWorkPath = curWorkPath.parent_path();
    }
    else
    {
        if (std::filesystem::exists(curWorkPath.string() + command->aruguments[0]))
        {
            curWorkPath = curWorkPath.string() + command->aruguments[0];
        }
    }
} 

// Sends client every file / directory found in CWD
void dirCommand(int socket, CommandStruct* command)
{
    std::vector<FullFileInfo> allDirFileInfo;
    GetDirectoryFileInfo(curWorkPath, curWorkPath, allDirFileInfo);

    for (auto& val : allDirFileInfo)
    {
        SendFileInfo(socket, val);
    }

    uint32_t terminationCode = htonl(0);
    send(socket, reinterpret_cast<char*>(&terminationCode), 4, 0);
}

// Sends client file / directory that he specifies
void getCommand(int socket, CommandStruct* command)
{
    Packet packet;
    std::filesystem::path path(curWorkPath.string() + command->aruguments[0]);

    if (std::filesystem::exists(path))
    {
        FullFileInfo fileInfo;
        GetFileInfo(path, path.parent_path(), fileInfo);
        sendFile(socket, fileInfo);

        if (std::filesystem::is_directory(path) == 1)
        {
            std::vector<FullFileInfo> allDirFileInfo;
            GetDirectoryFileInfo(path.parent_path(), path, allDirFileInfo, true);

            for (auto& val : allDirFileInfo)
            {
                packet.AddItem(command->command.size(), command->command[0]);
                SendPacket(socket, packet);
                sendFile(socket, val);
            }
        }
    }
    else
    {
        uint32_t terminationCode = htonl(0);
        send(socket, reinterpret_cast<char*>(&terminationCode), 4, 0);
    }
}

// Lists info about the specified file / directory
void listCommand(int socket, CommandStruct* command)
{
    FullFileInfo fullFileInfo;
    Packet packet;
    std::filesystem::path path(curWorkPath.string() + command->aruguments[0]);
    GetFileInfo(path, path.parent_path(), fullFileInfo);
    fullFileInfo.Print();
    packet.AddItem(sizeof(FullFileInfo), reinterpret_cast<char&>(fullFileInfo));
    SendPacket(socket, packet);
}

// Makes a directory
void mkdCommand(int socket, CommandStruct* command)
{
    std::filesystem::path path(curWorkPath.string() + command->aruguments[0]);
    std::filesystem::create_directory(path.string());
    return;
}

void DataSocketListenThread(int socket, int ID)
{
    ThreeWayHandshake(socket);

    Packet packet;
    while (true)
    {
        while (commands[ID].size() == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        CommandStruct* curCommand = commands[ID].dequeue();
        std::cout << "currently Working on: " << curCommand->command << " Command" << std::endl;
        packet.AddItem(curCommand->command.size(), curCommand->command[0]);
        SendPacket(socket, packet);

        if (curCommand->command == "dir")
        {
            dirCommand(socket, curCommand);
        }
        else if (curCommand->command == "cd")
        {
            cdCommand(socket, curCommand);
        }
        else if (curCommand ->command == "get")
        {
            getCommand(socket, curCommand);
        }
        else if (curCommand->command == "list")
        {
            listCommand(socket, curCommand);
        }
        else if (curCommand->command == "mkd")
        {
            mkdCommand(socket, curCommand);
        }
        else if (curCommand->command == "cwd")
        {
            SendCWD(socket);
        }
        else if (curCommand->command == "stop")
        {
            return;
        }

        delete curCommand;
    }
}

void CommandSocketListenThread(int socket, int ID)
{
    ThreeWayHandshake(socket);

    if (SendPortToClient(socket) == -1) { return; }
    struct sockaddr_storage their_addr;
    int addr_size = sizeof(struct sockaddr_storage);
    int acceptRes = accept(DataSocket, (struct sockaddr*)&their_addr, &addr_size);

    if (acceptRes == -1)
    {
        std::cout << "Command Socket Accept Error" << std::endl;
        return;
    }

    std::thread thread(DataSocketListenThread, acceptRes, ID);
    char buffer[CommandBufferSize];
    ZeroMemory(buffer, sizeof(buffer));

    while (true)
    {
        int recvRes = recv(socket, buffer, CommandBufferSize, 0);
        std::cout << recvRes << std::endl;
        buffer[recvRes] = '\0';

        if (recvRes == -1)
        {
            std::cout << "Command recv error" << std::endl;
            break;
        }
        else if (recvRes == 0)
        {
            std::cout << "User Disconected" << std::endl;
            break;
        }
        
        CommandStruct* command = ReadCommand(buffer);

        std::cout << "Recived command: " << command->command << std::endl;

        commands[ID].enqueue(command);
        ZeroMemory(buffer, sizeof(buffer));
    }

    commands[ID].enqueue(new CommandStruct { "stop" });
    thread.join();
}

int main()
{
    WSADATA wsaData;

    if (InitWSADATA(wsaData) == -1)                     { return -1; }
    if (CreateSocket(CommandSocket, COMMANDPORT) == -1) { return -1; }
    if (CreateSocket(DataSocket, DATAPORT) == -1)       { return -1; }
    if (MakeSocketsListen(CommandSocket) == -1)         { return -1; }
    if (MakeSocketsListen(DataSocket) == -1)            { return -1; }

    std::cout << "------------------------" << std::endl;

    while (true)
    {
        struct sockaddr_storage their_addr;
        int addr_size = sizeof(struct sockaddr_storage);

        int acceptRes = accept(CommandSocket, (struct sockaddr*)&their_addr, &addr_size);

        if (acceptRes == -1)
        {
            return -1;
        }

        int randID = rand() % 100000;

        while (commands.contains(randID))
        {
            randID = rand() % 100000;
        }
        commands[randID] = Queue {};

        std::thread thread(CommandSocketListenThread, acceptRes, randID);
        thread.detach();
    }

}








/*
bool sendFile(int socket, FileInfo& fileInfo)
{
    std::cout << "Sending: " << fileInfo.name << std::endl;

    std::filesystem::path absolutePath(path);
    std::ifstream file(absolutePath, std::ios::binary);
    FileInfo currFileInfo;
    Packet packet;

    memcpy(currFileInfo.name, std::filesystem::relative(path, curWorkPath).string().c_str(), path.size());
    currFileInfo.isDir = std::filesystem::is_directory(absolutePath);
    currFileInfo.setSize(std::filesystem::file_size(absolutePath));

    packet.AddItem(sizeof(FileInfo), reinterpret_cast<char&>(currFileInfo));
    SendPacket(socket, packet); // sending file info

    uint64_t totalSend = std::filesystem::file_size(absolutePath);
    while (totalSend != 0)
    {
        uint32_t readSize = __min(totalSend, BUFFERSIZE);

        packet.AddSize(readSize);
        file.read(packet.buffer, readSize);

        if (!SendPacket(socket, packet)) // sending file data
        {
            return false;
        }

        totalSend -= readSize;

        if (totalSend < 0)
        {
            return false; // if file send fails
        }

        std::cout << totalSend << std::endl;
    }

    return true;
}
*/





/*
for (auto val : std::filesystem::directory_iterator(curWorkPath))
            {
                FileInfo currFileInfo;

                std::string path;
                try
                {
                    path = std::filesystem::relative(val, curWorkPath).string();
                }
                catch (const std::exception&)
                {
                    path = val.path().string().substr(curWorkPath.string().length());
                }


                memcpy(currFileInfo.name, path.c_str(), path.size());
                currFileInfo.isDir = val.is_directory();
               // currFileInfo.setSize(val.file_size());
                if (packet.size() + sizeof(currFileInfo) >= 2048)
                {
                    SendPacket(DataSocket, packet);
                }

                packet.AddItem(sizeof(currFileInfo), reinterpret_cast<char&>(currFileInfo));
            }

            if (packet.size() != 0)
            {
                SendPacket(DataSocket, packet);
            }

            uint32_t terminationCode = htonl(0);
            send(DataSocket, reinterpret_cast<char*>(&terminationCode), 4, 0);


*/