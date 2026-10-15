// Ventana principal de la demo. La clase es una sola, pero sus métodos están
// repartidos por tema en Ventana*.cpp (menús, panel, dibujo, elevación,
// ficheros y objetivos) para que cada fichero se pueda leer de una vez.
#pragma once

#include "Comun.h"

class QCustomPlot;

class Ventana : public QMainWindow
{
    Q_OBJECT

    //! Punto que se está capturando del mapa (pestaña Elevación).
    enum class Pick { Ninguno, A, B, Anillos, OrigenEbl, DestinoEbl };

public:
    explicit Ventana(const QString &origen, bool esPaquete,
                     const QString &demDir = QString(),
                     const QString &demDb = QString(),
                     const QString &featuresDb = QString());

private:
    // ==================================================== barras ==========
    QAction *accHerr(const QString &texto, MapTool tool,
                     const QString &atajo = QString(),
                     const QString &claveIcono = QString());

    void construirMenuYBarra();

    // ==================================================== temas ===========
    void aplicarTema(bool oscuro);

    static QPalette paletaClara();

    static QPalette paletaOscura();

    void aplicarIconos(const QColor &fg);

    static QString qssClaro();

    static QString qssOscuro();

    void limpiarAnalisis();

    void ocultarOverlaysAlIniciar();

    void reponerBorrador();

    void nuevaEntidadGenerica();

    void modoObjetivo();

    void modoAreaObjetivo();

    void volverANavegar();

    void configurarNuevaEntidad(const QString &tipo, FeatureStyle base,
                                int maxVert, const QString &nombreSug);

    bool dialogoNuevaEntidad(const QString &nombreSug, QString *nombre,
                             QString *capa, QColor *linea, QColor *relleno,
                             double *ancho);

    void alEntidadCreada(qint64 id);

    void mostrarCoordsArea(qint64 id);

    // ==================================================== panel ===========
    void construirPanel();

    static QGeoCoordinate centroideDe(const MapFeature &f);

    void centrarEnItem(QTreeWidgetItem *item);

    // ==================================================== senales =========
    void conectarSenales();

    void mostrarCota(const QGeoCoordinate &p);

    void alElegirDemCarpeta();

    void alElegirDemDb();

    // ============================================= analisis de elevacion ==
    QWidget *construirTabElevacion();

    void reejecutar();

    void mostrarPerfil(const ElevationProfile &p, const QString &resumen,
                       double maxInicial = -1.0, bool conVision = false,
                       double zA = 0.0, double zB = 0.0, double D = 0.0,
                       double critD = -1.0, bool bloqueado = false);

    void pintarPerfil(const ElevationProfile &p, bool conVision,
                      double zA, double zB, double D, double critD, bool bloqueado,
                      double maxInicialM);

    static double terrEn(const ElevationProfile &p, double d);

    void mastil(double d, double base, double cima);

    void marcaObstaculo(double d, double cota, const QColor &c = QColor(0xc6, 0x28, 0x28));

    bool exigirDem();

    // Punto A / B leídos de los campos lat/lon de la pestaña.
    QGeoCoordinate puntoA() const { return QGeoCoordinate(m_latA->value(), m_lonA->value()); }
    QGeoCoordinate puntoB() const { return QGeoCoordinate(m_latB->value(), m_lonB->value()); }

    static QPixmap pinPixmap(const QColor &c);

    static QPixmap picoIcon(const QColor &c, bool hl);

    static const QVector<QColor> &paletaPicos();

    void resaltarPico(qint64 id);

    static QString tooltipDe(const MapFeature &f);

    bool eventFilter(QObject *obj, QEvent *ev) override;

    void moverPinCoord(Pick cual, const QGeoCoordinate &g);

    void iniciarPick(Pick cual);

    void onPointPicked(const QGeoCoordinate &p);

    void actualizarInfoPuntos();

    void actualizarEntradaAnalisis(int idx);

    void marcarPuntosActivos();

    void prepararCapa(const QString &id, const QString &nombre, int z);

    ElevationProfileParams paramsPerfil() const;
    double voidElev() const;
    bool curvaturaOn() const { return !m_curvatura || m_curvatura->isChecked(); }

    void analizarPerfil();

    void analizarVision();

    void analizarViewshed();

    void analizarPicosAltos();

    // ==================================================== arbol ===========
    void pedirReconstruir() { m_reconstruir->start(); }

    void reconstruirPanel();

    QTreeWidgetItem *nuevoGrupo(const QString &titulo);

