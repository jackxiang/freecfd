/************************************************************************
	
	Copyright 2007-2010 Emre Sozer

	Contact: emresozer@freecfd.com

	This file is a part of Free CFD

	Free CFD is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    any later version.

    Free CFD is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    For a copy of the GNU General Public License,
    see <http://www.gnu.org/licenses/>.

*************************************************************************/
#include "grid.h"
#define EPS 1e-10
#define INTERPOLATE_POINT 0
#define INTERPOLATE_LINE 1
#define INTERPOLATE_TRI 2
#define INTERPOLATE_TETRA 3

// Iterators

std::vector<int>::iterator it;
std::vector<int>::iterator it2;
std::vector<double>::iterator dit;

Grid *currentGrid;

// Names are a bit counter-intutivite here
// Larger tolerance means more strict quality measures
double area_tolerance=0.1;
double volume_tolerance=1.e-2;

int gelimd(vector<vector<double> > &a,vector<double> &b,vector<double> &x);
bool compare_closest (int first,int second);
	
struct interpolation_tetra {
	int cell[4];
	double weight;
};

struct interpolation_tri {
	int cell[3];
	double weight;
};

extern GridRawData raw;

// Store the cell stencil surronding a node
set<int> stencil;
set<int>::iterator sit,sit1,sit2,sit3;
// A structure to store data about an individual interpolation tetra

// For each node, store possible formations of interpolation tetras
vector<interpolation_tetra> tetras; 
vector<interpolation_tetra>::iterator tet;

vector<interpolation_tri> tris; 
vector<interpolation_tri>::iterator tri;
Vec3D centroid1,centroid2,centroid3,centroid4,ave_centroid;
Vec3D lineDirection,planeNormal;
Vec3D pp,basis1,basis2;
int method;

double weightSum,weightSum2;
double tetra_volume,tri_area,ave_edge,line_distance;
double closeness,skewness;
vector<vector<double> > a3,a4;
vector<double> b3,b4,weights3,weights4;

double distanceMin=1.e20;
int p1,p2;

Vec3D face_center_Vec;

