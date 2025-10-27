#include <QApplication>
#include <QSplashScreen>
#include <QTimer>
#include <QFile>
#include <QPixmap>
#include <QDebug>
#include "login_window.h"

// 스플래시 사용 여부(필요하면 true)
static constexpr bool showSplash = false;

int main(int argc, char *argv[]) {
    qInstallMessageHandler(nullptr); // (특별히 커스텀한 핸들러가 없다면 생략 가능)

    QApplication app(argc, argv);

    LoginWindow *login = new LoginWindow;

    if (showSplash) {
        QPixmap splashPixmap(":/assets/logo_placeholder.png");
        if (splashPixmap.isNull()) splashPixmap = QPixmap(480, 240);
        if (splashPixmap.isNull()) splashPixmap.fill(Qt::white);

        QSplashScreen splash(splashPixmap.scaled(480, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        splash.showMessage("안전관리 시스템", Qt::AlignHCenter | Qt::AlignBottom, Qt::black);
        splash.show();

        QTimer::singleShot(1500, [&]{
            splash.finish(login);
            login->show();
        });
    } else {
        login->show();
    }

    return app.exec();
}
