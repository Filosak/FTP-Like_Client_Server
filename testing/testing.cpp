#include <iostream>
#include <stdlib.h>
#include <vector>
#include <filesystem>
#include <chrono>
#include <format>
#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <unordered_map> 

#ifdef WIN32
#define stat _stat
#endif

#define Kb 1024
#define Mb 1048576
#define Gb 1073741824

static std::string curWorkPath = "C:/Users/Lenovo/source/repos/FTP_Client_Server/x64/Debug";

struct FullFileInfo // than hopefully cast it into FileInfo
{
    char name[256];           // FTP_Server.cpp
    bool isDir;               // 0 / 1
    uint64_t size;            // 328 246
    std::string absolutePath; // C:\Users\Lenovo\source\repos\FTP_Client_Server\Server\FTP_Server.cpp
};

struct FileInfo // client Recives this
{
    char name[256] = "";
    bool isDir;
    uint64_t size;
};





int main()
{
    std::unordered_map<int, std::vector<std::string>> commands;
    int ID = rand() % 65536;

    if (commands.find(ID) == commands.end())
    {
        return;
    }
    
    commands[ID] = std::vector<std::string> {};


}
