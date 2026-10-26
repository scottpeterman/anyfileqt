// core/tests/test_main.cpp
//
//   anyfile_core_tests            run every case
//   anyfile_core_tests <substr>   run the cases whose name contains <substr>
#include <QCoreApplication>
#include <QTemporaryDir>

#include "harness.h"

int main(int argc, char **argv) {
    // The trash test must not touch the real ~/.local/share/Trash. Qt's
    // freedesktop trash reads XDG_DATA_HOME on every call, so pointing it at a
    // scratch directory before anything runs keeps the whole suite off it.
    QTemporaryDir xdg;
    qputenv("XDG_DATA_HOME", xdg.path().toLocal8Bit());

    QCoreApplication app(argc, argv);
    return harness::run(argc > 1 ? argv[1] : nullptr);
}
