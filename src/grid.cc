#include <mpi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <map>
#include <iomanip>
#include<cmath>
using namespace std;
#include <cgnslib.h>
#include <parmetis.h>


#include "grid.h"
#include "bc.h"

extern Grid grid;
extern BC bc;
extern int np, rank;

double superbee(double a, double b);
double minmod(double a, double b);

Grid::Grid() {
	;
}

int Grid::read(string fname) {
	fstream file;
	fileName=fname;
	file.open(fileName.c_str());
	if (file.is_open()) {
		if (rank==0) cout << "* Found grid file " << fileName  << endl;
		file.close();
		ReadCGNS();
		return 1;
	} else {
		if (rank==0) cerr << "[!!] Grid file "<< fileName << " could not be found." << endl;
		return 0;
	}
}

int Grid::ReadCGNS() {



	int fileIndex,baseIndex,zoneIndex,sectionIndex,nBases,nZones,nSections,nBocos;
	char zoneName[20],sectionName[20]; //baseName[20]
	//  int nBases,cellDim,physDim;
	int size[3];
	globalNodeCount=0;
	globalCellCount=0;
	globalFaceCount=0;
	
	// Open the grid file for reading
	cg_open(fileName.c_str(),MODE_READ,&fileIndex);

	// Read number of bases
	cg_nbases(fileIndex,&nBases);

	if (rank==0) cout << "Number of Bases= " << nBases << endl;

	//for (int baseIndex=1;baseIndex<=nBases;++baseIndex) {
	// For now assuming there is only one base as I don't know in which cases there would be more
	baseIndex=1;
	// Read number of zones
	cg_nzones(fileIndex,baseIndex,&nZones);
	int zoneNodeCount[nZones],zoneCellCount[nZones];
	if (rank==0) cout << "Number of Zones= " << nZones << endl;
	std::vector<int> elemConnIndex,elemConnectivity;
	std::vector<ElementType_t> elemTypes;
	std::vector<double> coordX[nZones],coordY[nZones],coordZ[nZones];
	std::vector<int> zoneCoordMap[nZones];

	// Get total number of boundary conditions
	int totalnBocos=0;
	for (int zoneIndex=1;zoneIndex<=nZones;++zoneIndex) {
		cg_nbocos(fileIndex,baseIndex,zoneIndex,&nBocos);
		totalnBocos+=nBocos;
	}
	std::vector<int> bocos[totalnBocos],bocoConnectivity[totalnBocos];
	int bocoCount=0;
	for (int zoneIndex=1;zoneIndex<=nZones;++zoneIndex) {
		// Read the zone
		cg_zone_read(fileIndex,baseIndex,zoneIndex,zoneName,size);
		// These are the number of cells and nodes in that zone
		zoneNodeCount[zoneIndex-1]=size[0];
		zoneCellCount[zoneIndex-1]=size[1];
		// Read number of sections
		cg_nsections(fileIndex,baseIndex,zoneIndex,&nSections);
		cg_nbocos(fileIndex,baseIndex,zoneIndex,&nBocos);
		if (rank==0) cout << "In Zone " << zoneName << endl;
		if (rank==0) cout << "...Number of Nodes= " << size[0] << endl;
		if (rank==0) cout << "...Number of Cells= " << size[1] << endl;
		if (rank==0) cout << "...Number of Sections= " << nSections << endl;
		if (rank==0) cout << "...Number of Boundary Conditions= " << nBocos << endl;
			
		// Read the node coordinates
		int nodeStart[3],nodeEnd[3];
		nodeStart[0]=nodeStart[1]=nodeStart[2]=1;
		nodeEnd[0]=nodeEnd[1]=nodeEnd[2]=size[0];
				
		double x[size[0]],y[size[0]],z[size[0]];
		cg_coord_read(fileIndex,baseIndex,zoneIndex,"CoordinateX",RealDouble,nodeStart,nodeEnd,&x);
		cg_coord_read(fileIndex,baseIndex,zoneIndex,"CoordinateY",RealDouble,nodeStart,nodeEnd,&y);
		cg_coord_read(fileIndex,baseIndex,zoneIndex,"CoordinateZ",RealDouble,nodeStart,nodeEnd,&z);
		for (int i=0;i<size[0];++i) {
			coordX[zoneIndex-1].push_back(x[i]);
			coordY[zoneIndex-1].push_back(y[i]);
			coordZ[zoneIndex-1].push_back(z[i]);
			zoneCoordMap[zoneIndex-1].push_back(-1);
		}

		// In case there are multiple connected zones, collapse the repeated nodes and fix the node numbering
		if (zoneIndex==1) {
			for (int c=0;c<coordX[0].size();++c) {
				zoneCoordMap[0][c]=globalNodeCount;
				globalNodeCount++;
			}
		}
		for (int z=0;z<zoneIndex-1;++z) {
			for (int c=0;c<coordX[zoneIndex-1].size();++c) {
				bool foundFlag=false;
				for (int c2=0;c2<coordX[z].size();++c2) {
					if (fabs(coordX[zoneIndex-1][c]-coordX[z][c2])<1.e-7 && fabs(coordY[zoneIndex-1][c]-coordY[z][c2])<1.e-7 && fabs(coordZ[zoneIndex-1][c]-coordZ[z][c2])<1.e-7) {
						zoneCoordMap[zoneIndex-1][c]=zoneCoordMap[z][c2];
						foundFlag=true;
					}
				}
				if (!foundFlag) {
					zoneCoordMap[zoneIndex-1][c]=globalNodeCount;
					globalNodeCount++;
				}
			}
		}

		int bc_range[nBocos][2];
		for (int bocoIndex=1;bocoIndex<=nBocos;++bocoIndex) {
			int dummy;
			cg_boco_read(fileIndex,baseIndex,zoneIndex,bocoIndex,bc_range[bocoIndex-1],&dummy);
		} // for boco
			
			// Loop sections within the zone
		for (int sectionIndex=1;sectionIndex<=nSections;++sectionIndex) {
			ElementType_t elemType;
			int elemNodeCount,elemStart,elemEnd,nBndCells,parentFlag;
				// Read the section 
			cg_section_read(fileIndex,baseIndex,zoneIndex,sectionIndex,sectionName,&elemType,&elemStart,&elemEnd,&nBndCells,&parentFlag);

			switch (elemType) {
				case TRI_3:
					elemNodeCount=3; break;
				case QUAD_4:
					elemNodeCount=4; break;
				case TETRA_4:
					elemNodeCount=4; break;
				case PENTA_6:
					elemNodeCount=6; break;
				case HEXA_8:
					elemNodeCount=8; break;
			} //switch
			int elemNodes[elemEnd-elemStart+1][elemNodeCount];
				// Read element node connectivities
			cg_elements_read(fileIndex,baseIndex,zoneIndex,sectionIndex,*elemNodes,0);
				// Only pick the volume elements

			if (elemType==TETRA_4 | elemType==PENTA_6 | elemType==HEXA_8 ) {
				cout << "   ...Found Volume Section " << sectionName << endl;
					// elements array serves as an start index for connectivity list elemConnectivity

				for (int elem=0;elem<=(elemEnd-elemStart);++elem) {
					elemConnIndex.push_back(elemConnectivity.size());
					elemTypes.push_back(elemType);
					for (int n=0;n<elemNodeCount;++n) elemConnectivity.push_back(zoneCoordMap[zoneIndex-1][elemNodes[elem][n]-1]);
				}

				globalCellCount+=(elemEnd-elemStart+1);
			} else {
				bool bcFlag=false;
				for (int nbc=0;nbc<nBocos;++nbc) {
					if (elemStart==bc_range[nbc][0] && elemEnd==bc_range[nbc][1]) {
						bcFlag=true;
						break;
					}
				}
				if (bcFlag) {
					cout << "   ...Found BC Section " << sectionName << endl;
					for (int elem=0;elem<=(elemEnd-elemStart);++elem) {
						bocos[bocoCount].push_back(bocoConnectivity[bocoCount].size());
						for (int n=0;n<elemNodeCount;++n) bocoConnectivity[bocoCount].push_back(zoneCoordMap[zoneIndex-1][elemNodes[elem][n]-1]);
					}
					bocoCount+=1;
				}
			}// if
		} // for section
	} // for zone
	//} // for base

	if (rank==0) cout << "Total Node Count= " << globalNodeCount << endl;
	// Merge coordinates of the zones
	double x[globalNodeCount],y[globalNodeCount],z[globalNodeCount];
	int counter=0;
	// for zone 0
	for (int n=0;n<coordX[0].size();++n) {
		x[counter]=coordX[0][n];
		y[counter]=coordY[0][n];
		z[counter]=coordZ[0][n];
		counter++;
	}
	for (int zone=1;zone<nZones;++zone) {
		for (int n=0;n<coordX[zone].size();++n) {
			if (zoneCoordMap[zone][n]>zoneCoordMap[zone-1][zoneCoordMap[zone-1].size()-1]) {
				x[counter]=coordX[zone][n];
				y[counter]=coordY[zone][n];
				z[counter]=coordZ[zone][n];
				counter++;
			}
		}
	}
	if (counter!=globalNodeCount) cerr << "[E] counter is different from globalNodeCount" << endl;
	if (rank==0) cout << "Total Cell Count= " << globalCellCount << endl;
	// Store element node counts
	int elemNodeCount[globalCellCount];
	for (unsigned int c=0;c<globalCellCount-1;++c) {
		elemNodeCount[c]=elemConnIndex[c+1]-elemConnIndex[c];
	}
	elemNodeCount[globalCellCount-1]=elemConnectivity.size()-elemConnIndex[globalCellCount-1];
	
	// Initialize the partition sizes
	cellCount=floor(globalCellCount/np);
	int baseCellCount=cellCount;
	unsigned int offset=rank*cellCount;
	if (rank==np-1) cellCount=cellCount+globalCellCount-np*cellCount;

	//Implementing Parmetis
	/* ParMETIS_V3_PartMeshKway(idxtype *elmdist, idxtype *eptr, idxtype *eind, idxtype *elmwgt, int *wgtflag, int *numflag, int *ncon, int * ncommonnodes, int *nparts, float *tpwgts, float *ubvec, int *options, int *edgecut, idxtype *part, MPI_Comm) */

	/*  Definining variables
	elmdist- look into making the 5 arrays short int (for performance
		on 64 bit arch)
	eptr- like xadf
	eind- like adjncy
	elmwgt- null (element weights)
	wgtflag- 0 (no weights, can take value of 0,1,2,3 see documentation)
	numflag- 0 C-style numbers, 1 Fortran-style numbers
	ncon- 1  ( # of constraints)
	ncommonnodes- 4 ( we can probably put this to 3)
	nparts- # of processors (Note: BE CAREFUL if != to # of proc)
	tpwgts- 
	ubvec-  (balancing constraints,if needed 1.05 is a good value)
	options- [0 1 15] for default
	edgecut- output, # of edges cut (measure of communication)
	part- output, where our elements should be
	comm- most likely MPI_COMM_WORLD
	*/

	idxtype elmdist[np+1];
	idxtype *eptr;
	eptr = new idxtype[cellCount+1];
	idxtype *eind;
	int eindSize=0;
	if ((offset+cellCount)==globalCellCount) {
		eindSize=elemConnectivity.size()-elemConnIndex[offset];
	}
	else {
		eindSize=elemConnIndex[offset+cellCount]-elemConnIndex[offset]+1;
	}
	eind = new idxtype[eindSize];
	idxtype* elmwgt = NULL;
	int wgtflag=0; // no weights associated with elem or edges
	int numflag=0; // C-style numbering
	int ncon=1; // # of weights or constraints
	int ncommonnodes; ncommonnodes=3; // set to 3 for tetrahedra or mixed type

	float tpwgts[np];
	for (unsigned int p=0; p<np; ++p) tpwgts[p]=1./float(np);
	float ubvec=1.02;
	int options[3]; // default values for timing info set 0 -> 1

	options[0]=0; options[1]=1; options[2]=15;
	int edgecut ; // output
	idxtype* part = new idxtype[cellCount];

	for (unsigned int p=0;p<np;++p) elmdist[p]=p*floor(globalCellCount/np);
	elmdist[np]=globalCellCount;// Note this is because #elements mod(np) are all on last proc

	for (unsigned int c=0; c<cellCount;++c) {
		eptr[c]=elemConnIndex[offset+c]-elemConnIndex[offset];
	}
	if ((offset+cellCount)==globalCellCount) {
		 eptr[cellCount]=elemConnectivity.size()-elemConnIndex[offset];
	} else {
		eptr[cellCount]=elemConnIndex[offset+cellCount]-elemConnIndex[offset];
	}

	for (unsigned int i=0; i<eindSize; ++i) {
			eind[i]=elemConnectivity[elemConnIndex[offset]+i];
	}

	ompi_communicator_t* commWorld=MPI_COMM_WORLD;

	ParMETIS_V3_PartMeshKway(elmdist,eptr,eind, elmwgt,
	                         &wgtflag, &numflag, &ncon, &ncommonnodes,
	                         &np, tpwgts, &ubvec, options, &edgecut,
	                         part,&commWorld) ;
	
	delete[] eptr;
	delete[] eind;


	// Distribute the part list to each proc
	// Each proc has an array of length globalCellCount which says the processor number that cell belongs to [cellMap]
	int recvCounts[np];
	int displs[np];
	for (int p=0;p<np;++p) {
		recvCounts[p]=baseCellCount;
		displs[p]=p*baseCellCount;
	}
	recvCounts[np-1]=baseCellCount+globalCellCount-np*baseCellCount;
	int cellMap[globalCellCount];
	//cellMap of a cell returns which processor it is assigned to
	MPI_Allgatherv(part,cellCount,MPI_INT,cellMap,recvCounts,displs,MPI_INT,MPI_COMM_WORLD);

	// Find new local cellCount after ParMetis distribution
	cellCount=0.;
	int otherCellCounts[np]; 
	for (unsigned int p=0;p<np;p++) otherCellCounts[p]=0; 
	
	for (unsigned int c=0;c<globalCellCount;++c) {
		otherCellCounts[cellMap[c]]+=1;
		if (cellMap[c]==rank) ++cellCount;
	}
	cout << "* Processor " << rank << " has " << cellCount << " cells" << endl;
		
	//node.reserve(nodeCount/np);
	face.reserve(cellCount);
	cell.reserve(cellCount);

	// Create the nodes and cells for each partition
	// Run through the list of cells and check if it belongs to current partition
	// Loop through the cell's nodes
	// Mark the visited nodes so that no duplicates are created (nodeFound array).
	bool nodeFound[globalNodeCount];
	unsigned int nodeMap[globalNodeCount];

	for (unsigned int n=0;n<globalNodeCount;++n) nodeFound[n]=false;
	nodeCount=0;
	
	for (unsigned int c=0;c<globalCellCount;++c) {
		if (cellMap[c]==rank) {
			unsigned int cellNodes[elemNodeCount[c]];
			for (unsigned int n=0;n<elemNodeCount[c];++n) {

				if (!nodeFound[elemConnectivity[elemConnIndex[c]+n]]) {
					Node temp;
					temp.id=nodeCount;
					temp.globalId=elemConnectivity[elemConnIndex[c]+n];
					temp.comp[0]=x[temp.globalId];
					temp.comp[1]=y[temp.globalId];
					temp.comp[2]=z[temp.globalId];
					node.push_back(temp);
					nodeFound[temp.globalId]=true;
					nodeMap[temp.globalId]=temp.id;
					++nodeCount;
				}
				cellNodes[n]=nodeMap[elemConnectivity[elemConnIndex[c]+n]];
			}

			Cell temp;
			temp.Construct(elemTypes[c],cellNodes);
			temp.globalId=c;
			cell.push_back(temp);

		}
	}

	cout << "* Processor " << rank << " has created its cells and nodes" << endl;

	//Create the Mesh2Dual inputs
	//idxtype elmdist [np+1] (stays the same size)
	eptr = new idxtype[cellCount+1];
	eindSize=0;
	for (unsigned int c=0;c<cellCount;++c) {
		eindSize+=cell[c].nodeCount;
	}
	eind = new idxtype[eindSize];
	// numflag and ncommonnodes previously defined
	ncommonnodes=1;
	idxtype* xadj;
	idxtype* adjncy;

	elmdist[0]=0;
	for (unsigned int p=1;p<=np;p++) elmdist[p]=otherCellCounts[p-1]+elmdist[p-1];
	eptr[0]=0;
	for (unsigned int c=1; c<=cellCount;++c) eptr[c]=eptr[c-1]+cell[c-1].nodeCount;
	int eindIndex=0;
	for (unsigned int c=0; c<cellCount;c++){
		for (unsigned int cn=0; cn<cell[c].nodeCount; ++cn) {
			eind[eindIndex]=cell[c].node(cn).globalId;
			++eindIndex;
		}	
	}

	ParMETIS_V3_Mesh2Dual(elmdist, eptr, eind, &numflag, &ncommonnodes, &xadj, &adjncy, &commWorld);
	
	// Construct the list of cells for each node
	bool flag;
	for (unsigned int c=0;c<cellCount;++c) {
		unsigned int n;
		for (unsigned int cn=0;cn<cell[c].nodeCount;++cn) {
			n=cell[c].nodes[cn];
			flag=false;
			for (unsigned int i=0;i<node[n].cells.size();++i) {
				if (node[n].cells[i]==c) {
					flag=true;
					break;
				}
			}
			if (!flag) {
				node[n].cells.push_back(c);
			}
		}
	}

	cout << "* Processor " << rank << " has computed its node-cell connectivity" << endl;
	
	// Construct the list of neighboring cells for each cell
	int c2;
	for (unsigned int c=0;c<cellCount;++c) {
		unsigned int n;
		for (unsigned int cn=0;cn<cell[c].nodeCount;++cn) {
			n=cell[c].nodes[cn];
			for (unsigned int nc=0;nc<node[n].cells.size();++nc) {
				c2=node[n].cells[nc];
				flag=false;
				for (unsigned int cc=0;cc<cell[c].neighborCells.size();++cc) {
					if(cell[c].neighborCells[cc]==c2) {
						flag=true;
						break;
					}
				}
				if (!flag) cell[c].neighborCells.push_back(c2);
			} // end node cell loop
		} // end cell node loop
		cell[c].neighborCellCount=cell[c].neighborCells.size();
	} // end cell loop
	
	// Set face connectivity lists
	int hexaFaces[6][4]= {
		{0,3,2,1},
		{4,5,6,7},
		{1,2,6,5},
		{0,4,7,3},
		{1,5,4,0},
		{2,3,7,6}
	};

	int prismFaces[5][4]= {
		{0,2,1,0},
		{3,4,5,0},
		{0,3,5,2},
		{1,2,5,4},
		{0,1,4,3},
	};

	int tetraFaces[4][3]= {
		{0,2,1},
		{1,2,3},
		{0,3,2},
		{0,1,3}
	};


	// Search and construct faces
	faceCount=0;

	int boundaryFaceCount[totalnBocos];
	for (int boco=0;boco<totalnBocos;++boco) boundaryFaceCount[boco]=0;

	
	// Loop through all the cells
	double timeRef, timeEnd;
	if (rank==0) timeRef=MPI_Wtime();
	
	for (unsigned int c=0;c<cellCount;++c) {
		// Loop through the faces of the current cell
		for (unsigned int cf=0;cf<cell[c].faceCount;++cf) {
			Face tempFace;
			unsigned int *tempNodes;
			switch (elemTypes[c]) {
				case TETRA_4:
					tempFace.nodeCount=3;
					tempNodes= new unsigned int[3];
					break;
				case PENTA_6:
					if (cf<2) {
						tempFace.nodeCount=3;
						tempNodes= new unsigned int[3];
					} else {
						tempFace.nodeCount=4;
						tempNodes= new unsigned int[4];
					}
					break;
				case HEXA_8:
					tempFace.nodeCount=4;
					tempNodes= new unsigned int[4];
					break;
			}
			tempFace.id=faceCount;
			// Assign current cell as the parent cell
			tempFace.parent=c;
			// Assign boundary type as internal by default, will be overwritten later
			tempFace.bc=-1;
			// Store the nodes of the current face
			//if (faceCount==face.capacity()) face.reserve(int (face.size() *0.10) +face.size()) ; //TODO check how the size grows by default

			for (unsigned int fn=0;fn<tempFace.nodeCount;++fn) {
				switch (elemTypes[c]) {
					case TETRA_4: tempNodes[fn]=cell[c].node(tetraFaces[cf][fn]).id; break;
					case PENTA_6: tempNodes[fn]=cell[c].node(prismFaces[cf][fn]).id; break;
					case HEXA_8: tempNodes[fn]=cell[c].node(hexaFaces[cf][fn]).id; break;
				}
			}
			// Find the neighbor cell
			bool internal=false;
			bool unique=true;
			for (unsigned int nc=0;nc<node[tempNodes[0]].cells.size();++nc) {
				unsigned int i=node[tempNodes[0]].cells[nc];
				if (i!=c && cell[i].HaveNodes(tempFace.nodeCount,tempNodes)) {
					if (i>c) {
						tempFace.neighbor=i;
						internal=true;
					} else {
						unique=false;
					}
				}
			}
			if (unique) {
				for (unsigned int fn=0;fn<tempFace.nodeCount;++fn) tempFace.nodes.push_back(tempNodes[fn]);
				if (!internal) {
					bool match;
					std::set<int> fnodes,bfnodes;
					std::set<int>::iterator fnodes_it,bfnodes_it;
					for (unsigned int i=0;i<tempFace.nodeCount;++i) fnodes.insert(tempFace.node(i).globalId);
					tempFace.bc=-2; // unassigned boundary face
					int bfnodeCount;
					for (int boco=0;boco<totalnBocos;++boco) { // for each boundary condition region defined in grid file
						for (int bf=0;bf<bocos[boco].size();++bf) { // for each boundary face in current region
							if (bocoConnectivity[boco][bocos[boco][bf]]!=-1) {
								if (bf==bocos[boco].size()-1) { bfnodeCount=bocoConnectivity[boco].size()-bocos[boco][bf]; } else { bfnodeCount=bocos[boco][bf+1]-bocos[boco][bf]; }
								if (bfnodeCount==tempFace.nodeCount) { // if boco face and temp face has the same number of nodes
									bfnodes.clear();
									for (unsigned int i=0;i<tempFace.nodeCount;++i) bfnodes.insert(bocoConnectivity[boco][bocos[boco][bf]+i]);
									match=true;
									fnodes_it=fnodes.begin(); bfnodes_it=bfnodes.begin();
									for (unsigned int i=0;i<tempFace.nodeCount;++i) {
										if (*fnodes_it!=*bfnodes_it) { // if the sets are not equivalent
											match=false;
											break;
										}
										fnodes_it++;bfnodes_it++;
									}
									if (match) {
										tempFace.bc=boco;
										boundaryFaceCount[boco]++;
										bocoConnectivity[boco][bocos[boco][bf]]=-1; // this is destroying the bocos array
										break;
									}
								} // if same number of nodes
							} // if boundary face is not already assigned 
						} // for boco face
						if (tempFace.bc!=-2) break;
					} // for boco
				} // if not internal
				
				face.push_back(tempFace);
				cell[c].faces.push_back(tempFace.id);
				if (internal) cell[tempFace.neighbor].faces.push_back(tempFace.id);
				++faceCount;
			}
			delete [] tempNodes;
		} //for face cf
	} // for cells c

	cout << "[I rank=" << rank << "] Number of Faces=" << faceCount << endl;
	for (int boco=0;boco<totalnBocos;++boco) cout << "[I rank=" << rank << "] Number of Faces on BC_" << boco+1 << "=" << boundaryFaceCount[boco] << endl;
	
	if (rank==0) {
		timeEnd=MPI_Wtime();
		cout << "* Processor 0 took " << timeEnd-timeRef << " sec to find its faces" << endl;
	}
	
	// Determine and mark faces adjacent to other partitions
	// Create ghost elemets to hold the data from other partitions
	ghostCount=0;

	if (np==1) {
		for (unsigned int c=0;c<cellCount;++c) {
			cell[c].ghostCount=cell[c].ghosts.size();
		} // end cell loop
	} else {

		int counter=0;
		int cellCountOffset[np];
		
		for (unsigned int p=0;p<np;++p) {
			cellCountOffset[p]=counter;
			counter+=otherCellCounts[p];
		}
		
		// Now find the metis2global mapping
		int metis2global[globalCellCount];
		int counter2[np];
		for (unsigned int p=0;p<np;++p) counter2[p]=0;
		for (unsigned int c=0;c<globalCellCount;++c) {
			metis2global[cellCountOffset[cellMap[c]]+counter2[cellMap[c]]]=c;
			counter2[cellMap[c]]++;
		}

		int foundFlag[globalCellCount];
		for (unsigned int c=0; c<globalCellCount; ++c) foundFlag[c]=0;

		unsigned int parent, metisIndex, gg, matchCount;
		map<unsigned int,unsigned int> ghostGlobal2local;
		
		map<int,set<int> > nodeCellSet;
		Vec3D nodeVec;
		
		for (unsigned int f=0; f<faceCount; ++f) {
			if (face[f].bc==-2) { // if an assigned boundary face
				parent=face[f].parent;
				for (unsigned int adjCount=0;adjCount<(xadj[parent+1]-xadj[parent]);++adjCount)  {
					metisIndex=adjncy[xadj[parent]+adjCount];
					gg=metis2global[metisIndex];

					if (metisIndex<cellCountOffset[rank] || metisIndex>=(cellCount+cellCountOffset[rank])) {
						matchCount=0;
						for (unsigned int fn=0;fn<face[f].nodeCount;++fn) {
							set<int> tempSet;
							nodeCellSet.insert(pair<unsigned int,set<int> >(face[f].nodes[fn],tempSet) );
							for (unsigned int gn=0;gn<elemNodeCount[gg];++gn) {
								if (elemConnectivity[elemConnIndex[gg]+gn]==face[f].node(fn).globalId) {
									nodeCellSet[face[f].nodes[fn]].insert(gg);
									++matchCount;
								}
							}
						}
						if (matchCount>0 && foundFlag[gg]==0) {
							if (matchCount>=3) foundFlag[gg]=3;
							if (matchCount<3) foundFlag[gg]=matchCount;
							Ghost temp;
							temp.globalId=gg;
							temp.partition=cellMap[gg];
							// Calculate the centroid
							temp.centroid=0.;
							for (unsigned int gn=0;gn<elemNodeCount[gg];++gn) {
								nodeVec.comp[0]=x[elemConnectivity[elemConnIndex[gg]+gn]];
								nodeVec.comp[1]=y[elemConnectivity[elemConnIndex[gg]+gn]];
								nodeVec.comp[2]=z[elemConnectivity[elemConnIndex[gg]+gn]];
								temp.centroid+=nodeVec;
							}
							temp.centroid/=double(elemNodeCount[gg]);
							ghost.push_back(temp);
							ghostGlobal2local.insert(pair<unsigned int,unsigned int>(gg,ghostCount));
							++ghostCount;
						}
						if (matchCount>=3) {
							foundFlag[gg]=3;
							face[f].bc=-1*ghostGlobal2local[gg]-3;
						} 
					}
				}
					
			}
		}

		map<int,set<int> >::iterator mit;
		set<int>::iterator sit;
		for ( mit=nodeCellSet.begin() ; mit != nodeCellSet.end(); mit++ ) {
			for ( sit=(*mit).second.begin() ; sit != (*mit).second.end(); sit++ ) {
				node[(*mit).first].ghosts.push_back(ghostGlobal2local[*sit]);
				//cout << (*mit).first << "\t" << *sit << endl; // DEBUG
			}	
		}
		
		// Construct the list of neighboring ghosts for each cell
		int g;
		bool flag;
		for (unsigned int c=0;c<cellCount;++c) {
			unsigned int n;
			for (unsigned int cn=0;cn<cell[c].nodeCount;++cn) {
				n=cell[c].nodes[cn];
				for (unsigned int ng=0;ng<node[n].ghosts.size();++ng) {
					g=node[n].ghosts[ng];
					flag=false;
					for (unsigned int cg=0;cg<cell[c].ghosts.size();++cg) {
						if(cell[c].ghosts[cg]==g) {
							flag=true;
							break;
						}
					} // end cell ghost loop
					if (flag==false) {
						cell[c].ghosts.push_back(g);
						ghost[g].cells.push_back(c);
					}
				} // end node ghost loop
			} // end cell node loop
			cell[c].ghostCount=cell[c].ghosts.size();
		} // end cell loop
		
		
		
	} // if (np!=1) 
	
	cout << "* Processor " << rank << " has " << ghostCount << " ghost cells at partition boundaries" << endl;
	
// Now loop through faces and calculate centroids and areas
	for (unsigned int f=0;f<faceCount;++f) {
		Vec3D centroid=0.;
		Vec3D areaVec=0.;
		for (unsigned int n=0;n<face[f].nodeCount;++n) {
			centroid+=face[f].node(n);
		}
		centroid/=face[f].nodeCount;
		face[f].centroid=centroid;
		for (unsigned int n=0;n<face[f].nodeCount-1;++n) {
			areaVec+=0.5* (face[f].node(n)-centroid).cross(face[f].node(n+1)-centroid);
		}
		areaVec+=0.5* (face[f].node(face[f].nodeCount-1)-centroid).cross(face[f].node(0)-centroid);
		if (areaVec.dot(centroid-cell[face[f].parent].centroid) <0.) {
			// [TBM] Need to swap the face and reflect the area vector
		//	cout << "face " << f << " should be swapped" << endl;
		}
		face[f].area=fabs(areaVec);
		face[f].normal=areaVec/face[f].area;
	}

// Loop through the cells and calculate the volumes and length scales
	double totalVolume=0.;
	for (unsigned int c=0;c<cellCount;++c) {
		cell[c].lengthScale=1.e20;
		double volume=0.,height;
		unsigned int f;
		for (unsigned int cf=0;cf<cell[c].faceCount;++cf) {
			// FIXME Is this a generic volume formula?
			f=cell[c].faces[cf];
			height=fabs(face[f].normal.dot(face[f].centroid-cell[c].centroid));
			volume+=1./3.*face[f].area*height;
			cell[c].lengthScale=min(cell[c].lengthScale,height);
		}
		cell[c].volume=volume;
		totalVolume+=volume;
	}
	cout << "* Processor " << rank << " Total Volume: " << totalVolume << endl;
	return 0;

}


