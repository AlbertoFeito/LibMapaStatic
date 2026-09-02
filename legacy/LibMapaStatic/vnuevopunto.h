#ifndef VNUEVOPUNTO_H
#define VNUEVOPUNTO_H

#include <QMainWindow>
//#include <utiles.h>
#include "cpunto.h"

namespace Ui {
class VNuevoPunto;
}

class VNuevoPunto : public QMainWindow
{
    Q_OBJECT

public:
    explicit VNuevoPunto(QWidget *parent = 0);
    ~VNuevoPunto();

    void setInvocadoDesdeMenuAcciones(bool desde);
    void setNombrePunto(QString nombre);
    void setDescripcion(QString descripcion);
    void setImagen(const QPixmap& imagen);
    void setTipo(const QString& tipo);
    void setPosGeo(GMS_Float lat, GMS_Float longi);
    void setFecha(QString fechaCreacion);
    void setListadoSimbolos(QList<E_Icons> list);
    void setPosDesdeMapa(const QGeoCoordinate& posGeo);

    void ponerPosGeoInterface(const QGeoCoordinate& posGeoClickDerecho);
    void limpiar();
    void recalcularLatLongDecimal();

signals:
    void signal_nuevoPuntoAceptado(CPunto* eNuevoPunto);
    void signal_SeleccionDePuntoEnmapa();
    void signal_Cancelado(bool continuar = false);

private slots:
    void on_tb_Aceptar_clicked();

    void on_tb_Cancelar_clicked();

    void on_le_NombrePunto_textChanged(const QString &arg1);

    void on_sb_LatGrad_valueChanged(int arg1);

    void on_sb_LonGrad_valueChanged(int arg1);

    void on_sb_LatMin_valueChanged(int arg1);

    void on_dsb_LatSeg_valueChanged(double arg1);

    void on_sb_LonMin_valueChanged(int arg1);

    void on_dsb_LonSeg_valueChanged(double arg1);

    void on_cb_Simbolo_currentTextChanged(const QString &arg1);

    void on_tbSelecInMap_clicked();

private:
    Ui::VNuevoPunto *ui;

    CPunto* ePuntogeo{};
    QList<E_Icons> lTipoPunto;
    bool invocadoDesdeMenuAcciones{};
};

#endif // VNUEVOPUNTO_H