    // Item marcado en el arbol, o nullptr.
    QTreeWidgetItem *itemActual() const { return m_arbol->currentItem(); }

    void seleccionEnArbol(QTreeWidgetItem *item);

    void seleccionEnMapa(qint64 id);

    void visibilidadCambiada(QTreeWidgetItem *item, int col);

    void sincronizarBaseEnArbol(const QString &id);

    void resaltarCapaActiva();

    // ==================================================== borrado =========
    void borrarSeleccion();

    void vaciarCapa();

    // ==================================================== capas ===========
    void crearCapa();

    void borrarCapa();

    void moverCapa(int direccion);

    // =============================================== propiedades / estilo ==
    FeatureStyle estiloActual() const;

    void aplicarEstiloAlTrazo() { m_mapa->setDraftStyle(estiloActual()); }

    void aplicarEstilo();

    void aplicarPropiedades();

    void mostrarPropiedades(qint64 id);

    bool capaEditable(const QString &layerId) const;

    void mostrarCoords(qint64 id);

    static QTableWidgetItem *celdaCoord(double v);

    void coordEditada(int fila, int /*col*/);

    void anadirVertice();

    void quitarVertice();

    void pintarBotonColor(QPushButton *boton, const QColor &c);

    void elegirColor(QColor *destino, QPushButton *boton);

    // =============================================== persistencia =========
    void guardar();

    void guardarComo();

    void abrir();

    QString dirTrabajo() const;

    void actualizarTitulo();

    void marcarSucio();

    bool confirmarDescartar();

    void closeEvent(QCloseEvent *e) override;

    // ============================================ datos / simulacion ======
    void cargarGeo();

    void irACoordenada();

    void importarVectorial();

    void exportarVectorial();

    void prepararSeguimiento();

    void zonaSeleccionada(const QVector<QGeoCoordinate> &zona, bool rectangulo);

    void exportarVista(const QString &tituloSugerido = QString());

    void abrirReproduccion();

    void crearBarraReproduccion();

    void salirReproduccion();

    void exportarHistorialGpx();

    void configurarProximidad(bool activar);

    QString textoProximidad(qint64 a, qint64 b, double cpa, double tcpa) const;

    void ponerAnillos();

    void ponerMarcacion();
    void gestionarMarcas();
    QString textoDistancia(double metros) const;

    void iniciarPickMedida(Pick cual, const QString &aviso);

    qint64 objetivoCerca(const QGeoCoordinate &p) const;

    void puntoDeMedida(const QGeoCoordinate &p);

    void alternarVigilancia();

    QString textoAlertaZona(qint64 quienId, bool esEntidad, qint64 zona, bool entra) const;

    void actualizarIndicadorAlertas();

    void vigilarAreasObjetivo();

    void alternarSimulacion(bool on);

    void iniciarSimulacion(int n);

    void pasoSimulacion();

    // ==================================================== estado ==========
    void actualizarEstado();

    void filtrarObjetivos();

    // ==================================================== miembros ========
    MapWidget *m_mapa = nullptr;
    QTimer *m_reconstruir = nullptr;

    QComboBox *m_capasBase = nullptr;
    QActionGroup *m_grupo = nullptr;
    QAction *m_accNavegar = nullptr;
    QAction *m_accDeshacer = nullptr;
    QAction *m_accRehacer = nullptr;
    QAction *m_accBorrar = nullptr;
    QAction *m_accMedir = nullptr;
    QAction *m_accMedirArea = nullptr;      //!< medir superficie y perímetro
    TargetFilter m_filtro;                  //!< último filtro de objetivos aplicado
    QLabel *m_filtroLbl = nullptr;          //!< indicador "Filtro: N de M"
    QAction *m_accZoomArea = nullptr;
    QAction *m_accPunto = nullptr;
    QAction *m_accLinea = nullptr;
    QAction *m_accPoligono = nullptr;
    QAction *m_accObjetivo = nullptr;
    QAction *m_accArea = nullptr;
    QAction *m_accEditar = nullptr;
    QAction *m_accGuardar = nullptr;
    QAction *m_accGuardarComo = nullptr;
    QAction *m_accAbrir = nullptr;
    QAction *m_accRejilla = nullptr;
    QAction *m_accCobertura = nullptr;
    QAction *m_accEscala = nullptr;        //!< escala gráfica
    QAction *m_accEscalaNm = nullptr;      //!< escala en millas náuticas
    QAction *m_accCuadricula = nullptr;    //!< cuadrícula de coordenadas
    CoordinateFormat m_formatoCoord = CoordinateFormat::Decimal;   //!< coordenada bajo el cursor
    QMenu *m_menuVer = nullptr;
    QAction *m_accTemaClaro = nullptr;
    QAction *m_accTemaOscuro = nullptr;
    bool m_temaOscuro = false;

