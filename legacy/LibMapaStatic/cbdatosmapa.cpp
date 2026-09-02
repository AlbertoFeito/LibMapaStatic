#include "cbdatosmapa.h"
#include "QVariant"
#include <utility>
#include "QDir"

CBDatos::CBDatos(QString path)
{
    if(!QSqlDatabase::isDriverAvailable ("QSQLITE"))
    {

    }

    NoUltimoPunto = 0;
    NoUltimoPoligono = 0;
    DirPath = std::move(path);
}

void CBDatos::Poner_Tipo_de_BD(int tipo)
{
    Tipo_BD = tipo;
}

QString CBDatos::Dice_Tipo_BD_Cargada()
{
    switch (Tipo_BD)
    {
    case 0:return "Imagenes Satelitales";
    case 1:return "Imagenes Open Street Map";
    case 2:return "Configuracion";
    }
    return "";
}

bool CBDatos::abrirBD()
{
    if (!bd_Sat.isOpen())
    {
        if (!bd_Sat.isValid())
        {
            bd_Sat = QSqlDatabase::addDatabase("QSQLITE","Mapas");//tipo de base dato
            bd_Sat.setDatabaseName(DirPath);//nombre de la base datos
        }

        if (bd_Sat.isOpen())
            return true;

        if (bd_Sat.open())
            return true;

        qDebug() << bd_Sat.lastError().text();
        return false;
    }
    return false;
}

QList<E_Dat_BD> CBDatos::Cargar_BD_IMG(quint8 Zoom_Level, bool Tip_BD,int xMin,int xMax,int yMin,int yMax)
{
    Tipo_BD = Tip_BD;
    bool ok = false;
    if (Tip_BD == 0) //-------------------------> BD Imagenes Satelitales
    {
        QString dirBD = DirPath + "/Recursos/Cuba_Satelital_CID3.sqlitedb";
        qDebug() << dirBD;
        if (!bd_Sat.isValid())
        {
            bd_Sat = QSqlDatabase::addDatabase("QSQLITE","Mapas");//tipo de base dato
            bd_Sat.setDatabaseName(dirBD);//nombre de la base datos
        }

        int Zoom = 18 - Zoom_Level;
        ok = bd_Sat.isOpen();
        if(!bd_Sat.isOpen()) //----------------------------------------------------------> Si la BD ya esta abierta...
            ok = bd_Sat.open();
        if(ok)
        {
            QString Consul = "SELECT * FROM tiles WHERE z = " + QString::number(Zoom) +
                    " AND x <= " + QString::number(xMax) +
                    " AND x >= " + QString::number(xMin) +
                    " AND y <= " + QString::number(yMax) +
                    " AND y >= " + QString::number(yMin);

            QSqlQuery consulta(bd_Sat);
            consulta.prepare(Consul);
            consulta.exec(Consul);
            ListDatBD.clear();
            while (consulta.next())
            {
                e_DatBD.zoom_level = consulta.value(1).toUInt();
                e_DatBD.x = consulta.value(2).toInt();
                e_DatBD.y = consulta.value(3).toInt();
                e_DatBD.foto = consulta.value(5).toByteArray();
                ListDatBD.append(e_DatBD);
            }
            bd_Sat.close();
            QSqlDatabase::removeDatabase("QSQLITE"); // correct
        }
    }
    else if (Tip_BD == 1)           //-------------------------> BD Imagenes OSM
    {
        QString dirBD = DirPath + "/Recursos/Cuba_OSM_CID3.sqlitedb";
        if (!bd_Osm.isValid())
        {
            bd_Osm = QSqlDatabase::addDatabase("QSQLITE","Mapas");//tipo de base dato
            bd_Osm.setDatabaseName(dirBD);//nombre de la base datos
        }

        if(!bd_Osm.isOpen()) //------------------> Si la BD ya esta abierta...
            ok = bd_Osm.open();
        if(ok)
        {
//            QString Consul0 = "SELECT map.*, images.* FROM map INNER JOIN images ON map.tile_id = images.tile_id WHERE map.zoom_level = " + QString::number(Zoom_Level);
            QString ConsulOsm = "SELECT * FROM tiles WHERE z = " + QString::number(Zoom_Level) +
                    " AND x >= " + QString::number(xMin) +
                    " AND x <= " + QString::number(xMax) +
                    " AND y >= " + QString::number(yMin) +
                    " AND y <= " + QString::number(yMax);

            QSqlQuery consulta(bd_Osm);
            consulta.prepare(ConsulOsm);
            consulta.exec(ConsulOsm);
            ListDatBD.clear();
            while (consulta.next())
            {
                e_DatBD.zoom_level = consulta.value(1).toUInt();
                e_DatBD.x = consulta.value(2).toInt();
                e_DatBD.y = consulta.value(3).toInt();
                e_DatBD.foto = consulta.value(5).toByteArray();
                ListDatBD.append(e_DatBD);
            }
            QSqlDatabase::removeDatabase("QSQLITE"); // correct
            bd_Osm.close();
        }
    }

    return ListDatBD;
}

