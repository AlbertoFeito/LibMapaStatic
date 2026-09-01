#ifndef LIBMAPA_TILES_TILECACHE_H_
#define LIBMAPA_TILES_TILECACHE_H_

#include "tiles/TileKey.h"

#include <QCache>
#include <QImage>
#include <QMutex>

namespace libmapa {

/*!
 * \brief Cache LRU de teselas YA DECODIFICADAS.
 *
 * Ataca la falla F-15: CMapaPlot::Cargar_Imagenes destruia todos los
 * QCPItemPixmap y volvia a llamar a QPixmap::loadFromData en cada movimiento
 * del mapa, aunque el 90% de las teselas siguiera en pantalla. Decodificar un
 * PNG de 256x256 cuesta del orden de 1 ms; con 300 teselas visibles son
 * 300 ms por frame, en el hilo de la interfaz.
 *
 * Se guarda QImage y no QPixmap a proposito: QPixmap solo puede construirse
 * en el hilo de la GUI, y la decodificacion la hace un hilo trabajador
 * (TileLoader). La conversion QImage -> QPixmap se hace en el hilo de la GUI
 * al montar el item, y es mucho mas barata que decodificar el PNG.
 *
 * La clase es thread-safe: el hilo trabajador inserta y el de la GUI consulta.
 */
class TileCache
{
public:
    /*!
     * \brief Cache acotada por MEMORIA, no por numero de teselas.
     *
     * El sondeo de las BD reales dejo claro por que importa: la tesela media
     * de la BD de OSM ocupa 1.24 KiB comprimida (1454 MiB / 1203656 teselas)
     * y la de la satelital 10.6 KiB, ocho veces mas. Pero una vez
     * DECODIFICADA, cualquier tesela de 256x256 en RGB32 ocupa 256 KiB en
     * RAM, dé igual lo que pesara en disco.
     *
     * Contar teselas, por tanto, no dice nada sobre la memoria consumida: un
     * limite de 512 teselas son 128 MiB con cualquiera de las dos capas, y
     * seria mucho peor si algun dia aparecen teselas de 512 px (4 veces mas).
     * Por eso el limite se expresa en bytes y el coste de cada entrada se
     * mide sobre la imagen ya decodificada.
     */
    explicit TileCache(qint64 maxBytes = 128 * 1024 * 1024,
                       qint64 pinnedBytes = 32 * 1024 * 1024);

    //! Copia de la imagen cacheada, o QImage nula si no esta.
    //! Se devuelve por valor porque QImage es implicitamente compartida:
    //! la copia es barata y evita punteros colgando entre hilos.
    QImage take(const TileKey &key) const;

    bool contains(const TileKey &key) const;

    /*!
     * \brief Anota que una tesela NO existe en la base de datos.
     *
     * Hace falta un registro de ausencias, no solo de presencias. Sin el, una
     * rejilla con huecos nunca se considera resuelta y se vuelve a consultar
     * en cada movimiento del mapa, eternamente. En la capa satelital, donde
     * el 68 % de las teselas no existe, eso significa reconsultar 3.4 GiB una
     * y otra vez para no encontrar nada.
     *
     * El registro esta acotado y es LRU: no crece sin limite.
     */
    void markMissing(const TileKey &key);
    bool isKnownMissing(const TileKey &key) const;
    int missingCount() const;

    /*!
     * \brief Cuantas teselas de una rejilla estan ya RESUELTAS: en cache o
     *        sabidas inexistentes.
     *
     * Permite no volver a pedir lo que ya se tiene. Sin esto, cada
     * movimiento del mapa re-leia y re-decodificaba la escalera de niveles
     * gruesos, que por definicion cambia poco: medido sobre la BD real,
     * 13 ms tirados en cada peticion solo en re-decodificar teselas que ya
     * estaban en memoria.
     */
    int countCached(int z, int xMin, int xMax, int yMin, int yMax) const;

    //! Decodifica e inserta. Devuelve false si el blob no es una imagen valida.
    bool insert(const TileKey &key, const QByteArray &encoded, bool pinned = false);

    /*!
     * \brief Inserta una imagen ya decodificada.
     * \param pinned si va al area protegida.
     *
     * El area protegida existe porque, sin ella, la escalera de niveles
     * gruesos se autodestruye: son pocas teselas pero valiosisimas, y las
     * cientos de teselas de detalle que llegan despues las expulsan por LRU.
     * Medido en el test progressiveLoadCoversScreenImmediately: 88 huecos en
     * blanco reaparecian justo despues de haberlos cubierto.
     *
     * Al vivir en su propia area, con su propio presupuesto, los niveles de
     * respaldo sobreviven a cualquier avalancha de detalle. Ocupan poco: cada
     * peldano tiene 4^n veces menos teselas que el nivel pedido.
     */
    void insertImage(const TileKey &key, const QImage &image, bool pinned = false);

    void remove(const TileKey &key);
    void clear();

    //! Numero de teselas guardadas ahora mismo.
    int count() const;

    //! Memoria ocupada por las imagenes decodificadas, en bytes.
    qint64 usedBytes() const;

    qint64 maxBytes() const;
    void setMaxBytes(qint64 bytes);

    //! Presupuesto del area protegida (niveles de respaldo).
    qint64 pinnedBytes() const;
    void setPinnedBytes(qint64 bytes);
    int pinnedCount() const;

    //! Estadisticas acumuladas, utiles para verificar que la cache sirve.
    struct Stats {
        qint64 hits = 0;
        qint64 misses = 0;
        qint64 inserts = 0;
        qint64 decodeFailures = 0;
        double hitRatio() const {
            const qint64 total = hits + misses;
            return total > 0 ? static_cast<double>(hits)
                             / static_cast<double>(total)
                           : 0.0;
        }
    };
    Stats stats() const;
    void resetStats();

private:
    mutable QMutex m_mutex;
    QCache<TileKey, QImage> m_cache;    // teselas de detalle; QCache ya es LRU
    QCache<TileKey, QImage> m_pinned;   // niveles gruesos de respaldo
    QCache<TileKey, bool> m_missing;    // teselas que se sabe que no existen
    mutable Stats m_stats;
};

} // namespace libmapa

#endif // LIBMAPA_TILES_TILECACHE_H_
