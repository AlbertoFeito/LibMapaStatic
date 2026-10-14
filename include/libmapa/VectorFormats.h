#ifndef LIBMAPA_VECTORFORMATS_H_
#define LIBMAPA_VECTORFORMATS_H_

#include "libmapa/MapFeature.h"
#include "libmapa/MapTarget.h"
#include "libmapa/libmapa_export.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace libmapa {

/*!
 * \brief Formatos vectoriales estandar para intercambiar entidades con otras
 *        herramientas (QGIS, Google Earth, un GPS de mano...).
 *
 * - **GeoJSON** (.geojson, .json): el mas completo. Conserva nombre,
 *   descripcion, tipo de dominio, atributos y el estilo basico (claves
 *   "simplestyle": stroke, stroke-width, fill...).
 * - **KML** (.kml): nombre, descripcion, atributos (ExtendedData) y estilo
 *   (LineStyle/PolyStyle, en linea o por styleUrl). Las carpetas se aplanan.
 *   KMZ (KML comprimido) no se lee.
 * - **GPX** (.gpx): puntos (wpt), rutas (rte) y tracks (trk). GPX no tiene
 *   poligonos ni atributos libres: un poligono se escribe como track cerrado
 *   marcado con una extension propia, que al releerlo vuelve a ser poligono.
 *   Al leer, cada ruta o track lleva los atributos \c gpx_kind ("rte"/"trk"),
 *   y si sus puntos traen hora, \c time_start y \c time_end (ISO 8601, UTC):
 *   el mapa marca su inicio y su fin. Un historial de objetivo exportado
 *   (tracksToGpx) trae ademas \c target_id.
 *
 * En los tres, los huecos de los poligonos (anillos interiores) se ignoran: el
 * modelo de MapFeature no los tiene. Coordenadas siempre WGS84 lon/lat.
 */
enum class VectorFormat {
    Unknown,
    GeoJson,
    Kml,
    Gpx
};

//! Formato que corresponde a la extension del fichero (sin mirar el contenido).
LIBMAPA_EXPORT VectorFormat vectorFormatForPath(const QString &path);

/*!
 * \brief Lee un fichero GeoJSON, KML o GPX (segun su extension).
 *
 * Devuelve las entidades leidas (sin capa ni id: los pone quien las anade al
 * mapa). Si no se puede abrir, el formato no se reconoce o no hay ninguna
 * geometria valida, devuelve una lista vacia y deja el motivo en \a error.
 */
LIBMAPA_EXPORT QVector<MapFeature> readVectorFile(const QString &path,
                                                  QString *error = nullptr);

/*!
 * \brief Escribe entidades en GeoJSON, KML o GPX (segun la extension).
 *
 * La escritura es atomica (QSaveFile): o queda el fichero completo, o no se
 * toca el que hubiera. Devuelve false con el motivo en \a error si falla.
 */
LIBMAPA_EXPORT bool writeVectorFile(const QString &path,
                                    const QVector<MapFeature> &features,
                                    QString *error = nullptr);

//! Lectores por formato, sobre el contenido ya en memoria.
LIBMAPA_EXPORT QVector<MapFeature> parseGeoJson(const QByteArray &data,
                                                QString *error = nullptr);
LIBMAPA_EXPORT QVector<MapFeature> parseKml(const QByteArray &data,
                                            QString *error = nullptr);
LIBMAPA_EXPORT QVector<MapFeature> parseGpx(const QByteArray &data,
                                            QString *error = nullptr);

//! Escritores por formato, a memoria.
LIBMAPA_EXPORT QByteArray toGeoJson(const QVector<MapFeature> &features);
LIBMAPA_EXPORT QByteArray toKml(const QVector<MapFeature> &features);
LIBMAPA_EXPORT QByteArray toGpx(const QVector<MapFeature> &features);

/*!
 * \brief Un recorrido con hora en cada punto: el historial de un objetivo.
 *
 * Es lo que GPX llama track (trk). A diferencia de una entidad linea, cada
 * punto lleva su hora, que se escribe como <time> en UTC (ISO 8601 con
 * milisegundos): QGIS, Google Earth o GPXSee lo pueden reproducir en el tiempo.
 */
struct GpxTrack
{
    QString name;                        //!< <name> (p. ej. la etiqueta del objetivo).
    QString description;                 //!< <desc>.
    QString type;                        //!< <type> (p. ej. el kind del objetivo).
    qint64 targetId = -1;                //!< Se guarda como extension libmapa:targetId.
    QVector<TargetTrackPoint> points;    //!< En orden; los invalidos se omiten.
};

//! Escribe recorridos con hora a GPX (un <trk> con un <trkseg> por recorrido).
//! Un punto sin hora valida se escribe sin <time>.
LIBMAPA_EXPORT QByteArray tracksToGpx(const QVector<GpxTrack> &tracks);

/*!
 * \brief Lee los <trk> de un GPX conservando la hora de cada punto.
 *
 * Complemento de parseGpx (que da entidades y descarta las horas): sirve para
 * volver a cargar un historial exportado y reproducirlo. Los segmentos de un
 * mismo track se concatenan. Vacio (con el motivo en \a error) si no hay
 * ningun track con puntos.
 */
LIBMAPA_EXPORT QVector<GpxTrack> parseGpxTracks(const QByteArray &data,
                                                QString *error = nullptr);

//! Escribe recorridos con hora a un fichero GPX, de forma atomica (QSaveFile).
LIBMAPA_EXPORT bool writeGpxTracks(const QString &path, const QVector<GpxTrack> &tracks,
                                   QString *error = nullptr);

} // namespace libmapa

#endif // LIBMAPA_VECTORFORMATS_H_