Node::Node(double x, double y, double z) {
	comp[0]=x;
	comp[1]=y;
	comp[2]=z;
}

Cell::Cell(void) {
	;
}

int Cell::Construct(const ElementType_t elemType, unsigned int nodeList[]) {

	switch (elemType) {
		case TETRA_4:
			faceCount=4;
			nodeCount=4;
			break;
		case PENTA_6:
			faceCount=5;
			nodeCount=6;
			break;
		case HEXA_8:
			faceCount=6;
			nodeCount=8;
			break;
	}
	type=elemType;
	nodes.reserve(nodeCount);
	if (nodeCount==0) {
		cerr << "[!! proc " << rank << " ] Number of nodes of the cell must be specified before allocation" << endl;
		return -1;
	} else {
		centroid=0.;
		for (unsigned int i=0;i<nodeCount;++i) {
			nodes.push_back(nodeList[i]);
			centroid+=node(i);
		}
		centroid/=nodeCount;
		return 0;
	}
}

bool Cell::HaveNodes(unsigned int &nodelistsize, unsigned int nodelist []) {
	unsigned int matchCount=0,nodeId;
	for (unsigned int j=0;j<nodeCount;++j) {
		nodeId=node(j).id;
		for (unsigned int i=0;i<nodelistsize;++i) {
			if (nodelist[i]==nodeId) ++matchCount;
			if (matchCount==3) return true;
		}
	}
	return false;
}

