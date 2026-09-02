#ifndef wrjfile_h__
#define wrjfile_h__

#include <fstream>

struct wrjfile_header
{
	float xmin;
	float ymin;
	float xmax;
	float ymax;

    char geometry; // ST_NULL_SHAPE = 0, ST_POINT = 1, ST_POLYLINE = 3, ST_POLYGON = 5, ST_MULTI_POINT = 8, ST_POLYGONZ = 15, ST_POLYGONM = 25, ST_POLYLINEM = 23, ST_POLYLINEZ = 13

	char tipoCoordenada; // 0(coordenadas geograficas, 1: coordenadas planas)

	int epsg; // codigo del sistema de rerefrencia geografico segun EPSG(European Petroleum Survey Group)

	int nFeatures;


};

struct wrjfile_meta_header
{
    char code[8];
    char version;
    char nColumns;
    int nRecords;
};

struct wrjfile_meta_field
{
    int contentSize;
    char dataType;
    char* content;
};

class wrjfile
{
public:
	wrjfile(void);

	static void write_header(std::ofstream *file, wrjfile_header &header);

	static void read_header(std::ifstream *file, wrjfile_header &header);

	static void write_points(std::ofstream *file, int nPoints, double *points);

	static void read_points(std::ifstream *file, int nPoints, double *points);

    static void read_point(std::ifstream *file, double &x, double &y);

	~wrjfile(void);
};

#endif // wrjfile_h__
