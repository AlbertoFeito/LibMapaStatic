#include "vnuevopunto.h"
 #include <utility>
#include "ui_vnuevopunto.h"

VNuevoPunto::VNuevoPunto(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::VNuevoPunto)
{
    ui->setupUi(this);
    ui->cb_Simbolo->setEnabled(true);

    recalcularLatLongDecimal();
    ui->le_NombrePunto->setFocus();

    ui->tbSelecInMap->setVisible(false);
}

VNuevoPunto::~VNuevoPunto()
{
    delete ui;
}

void VNuevoPunto::setInvocadoDesdeMenuAcciones(bool desde)
{
    invocadoDesdeMenuAcciones = desde;

    ui->tbSelecInMap->setVisible(desde);
}

void VNuevoPunto::setNombrePunto(QString nombre)
{
    ePuntogeo->setNombre(std::move(nombre));
}

void VNuevoPunto::setDescripcion(QString descripcion)
{
    ePuntogeo->setDescripcion(std::move(descripcion));
}

void VNuevoPunto::setImagen(const QPixmap& imagen)
{
    ePuntogeo->setSimbolo(imagen);
}

void VNuevoPunto::setTipo(const QString& tipo)
{
    Q_UNUSED(tipo)
//    ePuntogeo->setTipo(tipo);
}

void VNuevoPunto::setPosGeo(GMS_Float lat, GMS_Float longi)
{
    Q_UNUSED(lat)
    Q_UNUSED(longi)

}

void VNuevoPunto::setFecha(QString fechaCreacion)
{
    ePuntogeo->setFechaCreacion(std::move(fechaCreacion));
}

void VNuevoPunto::ponerPosGeoInterface(const QGeoCoordinate& posGeoClickDerecho)
{
    GMS_Float lat = QUtiles::GradosToGmsFloat(posGeoClickDerecho.latitude());
    GMS_Float lon = QUtiles::GradosToGmsFloat(posGeoClickDerecho.longitude());

    ui->sb_LatGrad->setValue(lat.G);
    ui->sb_LatMin->setValue(lat.M);
    ui->dsb_LatSeg->setValue(lat.S);

    ui->sb_LonGrad->setValue(lon.G);
    ui->sb_LonMin->setValue(qAbs(lon.M));
    ui->dsb_LonSeg->setValue(qAbs(lon.S));

    recalcularLatLongDecimal();

}

void VNuevoPunto::limpiar()
{
    ui->le_NombrePunto->setText("");
    ui->pt_Descripcion->setPlainText("");
    ui->sb_LatGrad->setValue(23);
    ui->sb_LonGrad->setValue(-82);
    ui->sb_LatMin->setValue(0);
    ui->sb_LonMin->setValue(0);
    ui->dsb_LatSeg->setValue(0);
    ui->dsb_LonSeg->setValue(0);
    ui->cb_Simbolo->setCurrentIndex(0);
    ui->le_LatDec->text().clear();
    ui->le_LonDec->text().clear();
}

void VNuevoPunto::setListadoSimbolos(QList<E_Icons> list)
{
    //-- Llenando el combo box de Simbolos
    ui->cb_Simbolo->clear();
    for (auto & i : list)
    {
        ui->cb_Simbolo->addItem(i.icon,i.nombreIcon);
    }
    lTipoPunto = list;
}

void VNuevoPunto::setPosDesdeMapa(const QGeoCoordinate& posGeo)
{
    GMS_Float LatGMS = QUtiles::GradosToGmsFloat(posGeo.latitude());
    GMS_Float LonGMS = QUtiles::GradosToGmsFloat(-posGeo.longitude());

    ui->sb_LatGrad->setValue(LatGMS.G);
    ui->sb_LatMin->setValue(LatGMS.M);
    ui->dsb_LatSeg->setValue(LatGMS.S);

    ui->sb_LonGrad->setValue(-LonGMS.G);
    ui->sb_LonMin->setValue(LonGMS.M);
    ui->dsb_LonSeg->setValue(LonGMS.S);

    ui->le_LatDec->setText(QString::number(posGeo.latitude(),'f',6));
    ui->le_LonDec->setText(QString::number(posGeo.longitude(),'f',6));

    show();
}

void VNuevoPunto::recalcularLatLongDecimal()
{
    GMS_Float LatGMS;
    LatGMS.G = ui->sb_LatGrad->value();
    LatGMS.M = ui->sb_LatMin->value();
    LatGMS.S = ui->dsb_LatSeg->value();

    GMS_Float LonGMS;
    LonGMS.G = - ui->sb_LonGrad->value() ;
    LonGMS.M = ui->sb_LonMin->value();
    LonGMS.S = ui->dsb_LonSeg->value();


    double lat =  QUtiles::GmsFloatToGrados(LatGMS);
    double lon =  QUtiles::GmsFloatToGrados(LonGMS);

    ui->le_LatDec->setText(QString::number(lat,'f',6));
    ui->le_LonDec->setText(QString::number(-lon,'f',6));
}