QList<E_Dat_BD_OTROS> CBDatos::Cargar_BD_OTROS(const QString &capa, double xMin, double xMax, double yMin, double yMax)
{
    Q_UNUSED(xMin)
    Q_UNUSED(xMax)
    Q_UNUSED(yMin)
    Q_UNUSED(yMax)
    QList<E_Dat_BD_OTROS> lData;

//    qDebug() << "Inicio" << capa << QTime::currentTime();

    bd_Otros = QSqlDatabase::addDatabase("QSQLITE","BD_1_2_3_5");

    bool ok = false;
    if(capa.compare("sMapaRelieve")==0)
        bd_Otros.setDatabaseName(QDir::currentPath() + "/Recursos/Unificado Relieve.sqlitedb");
    else if(capa.compare("sMapaRelieve")==0)
        bd_Otros.setDatabaseName(QDir::currentPath() + "/Recursos/Unificado Vegetacion.sqlitedb");
    else if(capa.compare("sMapaHidro")==0)
        bd_Otros.setDatabaseName(QDir::currentPath() + "/Recursos/Unificado Hidrografia.sqlitedb");
    else if(capa.compare("sMapaViales")==0)
        bd_Otros.setDatabaseName(QDir::currentPath() + "/Recursos/Unificado Vial.sqlitedb");

    if(!bd_Otros.isOpen()) //------------------> Si la BD ya esta abierta...
        ok = bd_Otros.open();
    if(ok)
    {
        QString Consul = "SELECT * FROM tiles ";/*WHERE slat <= " + QString::number(xMax) +
                                                         " AND slat >= " + QString::number(xMin) +
                                                         " AND slong <= " + QString::number(yMax) +
                                                         " AND slong >= " + QString::number(yMin);;*/

        QSqlQuery consulta(bd_Otros);
        consulta.prepare(Consul);
        consulta.exec(Consul);
        while (consulta.next())
        {
            E_Dat_BD_OTROS eDatOtros;
            eDatOtros.lon = consulta.value(0).toDouble();
            eDatOtros.lat = consulta.value(1).toDouble();
            eDatOtros.alt = consulta.value(2).toString();

            lData.append(eDatOtros);
        }
    }

    bd_Otros.close();
    QSqlDatabase::removeDatabase("BD_1_2_3_5"); // correct
//    qDebug() << "Fin" << capa << QTime::currentTime();

    return lData;
}

void CBDatos::guardarConfigIni(const QStringList &listParametros)
{
    Q_UNUSED(listParametros)
}

