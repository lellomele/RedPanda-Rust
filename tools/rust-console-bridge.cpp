// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <string>
static std::wstring quote(const std::wstring &text){
    if(!text.empty()&&text.find_first_of(L" \t\"")==std::wstring::npos)return text;
    std::wstring result=L"\"";size_t slashes=0;
    for(auto ch:text){if(ch==L'\\'){++slashes;continue;}
        if(ch==L'"'){result.append(slashes*2+1,L'\\');result+=L'"';}
        else{result.append(slashes,L'\\');result+=ch;}slashes=0;}
    result.append(slashes*2,L'\\');return result+L'"';
}
int wmain(int argc,wchar_t **argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(argc<2)return 2;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
    HANDLE input=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,0,nullptr);
    HANDLE output=CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,0,nullptr);
    if(input==INVALID_HANDLE_VALUE||output==INVALID_HANDLE_VALUE){if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);return 3;}
    // Explicit console handles avoid inheriting redirected handles from the IDE.
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input;startup.hStdOutput=output;startup.hStdError=output;
    std::wstring command;for(int i=1;i<argc;++i){if(i>1)command+=L' ';command+=quote(argv[i]);}
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&process)){CloseHandle(input);CloseHandle(output);return 4;}
    CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);
    DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);CloseHandle(input);CloseHandle(output);return int(code);
}
