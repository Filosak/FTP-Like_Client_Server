#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <thread>
#include <fstream>
#include <filesystem>
#include <format>

#pragma comment (lib, "Ws2_32.lib")

#define COMMANDPORT         "27000"
#define DATAPORT            "27001"
#define BASESYN              100
#define CommandBufferSize    128

static int CommandSocket;
static int DataSocket;
static addrinfo *ClientAdrr;
static std::vector<char> generalBuffer;
static std::string curWorkPath;

struct FullFileInfo // than hopefully cast it into FileInfo
{
    char name[256];           // Server\FTP_Server.cpp
    bool isDir;               // 0 / 1
    uint64_t size;            // 328 246

    char absolutePath[256];   // C:\Users\Lenovo\source\repos\FTP_Client_Server\Server\FTP_Server.cpp
    long long lastEditTime;
    long long creationTime;

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

struct FileInfo
{
    char name[256];
    bool isDir;
    uint64_t size;

    void Print()
    {
        std::string nameStr = name;
        
        std::cout << std::left;
        std::cout << std::setw(32) << nameStr.substr(0, 30) << std::setw(10) << (isDir ? "<DIR>" : "<FILE>") << std::setw(12) << ntohll(size) << std::endl;
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
    addrinfo hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int getAdrrResult = getaddrinfo(NULL, port, &hints, &ClientAdrr);
    if (getAdrrResult == -1)
    {
        return -1;
    }

    for (; ClientAdrr != NULL; ClientAdrr = ClientAdrr->ai_next)
    {
        socketToCreate = socket(ClientAdrr->ai_family, ClientAdrr->ai_socktype, ClientAdrr->ai_protocol);
        if (socketToCreate == -1)
        {
            continue;
        }
        break;
    }

    if (ClientAdrr == NULL)
    {
        return -1;
    }
    freeaddrinfo(ClientAdrr);
    
    std::cout << "created socket on port: " << port << std::endl;
    return 1;
}

int ConnectSocket(int& socketToConnect, const char* port)
{
    char opt = '1';

    int setSockOptRes = setsockopt(socketToConnect, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (setSockOptRes == -1)
    {
        std::cout << "couldnt set socket opt" << std::endl;
        return -1;
    }

    int connectRes = connect(socketToConnect, ClientAdrr->ai_addr, ClientAdrr->ai_addrlen);
    if (connectRes == -1)
    {
        std::cout << "couldnt connect to server" << std::endl;
        return -1;
    }

    std::cout << "connected socket on port: " << port << std::endl;
    return 1;
}

int SendSYN(int socket)
{
    std::cout << "Sending SYN: " << BASESYN << std::endl;

    uint32_t message = htonl(BASESYN);

    int sendRES = send(socket, (char*)&message, 4, 0);

    if (sendRES == -1)
    {
        std::cout << "Sending SYN Error" << std::endl;
        return -1;
    }

    return 1;
}

int ReciveSYNACK(int socket)
{
    std::cout << "Recieving SYN" << std::endl;

    uint32_t SYN;
    int reciveResSYN = recv(socket, reinterpret_cast<char*>(&SYN), 4, 0);

    if (reciveResSYN == -1 || ntohl(SYN) != BASESYN)
    {
        std::cout << "Recieving SYN Error" << std::endl;
        return -1;
    }

    std::cout << "Recived SYN: " << ntohl(SYN) << std::endl;
    std::cout << "Recieving ACK: " << std::endl;

    uint32_t ACK;
    int reciveResACK = recv(socket, reinterpret_cast<char*>(&ACK), 4, 0);

    if (reciveResACK == -1)
    {
        std::cout << "Recieving ACK Error" << std::endl;
        return -1;
    }

    std::cout << "Recived ACK: " << ntohl(ACK) << std::endl;

    return ACK;
}

int SendACK(int socket, uint32_t &ACK)
{
    std::cout << "Sending ACK" << std::endl;

    int sendRES = send(socket, (char*)&ACK, 4, 0);

    if (sendRES == -1)
    {
        std::cout << "Send ACK Error" << std::endl;
        return -1;
    }

    return 1;
}

char* RecivePortOfDataSocket()
{
    char buffer[6];
    int recvRes = recv(CommandSocket, buffer, 6, 0);
    if (recvRes == -1)
    {
        return NULL;
    }

    std::cout << atoi(buffer) << std::endl;

    return buffer;
}

uint32_t RecivePacketSize(int socket)
{
    uint32_t sizeBuffer;
    int recvRes = recv(socket, reinterpret_cast<char*>(&sizeBuffer), 4, 0);

    if (recvRes == -1)
    {
        std::cout << "Recive Packet Size Error" << std::endl;
        return -1;
    }
    return ntohl(sizeBuffer);
}

uint32_t RecivePacket(int socket)
{
    uint32_t size = RecivePacketSize(socket);

    if (generalBuffer.size() != size)
    {
        generalBuffer.resize(size);
    }

    uint32_t recvRes = recv(socket, generalBuffer.data(), size, 0);

    if (recvRes == 0)
    {
        std::cout << "recvRes == 0" << std::endl;
        return -1;
    } 
    else if (recvRes == -1)
    {
        std::cout << "Recive packet Error" << std::endl;
        return -1;
    }

    //std::cout << "(Recived) " << recvRes << " Bytes" << std::endl;
    return recvRes;
}

bool ReciveCWD()
{
    RecivePacket(DataSocket);

    curWorkPath.clear();
    curWorkPath.assign(generalBuffer.data() + 4, generalBuffer.size() - 4);
    std::cout << curWorkPath << std::endl;
    return true;
}

void PrintProgress(float progress)
{
    int barWidth = 70;

    std::cout << "[";
    int pos = barWidth * progress;
    for (int i = 0; i < barWidth; ++i)
    {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << " %\r";
    std::cout.flush();
}

bool DirCommand()
{
    while (true)
    {
        uint32_t sizeBuffer;
        int recvRes = recv(DataSocket, reinterpret_cast<char*>(&sizeBuffer), 4, 0);

        if (ntohl(sizeBuffer) == 0)
        {
            return true;
        }

        std::vector<char> buffer(ntohl(sizeBuffer));
        recvRes = recv(DataSocket, buffer.data(), buffer.size(), 0);

        uint32_t size;
        FileInfo fileInf;
        int totalRead = 0;

        while (totalRead < ntohl(sizeBuffer))
        {
            memcpy(&size, buffer.data() + totalRead, 4);
            totalRead += 4;

            memcpy(&fileInf, buffer.data() + totalRead, ntohl(size));
            fileInf.Print();
            totalRead += ntohl(size);
        }
    }
}

bool GetCommand()
{
    RecivePacket(DataSocket);
    FileInfo* fileInf = reinterpret_cast<FileInfo*>(generalBuffer.data() + 4);
    fileInf->Print();

    if (fileInf->isDir == 1)
    {
        std::filesystem::create_directory("copies/" + std::string(fileInf->name));
        return true;
    }

    std::ofstream outFile("copies/" + std::string(fileInf->name), std::ios::binary);
    uint64_t totalRecv = ntohll(fileInf->size);
    uint64_t originalFileSize = totalRecv;
    while (totalRecv != 0)
    {
        RecivePacket(DataSocket);
        outFile.write(generalBuffer.data(), generalBuffer.size());
        totalRecv -= generalBuffer.size();

        PrintProgress((originalFileSize - totalRecv) / (double)originalFileSize);
    }

    std::cout << std::endl;
    std::cout << "end" << std::endl;

    return true;
}

bool ListCommand()
{
    RecivePacket(DataSocket);
    FullFileInfo fileInfo;
    memcpy(&fileInfo, generalBuffer.data() + 4, sizeof(FullFileInfo));
    fileInfo.Print();
    return true;
}

void DataSocketThread()
{
    while (true)
    {
        RecivePacket(DataSocket);
        std::string currCommand(generalBuffer.data() + 4, generalBuffer.size() - 4);

        //std::cout << "Currently working on: " << currCommand << " Command" << std::endl;

        if (currCommand == "cd")
        {
            continue;
        }
        else if (currCommand == "dir")
        {
            DirCommand();
        }
        else if (currCommand.substr(0, 3) == "get")
        {
            GetCommand();
        }
        else if (currCommand.substr(0, 4) == "list")
        {
            ListCommand();
        }
        else if (currCommand.substr(0, 3) == "cwd")
        {
            ReciveCWD();
        }

        std::cout << std::endl;
    }
}

int main()
{
    WSADATA wsaData;

    if (InitWSADATA(wsaData) == -1)                      { return -1; }
    if (CreateSocket(CommandSocket, COMMANDPORT) == -1)  { return -1; }
    if (ConnectSocket(CommandSocket, COMMANDPORT) == -1) { return -1; }

    std::cout << "---------------------" << std::endl;
    
    SendSYN(CommandSocket);
    uint32_t ACK = ReciveSYNACK(CommandSocket);
    SendACK(CommandSocket, ACK);
    char* DataPortServer = RecivePortOfDataSocket();
    
    if (CreateSocket(DataSocket, DATAPORT) == -1) 
    {
        return -1; 
    }
    if (ConnectSocket(DataSocket, DataPortServer) == -1)
    {
        return -1;
    }
    
    SendSYN(DataSocket);
    uint32_t ACKData = ReciveSYNACK(DataSocket);
    SendACK(DataSocket, ACKData);
    
    std::thread thread(DataSocketThread);

    while (true)
    {
        std::string commandStr;
        std::getline(std::cin, commandStr);

        int sendRes = send(CommandSocket, commandStr.c_str(), commandStr.size(), 0);

        if (sendRes == -1)
        {
            std::cout << "Command send Error" << std::endl;
        }
    }
}