    QDockWidget *m_dock = nullptr;   //!< panel lateral «Capas y entidades»
    QWidget *m_ficha = nullptr;      //!< ficha contextual (oculta sin selección)
    QLabel *m_lblFicha = nullptr;    //!< título de la ficha (nombre · tipo)
    QLabel *m_lblCapaEntidad = nullptr; //!< capa de la entidad (solo lectura)
    QTreeWidget *m_arbol = nullptr;
    QLineEdit *m_campoNombre = nullptr;
    QLineEdit *m_campoTipo = nullptr;
    QLineEdit *m_campoDescripcion = nullptr;
    QTableWidget *m_tablaAtributos = nullptr;
    QTableWidget *m_tablaCoords = nullptr;   //!< lat/lon editables de la entidad
    QPushButton *m_btnAddVert = nullptr;
    QPushButton *m_btnDelVert = nullptr;
    QPushButton *m_btnColorLinea = nullptr;
    QPushButton *m_btnColorRelleno = nullptr;
    QDoubleSpinBox *m_anchoLinea = nullptr;
    QDoubleSpinBox *m_radioPunto = nullptr;
    QComboBox *m_estiloLinea = nullptr;
    QCheckBox *m_verEtiqueta = nullptr;
    QCheckBox *m_verVertices = nullptr;
    QCheckBox *m_aplicarAlTrazo = nullptr;

    QLabel *m_coords = nullptr;
    QLabel *m_cota = nullptr;
    QLabel *m_info = nullptr;
    QSpinBox *m_zCobertura = nullptr;
    QPushButton *m_btnDem = nullptr;
    bool m_demActivo = false;

    // Analisis de elevacion (pestaña "Elevacion" del panel lateral).
    QTabWidget *m_tabs = nullptr;
    QDoubleSpinBox *m_latA = nullptr;       //!< punto A: latitud (perfil/visión/zvd)
    QDoubleSpinBox *m_lonA = nullptr;       //!< punto A: longitud
    QDoubleSpinBox *m_latB = nullptr;       //!< punto B: latitud (visión)
    QDoubleSpinBox *m_lonB = nullptr;       //!< punto B: longitud
    QDoubleSpinBox *m_rumbo = nullptr;      //!< rumbo (azimut) del perfil radial
    QDoubleSpinBox *m_altA = nullptr;       //!< antena A / altura del observador
    QDoubleSpinBox *m_altB = nullptr;       //!< antena B / altura del objetivo
    QDoubleSpinBox *m_alcanceKm = nullptr;  //!< alcance del viewshed / perfil
    Pick m_picking = Pick::Ninguno;
    Pick m_dragPick = Pick::Ninguno;        //!< pin A/B que se está arrastrando
    QObject *m_plotObj = nullptr;           //!< la MapView, objetivo del eventFilter
    QLabel *m_infoPuntos = nullptr;         //!< cota A/B, rumbo y distancia A→B
    QCheckBox *m_marComo0 = nullptr;        //!< tratar mar/sin dato como 0 m
    QCheckBox *m_curvatura = nullptr;       //!< aplicar curvatura 4/3
    QLabel *m_resultado = nullptr;          //!< lectura del ultimo analisis
    QDialog *m_perfilWin = nullptr;         //!< ventana flotante del perfil
    QLabel *m_perfilInfo = nullptr;
    QCustomPlot *m_plot = nullptr;          //!< gráfica del perfil (QCustomPlot)
    enum class Analisis { Ninguno, Perfil, Vision, Viewshed, Picos };
    Analisis m_ultimo = Analisis::Ninguno;  //!< para re-aplicar al cambiar un check
    const QString kCapaVision = QStringLiteral("elev_vision");
    const QString kCapaViewshed = QStringLiteral("elev_viewshed");
    const QString kCapaPuntos = QStringLiteral("elev_puntos");   //!< pines fijos A/B
    const QString kCapaPicos = QStringLiteral("elev_picos");     //!< 10 puntos más altos
    const QColor m_colorA = QColor(0x15, 0x65, 0xc0);            //!< azul = punto A
    const QColor m_colorB = QColor(0xc6, 0x28, 0x28);            //!< rojo = punto B
    QDoubleSpinBox *m_picosRadioKm = nullptr;  //!< radio de búsqueda de picos
    QSpinBox *m_picosSepM = nullptr;           //!< separación mínima entre cumbres
    // Entrada por análisis: un selector y filas que se muestran/ocultan según el tipo.
    QComboBox *m_tipoAnalisis = nullptr;
    QWidget *m_rowPosA = nullptr, *m_rowPosB = nullptr, *m_rowAzimut = nullptr;
    QWidget *m_rowDist = nullptr, *m_rowAnt1 = nullptr, *m_rowAnt2 = nullptr;
    QWidget *m_rowRadio = nullptr, *m_rowSep = nullptr, *m_rowChecks = nullptr;
    QLabel *m_lblPosA = nullptr, *m_lblDist = nullptr, *m_lblAnt1 = nullptr, *m_lblAnt2 = nullptr;
    QVector<qint64> m_picoIds;                 //!< ids de los marcadores de pico
    QHash<qint64, MapFeature> m_picoBase;      //!< estilo base (para restaurar tras hover)
    qint64 m_picoHover = -1;                   //!< pico resaltado bajo el cursor