void CBDatos::guardarNuevoPuntoVehiculo(void *newPunto)
{
    bool ok = false;
    if (!bd_vehiculos.isValid())
    {
        bd_vehiculos = QSqlDatabase::addDatabase("QSQLITE","vehiculos");//tipo de base dato
        bd_vehiculos.setDatabaseName(DirPath + "/Recursos/puntosBD.sig");//nombre de la base datos
    }

    ok = bd_vehiculos.open();
    if(ok)
    {
        //---- > creando tabla de Puntos Geo si no existe
        QSqlQuery Consulta(bd_vehiculos);
        QString NombTabl_Princ = "buques";
        QString Crea = "CREATE TABLE IF NOT EXISTS " + NombTabl_Princ + " (mmsi TEXT NOT nullptr, callsign TEXT, imo TEXT, nombre TEXT, bandera TEXT, tipoEmbarcacion TEXT, curso DOUBLE, velocidad DOUBLE, heading DOUBLE, destino TEXT, codePuertoDestino TEXT, calado DOUBLE, manga INTEGER, eslora INTEGER, estadoDatos TEXT, estatusNavegacion TEXT, latitud DOUBLE, longitud DOUBLE, ultimaActualizacion TEXT)";
        if (Consulta.prepare(Crea))
            Consulta.exec();

//        qDebug() << Consulta.lastError();
//        //---- > creando tabla de Trayectorias de Puntos Geo si no existe
//        NombTabl_Princ = "trayectorias_" + ((CPunto*)newPunto)->getNombre();
//        NombTabl_Princ.remove(' ');
//        Crea = "CREATE TABLE IF NOT EXISTS " + NombTabl_Princ + " (latitud DOUBLE, longitud DOUBLE, fecha_creacion TEXT)";
//        if (Consulta.prepare(Crea))
//            Consulta.exec();

//        qDebug() << Consulta.lastError();

//        //---- > consultando ultimo punto existente
//        NombTabl_Princ = "puntos";
//        QSqlQuery BuscarUltimoPunto(bd_Puntos);
//        QString Busqueda = "SELECT no_punto FROM " + NombTabl_Princ;
//        BuscarUltimoPunto.prepare(Busqueda);
//        if (BuscarUltimoPunto.exec())
//        {
//            while (BuscarUltimoPunto.next())
//            {
//                NoUltimoPunto = BuscarUltimoPunto.value(0).toInt();
//            }
//        }

//        if (NoUltimoPoligono < 0) NoUltimoPoligono = 0;

//        emit signal_devuelveUltimoNoPunto(NoUltimoPunto);

        //---- > adicionando nuevo punto
//        NombTabl_Princ = "puntos";
        QSqlQuery Consulta2(bd_vehiculos);

        QString estadoDatos =(((CBarco*)newPunto)->getEstadoDatos() == Actualizado) ? "Actualizado" : "SinActualizacion";
        QString Escribe = QString("INSERT INTO " + NombTabl_Princ + " (mmsi,callsign,imo,nombre,bandera,tipoEmbarcacion,curso,velocidad,heading,destino,codePuertoDestino,calado,manga,eslora,estadoDatos,estatusNavegacion,latitud,longitud,ultimaActualizacion)"
                       " VALUES (%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16,%17,%18,%19)")
                .arg("'"+((CBarco*)newPunto)->MMSI()+"'")
                .arg("'"+((CBarco*)newPunto)->SignalCall()+"'")
                .arg("'"+((CBarco*)newPunto)->IMONumber()+"'")
                .arg("'"+((CBarco*)newPunto)->nombreBuque()+"'")
                .arg("'"+((CBarco*)newPunto)->paisProcedencia()+"'")
                .arg("'"+((CBarco*)newPunto)->TipoEmbarcacion()+"'")
                .arg(((CBarco*)newPunto)->cursoSobreTierra())
                .arg(((CBarco*)newPunto)->getVelocidad())
                .arg(((CBarco*)newPunto)->heading())
                .arg("'"+((CBarco*)newPunto)->destino()+"'")
                .arg("'"+((CBarco*)newPunto)->nextPortLOCODE()+"'")
                .arg(((CBarco*)newPunto)->draught())
                .arg(((CBarco*)newPunto)->AnchoEmbarcacion())
                .arg(((CBarco*)newPunto)->LargoEmbarcacion())
                .arg("'"+ estadoDatos +"'")
                .arg("'"+((CBarco*)newPunto)->estadoNavegacion()+"'")
                .arg(((CBarco*)newPunto)->getGeoPos().latitude())
                .arg(((CBarco*)newPunto)->getGeoPos().longitude())
                .arg("'"+QDateTime::fromMSecsSinceEpoch(((CBarco*)newPunto)->getListTrayect().last().time).toString("dd/MM/yyyy hh:mm:ss")+"'");

        Consulta2.prepare(Escribe);
//        Consulta2.bindValue(":mmsi","'"+((CBarco*)newPunto)->MMSI()+"'");
//        Consulta2.bindValue(":callsign",((CBarco*)newPunto)->SignalCall());
//        Consulta2.bindValue(":imo",((CBarco*)newPunto)->IMONumber());
//        Consulta2.bindValue(":nombre",((CBarco*)newPunto)->nombreBuque());
//        Consulta2.bindValue(":bandera",((CBarco*)newPunto)->bandera());
//        Consulta2.bindValue(":tipoEmbarcacion",((CBarco*)newPunto)->TipoEmbarcacion());
//        Consulta2.bindValue(":curso",((CBarco*)newPunto)->cursoSobreTierra());
//        Consulta2.bindValue(":velocidad",((CBarco*)newPunto)->getVelocidad());
//        Consulta2.bindValue(":heading",((CBarco*)newPunto)->heading());
//        Consulta2.bindValue(":destino",((CBarco*)newPunto)->destino());
//        Consulta2.bindValue(":codePuertoDestino",((CBarco*)newPunto)->nextPortLOCODE());
//        Consulta2.bindValue(":calado",((CBarco*)newPunto)->draught());
//        Consulta2.bindValue(":manga",((CBarco*)newPunto)->AnchoEmbarcacion());
//        Consulta2.bindValue(":eslora",((CBarco*)newPunto)->LargoEmbarcacion());
//        Consulta2.bindValue(":ubicacion",((CBarco*)newPunto)->locationName());
//        Consulta2.bindValue(":estatusNavegacion",((CBarco*)newPunto)->estadoNavegacion());
//        Consulta2.bindValue(":latitud",((CBarco*)newPunto)->getGeoPos().latitude());
//        Consulta2.bindValue(":longitud",((CBarco*)newPunto)->getGeoPos().longitude());
//        Consulta2.bindValue(":ultimaActualizacion",QDateTime::fromMSecsSinceEpoch(((CBarco*)newPunto)->getListTrayect().last().time).toString("dd/MM/yyyy hh:mm:ss"));

 //       Consulta2.bindValue(":tipo",((CPunto*)newPunto)->getTipo());

//        QByteArray imgData = cUtiles::ConvierteQPixmap_A_QByteArray(((CPunto*)newPunto)->getSimbolo());

//        Consulta2.bindValue(":simbolo",imgData);
//        Consulta2.bindValue(":latitud",((CPunto*)newPunto)->getGeoPos().latitude());
//        Consulta2.bindValue(":longitud",((CPunto*)newPunto)->getGeoPos().longitude());
//        Consulta2.bindValue(":fecha_creacion",((CPunto*)newPunto)->getFechaCreacion());
        if (Consulta2.prepare(Escribe))
            Consulta2.exec();


        //----> Adicionando nueva Trayectoria al punto
//        NombTabl_Princ = "trayectorias_" + ((CPunto*)newPunto)->getNombre();
//        NombTabl_Princ.remove(' ');
//        Escribe = "INSERT INTO " + NombTabl_Princ + " (latitud,longitud,fecha_creacion)"
//                       " VALUES (:latitud,:longitud,:fecha_creacion)";
//        Consulta2.prepare(Escribe);
//        Consulta2.bindValue(":latitud",((CPunto*)newPunto)->getGeoPos().latitude());
//        Consulta2.bindValue(":longitud",((CPunto*)newPunto)->getGeoPos().longitude());
//        Consulta2.bindValue(":fecha_creacion",((CPunto*)newPunto)->getFechaCreacion());

//        Consulta2.exec();
    }
}