int Grid::face_exists(int &parentCell) {
	//int s=face.size();
	for (int f=0;f<faceCount;++f) {
		if (face[f].parent==parentCell) return 1;
	}
	return 0;
}

Node& Cell::node(int n) {
	return grid.node[nodes[n]];
};

Face& Cell::face(int f) {
	return grid.face[faces[f]];
};

Node& Face::node(int n) {
	return grid.node[nodes[n]];
};

void Grid::nodeAverages() {
	
	unsigned int c,g;
	double weight=0.,weightSum;
	Vec3D node2cell,node2ghost;
	map<int,double>::iterator it;
	
	for (unsigned int n=0;n<nodeCount;++n) {
		// Add contributions from real cells
		weightSum=0.;
		for (unsigned int nc=0;nc<node[n].cells.size();++nc) {
			c=node[n].cells[nc];
			node2cell=node[n]-cell[c].centroid;
			weight=1./(node2cell.dot(node2cell));
			node[n].average.insert(pair<int,double>(c,weight));
			weightSum+=weight;
		}
		// Add contributions from ghost cells		
		for (unsigned int ng=0;ng<node[n].ghosts.size();++ng) {
			g=node[n].ghosts[ng];
			node2ghost=node[n]-ghost[g].centroid;
			weight=1./(node2ghost.dot(node2ghost));
			node[n].average.insert(pair<int,double>(-g-1,weight));
			weightSum+=weight;
		}
		for ( it=node[n].average.begin() ; it != node[n].average.end(); it++ ) {
			(*it).second/=weightSum;
		}
	}
} // end Grid::nodeAverages()

