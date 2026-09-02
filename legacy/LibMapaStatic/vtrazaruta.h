#ifndef VTRAZARUTA_H
#define VTRAZARUTA_H

#include <QDialog>
#include <qgeocoordinate.h>
#include "cgooglemercator.h"
#include "qcustomplot.h"
#include "utiles.h"

namespace Ui {
class vTrazaRuta;
}

class vTrazaRuta : public QDialog
{
    Q_OBJECT

public:
    vTrazaRuta(QWidget *parent = 0);
    vTrazaRuta(QList<EPuntoRuta> lPuntos, QList<E_PuntoItem> lTextoPuntos, QWidget *parent = 0);
    ~vTrazaRuta();

    void mostrarListadoruta();

    QList<QTableWidgetItem *> getListItem() const;

    void setLPuntosRuta(const QList<EPuntoRuta> &value);

    void setLTextoPuntosRuta(const QList<E_PuntoItem> &value);

private slots:
    void on_buttonBox_accepted();

signals:
    void devuelveListadoPuntosRuta(QList<EPuntoRuta> listPuntosRuta);

private:
    Ui::vTrazaRuta *ui;
    QList<EPuntoRuta> lPuntosRuta;
    QList<E_PuntoItem> lTextoPuntosRuta;
    CGoogleMercator projection;
    QList<QTableWidgetItem*> listItem;
};

#endif // VTRAZARUTA_H
