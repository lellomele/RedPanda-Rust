#include "diagnostics.h"
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif
namespace {
#ifdef Q_OS_WIN
HANDLE nativeLog=INVALID_HANDLE_VALUE;
HMODULE dumpLibrary=nullptr;
using DumpWriter=BOOL(WINAPI *)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
DumpWriter dumpWriter=nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER previousNativeFilter=nullptr;
wchar_t dumpFilename[4096]={};
wchar_t pendingFilename[4096]={};
volatile LONG handlingNativeCrash=0;
char *hex(char *out,ULONG_PTR value,int digits){
    const char alphabet[]="0123456789ABCDEF";
    for(int i=digits-1;i>=0;--i)*out++=alphabet[(value>>(i*4))&15];
    return out;
}
LONG WINAPI nativeCrash(EXCEPTION_POINTERS *exception){
    // No Qt objects, heap allocations, application locks or GUI operations:
    // the fault may have occurred with those facilities in an invalid state.
    if(InterlockedCompareExchange(&handlingNativeCrash,1,0)!=0)return EXCEPTION_EXECUTE_HANDLER;
    if(exception&&exception->ExceptionRecord){
        char record[128]="Unhandled native exception code=0x";
        char *end=record+sizeof("Unhandled native exception code=0x")-1;
        end=hex(end,exception->ExceptionRecord->ExceptionCode,8);
        const char address[]=" address=0x";for(char ch:address)if(ch)*end++=ch;
        end=hex(end,reinterpret_cast<ULONG_PTR>(exception->ExceptionRecord->ExceptionAddress),sizeof(ULONG_PTR)*2);
        const char thread[]=" thread=0x";for(char ch:thread)if(ch)*end++=ch;
        end=hex(end,GetCurrentThreadId(),8);*end++='\n';
        DWORD written;
        if(nativeLog!=INVALID_HANDLE_VALUE){WriteFile(nativeLog,record,DWORD(end-record),&written,nullptr);FlushFileBuffers(nativeLog);}
        if(pendingFilename[0]){
            HANDLE pending=CreateFileW(pendingFilename,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(pending!=INVALID_HANDLE_VALUE){WriteFile(pending,record,DWORD(end-record),&written,nullptr);FlushFileBuffers(pending);CloseHandle(pending);}
        }
    }
    if(dumpWriter&&dumpFilename[0]&&exception){
        HANDLE file=CreateFileW(dumpFilename,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE){
            MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),exception,FALSE};
            const BOOL saved=dumpWriter(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,&info,nullptr,nullptr);
            FlushFileBuffers(file);CloseHandle(file);
            if(nativeLog!=INVALID_HANDLE_VALUE){
                const char ok[]="Native minidump saved\n",failed[]="Native minidump write failed\n";
                DWORD written;WriteFile(nativeLog,saved?ok:failed,saved?sizeof(ok)-1:sizeof(failed)-1,&written,nullptr);FlushFileBuffers(nativeLog);
            }
        }
    }
    return previousNativeFilter?previousNativeFilter(exception):EXCEPTION_EXECUTE_HANDLER;
}
void installNative(const QString &filename){
    if(nativeLog!=INVALID_HANDLE_VALUE)CloseHandle(nativeLog);
    if(QFileInfo(filename+".native-crash.log").size()>2*1024*1024){QFile::remove(filename+".native-crash.log.1");QFile::rename(filename+".native-crash.log",filename+".native-crash.log.1");}
    const auto nativePath=(filename+".native-crash.log").toStdWString();
    nativeLog=CreateFileW(nativePath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    const auto dump=QFileInfo(filename).absolutePath()+"/crash-"+QString::number(GetCurrentProcessId())+"-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz")+".dmp";
    const auto wide=dump.toStdWString();dumpFilename[0]=0;
    if(wide.size()<4096){for(size_t i=0;i<=wide.size();++i)dumpFilename[i]=wide.c_str()[i];}
    const auto pending=(filename+".crash-pending").toStdWString();pendingFilename[0]=0;
    if(pending.size()<4096){for(size_t i=0;i<=pending.size();++i)pendingFilename[i]=pending.c_str()[i];}
    if(!dumpLibrary)dumpLibrary=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(dumpLibrary)dumpWriter=reinterpret_cast<DumpWriter>(GetProcAddress(dumpLibrary,"MiniDumpWriteDump"));
    handlingNativeCrash=0;
}
#endif
QMutex logMutex; QString logFilename; QtMessageHandler previousHandler = nullptr; bool installed = false;
void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    static thread_local bool writing = false;
    if (writing) return;
    QScopedValueRollback<bool> guard(writing, true);
    QtMessageHandler forward = nullptr;
    {
        QMutexLocker lock(&logMutex);
        forward = previousHandler;
        if (installed && type != QtDebugMsg) {
            if (QFileInfo(logFilename).size() > 2 * 1024 * 1024) {
                const QString previous = logFilename + ".1";
                QFile::remove(previous); QFile::rename(logFilename, previous);
            }
            QFile file(logFilename);
            if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                const auto line = (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " [" + QString::number(type) + "] " + message + "\n").toUtf8();
                file.write(line);
            }
        }
    }
    if (forward) forward(type, context, message);
}
}
void Diagnostics::install(const QString &filename) {
    QMutexLocker lock(&logMutex);
    logFilename = filename;
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) file.write("RedPanda session started\n");
    // Reconfiguration must retain the original handler, not install our handler
    // as its own predecessor and leave it installed after shutdown.
#ifdef Q_OS_WIN
    installNative(filename);
#endif
    if (!installed) {
#ifdef Q_OS_WIN
        previousNativeFilter=SetUnhandledExceptionFilter(nativeCrash);
#endif
        previousHandler = qInstallMessageHandler(handler); installed = true;
    }
}
QString Diagnostics::takePreviousCrashDirectory(){
    QMutexLocker lock(&logMutex);
    QFile pending(logFilename+".crash-pending");
    if(!pending.open(QIODevice::ReadOnly)||pending.size()>1024)return {};
    const auto record=pending.read(1024);pending.close();
    if(!record.startsWith("Unhandled native exception"))return {};
    if(!pending.remove())return {};
    return QFileInfo(logFilename).absolutePath();
}
void Diagnostics::uninstall() {
    QMutexLocker lock(&logMutex);
    if (installed) {
        qInstallMessageHandler(previousHandler); installed = false;
#ifdef Q_OS_WIN
        SetUnhandledExceptionFilter(previousNativeFilter);
        if(nativeLog!=INVALID_HANDLE_VALUE){CloseHandle(nativeLog);nativeLog=INVALID_HANDLE_VALUE;}
#endif
    }
}