void Grid::faceAverages() {

	unsigned int n;
	map<int,double>::iterator it;
	
	for (unsigned int f=0;f<faceCount;++f) {
		// Add contributions from nodes
		for (unsigned int fn=0;fn<face[f].nodeCount;++fn) {
			n=face[f].nodes[fn];
			for ( it=node[n].average.begin() ; it != node[n].average.end(); it++ ) {
				if (face[f].average.find((*it).first)!=face[f].average.end()) { // if the cell contributing to the node average is already contained in the face average map
					face[f].average[(*it).first]+=(*it).second/face[f].nodeCount;

				} else {
					face[f].average.insert(pair<int,double>((*it).first,(*it).second/face[f].nodeCount));
				}
			} 
		} // end face node loop
	} // end face loop
} // end Grid::faceAverages()

void Grid::gradMaps() {
	
	unsigned int f;
	map<int,double>::iterator it;
	Vec3D areaVec;
	for (unsigned int c=0;c<cellCount;++c) {
		for (unsigned int cf=0;cf<cell[c].faceCount;++cf) { 
			f=cell[c].faces[cf];
			if (face[f].bc<0 ) { // if internal or interpartition face
				areaVec=face[f].normal*face[f].area/cell[c].volume;
				if (face[f].parent!=c) areaVec*=-1.;
				for ( it=face[f].average.begin() ; it != face[f].average.end(); it++ ) {
					if (cell[c].gradMap.find((*it).first)!=cell[c].gradMap.end()) { // if the cell contributing to the face average is already contained in the cell gradient map
						cell[c].gradMap[(*it).first]+=(*it).second*areaVec;
					} else {
						cell[c].gradMap.insert(pair<int,Vec3D>((*it).first,(*it).second*areaVec));
					}
				}
			} // end if internal face
		} // end cell face loop
	} // end cell loop
	
} // end Grid::gradMaps()
	