void Grid::faceAverages() {

	a3.resize(3); b3.resize(3); weights3.resize(3);
	for (int i=0;i<3;++i) a3[i].resize(3);
	
	a4.resize(4); b4.resize(4); weights4.resize(4);
	for (int i=0;i<4;++i) a4[i].resize(4);
	
	
	int tetra_node_count=0;		
	int tri_node_count=0;		
	int line_node_count=0;		
	int point_node_count=0;	

	// Loop all the faces
	for (int f=0;f<faceCount;++f) {
		// Initialize stencil to nearest neighbor cells
		// Loop face nodes and their neighboring cells
		for (int fn=0;fn<face[f].nodeCount;++fn) {
			for (int nc=0;nc<node[face[f].nodes[fn]].cells.size();++nc) stencil.insert(node[face[f].nodes[fn]].cells[nc]);
			// Include nearest ghost cells in the stencil
			for (int ng=0;ng<node[face[f].nodes[fn]].ghosts.size();++ng) stencil.insert(-1*node[face[f].nodes[fn]].ghosts[ng]-1);	
		}
		
		// Sort the stencil based on distance
		sortStencil(f);
		
		if (stencil.size()==1) {
			method=INTERPOLATE_POINT;
		} else if (stencil.size()==2) {
			method=INTERPOLATE_LINE;
		} else {
			// If stencil size is larger than 4, potentially we can find at least one interpolation tetra
			// Check if all the points lie on a line (True if a 1D problem)
			// Pick first 2 points in the stencil
			sit=sit2=stencil.begin(); sit1=++sit2; ++sit2;
			centroid1=(*sit>=0) ? cell[*sit].centroid : ghost[-1*(*sit)-1].centroid;
			centroid2=(*sit1>=0) ? cell[*sit1].centroid : ghost[-1*(*sit1)-1].centroid;
			// Direction of the line formed by the first 2 points
			lineDirection=(centroid2-centroid1).norm();
			// Loop the rest of the stencil and check if a triangle can be constructed with other points 
			method=INTERPOLATE_LINE;
			for (;sit2!=stencil.end();sit2++) {
				centroid3=(*sit2>=0) ? cell[*sit2].centroid : ghost[-1*(*sit2)-1].centroid;
				ave_edge=1./3.*(fabs(centroid2-centroid1)+fabs(centroid3-centroid1)+fabs(centroid3-centroid2));
                                // Calculate the normal vector of the plane formed by these 3 points
                                tri_area=0.5*fabs((centroid2-centroid1).cross(centroid3-centroid1));
				// Compare triangle area to that of an equilateral one
				if (tri_area/(0.433*ave_edge*ave_edge)>area_tolerance) {
					// point 3 doesn't lie on the same line formed by the first 2 points
					// Then at least one interpolation triangle can be formed
					method=INTERPOLATE_TRI;
					break;
				}
			}
			
			if (method==INTERPOLATE_TRI && dimension==3) { // If at least one interpolation triangle can be formed
				// Check if a tetra can be formed with the rest of the points in the stencil
				for (sit3=sit2;sit3!=stencil.end();sit3++) {
					centroid4=(*sit3>=0) ? cell[*sit3].centroid : ghost[-1*(*sit3)-1].centroid;
					// Compare tri area with that of an equilateral triangel
                                        tetra_volume=0.166666667*fabs(((centroid2-centroid1).cross(centroid3-centroid1)).dot(centroid4-centroid1));
                                        ave_edge=1./6.*(fabs(centroid1-centroid2)+fabs(centroid1-centroid3)+fabs(centroid1-centroid4)
                                                       +fabs(centroid2-centroid3)+fabs(centroid2-centroid4)+fabs(centroid3-centroid4));

					// Compare volume with that of an equilateral tetra
					if (tetra_volume/(0.11785113*(pow(ave_edge,3)))>volume_tolerance) {
                                            // point 4 is not on the same plane
                                                // Then at least one interpolation tetrahedra can be formed
                                                method=INTERPOLATE_TETRA;
                                                break;
					}
				}
			}	
		}
		
		face[f].average.clear();
		if (method==INTERPOLATE_TETRA) {
			interpolate_tetra(f); tetra_node_count++;
		}
		if (method==INTERPOLATE_TRI) {
			interpolate_tri(f); 
			tri_node_count++;
		}
		if (method==INTERPOLATE_LINE) {
			interpolate_line(f);
			line_node_count++;
		}
		if (method==INTERPOLATE_POINT) {
			face[f].average.insert(pair<int,double>(*(stencil.begin()),1.));
			point_node_count++;
		}
		
		// Clear temp data
		stencil.clear();
		tetras.clear();
		tris.clear();

	}
		cout << "[I Rank=" << Rank << "] Face centers for which tetra interpolation method was used = " << tetra_node_count << endl; 
		cout << "[I Rank=" << Rank << "] Face centers for which tri interpolation method was used = " << tri_node_count << endl; 
		cout << "[I Rank=" << Rank << "] Face centers for which line interpolation method was used = " << line_node_count << endl; 
		cout << "[I Rank=" << Rank << "] Face centers for which point interpolation method was used = " << point_node_count << endl;
	
		a3.clear(); b3.clear(); weights3.clear();
		a4.clear(); b4.clear(); weights4.clear();

}

// comparison, not case sensitive.
bool compare_closest (int first, int second) {
	centroid1= (first>=0) ? currentGrid->cell[first].centroid : currentGrid->ghost[-1*first-1].centroid;
	centroid2= (second>=0) ? currentGrid->cell[second].centroid : currentGrid->ghost[-1*second-1].centroid;
	if (fabs(face_center_Vec-centroid1)<=fabs(face_center_Vec-centroid2)) return true;
	else return false;
}