void CBDatos::guardarNuevoPunto(void *newPunto)
{
   bool ok = false;
   if (!bd_Puntos.isValid())
   {
       bd_Puntos = QSqlDatabase::addDatabase("QSQLITE","Puntos");//tipo de base dato
       bd_Puntos.setDatabaseName(DirPath + "/Recursos/puntosBD.sig");//nombre de la base datos
   }

   if(!bd_Puntos.isOpen()) //----------------------------------------------------------> Si la BD ya esta abierta...
       ok = bd_Puntos.open();
   else
       ok = true;
   if(ok)
   {
       //---- > creando tabla de Puntos Geo si no existe
       QSqlQuery Consulta(bd_Puntos);
       QString NombTabl_Princ = "puntos";
       QString Crea = "CREATE TABLE IF NOT EXISTS " + NombTabl_Princ + " (no_punto KEY INTEGER NOT nullptr UNIQUE, nombre TEXT NOT nullptr UNIQUE, descripcion TEXT, tipo TEXT, simbolo BLOB, latitud DOUBLE, longitud DOUBLE, fecha_creacion TEXT)";
       if (Consulta.prepare(Crea))
           Consulta.exec();

//       qDebug() << Consulta.lastError();
       //---- > creando tabla de Trayectorias de Puntos Geo si no existe
       NombTabl_Princ = "trayectorias_" + ((CPunto*)newPunto)->getNombre();
       NombTabl_Princ.remove(' ');
       Crea = "CREATE TABLE IF NOT EXISTS " + NombTabl_Princ + " (latitud DOUBLE, longitud DOUBLE, fecha_creacion TEXT)";
       if (Consulta.prepare(Crea))
           Consulta.exec();

//       qDebug() << Consulta.lastError();

       //---- > consultando ultimo punto existente
       NombTabl_Princ = "puntos";
       QSqlQuery BuscarUltimoPunto(bd_Puntos);
       QString Busqueda = "SELECT no_punto FROM " + NombTabl_Princ;
       BuscarUltimoPunto.prepare(Busqueda);
       if (BuscarUltimoPunto.exec())
       {
           while (BuscarUltimoPunto.next())
           {
               NoUltimoPunto = BuscarUltimoPunto.value(0).toInt();
           }
       }

       if (NoUltimoPoligono < 0) NoUltimoPoligono = 0;

       emit signal_devuelveUltimoNoPunto(NoUltimoPunto);

       //---- > adicionando nuevo punto
       NombTabl_Princ = "puntos";
       QSqlQuery Consulta2(bd_Puntos);
       QString Escribe = "INSERT INTO " + NombTabl_Princ + " (no_punto,nombre,descripcion,tipo,simbolo,latitud,longitud,fecha_creacion)"
                      " VALUES (:no_punto,:nombre,:descripcion,:tipo,:simbolo,:latitud,:longitud,:fecha_creacion)";
       Consulta2.prepare(Escribe);
       Consulta2.bindValue(":no_punto",NoUltimoPunto + 1);
       Consulta2.bindValue(":nombre",((CPunto*)newPunto)->getNombre());
       Consulta2.bindValue(":descripcion",((CPunto*)newPunto)->getDescripcion());
//       Consulta2.bindValue(":tipo",((CPunto*)newPunto)->getTipo());

       QByteArray imgData = QUtiles::ConvierteQPixmap_A_QByteArray(((CPunto*)newPunto)->getSimbolo());

       Consulta2.bindValue(":simbolo",imgData);
       Consulta2.bindValue(":latitud",((CPunto*)newPunto)->getGeoPos().latitude());
       Consulta2.bindValue(":longitud",((CPunto*)newPunto)->getGeoPos().longitude());
       Consulta2.bindValue(":fecha_creacion",((CPunto*)newPunto)->getFechaCreacion());

       Consulta2.exec();

       //----> Adicionando nueva Trayectoria al punto
       NombTabl_Princ = "trayectorias_" + ((CPunto*)newPunto)->getNombre();
       NombTabl_Princ.remove(' ');
       Escribe = "INSERT INTO " + NombTabl_Princ + " (latitud,longitud,fecha_creacion)"
                      " VALUES (:latitud,:longitud,:fecha_creacion)";
       Consulta2.prepare(Escribe);
       Consulta2.bindValue(":latitud",((CPunto*)newPunto)->getGeoPos().latitude());
       Consulta2.bindValue(":longitud",((CPunto*)newPunto)->getGeoPos().longitude());
       Consulta2.bindValue(":fecha_creacion",((CPunto*)newPunto)->getFechaCreacion());

       Consulta2.exec();
   }

}