void Grid::gradients(void) {

	// Calculate cell gradients

	map<int,Vec3D>::iterator it;
	map<int,double>::iterator fit;
	unsigned int f;
	Vec3D faceVel,areaVec;
	double faceRho,faceP;
	
	for (unsigned int c=0;c<cellCount;++c) {
		// Initialize all gradients to zero
		for (unsigned int i=0;i<5;++i) cell[c].grad[i]=0.;
		// Add internal and interpartition face contributions
		for (it=cell[c].gradMap.begin();it!=cell[c].gradMap.end(); it++ ) {
			if ((*it).first>=0) { // if contribution is coming from a real cell
				cell[c].grad[0]+=(*it).second*cell[(*it).first].rho;
				for (unsigned int i=1;i<4;++i) cell[c].grad[i]+=(*it).second*cell[(*it).first].v.comp[i-1];
				cell[c].grad[4]+=(*it).second*cell[(*it).first].p;
			} else { // if contribution is coming from a ghost cell
				cell[c].grad[0]+=(*it).second*ghost[-1*((*it).first+1)].rho;
				for (unsigned int i=1;i<4;++i) cell[c].grad[i]+=(*it).second*ghost[-1*((*it).first+1)].v.comp[i-1];
				cell[c].grad[4]+=(*it).second*ghost[-1*((*it).first+1)].p;
			}
		} // end gradMap loop
		// Add boundary face contributions
		for (unsigned int cf=0;cf<cell[c].faceCount;++cf) {
			f=cell[c].faces[cf];
			if (face[f].bc>=0) { // if a boundary face
				areaVec=face[f].area*face[f].normal/cell[c].volume;
				if (face[f].parent!=c) areaVec*=-1.;
				if (bc.region[face[f].bc].type=="inlet") {
					cell[c].grad[0]+=bc.region[face[f].bc].rho*areaVec;
					cell[c].grad[1]+=bc.region[face[f].bc].v.comp[0]*areaVec;
					cell[c].grad[2]+=bc.region[face[f].bc].v.comp[1]*areaVec;
					cell[c].grad[3]+=bc.region[face[f].bc].v.comp[2]*areaVec;
					cell[c].grad[4]+=bc.region[face[f].bc].p*areaVec; // FIXME Can you specify everything at inlet???
				} else { // if not an inlet
					// Find face averaged variables 
					faceVel=0.;faceRho=0.;faceP=0.;
					for (fit=face[f].average.begin();fit!=face[f].average.end();fit++) {
						if ((*fit).first>=0) { // if contribution is coming from a real cell
							faceRho+=(*fit).second*cell[(*fit).first].rho;
							faceVel+=(*fit).second*cell[(*fit).first].v;
							faceP+=(*fit).second*cell[(*fit).first].p;
						} else { // if contribution is coming from a ghost cell
							faceRho+=(*fit).second*ghost[-1*((*fit).first+1)].rho;
							faceVel+=(*fit).second*ghost[-1*((*fit).first+1)].v;
							faceP+=(*fit).second*ghost[-1*((*fit).first+1)].p;
						}
					}
					// These two are interpolated for all boundary types other than inlet
					cell[c].grad[0]+=faceRho*areaVec;
					cell[c].grad[4]+=faceP*areaVec;
					// Kill the wall normal component for slip
					if (bc.region[face[f].bc].type=="slip") faceVel-=faceVel.dot(face[f].normal)*face[f].normal;
					// Simply don't add face velocity gradient contributions for noslip
					if (bc.region[face[f].bc].type!="noslip") {
						cell[c].grad[1]+=faceVel.comp[0]*areaVec;
						cell[c].grad[2]+=faceVel.comp[1]*areaVec;
						cell[c].grad[3]+=faceVel.comp[2]*areaVec;
					}
				} // end if inlet or not
			} // end if a boundary face
		} // end cell face loop
	} // end cell loop
 	
} // end Grid::gradients(void)

