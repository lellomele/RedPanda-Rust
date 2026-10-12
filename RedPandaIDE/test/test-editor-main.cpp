#include <QTest>
#include "test_editor_symbol_completion.h"
#include "test_settingswidget.h"
#include "test_ide_services.h"
#include "test_service_failure_paths.h"
#include "test_search_models.h"
#include "src/resilientapplication.h"
#include "src/diagnostics.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "../../tests/qtest_suite_runner.h"
int main(int argc, char **argv) {
    QTest::setMainSourcePath(__FILE__, QT_TESTCASE_BUILDDIR);
    ResilientApplication app(argc, argv);
#ifdef Q_OS_WIN
    if(app.arguments().size()==3&&app.arguments()[1]=="--native-crash-probe"){
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
        Diagnostics::install(app.arguments()[2]);
        RaiseException(EXCEPTION_ACCESS_VIOLATION,EXCEPTION_NONCONTINUABLE,0,nullptr);
        return 99;
    }
#endif
    using namespace TestSuites;
    return run(argc,argv,{suite<TestSearchModels>("search-models"),suite<TestIdeServices>("ide-services"),suite<TestServiceFailurePaths>("failure-paths"),suite<TestSettingsWidget>("settings"),suite<TestEditorSymbolCompletion>("smart-editing")});
}