void Grid::sortStencil(int f) {
	// Split stencil to 8 quadrants around the node
	vector<list<int> > quadrant; quadrant.resize(8);
	list<int>::iterator lit,lit2;
	Vec3D face2cell;
	int stencilSize=stencil.size();
	
	// Loop the stencil and start filling in the quadrants
	for (sit=stencil.begin();sit!=stencil.end();sit++) {
		centroid1=(*sit>=0) ? cell[*sit].centroid : ghost[-1*(*sit)-1].centroid;
		face2cell=centroid1-face[f].centroid;
		if (face2cell[0]>=0.) {
			if (face2cell[1]>=0.) {
				if (face2cell[2]>=0.) {
					// Quadrant 0	
					quadrant[0].push_back(*sit);
				} else {
					// Quadrant 1
					quadrant[1].push_back(*sit);
				}
			} else {
				if (face2cell[2]>=0.) {
					// Quadrant 2	
					quadrant[2].push_back(*sit);
				} else {
					// Quadrant 3
					quadrant[3].push_back(*sit);
				}
			}
		} else {
			if (face2cell[1]>=0.) {
				if (face2cell[2]>=0.) {
					// Quadrant 4
					quadrant[4].push_back(*sit);
				} else {
					// Quadrant 5
					quadrant[5].push_back(*sit);
				}
			} else {
				if (face2cell[2]>=0.) {
					// Quadrant 6	
					quadrant[6].push_back(*sit);
				} else {
					// Quadrant 7
					quadrant[7].push_back(*sit);
				}
			}	
		}
	}
	// Now sort entries in each quadrant according to the distance to the face center (closest first)
	face_center_Vec=face[f].centroid;
	currentGrid=this;
	for (int q=0;q<8;++q) quadrant[q].sort(compare_closest);
	
	stencil.clear();
	
	int non_empty_quadrant_count=0;
	for (int q=0;q<8;++q) {
		if (quadrant[q].size()!=0) non_empty_quadrant_count++;
	}
	
	int counter=0;
	int size_cutoff;
	if (dimension==1) {
		size_cutoff=2;
	} else if (dimension==2) {
		size_cutoff=6;
	} else {
		size_cutoff=12;
	}
	if (stencil_size_input<0) {
		size_cutoff=min(size_cutoff,stencilSize);
	} else {
		size_cutoff=min(stencil_size_input,stencilSize);
	}
	
	while (counter<size_cutoff) {
		for (int q=0;q<8;++q) {
			lit=quadrant[q].begin();
			if (lit!=quadrant[q].end()) {
				stencil.insert(*lit); counter++;
				lit2=lit; lit2++;
				if (lit2!=quadrant[q].end()) {
					centroid1=(*lit>=0) ? cell[*lit].centroid : ghost[-1*(*lit)-1].centroid;
					centroid2=(*lit2>=0) ? cell[*lit2].centroid : ghost[-1*(*lit2)-1].centroid;
					face2cell=centroid1-face[f].centroid;
					if (fabs(fabs(face2cell)-fabs(centroid2-face[f].centroid))/fabs(face2cell)<1.e-8) {
						stencil.insert(*lit2);
						quadrant[q].pop_front();	
						if (counter<stencilSize) counter++;
						if (size_cutoff<stencilSize) size_cutoff++;
					}
					
				}
				quadrant[q].pop_front();
				if (counter==size_cutoff) break;
			}
		}
	}
	quadrant.clear();
	
	return;
}