    bool m_actualizandoPropiedades = false;
    QColor m_colorLinea = QColor(0xd3, 0x2f, 0x2f);
    QColor m_colorRelleno = QColor(0xd3, 0x2f, 0x2f, 70);
    QString m_archivoActual;
    bool m_sucio = false;        //!< hay entidades sin guardar (guardado explícito)
    QString m_ultimoDir;

    // Alta de entidad en curso (diálogo → dibuja → Navegar): nombre a aplicar a
    // la siguiente entidad dibujada y si hay un alta armada.
    QString m_nombrePendiente;
    bool m_esperandoDibujo = false;

    // Simulacion de objetivos moviles.
    QSpinBox *m_numObjetivos = nullptr;
    QComboBox *m_traza = nullptr;
    QAction *m_accSimular = nullptr;
    QAction *m_accVerObjetivos = nullptr;   //!< mostrar/ocultar objetivos
    QAction *m_accVigilarZona = nullptr;    //!< alertas de zona del polígono seleccionado
    QAction *m_accSelArea = nullptr;        //!< seleccionar área (bbox)
    QAction *m_accSelPoli = nullptr;        //!< seleccionar polígono (zona)
    int m_zonasCreadas = 0;                 //!< para nombrar «Zona N» desde una selección
    QAction *m_accProximidad = nullptr;     //!< alertas CPA/TCPA entre objetivos
    double m_cpaMillas = 1.0;               //!< distancia de la alerta CPA (NM)
    int m_cpaMinutos = 10;                  //!< horizonte de la alerta CPA (min)
    RangeRings m_anillos = [] { RangeRings r; r.spacingMeters = 0.0; return r; }();   //!< últimos ajustes (automáticos)
    int m_anillosUnidad = 2;                //!< unidad de la separación fija (0 m, 1 km, 2 NM)
    BearingLine m_ebl;                      //!< marcación en curso (origen ya fijado)
    TrackPlayer *m_player = nullptr;        //!< reproducción de historiales (al abrir uno)
    QToolBar *m_barraRep = nullptr;         //!< barra de reproducción (abajo)
    QSlider *m_repSlider = nullptr;         //!< posición en el tiempo
    QLabel *m_repHora = nullptr;            //!< hora de la reproducción
    QCheckBox *m_repRutas = nullptr;        //!< mostrar rutas completas
    QAction *m_accSalirRep = nullptr;       //!< salir del modo reproducción (Esc)
    QPushButton *m_modoRep = nullptr;       //!< indicador "▶ Reproducción" (clic = salir)
    QTimer *m_simReloj = nullptr;
    QVector<qint64> m_simIds;
    QVector<QPointF> m_simVel;      //!< x = dLon, y = dLat por paso.
    QDateTime m_simHora;            //!< Reloj simulado (avanza kSegundosPorPaso por paso).
    static constexpr int kSegundosPorPaso = 30;
    // Juego de iconos de la app para la simbologia de objetivos (Fase 2).
    QPixmap m_icoBuque, m_icoAereo, m_icoUav, m_icoUavBajo;
    QPixmap m_icoBuqueZona, m_icoAereoZona, m_icoUavZona;   //!< dentro de zona vigilada
    QLabel *m_alertas = nullptr;            //!< indicador fijo «⚠ N en zonas»
    QSet<qint64> m_noVigilar;               //!< áreas objetivo que el usuario desactivó
};