void CBDatos::guardarNuevoPoligono(const E_DataPoligono &newPoligono)
{
    bool ok = false;
    if (!bd_Puntos.isValid())
    {
        bd_Puntos = QSqlDatabase::addDatabase("QSQLITE","Poligonos");//tipo de base dato
        bd_Puntos.setDatabaseName(DirPath + "/Recursos/puntosBD.sig");//nombre de la base datos
    }

    if(!bd_Puntos.isOpen()) //----------------------------------------------------------> Si la BD ya esta abierta...
        ok = bd_Puntos.open();
    else
        ok = true;
    if(ok)
    {
        //---- > creando tabla de Polígonos si no existe.
        QSqlQuery Consulta(bd_Puntos);
        QString NombTabl_Princ = "poligonos";
        QString Crea = "CREATE TABLE IF NOT EXISTS " + NombTabl_Princ + " (idPoligono KEY INTEGER NOT nullptr UNIQUE, nombre TEXT NOT nullptr UNIQUE, colorRGB TEXT, tipoRelleno INTEGER, noVertices INTEGER NOT nullptr, fecha_creacion TEXT)";
        if (Consulta.prepare(Crea))
            Consulta.exec();

        //---- > creando tabla de Puntos de Polígonos si no existe.
        NombTabl_Princ = "poligono_" + newPoligono.nombrePolig;
        NombTabl_Princ.remove(' ');
        Crea = "CREATE TABLE IF NOT EXISTS " + NombTabl_Princ + " (NoVertice INTEGER NOT nullptr, latitud DOUBLE, longitud DOUBLE)";
        if (Consulta.prepare(Crea))
            Consulta.exec();

        //---- > consultando ultimo punto existente
        NombTabl_Princ = "poligonos";
        QSqlQuery BuscarUltimoPoligono(bd_Puntos);
        QString Busqueda = "SELECT idPoligono FROM " + NombTabl_Princ;
        BuscarUltimoPoligono.prepare(Busqueda);
        if (BuscarUltimoPoligono.exec())
        {
            while (BuscarUltimoPoligono.next())
            {
                NoUltimoPoligono = BuscarUltimoPoligono.value(0).toInt();
            }
        }
        emit signal_devuelveUltimoNoPoligono(NoUltimoPoligono);

        //---- > adicionando nuevo Poligono
        NombTabl_Princ = "poligonos";
        QSqlQuery Consulta2(bd_Puntos);
        QString Escribe = "INSERT INTO " + NombTabl_Princ + " (idPoligono,nombre,colorRGB,tipoRelleno,noVertices,fecha_creacion)"
                                                            " VALUES (:idPoligono,:nombre,:colorRGB,:tipoRelleno,:noVertices,:fecha_creacion)";
        Consulta2.prepare(Escribe);
        Consulta2.bindValue(":idPoligono",NoUltimoPoligono + 1);
        Consulta2.bindValue(":nombre",newPoligono.nombrePolig);
        Consulta2.bindValue(":colorRGB",
                            QString::number(newPoligono.pen.color().toRgb().red()) + ','
                            + QString::number(newPoligono.pen.color().toRgb().green()) + ','
                            + QString::number(newPoligono.pen.color().toRgb().blue()));
        Consulta2.bindValue(":tipoRelleno",newPoligono.relleno);
        Consulta2.bindValue(":noVertices",newPoligono.LPuntosGeoPolig.size());
        Consulta2.bindValue(":fecha_creacion",newPoligono.fechaCreacion);

        Consulta2.exec();

        //----> Adicionando Puntos del Poligono

        NombTabl_Princ = "poligono_" + newPoligono.nombrePolig;
        NombTabl_Princ.remove(' ');
        for (int i = 0; i < newPoligono.LPuntosGeoPolig.size(); ++i)
        {
            Escribe = "INSERT INTO " + NombTabl_Princ + " (NoVertice,latitud,longitud)"
                                                        " VALUES (:NoVertice,:latitud,:longitud)";
            Consulta2.prepare(Escribe);
            Consulta2.bindValue(":NoVertice",i + 1);
            Consulta2.bindValue(":latitud",newPoligono.LPuntosGeoPolig.at(i).latitude());
            Consulta2.bindValue(":longitud",newPoligono.LPuntosGeoPolig.at(i).longitude());

            Consulta2.exec();
        }
    }

}

