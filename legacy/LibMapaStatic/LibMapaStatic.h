#ifndef LIBMAPASTATIC_H
#define LIBMAPASTATIC_H

#include <QWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTimer>
#include <QResizeEvent>
#include <QGeoCoordinate>
#include "cmapaplot.h"
#include "cpunto.h"

class LibMapaStatic : public QWidget
{
    Q_OBJECT

public:
    explicit LibMapaStatic(const QString& path, QWidget *parent = nullptr);
    ~LibMapaStatic();

    // Getters
    CMapaPlot *getOMapa() const;
    QString getDirPath() const;
    QToolButton *getTb_zMas() const;
    QToolButton *getTb_zMenos() const;
    QToolButton *getTb_trazaRuta() const;
    QToolButton *getTb_zoomArea() const;
    QToolButton *getTb_nuevoPunto() const;
    QToolButton *getTb_nuevoPoligono() const;
    QToolButton *getTb_medirDistancia() const;

    // Setters
    void setOMapa(CMapaPlot *newOMapa);
    void setPathFicheros(const QString &path);
    void setTb_zMenos(QToolButton *tb_zMenos);
    void setTb_trazaRuta(QToolButton *tb_trazaRuta);
    void setTb_zoomArea(QToolButton *tb_zoomArea);
    void setTb_nuevoPunto(QToolButton *tb_nuevoPunto);
    void setTb_nuevoPoligono(QToolButton *tb_nuevoPoligono);
    void setTb_medirDistancia(QToolButton *tb_medirDistancia);

    void inicializarComponentesVisuales();

public slots:
    void seleccionarUbicacionEnMapa();
    void mostrarMedicionDistancia(const QString &azimut, const QString &distancia);
    void mostrarPosActualMouse(const QGeoCoordinate &geoPosPunto);
    void mostrarOcultarControlesGraficos(bool visible);
    void mostrarCapaImgSatSeleccionada(bool capa);
    void mostrarCapaImgOSMSeleccionada(bool capa);
    void mostrarCapaCubaVectSeleccionada(bool capa);
    void mostrarConfiguracion(bool abrir);
    void devuelveWidgetClickDerechoParaDisenoExterno(const QGeoCoordinate &posGeoMouse);
    void terminoZoomArea();
    void accionarMedirDistancia();
    void chequearEstadoMedicionDistancia();

signals:
    void sig_devuelveWidgetClickDerechoParaDisenoExterno(QWidget* menuClickDerecho, QGeoCoordinate posGeoMouse);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void mostrarCapaSeleccionada(int capa);
    void limpiarRecursos();
    bool verificarRecursosExisten(const QString& recursoPath);

    // Miembros principales
    CMapaPlot* oMapa;
    QString DirPath;

    // Componentes gráficos
    QVBoxLayout *gridla;
    QHBoxLayout* m_hLayoutMapa;

    // Botones de herramientas
    QToolButton *m_tb_medirDistancia;
    QToolButton *m_tb_nuevoPoligono;
    QToolButton *m_tb_nuevoPunto;
    QToolButton *m_tb_zoomArea;
    QToolButton *m_tb_trazaRuta;
    QToolButton *m_tb_zMas;
    QToolButton *m_tb_zMenos;

    // Widgets de interfaz
    QWidget* menuClickDerecho;

    // Barra de estado
    QLabel *m_l_azimut;
    QLabel *m_l_distancia;
    QLabel *m_l_geoPosMouse;
    QLineEdit *m_le_azimut;
    QLineEdit *m_le_distancia;
    QLineEdit *m_le_geoPosMouse;

    // Otros controles
    QLabel* m_l_cantPuntos;
    QSpinBox* m_sb_cantPuntos;

    // Timer
    QTimer *tim;

    // Estado
    bool b_seCambioCapa;
    void crearBotonesHerramientas();
    QToolButton *crearBotonHerramienta(const QString &toolTip, const QString &iconPath, const QPoint &position, const QString &text);
    void crearBarraEstado();
    void aplicarEstiloLabelBarraEstado(QLabel *label);
    void aplicarEstiloLineEditBarraEstado(QLineEdit *lineEdit);
    void crearBotonesZoom();
    void configurarConexiones();
    void aplicarEstiloCapaSatelital();
    void actualizarElementosVisualesCapaSatelital();
    void aplicarEstiloCapaOSM();
    void actualizarElementosVisualesCapaOSM();
};

#endif // LIBMAPASTATIC_H
