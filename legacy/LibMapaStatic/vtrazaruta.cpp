#include "vtrazaruta.h"
 #include <utility>
#include "ui_vtrazaruta.h"

vTrazaRuta::vTrazaRuta(QWidget * /*parent*/): ui(new Ui::vTrazaRuta)
{
    ui->setupUi(this);
    ui->tabla->setColumnCount(3);
    this->setWindowTitle("Confirmación de Ruta");
}

vTrazaRuta::vTrazaRuta(QList<EPuntoRuta> lPuntos, QList<E_PuntoItem> lTextoPuntos, QWidget *parent) : vTrazaRuta(parent)
{
    lPuntosRuta = std::move(lPuntos);
    lTextoPuntosRuta = std::move(lTextoPuntos);

    setGeometry(0,0,this->width(),this->height());
    setModal(true);
    show();
    move(parent->width() - width(),10);
    mostrarListadoruta();
}

vTrazaRuta::~vTrazaRuta()
{
    lPuntosRuta.clear();

    listItem.clear();
    lTextoPuntosRuta.clear();

    delete ui;
}

void vTrazaRuta::mostrarListadoruta()
{
    ui->tabla->setRowCount(lPuntosRuta.size());

    listItem.clear();

    for (int i = 0; i < lPuntosRuta.size(); ++i)
    {
        QGeoCoordinate pos = lPuntosRuta.at(i).pos;
        QTableWidgetItem* ItemPrioridad = new QTableWidgetItem(lTextoPuntosRuta.at(i).text->text());
        ItemPrioridad->setTextAlignment(4);
        QTableWidgetItem* ItemPosPuntoRuta = new QTableWidgetItem(pos.toString(QGeoCoordinate::DegreesMinutesSecondsWithHemisphere));
        ItemPosPuntoRuta->setTextAlignment(Qt::AlignCenter);
        QTableWidgetItem* ItemDescripcion = new QTableWidgetItem("Sin descripción");
        ItemDescripcion->setToolTip("Inserte una descripción que defina este Punto");
        ItemDescripcion->setTextAlignment(Qt::AlignCenter);

        ui->tabla->setItem(i,0,ItemPrioridad);
        ui->tabla->setItem(i,1,ItemDescripcion);
        ui->tabla->setItem(i,2,ItemPosPuntoRuta);

        listItem.append(ItemPrioridad);
        listItem.append(ItemPosPuntoRuta);
        listItem.append(ItemDescripcion);
    }
}

void vTrazaRuta::on_buttonBox_accepted()
{
    QList<EPuntoRuta> listERuta;
    EPuntoRuta eRuta;

    for (int i = 0; i < ui->tabla->rowCount(); ++i)
    {
        eRuta.prioridad = ui->tabla->item(i,0)->text().toInt();
        eRuta.descripcion = ui->tabla->item(i,1)->text();
        GMS_Float lat; GMS_Float lon;
        QUtiles::SexagesFromQGeoCoordinate_ToGms_Float(ui->tabla->item(i,2)->text(),lat,lon);
        double lati = QUtiles::GmsFloatToGrados(lat);
        double longi = QUtiles::GmsFloatToGrados(lon);
        eRuta.pos.setLatitude(lati);
        eRuta.pos.setLongitude(longi);

        listERuta.append(eRuta);
    }

    emit devuelveListadoPuntosRuta(listERuta);
    listERuta.clear();
    lPuntosRuta.clear();
    close();
}

void vTrazaRuta::setLTextoPuntosRuta(const QList<E_PuntoItem> &value)
{
    lTextoPuntosRuta = value;
}

void vTrazaRuta::setLPuntosRuta(const QList<EPuntoRuta> &value)
{
    lPuntosRuta = value;
}

QList<QTableWidgetItem *> vTrazaRuta::getListItem() const
{
    return listItem;
}