void CBDatos::guardarNuevaRuta(const QList<EPuntoRuta> &newRuta)
{
    bool ok = false;

    if (!bd_Puntos.isValid())
    {
        bd_Puntos = QSqlDatabase::addDatabase("QSQLITE","Rutas");//tipo de base dato
        bd_Puntos.setDatabaseName(DirPath + "/Recursos/puntosBD.sig");//nombre de la base datos
    }

    if(!bd_Puntos.isOpen()) //----------------------------------------------------------> Si la BD ya esta abierta...
        ok = bd_Puntos.open();
    else
        ok = true;
    if(ok)
    {
        // creando tabla
        QString dateTime = QDate::currentDate().toString("ddMMyyyy") + "_" + QTime::currentTime().toString("hhmmss");
        QString NombTabl_Princ = "Ruta_" + dateTime;
        NombTabl_Princ.remove(' ');
        QSqlQuery Consulta2(bd_Puntos);
        QString Escribe = "CREATE TABLE IF NOT EXISTS " + NombTabl_Princ +  " (prioridad INTEGER,descripcion TEXT,pos TEXT)";
        if (Consulta2.prepare(Escribe))
        {
            Consulta2.exec();
        }

        //----> Adicionando nueva Trayectoria al punto
        foreach (EPuntoRuta ruta, newRuta)
        {
            Escribe = "INSERT INTO " + NombTabl_Princ + " (prioridad,descripcion,pos)"
                                                                " VALUES (:prioridad,:descripcion,:pos)";
            if (Consulta2.prepare(Escribe))
            {
                Consulta2.bindValue(":prioridad",ruta.prioridad);
                Consulta2.bindValue(":descripcion",ruta.descripcion);
                Consulta2.bindValue(":pos",ruta.pos.toString());
                Consulta2.exec();
            }
        }
    }
}

void CBDatos::insertarNuevaTrayectoria(const QString &nombrePunto, double Lat, double Lon, const QString &fecha)
{
    bool ok = false;

    if (!bd_Puntos.isValid())
    {
        bd_Puntos = QSqlDatabase::addDatabase("QSQLITE","Trayectorias");//tipo de base dato
        bd_Puntos.setDatabaseName(DirPath + "/Recursos/puntosBD.sig");//nombre de la base datos
    }

    if(!bd_Puntos.isOpen()) //----------------------------------------------------------> Si la BD ya esta abierta...
        ok = bd_Puntos.open();
    if(ok)
    {
        //----> Adicionando nueva Trayectoria al punto
        QString NombTabl_Princ = "trayectorias_" + nombrePunto;
        NombTabl_Princ.remove(' ');
        QSqlQuery Consulta2(bd_Puntos);
        QString Escribe = "INSERT INTO " + NombTabl_Princ + " (latitud,longitud,fecha_creacion)"
                                                            " VALUES (:latitud,:longitud,:fecha_creacion)";
        Consulta2.prepare(Escribe);
        Consulta2.bindValue(":latitud",Lat);
        Consulta2.bindValue(":longitud",Lon);
        Consulta2.bindValue(":fecha_creacion",fecha);

        Consulta2.exec();
    }
}

