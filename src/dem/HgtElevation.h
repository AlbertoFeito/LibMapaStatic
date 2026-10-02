#ifndef LIBMAPA_DEM_HGTELEVATION_H_
#define LIBMAPA_DEM_HGTELEVATION_H_

#include <QByteArray>
#include <QGeoCoordinate>
#include <QHash>
#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Lectura de altura del terreno a partir de ficheros SRTM `.hgt`.
 *
 * Un `.hgt` es el formato crudo de SRTM/NASADEM: una rejilla CUADRADA de muestras
 * de 16 bits con signo, big-endian, que cubre un tile de 1x1 grado. El nombre
 * dice la esquina SUROESTE del tile (p.ej. `N19W077.hgt` cubre de 19N a 20N y de
 * 77O a 76O). No hay cabecera: la resolucion se deduce del tamano del fichero
 * (1201x1201 = ~90 m, 3601x3601 = ~30 m). La fila 0 es el borde NORTE y la
 * columna 0 el borde OESTE.
 *
 * Esta clase localiza el tile de una coordenada, lo carga (con una cache LRU
 * pequena para no releer el disco al mover el raton) e interpola bilinealmente
 * la cota. No depende de widgets: vive en libmapa_core y se consulta igual desde
 * la ventana que desde consola o un test.
 *
 * Uso:
 *   HgtElevation dem;
 *   dem.setDirectory("/ruta/a/los/hgt");
 *   double m = dem.elevationAt(QGeoCoordinate(19.989, -76.996));  // Pico Turquino
 *   if (std::isnan(m)) { ...no hay dato... }
 */
class HgtElevation
{
public:
    HgtElevation() = default;

    //! Fija la carpeta donde estan los `.hgt` (nombres tipo `N19W077.hgt`).
    //! Vacia la cache: la proxima consulta releera del disco.
    void setDirectory(const QString &dir);
    QString directory() const { return m_dir; }

    //! Cota del terreno en metros en \a c, o NaN si no hay dato: tile ausente,
    //! fuera de la carpeta configurada, o hueco SRTM (valor de relleno) en
    //! alguno de los nodos que rodean el punto. El llamador decide que mostrar
    //! ante NaN (p.ej. "-"). No lanza excepciones.
    double elevationAt(const QGeoCoordinate &c) const;

    //! Numero maximo de tiles abiertos a la vez (cache LRU). Por defecto 4.
    void setCacheSize(int tiles);

private:
    // Un tile `.hgt` ya cargado en memoria: sus bytes crudos y el lado detectado
    // (numero de muestras por fila/columna). 'ok' en false marca un tile que no
    // existe en la carpeta, para no reintentar el disco en cada movimiento.
    struct Tile {
        QByteArray data;   // muestras int16 big-endian, lado*lado de ellas
        int side = 0;      // 1201, 3601, ...
        bool ok = false;   // false = no hay fichero (se cachea el "no existe")
    };

    //! Nombre de fichero del tile que contiene (latDeg, lonDeg) tomando el suelo
    //! (floor) de cada uno: hemisferio por el signo, `N%02dE%03d` / `S`/`O`.
    static QString fileNameFor(int latFloor, int lonFloor);

    //! Raiz cuadrada entera (el lado) de \a v, o 0 si no es cuadrado perfecto.
    //! Sirve para deducir el lado del tile a partir del numero de muestras.
    static int isqrtExact(qint64 v);

    //! Devuelve (cargando si hace falta) el tile cuya esquina SO es
    //! (latFloor, lonFloor). Gestiona la cache LRU. Nunca falla: si no existe
    //! el fichero, devuelve un Tile con ok=false (tambien cacheado).
    const Tile &tileFor(int latFloor, int lonFloor) const;

    //! Muestra cruda (metros, o valor de hueco) en la fila/columna dadas de un
    //! tile ya cargado. row=0 es el NORTE; col=0 es el OESTE.
    static int sampleAt(const Tile &t, int row, int col);

    QString m_dir;
    int m_cacheSize = 4;

    // Cache de tiles cargados, con una lista de uso para el desalojo LRU. Son
    // mutable porque elevationAt() es const (consulta) pero puebla la cache.
    mutable QHash<QString, Tile> m_cache;   // clave = nombre de fichero
    mutable QVector<QString> m_lru;         // mas reciente al final
};

} // namespace libmapa

#endif // LIBMAPA_DEM_HGTELEVATION_H_
