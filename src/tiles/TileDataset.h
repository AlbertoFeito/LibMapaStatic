#ifndef LIBMAPA_TILES_TILEDATASET_H_
#define LIBMAPA_TILES_TILEDATASET_H_

#include <QJsonObject>
#include <QString>

namespace libmapa {

/*!
 * \brief Orden del eje Y en el esquema de teselas.
 *
 * XYZ  : y crece hacia el SUR   (slippy map estandar de OSM)
 * TMS  : y crece hacia el NORTE (invertido);  y_tms = (2^z - 1) - y_xyz
 */
enum class TileScheme {
    XYZ,
    TMS
};

QString tileSchemeToString(TileScheme s);
TileScheme tileSchemeFromString(const QString &s, bool *ok = nullptr);

/*!
 * \brief Todo lo que hay que saber de una BD de teselas.
 *
 * Esta estructura sustituye a los #define invertidos de cutiles.h
 * (ImagenesOSM 0 / ImagenesSatelitales 1, que decian justo lo contrario
 * que Cargar_BD_IMG) y a los parches de zoom dispersos por el codigo:
 *
 *   - "int Zoom = 18 - Zoom_Level;" (solo para satelital)  -> zFactor/zOffset
 *   - "if (ZoomValido == 17) ZoomValido = 16;"             -> minZoom/maxZoom
 *   - "if (tipoBD == 1) { if (ZoomValido < 16) ... }"      -> minZoom/maxZoom
 *
 * Cada BD trae sus propios parametros; el resto del codigo nunca vuelve a
 * preguntar "que tipo de mapa es este".
 */
struct TileDataset
{
    QString id;              //!< Identificador estable: "osm", "satelital".
    QString displayName;     //!< Texto para la interfaz.
    QString filePath;        //!< Ruta absoluta al .sqlitedb.
    QString tableName = QStringLiteral("tiles");

    // --- Mapeo zoom logico <-> valor almacenado en la columna z -------------
    // storedZ = zFactor * logicalZ + zOffset
    //   OSM        : zFactor =  1, zOffset =  0   -> storedZ == logicalZ
    //   Satelital  : zFactor = -1, zOffset = 17   -> storedZ == 17 - logicalZ
    int zFactor = 1;
    int zOffset = 0;

    /*!
     * \brief Nivel de FONDO GARANTIZADO: el mas grueso con cobertura total.
     *
     * Las BD de este proyecto solo guardan teselas donde hay tierra, salvo en
     * los niveles muy gruesos. Medido sobre la satelital real:
     *
     *     z 1..5  relleno 100 %, lon -180..180  -> mundo entero, CON mar
     *     z 6     relleno 100 %, lon -90..-73   -> solo el recuadro de Cuba
     *     z 9     relleno  65 %                 -> ya faltan teselas de mar
     *     z 15    relleno  32 %                 -> solo tierra
     *
     * Sin cargar un nivel de fondo, al mirar mar abierto no hay ningun
     * ancestro disponible y la pantalla queda en blanco: la escalera de
     * respaldo es relativa al zoom pedido (z-6) y desde z15 solo llega al 9,
     * que tambien tiene huecos.
     *
     * Este nivel se carga siempre, cuesta una o dos teselas y se queda
     * anclado en cache para toda la sesion.
     */
    int baseZoom = 0;

    int minZoom  = 0;        //!< Zoom logico minimo disponible.
    int maxZoom  = 17;       //!< Zoom logico maximo con teselas de cualquier tipo.

    /*!
     * \brief Ultimo zoom con cobertura comparable a la del nivel de
     *        referencia.
     *
     * Existe porque las dos BD reales tienen niveles finales a medio poblar:
     * la de OSM guarda 23407 teselas en z=16 que cubren una franja de 0.099
     * grados de longitud, en mar abierto al oeste de Cuba. Permitir el zoom
     * hasta ahi deja la pantalla resolviendose casi entera por respaldo de
     * tesela padre. El widget limita el zoom a este valor por defecto.
     */
    int recommendedMaxZoom = 17;

    /*!
     * \brief Relleno tipico: teselas presentes / teselas del rectangulo.
     *
     * Medido por la sonda sobre el nivel de referencia. Determina cuanta
     * escalera de respaldo hay que precargar:
     *
     *   OSM        1.00  -> no hay huecos; la escalera solo aporta latencia
     *   Satelital  0.32  -> dos tercios de la rejilla necesitan respaldo
     *
     * Sin este dato, la escalera era fija de tres peldanos y en la BD de OSM
     * mas que duplicaba el tiempo hasta ver el detalle: 188 ms de escalera
     * inutil frente a 155 ms del nivel pedido.
     */
    double typicalFill = 1.0;

    TileScheme scheme = TileScheme::XYZ;
    int sValue   = 0;        //!< Valor de la columna 's' presente en la BD.
    int tileSize = 256;      //!< Lado del tile en pixeles.

    //! Nombres de columna, resueltos al abrir (no se asumen posiciones fijas).
    QString colZ     = QStringLiteral("z");
    QString colX     = QStringLiteral("x");
    QString colY     = QStringLiteral("y");
    QString colS     = QStringLiteral("s");
    QString colImage = QStringLiteral("image");
    bool hasSColumn  = true;

    int storedZ(int logicalZ) const { return zFactor * logicalZ + zOffset; }

    int logicalZ(int storedZ) const
    {
        // zFactor es siempre +1 o -1, asi que la division es exacta.
        return (storedZ - zOffset) / zFactor;
    }

    /*!
     * \brief Nivel de fondo ya acotado al rango real del dataset.
     *
     * Se usa siempre esto en vez de baseZoom a secas: un descriptor
     * construido a mano puede dejar baseZoom a 0 mientras minZoom vale 6, y
     * entonces se pediria un nivel que no existe.
     */
    int effectiveBaseZoom() const
    {
        return qBound(minZoom, baseZoom, maxZoom);
    }

    bool zoomInRange(int logicalZ) const
    {
        return logicalZ >= minZoom && logicalZ <= maxZoom;
    }

    //! Rango que deberia ofrecer la interfaz al usuario.
    bool zoomRecommended(int logicalZ) const
    {
        return logicalZ >= minZoom && logicalZ <= recommendedMaxZoom;
    }

    bool isValid() const
    {
        return !id.isEmpty() && !filePath.isEmpty()
               && (zFactor == 1 || zFactor == -1)
               && maxZoom >= minZoom
               && tileSize > 0;
    }

    QJsonObject toJson() const;
    static TileDataset fromJson(const QJsonObject &o);
};

} // namespace libmapa

#endif // LIBMAPA_TILES_TILEDATASET_H_
