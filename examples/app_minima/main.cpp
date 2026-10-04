// app_minima - un producto minimo con mapa SIN conexion.
//
// Todo lo que necesita de la libreria es MapConfig::dataDir: la carpeta del
// paquete de datos, que se distribuye junto al .exe (ver desplegar.bat).
//
//   app_minima                    -> usa <carpeta del .exe>/datos
//   app_minima <carpeta paquete>  -> usa esa carpeta
//   app_minima <carpeta> --comprobar
//                                 -> no abre ventana: dice si el mapa arranca y
//                                    que problemas ve en los datos, y sale con 0
//                                    si todo esta bien (para probar un despliegue).

#include <libmapa/MapWidget.h>

#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>
#include <QTextStream>

// Crea el mapa sobre el paquete de datos y lo muestra en una ventana. Si el
// paquete tiene problemas, el mapa arranca con lo que funcione y se avisa.
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("app_minima"));

    const QStringList args = QCoreApplication::arguments();
    const bool comprobar = args.contains(QStringLiteral("--comprobar"));

    libmapa::MapConfig cfg;
    cfg.dataDir = (args.size() > 1 && !args.at(1).startsWith(QLatin1String("--")))
        ? args.at(1)
        : QCoreApplication::applicationDirPath() + QStringLiteral("/datos");

    QMainWindow ventana;
    auto *mapa = new libmapa::MapWidget(cfg, &ventana);
    ventana.setCentralWidget(mapa);

    if (comprobar) {
        QTextStream out(stdout);
        out << "listo: " << (mapa->isReady() ? "si" : "no") << "\n";
        if (!mapa->isReady())
            out << "error: " << mapa->lastError() << "\n";
        out << "paquete: " << mapa->packageInfo().name << "\n";
        out << "capas base: " << mapa->availableBaseLayers().size() << "\n";
        for (const QString &aviso : mapa->dataWarnings())
            out << "AVISO " << aviso << "\n";
        return (mapa->isReady() && mapa->dataWarnings().isEmpty()) ? 0 : 1;
    }

    if (!mapa->isReady()) {
        QMessageBox::critical(nullptr, QStringLiteral("Mapa"), mapa->lastError());
        return 1;
    }
    ventana.setWindowTitle(QStringLiteral("Mapa - %1").arg(mapa->packageInfo().name));
    ventana.statusBar()->showMessage(mapa->packageInfo().attribution);
    ventana.resize(1100, 750);
    ventana.show();

    if (!mapa->dataWarnings().isEmpty())
        QMessageBox::warning(&ventana, QStringLiteral("Datos del mapa"),
                             mapa->dataWarnings().join(QLatin1Char('\n')));
    return app.exec();
}
