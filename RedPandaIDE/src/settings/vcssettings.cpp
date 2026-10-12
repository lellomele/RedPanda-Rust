#include "vcssettings.h"
#include "../systemconsts.h"
#include <QDir>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>

VCSSettings::VCSSettings(SettingsPersistor *persistor):
    BaseSettings{persistor,SETTING_VCS},
    mGitOk(false)
{
}

void VCSSettings::doSave()
{
    saveValue("git_path",mGitPath);
}

void VCSSettings::doLoad()
{
    setGitPath(stringValue("git_path", ""));
    if (!mGitOk) detectGitInPath();
}

const QString &VCSSettings::gitPath() const
{
    return mGitPath;
}

void VCSSettings::setGitPath(const QString &newGitPath)
{
    mGitPath = newGitPath;
    validateGit();
}

void VCSSettings::validateGit()
{
    mGitOk = false;
    QFileInfo fileInfo(mGitPath);
    if (!fileInfo.isFile() || !fileInfo.isExecutable()) {
        return;
    }
    mGitOk=true;
//    QStringList args;
//    args.append("--version");
//    QString output = runAndGetOutput(
//                fileInfo.fileName(),
//                fileInfo.absolutePath(),
//                args);
//    mGitOk = output.startsWith("git version");
}

bool VCSSettings::gitOk() const
{
    return mGitOk;
}

void VCSSettings::detectGitInPath()
{
    const QString found = QStandardPaths::findExecutable(GIT_PROGRAM);
    if (!found.isEmpty()) {
        setGitPath(found);
        if (mGitOk) { save(); return; }
    }
#ifdef Q_OS_WIN
    // Git for Windows is commonly installed without adding it to PATH.
    const auto system = QProcessEnvironment::systemEnvironment();
    for (const auto &key : {"ProgramFiles", "ProgramW6432", "LOCALAPPDATA"}) {
        const auto base = system.value(key);
        if (base.isEmpty()) continue;
        for (const auto &suffix : {"/Git/cmd/git.exe", "/Programs/Git/cmd/git.exe"}) {
            const auto candidate = base + suffix;
            if (QFileInfo(candidate).isFile()) {
                setGitPath(candidate);
                if (mGitOk) { save(); return; }
            }
        }
    }
#endif
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");
    QStringList pathList = path.split(PATH_SEPARATOR);
    QSet<QString> searched;
    foreach (const QString& s, pathList){
        if (searched.contains(s))
            continue;;
        searched.insert(s);
        QDir dir(s);
        if (dir.exists(GIT_PROGRAM)) {
            QString oldPath = mGitPath;
            setGitPath(dir.filePath(GIT_PROGRAM));
            validateGit();
            if (mGitOk) {
                save();
                return;
            } else {
                mGitPath = oldPath;
            }
        }

    }
}
