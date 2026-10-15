/*!
 * demo - banco de pruebas de libmapa::MapWidget.
 *
 * Uso:
 *     demo [ruta/a/datasets.json]
 *
 * Ejercita TODA la API de la Fase 6 sin escribir codigo: capas con
 * visibilidad y orden, dibujo y edicion de entidades, propiedades y atributos
 * de dominio, estilo, deshacer/rehacer y persistencia en SQLite.
 *
 * El panel lateral se mantiene al dia SOLO a partir de las senales del
 * MapWidget (featureAdded/featureRemoved/featureLayersChanged): no hay que
 * acordarse de refrescarlo tras cada operacion, asi que da igual que el cambio
 * venga de un boton, del teclado sobre el mapa, de deshacer o de una carga.
 */

#include "Ventana.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QTranslator>


// Traduce al español los textos propios de Qt (botones «Cerrar», «Cancelar»,
// «Sí», diálogos de fichero...): la demo está en español y sin esto salían en
// inglés. Busca qtbase_es.qm primero en "translations" junto al ejecutable (lo
// que deja desplegar.bat) y, si no, en la instalación de Qt. Si no lo
// encuentra, sigue en inglés sin más.
static void cargarTraduccionQt(QApplication &app)
{
    auto *qt = new QTranslator(&app);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QString deQt = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
    const QString deQt = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
    const QStringList dirs = { QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
                               deQt };
    for (const QString &dir : dirs) {
        if (qt->load(QStringLiteral("qtbase_es"), dir)) {
            app.installTranslator(qt);
            return;
        }
    }
    delete qt;
}

int main(int argc, char *argv[])
{
    // Escalado de pantalla. En Windows con el zoom de pantalla al 125 % o
    // 150 %, sin esto Qt 5 entrega las coordenadas del raton en pixeles
    // fisicos mientras dibuja en pixeles logicos (o al reves, segun la
    // version), y todo lo que se coloque con el raton queda desplazado
    // respecto al cursor. En Qt 6 ya es el comportamiento por defecto.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    cargarTraduccionQt(app);
    // Para QSettings (recordar el tema elegido entre sesiones).
    QCoreApplication::setOrganizationName(QStringLiteral("LibMapaStatic"));
    QCoreApplication::setApplicationName(QStringLiteral("demo"));

    // Primer positional: la carpeta de un paquete de datos (o su mapa.json), o un
    // datasets.json. Opcionales: --dem <carpeta> / --dem-db <fichero>
    // (elevacion) y --features <fichero> (entidades: se CARGA al arrancar y es el
    // fichero de trabajo para «Guardar»; el guardado es EXPLÍCITO, no automático).
    QString datasets;
    QString demDir, demDb, featuresDb;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--dem-db") && i + 1 < argc)
            demDb = QString::fromLocal8Bit(argv[++i]);
        else if ((a == QLatin1String("--dem") || a == QLatin1String("--elev"))
                 && i + 1 < argc)
            demDir = QString::fromLocal8Bit(argv[++i]);
        else if (a == QLatin1String("--features") && i + 1 < argc)
            featuresDb = QString::fromLocal8Bit(argv[++i]);
        else if (datasets.isEmpty() && !a.startsWith(QLatin1String("--")))
            datasets = a;
    }
    // Sin argumento: un mapa.json en la carpeta actual, si lo hay; si no, el
    // datasets.json de siempre.
    if (datasets.isEmpty())
        datasets = QFile::exists(QDir::currentPath() + QStringLiteral("/mapa.json"))
            ? QDir::currentPath()
            : QDir::currentPath() + QStringLiteral("/datasets.json");
    const QFileInfo origen(datasets);
    const bool esPaquete = origen.isDir()
        || origen.fileName().compare(QLatin1String("mapa.json"), Qt::CaseInsensitive) == 0;

    Ventana v(datasets, esPaquete, demDir, demDb, featuresDb);
    v.show();
    return app.exec();
}