QList<void *> CBDatos::cargarPuntos(QString nombreTabla)
{
   QList<void*> lPuntos;
   bool ok = false;

   if (!bd_Puntos.isValid())
   {
       bd_Puntos = QSqlDatabase::addDatabase("QSQLITE","Puntos");//tipo de base dato
       bd_Puntos.setDatabaseName(DirPath + "/Recursos/puntosBD.sig");//nombre de la base datos
   }

   if(!bd_Puntos.isOpen()) //----------------------------------------------------------> Si la BD ya esta abierta...
       ok = bd_Puntos.open();
   if(ok)
   {
       //---- > leyendo puntos desde la base de datos
       QSqlQuery Consulta(bd_Puntos);
       QString NombTabl_Princ = nombreTabla;
       QString Lee = "SELECT * FROM " + NombTabl_Princ;

       QDateTime timeToday = QDateTime::currentDateTime();
       if (Consulta.prepare(Lee))
       {
           if (Consulta.exec())
           {
               if (nombreTabla.contains("buques"))
               {
                   while (Consulta.next())
                   {
                       QDateTime tBuque = QDateTime::fromString(Consulta.value(18).toString(),"dd/MM/yyyy hh:mm:ss");
                       if (qAbs(timeToday.date().day() - tBuque.date().day()) <= 1 && (qAbs(timeToday.date().month() - tBuque.date().month()) <= 1 || qAbs(timeToday.date().month() - tBuque.date().month()) == 11 ) && qAbs(timeToday.date().year() - tBuque.date().year()) <= 1) //ultimo dia
                       {
                           bool existe = false;
                           foreach (void* buqueExistente, lPuntos)
                           {
                               if (((CBarco*)buqueExistente)->MMSI() == Consulta.value(0).toString())
                               {
                                   existe = true;
                                   ((CBarco*)buqueExistente)->setSignalCall(Consulta.value(1).toString());
                                   ((CBarco*)buqueExistente)->setIMOnumber(Consulta.value(2).toString());
                                   ((CBarco*)buqueExistente)->setNombreBuque(Consulta.value(3).toString());
                                   ((CBarco*)buqueExistente)->setPaisProcedencia(Consulta.value(4).toString());
                                   ((CBarco*)buqueExistente)->setTipoEmbarcacion(Consulta.value(5).toString());
                                   ((CBarco*)buqueExistente)->setCursoSobreTierra(Consulta.value(6).toDouble());
                                   ((CBarco*)buqueExistente)->setVelocidad(Consulta.value(7).toDouble());
                                   ((CBarco*)buqueExistente)->setHeading(Consulta.value(8).toDouble());
                                   ((CBarco*)buqueExistente)->setDestino(Consulta.value(9).toString());
                                   ((CBarco*)buqueExistente)->setDraught(Consulta.value(11).toFloat());
                                   ((CBarco*)buqueExistente)->setAnchoEmbarcacion(Consulta.value(12).toFloat());
                                   ((CBarco*)buqueExistente)->setLargoEmbarcacion(Consulta.value(13).toFloat());
                                   ((CBarco*)buqueExistente)->setEstadoDatos(Consulta.value(14).toString() == "Actualizado" ? Actualizado : SinActualizacion);
                                   ((CBarco*)buqueExistente)->setEstadoNavegacion(Consulta.value(15).toString());
                                   ((CBarco*)buqueExistente)->addNewPuntoToTrayect(QGeoCoordinate(Consulta.value(16).toDouble(),Consulta.value(17).toDouble()),tBuque.toMSecsSinceEpoch(),((CBarco*)buqueExistente)->cursoSobreTierra(),((CBarco*)buqueExistente)->getVelocidad());
                               }
                           }
                           if (!existe)
                           {
                               CBarco* buque = new CBarco();
                               buque->setMMSI(Consulta.value(0).toString());
                               buque->setSignalCall(Consulta.value(1).toString());
                               buque->setIMOnumber(Consulta.value(2).toString());
                               buque->setNombreBuque(Consulta.value(3).toString());
                               buque->setPaisProcedencia(Consulta.value(4).toString());
                               buque->setTipoEmbarcacion(Consulta.value(5).toString());
                               buque->setCursoSobreTierra(Consulta.value(6).toDouble());
                               buque->setVelocidad(Consulta.value(7).toDouble());
                               buque->setHeading(Consulta.value(8).toDouble());
                               buque->setDestino(Consulta.value(9).toString());
                               buque->setDraught(Consulta.value(11).toFloat());
                               buque->setAnchoEmbarcacion(Consulta.value(12).toFloat());
                               buque->setLargoEmbarcacion(Consulta.value(13).toFloat());
                               buque->setEstadoDatos(Consulta.value(14).toString() == "Actualizado" ? Actualizado : SinActualizacion);
                               buque->setEstadoNavegacion(Consulta.value(15).toString());
                               buque->addNewPuntoToTrayect(QGeoCoordinate(Consulta.value(16).toDouble(),Consulta.value(17).toDouble()),tBuque.toMSecsSinceEpoch(),buque->cursoSobreTierra(),buque->getVelocidad());
                               lPuntos.append(buque);
                           }
                       }
                   }
                   return lPuntos;
               }
               else
               {
                   while (Consulta.next())
                   {
                       CPunto * punto = new CPunto();
                       punto->setNumero(Consulta.value(0).toInt());
                       punto->setNombre(Consulta.value(1).toString());
                       punto->setDescripcion(Consulta.value(2).toString());
                       //                   punto->setTipo(Consulta.value(3).toString());
                       QPixmap pix;
                       pix.loadFromData(Consulta.value(3).toByteArray());
                       punto->setSimbolo(pix);
                       punto->setGeoPos(QGeoCoordinate(Consulta.value(4).toDouble(),Consulta.value(5).toDouble()));
                       punto->setFechaCreacion(Consulta.value(6).toString());

                       //---> Leyendo datos de la tabla de trayectorias
                       QSqlQuery Consulta2(bd_Puntos);
                       QString NombTabl_Princ2 = "trayectorias_" + punto->getNombre();
                       NombTabl_Princ2.remove(' ');
                       QString Lee2 = "SELECT * FROM " + NombTabl_Princ2;
                       if (Consulta2.prepare(Lee2))
                       {
                           if (Consulta2.exec())
                           {
                               while (Consulta2.next())
                               {
                                   QGeoCoordinate tray;
                                   tray.setLatitude(Consulta2.value(0).toDouble());
                                   tray.setLongitude(Consulta2.value(1).toDouble());

                                   //                               punto->addNewPuntoToTrayect(tray,QDateTime::fromString(Consulta2.value(2).toString()));
                               }
                           }
                       }
                       lPuntos.append(punto);
                   }
               }
               if (lPuntos.isEmpty())
                   emit signal_NoExistenElementos();
           }
           else
               emit signal_ConsultaInvalida();
       }
       else
           emit signal_ConsultaInvalida();
   }
   return lPuntos;
}

