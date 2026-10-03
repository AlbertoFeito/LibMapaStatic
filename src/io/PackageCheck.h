#ifndef LIBMAPA_IO_PACKAGECHECK_H_
#define LIBMAPA_IO_PACKAGECHECK_H_

#include <QString>
#include <QStringList>
#include <QVector>

namespace libmapa {

struct DataPackage;

/*!
 * \brief Comprueba que un paquete de datos esta completo y se puede usar SIN
 *        conexion, antes de distribuirlo o al abrirlo.
 *
 * El producto final no puede descargar nada: si falta un fichero, una base no
 * abre o sus imagenes no se pueden decodificar (falta el plugin de Qt `qjpeg`,
 * tipico al desplegar), el mapa sale en blanco sin decir por que. Esta clase lo
 * dice antes.
 *
 * Dos modos:
 *  - RAPIDO (\ref Options::coverage = false): lo que cuesta milisegundos y se
 *    puede hacer al arrancar (driver SQLite, abrir cada base, decodificar una
 *    tesela, leer las capas fijas). Lo usa MapWidget.
 *  - COMPLETO: ademas cuenta, nivel a nivel, cuantas teselas hay dentro de la
 *    zona del paquete. Lo usa la herramienta check_data.
 *
 * No escribe nada: todas las bases se abren en solo lectura y la BD de
 * entidades del usuario ni se toca (abrirla podria migrar su esquema).
 */
struct PackageCheck
{
    enum class Severity {
        Info,       //!< Dato util para el informe; no es un problema.
        Warning,    //!< El mapa funciona, pero algo falta o no es portable.
        Error       //!< Una parte del mapa NO va a funcionar.
    };

    //! Un hallazgo del informe.
    struct Finding {
        Severity severity = Severity::Info;
        QString subject;     //!< Sobre que: "osm", "elevation", "package"...
        QString message;
    };

    //! Teselas presentes en un nivel dentro de la zona del paquete.
    struct LevelCoverage {
        int z = 0;
        qint64 present = 0;      //!< Teselas en la BD dentro de la zona.
        qint64 expected = 0;     //!< Teselas de la rejilla que cubren la zona.
        double fraction() const
        {
            return expected > 0 ? static_cast<double>(present)
                                      / static_cast<double>(expected)
                                : 0.0;
        }
    };

    //! Resumen de una capa base.
    struct DatasetReport {
        QString id;
        QString file;
        qint64 bytes = 0;
        bool opened = false;
        QString imageFormat;             //!< "jpeg", "png"... de una tesela real.
        QVector<LevelCoverage> levels;   //!< Solo en modo completo.
    };

    struct Options {
        bool coverage = true;            //!< Contar cobertura por nivel (lento en BD grandes).
        int maxCoverageZoom = 99;        //!< No contar por encima de este zoom.
    };

    QString manifestPath;
    QVector<Finding> findings;
    QVector<DatasetReport> datasets;
    qint64 totalBytes = 0;               //!< Suma de los ficheros del paquete.

    bool hasErrors() const { return count(Severity::Error) > 0; }
    int count(Severity s) const;

    //! Mensajes de aviso y error, uno por linea ("osm: ..."), para un log o
    //! una ventana. Sin los Info.
    QStringList problems() const;

    //! Comprueba el paquete de \a path (carpeta o mapa.json).
    static PackageCheck run(const QString &path, const Options &options);
    //! Igual, sobre un paquete ya leido (no lo vuelve a parsear).
    static PackageCheck run(const DataPackage &package, const Options &options);

private:
    void add(Severity s, const QString &subject, const QString &message);
};

} // namespace libmapa

#endif // LIBMAPA_IO_PACKAGECHECK_H_