void Grid::limit_gradients(string limiter, double sharpeningFactor) {
	
	unsigned int neighbor,g;
	Vec3D maxGrad[5],minGrad[5];
	
	for (unsigned int c=0;c<cellCount;++c) {

		// Initialize min and max to current cells values
		for (unsigned int i=0;i<5;++i) maxGrad[i]=minGrad[i]=cell[c].grad[i];
		// Find extremes in neighboring real cells
		for (unsigned int cc=0;cc<cell[c].neighborCellCount;++cc) {
			neighbor=cell[c].neighborCells[cc];
			for (unsigned int var=0;var<5;++var) {
				for (unsigned int comp=0;comp<3;++comp) {
					maxGrad[var].comp[comp]=max(maxGrad[var].comp[comp],(1.-sharpeningFactor)*cell[neighbor].grad[var].comp[comp]+sharpeningFactor*cell[c].grad[var].comp[comp]);
					minGrad[var].comp[comp]=min(minGrad[var].comp[comp],(1.-sharpeningFactor)*cell[neighbor].grad[var].comp[comp]+sharpeningFactor*cell[c].grad[var].comp[comp]);
				}
			}
		}
		// Find extremes in neighboring ghost cells
		for (unsigned int cg=0;cg<cell[c].ghostCount;++cg) {
			g=cell[c].ghosts[cg];
			for (unsigned int var=0;var<5;++var) {
				for (unsigned int comp=0;comp<3;++comp) {
					maxGrad[var].comp[comp]=max(maxGrad[var].comp[comp],(1.-sharpeningFactor)*ghost[g].grad[var].comp[comp]+sharpeningFactor*cell[c].grad[var].comp[comp]);
					minGrad[var].comp[comp]=min(minGrad[var].comp[comp],(1.-sharpeningFactor)*ghost[g].grad[var].comp[comp]+sharpeningFactor*cell[c].grad[var].comp[comp]);
				}
			}
		}
		if(limiter=="superbee") for (unsigned int var=0;var<5;++var) for (unsigned int comp=0;comp<3;++comp) cell[c].limited_grad[var].comp[comp]=superbee(maxGrad[var].comp[comp],minGrad[var].comp[comp]);
		if(limiter=="minmod") for (unsigned int var=0;var<5;++var) for (unsigned int comp=0;comp<3;++comp) cell[c].limited_grad[var].comp[comp]=minmod(maxGrad[var].comp[comp],minGrad[var].comp[comp]);

	}

} // end Grid::limit_gradients(string limiter, double sharpeningFactor)
