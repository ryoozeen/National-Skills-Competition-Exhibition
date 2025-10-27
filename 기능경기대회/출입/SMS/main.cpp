// main.cpp
// - 앱 전역 폰트만 살짝 다듬고, 나머지는 메인윈도우에서 QSS로 테마 적용

#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 전역 폰트 힌팅만 완만하게
    QFont f = app.font();
    f.setHintingPreference(QFont::PreferNoHinting);
    app.setFont(f);

    MainWindow w;
    w.setWindowTitle("Safety Management System");
    w.show();                       // OS 타이틀바/창 동작은 그대로

    return app.exec();
}
