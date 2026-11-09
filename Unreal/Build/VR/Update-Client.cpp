// Standalone Win32 updater: no PowerShell/.NET dependency under Wine/Proton.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cwctype>
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"user32.lib")
namespace fs=std::filesystem;
struct Handle
{
    HANDLE value=nullptr;
    explicit Handle(HANDLE v=nullptr):value(v){}
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;
};
static std::wstring Quote(const std::wstring& text)
{
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t c:text)
    {
        if(c==L'\\'){++slashes;continue;}
        result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;
    }
    result.append(slashes*2,L'\\');return result+L"\"";
}
static std::string Sha256(const fs::path& path)
{
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot open downloaded installer.");
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw std::runtime_error("SHA-256 is unavailable.");
    struct Cleanup{BCRYPT_ALG_HANDLE a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,hash};
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("Cannot verify installer.");
    std::vector<unsigned char> bytes(1024*1024);
    while(file)
    {
        file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(file.gcount() && BCryptHashData(hash,bytes.data(),static_cast<ULONG>(file.gcount()),0)<0)
            throw std::runtime_error("Installer verification failed.");
    }
    if(!file.eof())throw std::runtime_error("Installer read failed.");
    unsigned char digest[32];
    if(BCryptFinishHash(hash,digest,32,0)<0)throw std::runtime_error("Installer verification failed.");
    std::string result;for(auto c:digest){result+="0123456789abcdef"[c>>4];result+="0123456789abcdef"[c&15];}return result;
}
static void Log(const fs::path& log,const std::string& message)
{
    std::ofstream(log,std::ios::app)<<message<<"\n";
}
static std::string Utf8(const std::wstring& text)
{
    const int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string result(size,'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);
    return result;
}
static std::wstring Utf16(const std::string& text)
{
    const int size=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(size,L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size);
    return result;
}
static std::string WindowsError(DWORD code)
{
    wchar_t text[1024]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,text,1024,nullptr);
    auto message=Utf8(text);
    while(!message.empty() && (message.back()=='\r'||message.back()=='\n'))message.pop_back();
    return "Windows error "+std::to_string(code)+": "+message;
}
static DWORD Launch(const fs::path& exe,const std::vector<std::wstring>& args,const fs::path& cwd,bool wait,bool visible,
    const fs::path& log,const std::string& stage)
{
    PROCESS_INFORMATION process{};
    Log(log,stage+": "+exe.u8string());
    for(int attempt=0;;++attempt)
    {
        // CreateProcess may modify its command buffer even when it fails.
        std::wstring command=Quote(exe.wstring());for(const auto& arg:args)command+=L" "+Quote(arg);
        STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=visible?SW_SHOWNORMAL:SW_HIDE;
        if(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,cwd.c_str(),&start,&process))break;
        const DWORD error=GetLastError();
        // Only retry a failed launch, never an installer that has already run.
        // Temporary scanner/file locks can outlive the installer process.
        if(attempt<16 && (error==ERROR_ACCESS_DENIED||error==ERROR_SHARING_VIOLATION||error==ERROR_LOCK_VIOLATION))
        {
            Log(log,stage+" temporarily blocked; retry "+std::to_string(attempt+1)+". "+WindowsError(error));
            Sleep(500);continue;
        }
        throw std::runtime_error("Path: "+exe.u8string()+"\n"+WindowsError(error));
    }
    Handle h(process.hProcess),thread(process.hThread);
    if(!wait)return process.dwProcessId;
    if(WaitForSingleObject(h.value,INFINITE)!=WAIT_OBJECT_0)throw std::runtime_error("Cannot wait for installer.");
    DWORD code=1;GetExitCodeProcess(h.value,&code);return code;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)
{
    wchar_t own[MAX_PATH];GetModuleFileNameW(nullptr,own,MAX_PATH);
    const auto cache=fs::path(own).parent_path(),log=cache/L"update-result.txt";
    bool dialogs=true;
    bool installed=false;
    std::string stage="Validating update";
    std::ofstream(log)<<"Starting Windows update.\n";
    try
    {
        int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        if(!argv)throw std::runtime_error("Cannot read update arguments.");
        struct ArgsCleanup{wchar_t** p;~ArgsCleanup(){LocalFree(p);}} cleanup{argv};
        std::wstring installer,root,expected,mode=L"Desktop",pid;
        for(int i=1;i<argc;++i)
        {
            const std::wstring key=argv[i];if(key==L"-NoDialogs"){dialogs=false;continue;}
            if(i+1>=argc)throw std::runtime_error("Incomplete updater arguments.");
            auto value=argv[++i];
            if(key==L"-Installer")installer=value;else if(key==L"-InstallDirectory")root=value;
            else if(key==L"-ExpectedSha256")expected=value;else if(key==L"-GameProcessId")pid=value;
            else if(key==L"-Mode")mode=value;else throw std::runtime_error("Unknown updater argument.");
        }
        if(installer.empty()||root.empty()||expected.size()!=64||pid.empty()||(mode!=L"Desktop"&&mode!=L"VR"))
            throw std::runtime_error("Invalid update request.");
        std::string hash;
        for(auto c:expected){if(!iswxdigit(c)||c>127)throw std::runtime_error("Invalid installer checksum.");hash+=char(towlower(c));}
        const auto payload=fs::canonical(installer),installRoot=fs::canonical(root);
        Log(log,"Installer: "+payload.u8string()+"\nInstall directory: "+installRoot.u8string());
        const auto game=installRoot/L"ACUnreal/Binaries/Win64/ACUnreal.exe";
        if(!fs::is_regular_file(game)||!fs::is_regular_file(installRoot/L"ACUnreal.exe"))throw std::runtime_error("The selected game folder is incomplete.");
        if(Sha256(payload)!=hash)throw std::runtime_error("The update failed verification. Download it again.");
        stage="Waiting for game shutdown";Log(log,stage);
        size_t used=0;const auto parent=std::stoul(pid,&used);
        if(used!=pid.size()||!parent||parent==GetCurrentProcessId())throw std::runtime_error("Invalid game process.");
        Handle gameProcess(OpenProcess(SYNCHRONIZE,FALSE,parent));
        if(!gameProcess.value && GetLastError()!=ERROR_INVALID_PARAMETER)throw std::runtime_error("Cannot wait for the current game to close.");
        if(gameProcess.value && WaitForSingleObject(gameProcess.value,120000)!=WAIT_OBJECT_0)throw std::runtime_error("The game did not close. Close it and retry.");
        // A second client in the same installation must not have its files replaced.
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
        if(snapshot.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot check running game instances.");
        PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
        for(BOOL found=Process32FirstW(snapshot.value,&entry);found;found=Process32NextW(snapshot.value,&entry))
        {
            if(_wcsicmp(entry.szExeFile,L"ACUnreal.exe"))continue;
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID));
            wchar_t name[32768];DWORD length=32768;
            if(process.value && QueryFullProcessImageNameW(process.value,0,name,&length) && fs::equivalent(fs::path(name),game))
                throw std::runtime_error("Another copy of this client is running. Close it before updating.");
        }
        std::vector<std::wstring> options={L"/SP-",L"/SILENT",L"/NORESTART",L"/NOCLOSEAPPLICATIONS",L"/NORESTARTAPPLICATIONS",L"/DIR="+installRoot.wstring(),L"/LOG="+(cache/L"installer.log").wstring()};
        if(!fs::exists(installRoot/L"unins000.exe"))options.push_back(L"/ACPORTABLE=1");
        stage="Starting installer";
        const DWORD code=Launch(payload,options,cache,true,false,log,stage);
        stage="Installing update";Log(log,"Installer exit code: "+std::to_string(code));
        if(code)throw std::runtime_error("Installer failed (exit "+std::to_string(code)+"). See installer.log.");
        installed=true;Log(log,"Update installed successfully.");
        stage="Restarting updated game";
        const DWORD restarted=Launch(game,{mode==L"VR"?L"-vr":L"-nohmd"},installRoot,false,true,log,stage);
        Log(log,"Restarted client PID "+std::to_string(restarted)+".");
        return 0;
    }
    catch(const std::exception& error)
    {
        const std::string message=(installed ? "The update installed successfully, but the game could not reopen.\nOpen the game from your normal shortcut.\n\n"
            : "The update could not finish. Use the latest installer from thwargle.com if retrying fails.\n\n")
            +stage+" failed. "+error.what()+"\n\nIf Windows denies access, check folder permissions and Windows Security Protection history."
            +"\nDetails: "+log.u8string();
        Log(log,message);
        if(dialogs)MessageBoxW(nullptr,Utf16(message).c_str(),installed?L"AC updated; restart needed":L"AC update could not finish",MB_OK|MB_ICONWARNING);
        return 1;
    }
}
