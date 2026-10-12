// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_PROCESSJOB_H
#define REDPANDA_PROCESSJOB_H
#include <QProcess>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
// Declare before the QProcess: the process must be destroyed before this guard.
// Assignment at CreateProcess time closes the child-spawn race on Windows.
class ProcessJob final {
public:
    ProcessJob() = default;
    ProcessJob(const ProcessJob &) = delete;
    ProcessJob &operator=(const ProcessJob &) = delete;
    ~ProcessJob() {
#ifdef Q_OS_WIN
        if (mStartup.lpAttributeList) DeleteProcThreadAttributeList(mStartup.lpAttributeList);
        if (mJob) CloseHandle(mJob);
#endif
    }
    bool protect(QProcess &process) {
#ifdef Q_OS_WIN
        if (mJob) return false;
        mJob = CreateJobObjectW(nullptr, nullptr);
        if (!mJob) return false;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(mJob, JobObjectExtendedLimitInformation, &info, sizeof(info))) return false;
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        mAttributes.resize(bytes);
        mStartup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(mAttributes.data());
        if (!InitializeProcThreadAttributeList(mStartup.lpAttributeList, 1, 0, &bytes)) {
            mStartup.lpAttributeList = nullptr;
            return false;
        }
        if (!UpdateProcThreadAttribute(mStartup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                      &mJob, sizeof(mJob), nullptr, nullptr)) return false;
        process.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *args) {
            mStartup.StartupInfo = *reinterpret_cast<STARTUPINFOW *>(args->startupInfo);
            mStartup.StartupInfo.cb = sizeof(mStartup);
            args->startupInfo = reinterpret_cast<Q_STARTUPINFO *>(&mStartup);
            args->flags |= EXTENDED_STARTUPINFO_PRESENT;
        });
#else
        Q_UNUSED(process);
#endif
        return true;
    }
    void terminate() {
#ifdef Q_OS_WIN
        if (mJob) TerminateJobObject(mJob, 1);
#endif
    }
private:
#ifdef Q_OS_WIN
    HANDLE mJob = nullptr;
    STARTUPINFOEXW mStartup{};
    QByteArray mAttributes;
#endif
};
#endif
