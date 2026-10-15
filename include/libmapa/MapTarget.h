#ifndef LIBMAPA_MAPTARGET_H_
#define LIBMAPA_MAPTARGET_H_

#include <QColor>
#include <QDateTime>
#include <QGeoCoordinate>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <cmath>
#include <functional>
#include <limits>

namespace libmapa {

/*!
 * \brief Un objetivo movil: posicion, rumbo y una etiqueta de texto.
 *
 * A diferencia de MapFeature (entidades estaticas que se editan a mano), un
 * MapTarget se actualiza en tiempo real desde una fuente externa. La libreria
 * lleva su traza (las ultimas posiciones) y lo dibuja en una capa propia, para
 * que actualizar cientos de objetivos no obligue a redibujar las zonas
 * estaticas.
 *
 * La identidad la pone la aplicacion: suele ser un identificador de pista, un
 * MMSI o similar. Si se deja en -1, MapWidget asigna uno.
 */
struct MapTarget
{
    qint64 id = -1;

    QGeoCoordinate position;
    double headingDeg = 0.0;   //!< 0 = norte, sentido horario. Orienta el simbolo.
    double speed = 0.0;        //!< Informativo; la libreria no lo interpreta.

    QString label;             //!< Texto que se dibuja junto al objetivo.

    //! Clase que la aplicacion asigna al objetivo ("buque", "aeronave", "uav"...).
    //! La libreria no la interpreta; sirve para que la app elija simbolo o filtre.
    QString kind;

    //! Datos libres del objetivo (p.ej. mmsi/imo para AIS, callsign/squawk para
    //! ADS-B, bateria/enlace para un UAV). La libreria los guarda y los devuelve
    //! tal cual, SIN interpretarlos: asi un mismo MapTarget vale para cualquier
    //! dominio sin que la libreria conozca su semantica.
    QVariantMap attributes;

    QColor color = QColor(0xff, 0x8f, 0x00);   //!< Simbolo y traza.
    bool labelVisible = true;
    bool trailVisible = true;

    //! Desplazamiento de la ETIQUETA respecto al objetivo, en PIXELES. Relativo
    //! al punto: al moverse el objetivo, la etiqueta lo sigue; arrastrar la
    //! etiqueta solo cambia esto. (0,0) = pegada al simbolo. Cuando no es nula
    //! se traza una LINEA GUIA del objetivo a la caja de texto.
    QPointF labelOffsetPx = QPointF(0.0, 0.0);

    bool isValid() const { return position.isValid(); }
};

/*!
 * \brief Un punto del historial de un objetivo: donde estaba y cuando.
 *
 * El primero del historial es el ORIGEN: donde se vio el objetivo por primera
 * vez en el sistema. La hora es la que dio la app al actualizar (p. ej. la del
 * mensaje AIS) o, si no dio ninguna, la del momento de la actualizacion (UTC).
 */
struct TargetTrackPoint
{
    QGeoCoordinate position;
    QDateTime time;

    bool isValid() const { return position.isValid(); }
};

/*!
 * \brief Que objetivos mostrar (MapWidget::setTargetFilter).
 *
 * Un objetivo PASA si cumple todos los criterios que se fijen (los que se
 * dejan sin poner no filtran). Los que no pasan se ocultan o, con \a dim, se
 * dibujan tenues. La libreria no interpreta el dominio: el \c kind y los
 * atributos los pone la app, y para lo que no cubren los criterios fijos esta
 * \a predicate.
 */
struct TargetFilter
{
    //! Clases admitidas (MapTarget::kind). Vacio = todas.
    QStringList kinds;
    //! Velocidad minima y maxima en m/s (NaN = sin limite). Es la que da la app
    //! con MapWidget::setTargetVelocity o, si no, la estimada por la traza.
    double minSpeed = std::numeric_limits<double>::quiet_NaN();
    double maxSpeed = std::numeric_limits<double>::quiet_NaN();
    //! Solo los que estan dentro de alguna zona vigilada (watchZone).
    bool onlyInWatchedZones = false;
    //! Criterio libre de la app (p. ej. sobre attributes). Vacio = no filtra.
    std::function<bool(const MapTarget &)> predicate;
    //! true: los que no pasan se ven tenues (sin rotulo ni traza); false: se
    //! ocultan y no se pueden pinchar.
    bool dim = false;

    //! ¿No filtra nada?
    bool isEmpty() const
    {
        return kinds.isEmpty() && std::isnan(minSpeed) && std::isnan(maxSpeed)
            && !onlyInWatchedZones && !predicate;
    }
};

/*!
 * \brief Agrupar objetivos cercanos en pantalla (MapWidget::setTargetClustering).
 *
 * Con miles de objetivos, al alejar el zoom los iconos se amontonan en una
 * mancha. Agrupados, cada monton se dibuja como un circulo con el NUMERO de
 * objetivos que reune. Se agrupa por celdas de \a cellPixels pixeles; una celda
 * con al menos \a minTargets forma grupo. El objetivo seleccionado y los
 * atenuados por el filtro no se agrupan nunca.
 */
struct TargetClustering
{
    bool enabled = false;
    int cellPixels = 64;            //!< Tamaño de la celda (16..512 px).
    int minTargets = 3;             //!< Minimo por grupo (2 o mas).
    //! Solo se agrupa hasta este zoom (incluido); por encima, todos sueltos.
    //! -1 = a cualquier zoom.
    int maxZoom = -1;
    QColor color = QColor(0x15, 0x65, 0xc0);
};

} // namespace libmapa

Q_DECLARE_METATYPE(libmapa::MapTarget)

#endif // LIBMAPA_MAPTARGET_H_