void VNuevoPunto::on_tb_Aceptar_clicked()
{
    bool bNombre = ui->le_NombrePunto->text().isEmpty();
    bool bNombreERROR = false;

    //--> Asegurandonos de que el nombre no contenga caracteres especiales.
    for (int i = 0; i < ui->le_NombrePunto->text().size(); ++i)
    {
        QChar c = ui->le_NombrePunto->text().at(i);
        if (!c.isLetterOrNumber())
            if (!c.isSpace())
                bNombreERROR = true;
    }

    bool bSimbolo = ui->cb_Simbolo->count() == 0;

    if (bNombre || bSimbolo || bNombreERROR)
    {//----------------------------------------> Mostrando Advertencias.
        if (bNombre)
        {
            ui->le_NombrePunto->setFocus();
            ui->le_NombrePunto->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(255, 169, 169, 255), stop:0.454545 rgba(255, 191, 191, 255), stop:0.971591 rgba(255, 214, 214, 255), stop:1 rgba(0, 0, 0, 0));");
        }
        if (bSimbolo)
        {
            ui->cb_Simbolo->setFocus();
            ui->cb_Simbolo->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(255, 169, 169, 255), stop:0.454545 rgba(255, 191, 191, 255), stop:0.971591 rgba(255, 214, 214, 255), stop:1 rgba(0, 0, 0, 0));");
        }
        if (bNombreERROR)
        {
            ui->cb_Simbolo->setFocus();
            ui->le_NombrePunto->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(255, 180, 102, 255), stop:0.55 rgba(218, 235, 61, 255), stop:0.98 rgba(207, 149, 0, 255), stop:1 rgba(0, 0, 0, 0));");
        }
    }
    else
    {
        //Creando nuevo Punto
        ePuntogeo = new CPunto();
        ePuntogeo->setTipoPunto(PuntoInteres);
        ePuntogeo->setNombre(ui->le_NombrePunto->text());
        ePuntogeo->setDescripcion(ui->pt_Descripcion->toPlainText());
        ePuntogeo->setSimbolo(ui->cb_Simbolo->itemIcon(ui->cb_Simbolo->currentIndex()).pixmap(80,80));

        GMS_Float Lat;
        Lat.G = ui->sb_LatGrad->value();
        Lat.M = ui->sb_LatMin->value();
        Lat.S = ui->dsb_LatSeg->value();

        GMS_Float Long;
        Long.G = ui->sb_LonGrad->value();
        Long.M = ui->sb_LonMin->value();
        Long.S = ui->dsb_LonSeg->value();

        double latitud = QUtiles::GmsFloatToGrados(Lat);
        Long.G = qAbs(Long.G);
        double longitud = -(QUtiles::GmsFloatToGrados(Long));

        QGeoCoordinate geoPos;
        geoPos.setLatitude(latitud);
        geoPos.setLongitude(longitud);
        ePuntogeo->setGeoPos(geoPos);

        ePuntogeo->setFechaCreacion(QDateTime::currentDateTime().toString("dd-MM-yyyy | hh:mm:ss"));

        emit signal_nuevoPuntoAceptado(ePuntogeo);

        limpiar();
        close();
    }
}

void VNuevoPunto::on_tb_Cancelar_clicked()
{
    limpiar();
    emit signal_Cancelado();
    close();
}

void VNuevoPunto::on_le_NombrePunto_textChanged(const QString &arg1)
{
    ui->le_NombrePunto->setStyleSheet("");
    if (!arg1.isEmpty())
    {
        QChar c = arg1.at(arg1.size() - 1);
        if (!c.isLetterOrNumber()) //---> Comprobando que el caracter solo sea Numero o Letras
        {
            if (!c.isSpace())//----> el Espacio esta permitido
            {
                int pos = arg1.size() - 1;
                ui->le_NombrePunto->setText(ui->le_NombrePunto->text().remove(pos,1));
                ui->le_NombrePunto->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(255, 180, 102, 255), stop:0.55 rgba(218, 235, 61, 255), stop:0.98 rgba(207, 149, 0, 255), stop:1 rgba(0, 0, 0, 0));");
            }
            else
                ui->le_NombrePunto->setStyleSheet("");
        }
        else
        {
            ui->le_NombrePunto->setStyleSheet("");
        }
    }
}

void VNuevoPunto::on_sb_LatGrad_valueChanged(int arg1)
{
    Q_UNUSED(arg1)
    recalcularLatLongDecimal();
    ui->sb_LatGrad->setStyleSheet("");
}

void VNuevoPunto::on_sb_LonGrad_valueChanged(int arg1)
{
    Q_UNUSED(arg1)
    recalcularLatLongDecimal();
    ui->sb_LonGrad->setStyleSheet("");
}

void VNuevoPunto::on_sb_LatMin_valueChanged(int arg1)
{
    Q_UNUSED(arg1)
    recalcularLatLongDecimal();
}

void VNuevoPunto::on_dsb_LatSeg_valueChanged(double arg1)
{
    Q_UNUSED(arg1)
    recalcularLatLongDecimal();
}

void VNuevoPunto::on_sb_LonMin_valueChanged(int arg1)
{
    Q_UNUSED(arg1)
    recalcularLatLongDecimal();
}

void VNuevoPunto::on_dsb_LonSeg_valueChanged(double arg1)
{
    Q_UNUSED(arg1)
    recalcularLatLongDecimal();
}

void VNuevoPunto::on_cb_Simbolo_currentTextChanged(const QString &arg1)
{
    Q_UNUSED(arg1)
    ui->cb_Simbolo->setFocus();
    ui->cb_Simbolo->setStyleSheet("");
}

void VNuevoPunto::on_tbSelecInMap_clicked()
{
    emit signal_SeleccionDePuntoEnmapa();
    hide();
}
