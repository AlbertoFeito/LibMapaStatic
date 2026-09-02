#include "wrjfile.h"

wrjfile::wrjfile(void)
{
}

wrjfile::~wrjfile(void)
{
}


void wrjfile::read_header( std::ifstream *file, wrjfile_header &header )
{
	if (file->is_open())
	{
		file->read((char*)&header.xmin, sizeof(float));
		file->read((char*)&header.ymin, sizeof(float));
		file->read((char*)&header.xmax, sizeof(float));
		file->read((char*)&header.ymax, sizeof(float));

		file->read(&header.geometry, 1);
		file->read(&header.tipoCoordenada, 1);
		file->read((char*)&header.epsg, sizeof(int));
		file->read((char*)&header.nFeatures, sizeof(int));
	}
}

void wrjfile::write_header( std::ofstream *file, wrjfile_header &header )
{
	if (file->is_open())
	{
		file->write((char*)&header.xmin, sizeof(float));
		file->write((char*)&header.ymin, sizeof(float));
		file->write((char*)&header.xmax, sizeof(float));
		file->write((char*)&header.ymax, sizeof(float));

		file->write(&header.geometry, 1);
		file->write(&header.tipoCoordenada, 1);
		file->write((char*)&header.epsg, sizeof(int));
		file->write((char*)&header.nFeatures, sizeof(int));
	}
}


void wrjfile::write_points( std::ofstream *file, int nPoints, double *points )
{
	file->write((char*)&nPoints, sizeof(int));
	file->write((char*)&points, nPoints * sizeof(double) * 2);
}

void wrjfile::read_points( std::ifstream *file, int nPoints, double *points )
{
    file->read((char*)points, nPoints * sizeof(double) * 2);
}

void wrjfile::read_point(std::ifstream *file, double &x, double &y)
{
    file->read((char*)&x, sizeof(double));
    file->read((char*)&y, sizeof(double));
}