void Grid::interpolate_tetra(int f) {
	
	// Loop through the stencil and construct tetra combinations
	for (sit=stencil.begin();sit!=stencil.end();sit++) {
		centroid1=(*sit>=0) ? cell[*sit].centroid : ghost[-1*(*sit)-1].centroid;
		sit1=sit; sit1++;
		for (sit1=sit1;sit1!=stencil.end();sit1++) {
			centroid2=(*sit1>=0) ? cell[*sit1].centroid : ghost[-1*(*sit1)-1].centroid;
			sit2=sit1; sit2++;
			for (sit2=sit2;sit2!=stencil.end();sit2++) {
				centroid3=(*sit2>=0) ? cell[*sit2].centroid : ghost[-1*(*sit2)-1].centroid;
				sit3=sit2; sit3++;
				for (sit3=sit3;sit3!=stencil.end();sit3++) {
					centroid4=(*sit3>=0) ? cell[*sit3].centroid : ghost[-(*sit3)-1].centroid;
                                        tetra_volume=0.166666667*fabs(((centroid2-centroid1).cross(centroid3-centroid1)).dot(centroid4-centroid1));
                                        ave_edge=1./6.*(fabs(centroid1-centroid2)+fabs(centroid1-centroid3)+fabs(centroid1-centroid4)
                                                       +fabs(centroid2-centroid3)+fabs(centroid2-centroid4)+fabs(centroid3-centroid4));
					ave_centroid=0.25*(centroid1+centroid2+centroid3+centroid4);
                                        // Compare volume with that of an equilateral tetra

					// How close the tetra center to the node for which we are interpolating
					// Normalized by average edge length	
					closeness=fabs(face[f].centroid-ave_centroid)/ave_edge;
					closeness=max(closeness,1.e-1*ave_edge);
					closeness=1./closeness;
					// If it was an equilateral tri,skewness should be 1, thus the multiplier here.
					skewness=tri_area/(0.433*ave_edge*ave_edge);
					
					closeness=fabs(face[f].centroid-ave_centroid)/ave_edge;
					closeness=max(closeness,1.e-1*ave_edge);
					closeness=1./closeness;
					// If it was an equilateral tetra,skewness should be 1, thus the multiplier here.
					// Low skewness corresponds to highly skewed cells (I know, counter-intuitive)
					skewness=tetra_volume/(0.11785113*(pow(ave_edge,3)));
					if (skewness>volume_tolerance) {
						// Declare an interpolation tetra
						interpolation_tetra temp;
						temp.cell[0]=*sit; temp.cell[1]=*sit1; temp.cell[2]=*sit2; temp.cell[3]=*sit3;
						temp.weight=closeness*skewness;
						tetras.push_back(temp);
					}
				}
			}
		}
	} // Loop through stencil 
	if (tetras.size()==0) { // This really shouldn't happen
		cerr << "[E Rank=" << Rank << "] An interpolation tetra couldn't be found for face " << f << endl;
		cerr << "[E Rank=" << Rank << "] This really shouldn't happen " << endl;
		cerr << "[E Rank=" << Rank << "] Please report this bug !! " << endl;
		exit(1);
	} else { // Everyting is normal, go ahead
		// Normalize the weights
		weightSum=0.; 
		for (tet=tetras.begin();tet!=tetras.end();tet++) weightSum+=(*tet).weight;
		for (tet=tetras.begin();tet!=tetras.end();tet++) {
			(*tet).weight/=weightSum;
			// Form the linear system

			// Let's move to a new origin located at the first cell's centroid in the stencil
			// This improves accuracy for large domain sizes
			centroid1= ((*tet).cell[0]>=0) ? cell[(*tet).cell[0]].centroid : ghost[-1*(*tet).cell[0]-1].centroid;	
			for (int i=0;i<4;++i) {
				if ((*tet).cell[i]>=0) {
					for (int j=0;j<3;++j) a4[j][i]=cell[(*tet).cell[i]].centroid[j]-centroid1[j];
				} else {
					for (int j=0;j<3;++j) a4[j][i]=ghost[-((*tet).cell[i])-1].centroid[j]-centroid1[j];	
				}
			}

			a4[3][0]=1.; a4[3][1]=1.; a4[3][2]=1.; a4[3][3]=1.;
			b4[0]=face[f].centroid[0]-centroid1[0]; b4[1]=face[f].centroid[1]-centroid1[1]; b4[2]=face[f].centroid[2]-centroid1[2]; b4[3]=1.;
			// Solve the 4x4 linear system by Gaussion Elimination
			gelimd(a4,b4,weights4);
			// Let's see if the linear system solution is good
			weightSum2=0.;
			for (int i=0;i<4;++i) weightSum2+=weights4[i];
			if (fabs(weightSum2-1.)>1.e-8) {
				cerr << "[E Rank=" << Rank << "] Tetra interpolation weightSum=" << weightSum2 << " is not unity for face " << f  << endl;
				exit(1);
			}

			for (int i=0;i<4;++i) {
				if (face[f].average.find((*tet).cell[i])!=face[f].average.end()) { // if the cell contributing to the node average is already contained in the map
					face[f].average[(*tet).cell[i]]+=(*tet).weight*weights4[i];
				} else {
					face[f].average.insert(pair<int,double>((*tet).cell[i],(*tet).weight*weights4[i]));
				}
			}
		}
	}
	return;
	
}