QList<E_DataPoligono> CBDatos::cargarPoligonos()
{
    QList<E_DataPoligono> lPolig;

    bool ok = false;

    if (!bd_Puntos.isValid())
    {
        bd_Puntos = QSqlDatabase::addDatabase("QSQLITE","Poligonos");//tipo de base dato
        bd_Puntos.setDatabaseName(DirPath + "/Recursos/puntosBD.sig");//nombre de la base datos
    }

    if(!bd_Puntos.isOpen()) //----------------------------------------------------------> Si la BD ya esta abierta...
        ok = bd_Puntos.open();
    if(ok)
    {
        //---- > leyendo poligonos desde la base de datos
        QSqlQuery Consulta(bd_Puntos);
        QString NombTabl_Princ = "poligonos";
        QString Lee = "SELECT * FROM " + NombTabl_Princ;
        if (Consulta.prepare(Lee))
        {
            if (Consulta.exec())
            {
                while (Consulta.next())
                {
                    E_DataPoligono ePoligon;
                    ePoligon.idPoligono = Consulta.value(0).toInt();
                    ePoligon.nombrePolig = Consulta.value(1).toString();

                    QStringList lRGB = Consulta.value(2).toString().split(',');
                    QColor c;
                    c.setRgb(lRGB.at(0).toInt(),lRGB.at(1).toInt(),lRGB.at(2).toInt());

                    ePoligon.pen = c;
                    ePoligon.relleno = Consulta.value(3).toInt();
                    ePoligon.noVertices = Consulta.value(4).toInt();
                    ePoligon.fechaCreacion = Consulta.value(5).toString();

                    //---> Leyendo datos de la tabla de datos del poligono
                    QSqlQuery Consulta2(bd_Puntos);
                    QString NombTabl_Princ2 = "poligono_" + ePoligon.nombrePolig;
                    NombTabl_Princ2.remove(' ');
                    QString Lee2 = "SELECT * FROM " + NombTabl_Princ2;
                    if (Consulta2.prepare(Lee2))
                    {
                        if (Consulta2.exec())
                        {
                            while (Consulta2.next())
                            {
                                ePoligon.LPuntosGeoPolig.append(QGeoCoordinate(Consulta2.value(1).toDouble(),Consulta2.value(2).toDouble()));
                            }
                        }
                    }

                    lPolig.append(ePoligon);
                }
                if (lPolig.isEmpty())
                    emit signal_NoExistenElementos();
            }
            else
                emit signal_ConsultaInvalida();
        }
        else
            emit signal_ConsultaInvalida();
    }
    return lPolig;
}

bool CBDatos::eliminarPoligono(const E_DataPoligono &poligono)
{
   bool ok = false;

   if (!bd_Puntos.isValid())
   {
       bd_Puntos = QSqlDatabase::addDatabase("QSQLITE","Poligonos");//tipo de base dato
       bd_Puntos.setDatabaseName(DirPath + "/Recursos/puntosBD.sig");//nombre de la base datos
   }

   if(!bd_Puntos.isOpen()) //----------------------------------------------------------> Si la BD ya esta abierta...
       ok = bd_Puntos.open();
   if(ok)
   {
       //---- > Eliminando registro de poligonos en la BD
       QSqlQuery Consulta(bd_Puntos);
       QString NombTabl_Princ = "poligonos";
       QString Elimina = "DELETE FROM " + NombTabl_Princ + " WHERE idPoligono = " + QString::number(poligono.idPoligono);
       if (Consulta.prepare(Elimina))
       {
           if (Consulta.exec())
           {
               //----> Eliminando Tabla datos del poligono en la BD
               NombTabl_Princ = "poligono_" + poligono.nombrePolig;
               NombTabl_Princ.remove(' ');
               Elimina = "DROP TABLE " + NombTabl_Princ;
               if (Consulta.prepare(Elimina))
                   return Consulta.exec();
           }
           return false;
       }
       return false;
   }
   return false;
}

void CBDatos::guardarUltimoRangoMapa(int xMin, int xMax, int yMin, int yMax)
{
    Q_UNUSED(xMin)
    Q_UNUSED(xMax)
    Q_UNUSED(yMin)
    Q_UNUSED(yMax)
}

QString CBDatos::getDirPath() const
{
    return DirPath;
}

void CBDatos::setDirPath(const QString &value)
{
    DirPath = value;
}

