#ifndef GRID_H
#define GRID_H

/*
  Classes for reading and storing grid information
*/

#include <cgnslib.h>
#include <vector>
#include "vec3d.h"
#include "sparse.h"

enum {TETRA,WEDGE,HEXA};

class Node : public Vec3D {
public:
  unsigned int id;
  std::vector<int> cells;
  Node(double x=0., double y=0., double z=0.);
};

class Face {
public:
	int bc;
	unsigned int id;
	int parent,neighbor;
	unsigned int nodeCount;
    Vec3D centroid;
    Vec3D normal;
    VecSparse cellContributions;
    double area;
	std::vector<int> nodes;
	Node& node(int n);
};

class Cell {
  public:
  unsigned int nodeCount,faceCount;
  int type;
  double volume;
  Vec3D centroid;
  std::vector<int> nodes;
  std::vector<int> faces;
  double rho;
  Vec3D v,grad[5]; 
  double p; 
  double flux[5];
  Cell(void);
  int Construct(const int type,const int nodeCount, const int nodeList[]);
  int HaveNodes(unsigned int &nodelistsize, unsigned int nodelist[]);
  Node& node(int n);
  Face& face(int f);
};

class Grid {
 public:
  string fileName;
  unsigned int nodeCount,cellCount,faceCount;
  std::vector<Node> node;
  std::vector<Face> face;
  std::vector<Cell> cell;
  int ReadCGNS();
  Grid();
  int read(string);
  int face_exists(int &parentCell); 
  void gradients();
};

#endif