void Grid::interpolate_tri(int f) {
	
	// Loop through the stencil and construct tri combinations
	for (sit=stencil.begin();sit!=stencil.end();sit++) {
		centroid1=(*sit>=0) ? cell[*sit].centroid : ghost[-(*sit)-1].centroid;
		sit1=sit; sit1++;
		for (sit1=sit1;sit1!=stencil.end();sit1++) {
			centroid2=(*sit1>=0) ? cell[*sit1].centroid : ghost[-(*sit1)-1].centroid;
			sit2=sit1; sit2++;
			for (sit2=sit2;sit2!=stencil.end();sit2++) {
				centroid3=(*sit2>=0) ? cell[*sit2].centroid : ghost[-(*sit2)-1].centroid;
				planeNormal=(centroid2-centroid1).cross(centroid3-centroid1);
				tri_area=0.5*fabs(planeNormal);
				planeNormal=planeNormal.norm();
				ave_centroid=1./3.*(centroid1+centroid2+centroid3);
				ave_edge=1./3.*(fabs(centroid1-centroid2)+fabs(centroid1-centroid3)+fabs(centroid2-centroid3));
				// How close the triangle center to the node for which we are interpolating
				// Normalized by average edge length
				closeness=fabs((face[f].centroid-face[f].centroid.dot(planeNormal)*planeNormal)-ave_centroid)/ave_edge;
				closeness=max(closeness,1.e-1*ave_edge);
				closeness=1./closeness;
				// If it was an equilateral tri,skewness should be 1, thus the multiplier here.
				skewness=tri_area/(0.433*ave_edge*ave_edge);
				if (skewness>area_tolerance) {
					// Declare an interpolation tetra
					interpolation_tri temp;
					temp.cell[0]=*sit; temp.cell[1]=*sit1; temp.cell[2]=*sit2; 
					temp.weight=closeness*skewness;
					tris.push_back(temp);
				}
					
				
			}
		}
	} // Loop through stencil 
	if (tris.size()==0) { // This really shouldn't happen
		cerr << "[E] An interpolation triangle couldn't be found for face " << f << endl;
		cerr << "[E] This really shouldn't happen " << endl;
		cerr << "[E] Please report this bug !! " << endl;
		exit(1);
	} else { // Everyting is normal, go ahead
		// Normalize the weights
		weightSum=0.; 
		for (tri=tris.begin();tri!=tris.end();tri++) weightSum+=(*tri).weight;
		for (tri=tris.begin();tri!=tris.end();tri++) {
			(*tri).weight/=weightSum;
			centroid1= ((*tri).cell[0]>=0) ? cell[(*tri).cell[0]].centroid : ghost[-1*(*tri).cell[0]-1].centroid;
			centroid2= ((*tri).cell[1]>=0) ? cell[(*tri).cell[1]].centroid : ghost[-1*(*tri).cell[1]-1].centroid;
			centroid3= ((*tri).cell[2]>=0) ? cell[(*tri).cell[2]].centroid : ghost[-1*(*tri).cell[2]-1].centroid;
		
			// Find out best choice for origin
			int choice;
			double metric1,metric2;
			choice=1;
			// Try centroid 1
			metric1=fabs(((centroid2-centroid1).norm()).dot((centroid3-centroid1).norm()));
			// Try centroid 2
			metric2=fabs(((centroid1-centroid2).norm()).dot((centroid3-centroid2).norm()));
			if (metric2<metric1) {choice=2; metric1=metric2;}
			// Try centroid 3
			metric2=fabs(((centroid1-centroid3).norm()).dot((centroid2-centroid3).norm()));
			if (metric2<metric1) choice=3;
			
			Vec3D origin;
			if (choice==1) {
				origin=centroid1;
				centroid1-=origin;
				centroid2-=origin;
				centroid3-=origin;
				basis1=centroid2;
				planeNormal=(basis1.cross(centroid3)).norm();
				basis2=-1.*(basis1.cross(planeNormal)).norm();
			} else if (choice==2) {
				origin=centroid2;
				centroid1-=origin;
				centroid2-=origin;
				centroid3-=origin;
				basis1=centroid1;
				planeNormal=(basis1.cross(centroid3)).norm();
			} else {
				origin=centroid3;
				centroid1-=origin;
				centroid2-=origin;
				centroid3-=origin;
				basis1=centroid1;
				planeNormal=(basis1.cross(centroid2)).norm();
			}

			basis2=-1.*(basis1.cross(planeNormal)).norm();
			pp=face[f].centroid-origin;
			pp-=pp.dot(planeNormal)*planeNormal;
			
			// Form the linear system
			a3[0][0]=centroid1.dot(basis1);
			a3[0][1]=centroid2.dot(basis1);
			a3[0][2]=centroid3.dot(basis1);
			a3[1][0]=centroid1.dot(basis2);
			a3[1][1]=centroid2.dot(basis2);
			a3[1][2]=centroid3.dot(basis2);
			a3[2][0]=1.;
			a3[2][1]=1.;
			a3[2][2]=1.;
			b3[0]=pp.dot(basis1);
			b3[1]=pp.dot(basis2);
			b3[2]=1.;
			
 			// Solve the 3x3 linear system by Gaussion Elimination
			gelimd(a3,b3,weights3);
			// Let's see if the linear system solution is good
			weightSum2=0.;
			for (int i=0;i<3;++i) weightSum2+=weights3[i];
			if (fabs(weightSum2-1.)>1.e-8) {
				cerr << "[W rank=" << Rank << "] Tri interpolation weightSum=" << setprecision(8) << weightSum2 << " is not unity for face " << f  << endl;
				//exit(1);
			}
			
			for (int i=0;i<3;++i) {
				if (face[f].average.find((*tri).cell[i])!=face[f].average.end()) { // if the cell contributing to the face average is already contained in the map
					face[f].average[(*tri).cell[i]]+=(*tri).weight*weights3[i];
				} else {
					face[f].average.insert(pair<int,double>((*tri).cell[i],(*tri).weight*weights3[i]));
				}
			}
		}
	}
	return;
}

