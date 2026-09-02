#ifndef CONSTANTES_H
#define CONSTANTES_H

#include <QString>
#include <QPen>
#include <QColor>

//---------------CAPAS--------------
const QString sMapaCuba         = "sMapaCuba";
const QString sMapaFir          = "sMapaFir";
const QString sMapaAguas        = "sMapaAguas";
const QString sMapaCorredores   = "sMapaCorredores";
const QString sMapaEjercitos    = "sMapaEjercitos";
const QString sMapaProvincia    = "sMapaProvincia";
const QString sMapaTCAA         = "sMapaTCAA";
const QString sMapaTRT          = "sMapaTRT";
const QString sMapaIMG          = "sMapaIMG";
const QString sMapaOSM_IMG      = "sMapaOSM_IMG";
const QString sMapaFuentes      = "sMapaFuentes";
const QString sMapaPuntos       = "sMapaPuntos";
const QString sMapaZOOM         = "sMapaZOOM";
const QString sMapaTextoRuta    = "sMapaZOOM";
const QString sMapaTargets      = "sMapaTargets";
const QString sMapaPoligonos    = "sMapaPoligonos";
const QString sMapaRuta         = "sMapaRuta";
const QString sMapaArea         = "sMapaArea";
const QString sMapaViales       = "sMapaViales";
const QString sMapaRelieve      = "sMapaRelieve";
const QString sMapaVege         = "sMapaVege";
const QString sMapaHidro        = "sMapaHidro";

//-------GAMA DE COLORES USADOS-------
//const QPen clLineas             = QPen(QColor(0,0,255),0);
const QPen clAguas              = QPen(QColor(0,128,192),0);
const QPen clLimiteAguas        = QPen(QColor(190,231,253),2);
const QPen clLimiteFIR          = QPen(QColor(255,231,100),2);
const QPen clBosques            = QPen(QColor(173,209,158),0);
const QPen clLlanos             = QPen(QColor(174,223,163),0);
const QPen clEdificios          = QPen(QColor(217,208,201),0);
const QPen clAvenidas           = QPen(QColor(252,214,164),0);
const QPen clCalles             = QPen(QColor(255,255,255),0);
const QPen clCalzadas           = QPen(QColor(247,250,191),0);
const QPen clContornos          = QPen(QColor(151,132,154),0);


// Coordenadas aproximadas de Cuba (ajústalas según necesidad)
const double MIN_LAT = 17.099f/* 19.8f*/;    // Punto más al sur (Cabo Cruz)
const double MAX_LAT = 31.541f/* 23.2f*/;    // Punto más al norte (Cayo Saetía)
const double MIN_LON = -91.846f/*-85.0f*/;   // Punto más al oeste (Cabo San Antonio)
const double MAX_LON = -68.071f/*-74.1f*/;   // Punto más al este (Punta de Maisí)

#endif // CONSTANTES_H