void Grid::interpolate_line(int f) {
	distanceMin=1.e20;
	// Pick the two points in the stencil which are closest to the node
	for (sit=stencil.begin();sit!=stencil.end();sit++) {
		centroid3=(*sit>=0) ? cell[*sit].centroid : ghost[-(*sit)-1].centroid;
		line_distance=fabs(face[f].centroid-centroid3);
		if (line_distance<distanceMin) {
			distanceMin=line_distance;
			p1=*sit;
			centroid1=centroid3;
		}
		
	}
	distanceMin=1.e20;
	for (sit=stencil.begin();sit!=stencil.end();sit++) {
		if (*sit!=p1) {
			centroid3=(*sit>=0) ? cell[*sit].centroid : ghost[-(*sit)-1].centroid;
			line_distance=fabs(face[f].centroid-centroid3);
			if (line_distance<distanceMin) {
				distanceMin=line_distance;
				p2=*sit;
				centroid2=centroid3;
			}
		}
	}
	
	lineDirection=(centroid2-centroid1).norm();
	vector<double> weights; weights.resize(2);
	centroid2-=centroid1;
	Vec3D pn;
	pn=face[f].centroid-centroid1;
	double signof_pn=pn.dot(lineDirection)/fabs(pn.dot(lineDirection));
	pn=pn.dot(lineDirection)*lineDirection;
	
	weights[1]=fabs(pn)/fabs(centroid2)*signof_pn;
	weights[0]=1.-weights[1];
		
	face[f].average.insert(pair<int,double>(p1,weights[0]));
	face[f].average.insert(pair<int,double>(p2,weights[1]));
	
	weights.clear();
	
	return;
}

void Grid::nodeAverages() {
	map<int,double>::iterator it;
	
	// Get node averaging weights based on the face averaging metrics
	for (int n=0;n<nodeCount;++n) {
		node[n].average.clear();
		vector<double> faceWeights;
		weightSum=0.;
		double weight;
		// Loop faces neighboring the current node
		for (int nf=0;nf<node[n].faces.size();++nf) {
			// Use inverse distance square
			weight=fabs(face[node[n].faces[nf]].centroid-node[n]);
			weight=1./(weight*weight);
			faceWeights.push_back(weight);
			weightSum+=weight;
		}
		// Normalize weights
		for (int i=0;i<faceWeights.size();++i) faceWeights[i]/=weightSum;
		// Loop faces neighboring the current node
		for (int nf=0;nf<node[n].faces.size();++nf) {
			int f=node[n].faces[nf];
			for ( it=face[f].average.begin() ; it != face[f].average.end(); it++ ) {
				
				if (node[n].average.find((*it).first)!=node[n].average.end()) { // if the cell contributing to the face average is already contained in the node average map
					node[n].average[(*it).first]+=faceWeights[nf]*(*it).second;
				} else {
					node[n].average.insert(pair<int,double>((*it).first,faceWeights[nf]*(*it).second));
				}
			}
		}
	}
	
	return;
}

int gelimd(vector<vector<double> > &a,vector<double> &b,vector<double> &x) {
	
	int n=b.size();
	double tmp,pvt;
	int i,j,k;

	for (i=0;i<n;i++) {             // outer loop on rows
		pvt = a[i][i];              // get pivot value
		if (fabs(pvt) < EPS) {
			for (j=i+1;j<n;j++) {
				if(fabs(pvt = a[j][i]) >= EPS) break;
			}
			if (fabs(pvt) < EPS) return 1;     // nowhere to run!
			a[i].swap(a[j]);	// swap matrix rows...
			tmp=b[j];               // ...and result vector
			b[j]=b[i];
			b[i]=tmp;
		}
// (virtual) Gaussian elimination of column
		for (k=i+1;k<n;k++) {       // alt: for (k=n-1;k>i;k--)
			tmp = a[k][i]/pvt;
			for (j=i+1;j<n;j++) {   // alt: for (j=n-1;j>i;j--)
				a[k][j] -= tmp*a[i][j];
			}
			b[k] -= tmp*b[i];
		}
	}
// Do back substitution
	for (i=n-1;i>=0;i--) {
		x[i]=b[i];
		for (j=n-1;j>i;j--) {
			x[i] -= a[i][j]*x[j];
		}
		x[i] /= a[i][i];
	}
	return 0;
}

/*
void Grid::nodeAverages_idw() {
	int c,g;
	double weight=0.;
	Vec3D face2cell,node2ghost;
	map<int,double>::iterator it;

	for (int n=0;n<nodeCount;++n) {
		node[n].average.clear();
		// Add contributions from real cells
		weightSum=0.;
		for (int nc=0;nc<node[n].cells.size();++nc) {
			c=node[n].cells[nc];
			face2cell=node[n]-cell[c].centroid;
			weight=1./(face2cell.dot(face2cell));
			//weight=1./fabs(face2cell);
			node[n].average.insert(pair<int,double>(c,weight));
			weightSum+=weight;
		}
		// Add contributions from ghost cells		
		for (int ng=0;ng<node[n].ghosts.size();++ng) {
			g=node[n].ghosts[ng];
			node2ghost=node[n]-ghost[g].centroid;
			weight=1./(node2ghost.dot(node2ghost));
			//weight=1./fabs(node2ghost);
			node[n].average.insert(pair<int,double>(-g-1,weight));
			weightSum+=weight;
		}
		for ( it=node[n].average.begin() ; it != node[n].average.end(); it++ ) {
			(*it).second/=weightSum;
		}
	}
} // end Grid::nodeAverages_idw()

*/
