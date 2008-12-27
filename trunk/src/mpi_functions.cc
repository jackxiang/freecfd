/************************************************************************
	
	Copyright 2007-2008 Emre Sozer & Patrick Clark Trizila

	Contact: emresozer@freecfd.com , ptrizila@freecfd.com

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
#include "mpi_functions.h"
#include "commons.h"
std::vector<unsigned int> *sendCells;
unsigned int *recvCount;
MPI_Datatype MPI_GRAD;
MPI_Datatype MPI_VEC3D;
MPI_Datatype MPI_GHOST;

void mpi_init(int argc, char *argv[]) {
	// Initialize mpi
	MPI_Init(&argc,&argv);
	// Find the number of processors
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	// Find current processor Rank
	MPI_Comm_rank(MPI_COMM_WORLD, &Rank);
	
	sendCells = new vector<unsigned int> [np];
	recvCount = new unsigned int [np];
	// Commit custom communication datatypes
	int array_of_block_lengths[2]={1,8};
	MPI_Aint extent;
	MPI_Type_extent(MPI_UNSIGNED,&extent);
	MPI_Aint array_of_displacements[2]={0,extent};
	MPI_Datatype array_of_types[2]={MPI_UNSIGNED,MPI_DOUBLE};
	MPI_Type_struct(2,array_of_block_lengths,array_of_displacements,array_of_types,&MPI_GHOST);
	MPI_Type_commit(&MPI_GHOST);
	
	array_of_block_lengths[0]=1;array_of_block_lengths[1]=21;
	MPI_Type_struct(2,array_of_block_lengths,array_of_displacements,array_of_types,&MPI_GRAD);
	MPI_Type_commit(&MPI_GRAD);
	
	array_of_block_lengths[0]=2;array_of_block_lengths[1]=3;
	array_of_displacements[1]=2*extent;
	MPI_Type_struct(2,array_of_block_lengths,array_of_displacements,array_of_types,&MPI_VEC3D);
	MPI_Type_commit(&MPI_VEC3D);
	
	return;
} // end mpi_init

void mpi_handshake(void) {

	int maxGhost=grid.globalCellCount/np*2;
	unsigned int ghosts2receive[np][maxGhost],ghosts2send[np][maxGhost];
	
	for (unsigned int p=0;p<np;++p) {
		// Global id's of ghosts to request from each other processors
		// First entry in the array indicates how many of ghosts to be received
		// The rest are global id's
		ghosts2receive[p][0]=0;
	}
	
	for (unsigned int g=0; g<grid.ghostCount; ++g) {
		unsigned int p=grid.ghost[g].partition;
		ghosts2receive[p][++ghosts2receive[p][0]]=grid.ghost[g].globalId;
	}

	for (unsigned int p=0;p<np;++p) {
		MPI_Alltoall(ghosts2receive,maxGhost,MPI_UNSIGNED,ghosts2send,maxGhost,MPI_UNSIGNED,MPI_COMM_WORLD);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	
	// Transfer data to more efficient containers
	for (unsigned int p=0;p<np;++p) {
		for (unsigned int i=1;i<=ghosts2send[p][0];++i) sendCells[p].push_back(ghosts2send[p][i]);
		recvCount[p]=ghosts2receive[p][0];
	}

} // end mpi_handshake


void mpi_get_ghost_centroids(void) {
	
	for (unsigned int p=0;p<np;++p) {
		if (Rank!=p) {
			mpiVec3D sendBuffer[sendCells[p].size()];
			mpiVec3D recvBuffer[recvCount[p]];
			int id;
			for (unsigned int g=0;g<sendCells[p].size();++g)	{
				id=maps.cellGlobal2Local[sendCells[p][g]];
				sendBuffer[g].globalId=grid.cell[id].globalId;
				sendBuffer[g].matrix_id=grid.myOffset+id;
				for (int i=0;i<3;++i) sendBuffer[g].comp[i]=grid.cell[id].centroid[i];
			}

			int tag=Rank; // tag is set to source
			MPI_Sendrecv(sendBuffer,sendCells[p].size(),MPI_VEC3D,p,0,recvBuffer,recvCount[p],MPI_VEC3D,p,MPI_ANY_TAG,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
			
			for (unsigned int g=0;g<recvCount[p];++g) {
				id=maps.ghostGlobal2Local[recvBuffer[g].globalId];
				grid.ghost[id].matrix_id=recvBuffer[g].matrix_id;
				for (int i=0;i<3;++i) grid.ghost[id].centroid[i]=recvBuffer[g].comp[i];
			}
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
	return;
} // end mpi_get_ghost_centroids

void mpi_update_ghost_primitives(void) {
	
	for (unsigned int p=0;p<np;++p) {
		if (Rank!=p) {
			mpiGhost sendBuffer[sendCells[p].size()];
			mpiGhost recvBuffer[recvCount[p]];
			int id;
			for (unsigned int g=0;g<sendCells[p].size();++g)	{
				id=maps.cellGlobal2Local[sendCells[p][g]];
				sendBuffer[g].globalId=grid.cell[id].globalId;
				sendBuffer[g].vars[0]=grid.cell[id].rho;
				sendBuffer[g].vars[1]=grid.cell[id].v.comp[0];
				sendBuffer[g].vars[2]=grid.cell[id].v.comp[1];
				sendBuffer[g].vars[3]=grid.cell[id].v.comp[2];
				sendBuffer[g].vars[4]=grid.cell[id].p;
				sendBuffer[g].vars[5]=grid.cell[id].k;
				sendBuffer[g].vars[6]=grid.cell[id].omega;
				sendBuffer[g].vars[7]=grid.cell[id].mu;
			}

			int tag=Rank; // tag is set to source
			MPI_Sendrecv(sendBuffer,sendCells[p].size(),MPI_GHOST,p,0,recvBuffer,recvCount[p],MPI_GHOST,p,MPI_ANY_TAG,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

			for (unsigned int g=0;g<recvCount[p];++g) {
				id=maps.ghostGlobal2Local[recvBuffer[g].globalId];
				if (timeStep==1) {
					for (int i=0;i<7;++i) grid.ghost[id].update[0]=0.;
				} else {
					grid.ghost[id].update[0]=recvBuffer[g].vars[0]-grid.ghost[id].rho;
					grid.ghost[id].update[1]=recvBuffer[g].vars[1]-grid.ghost[id].v[0];
					grid.ghost[id].update[2]=recvBuffer[g].vars[2]-grid.ghost[id].v[1];
					grid.ghost[id].update[3]=recvBuffer[g].vars[3]-grid.ghost[id].v[2];
					grid.ghost[id].update[4]=recvBuffer[g].vars[4]-grid.ghost[id].p;
					grid.ghost[id].update[5]=recvBuffer[g].vars[5]-grid.ghost[id].k;
					grid.ghost[id].update[6]=recvBuffer[g].vars[6]-grid.ghost[id].omega;
				}
				grid.ghost[id].rho=recvBuffer[g].vars[0];
				grid.ghost[id].v.comp[0]=recvBuffer[g].vars[1];
				grid.ghost[id].v.comp[1]=recvBuffer[g].vars[2];
				grid.ghost[id].v.comp[2]=recvBuffer[g].vars[3];
				grid.ghost[id].p=recvBuffer[g].vars[4];
				grid.ghost[id].k=recvBuffer[g].vars[5];
				grid.ghost[id].omega=recvBuffer[g].vars[6];
				grid.ghost[id].mu=recvBuffer[g].vars[7];
			}
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
	return;
} // end mpi_update_ghost_primitives

void mpi_update_ghost_gradients(void) {
	
	// Update ghost gradients
	for (unsigned int p=0;p<np;++p) {
		mpiGrad sendBuffer[sendCells[p].size()];
		mpiGrad recvBuffer[recvCount[p]];
		int id;
		for (unsigned int g=0;g<sendCells[p].size();++g) {
			id=maps.cellGlobal2Local[sendCells[p][g]];
			sendBuffer[g].globalId=grid.cell[id].globalId;
			int count=0;
			for (unsigned int var=0;var<7;++var) {
				for (unsigned int comp=0;comp<3;++comp) {
					sendBuffer[g].grads[count]=grid.cell[id].grad[var].comp[comp];
					count++;
				}
			}
		}

		int tag=Rank; // tag is set to source
		MPI_Sendrecv(sendBuffer,sendCells[p].size(),MPI_GRAD,p,0,recvBuffer,recvCount[p],MPI_GRAD,p,MPI_ANY_TAG,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

		for (unsigned int g=0;g<recvCount[p];++g) {
			id=maps.ghostGlobal2Local[recvBuffer[g].globalId];
			int count=0;
			for (unsigned int var=0;var<7;++var) {
				for (unsigned int comp=0;comp<3;++comp) {
					grid.ghost[id].grad[var].comp[comp]=recvBuffer[g].grads[count];
					count++;
				}
			}
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
	return;
} // end mpi_update_ghost_gradients

void mpi_update_ghost_limited_gradients(void) {
	// Send limited gradients
	for (unsigned int p=0;p<np;++p) {
		mpiGrad sendBuffer[sendCells[p].size()];
		mpiGrad recvBuffer[recvCount[p]];
		int id;
		for (unsigned int g=0;g<sendCells[p].size();++g) {
			id=maps.cellGlobal2Local[sendCells[p][g]];
			sendBuffer[g].globalId=grid.cell[id].globalId;
			int count=0;
			for (unsigned int var=0;var<7;++var) {
				for (unsigned int comp=0;comp<3;++comp) {
					sendBuffer[g].grads[count]=grid.cell[id].limited_grad[var].comp[comp];
					count++;
				}
			}
		}

		int tag=Rank; // tag is set to source
		MPI_Sendrecv(sendBuffer,sendCells[p].size(),MPI_GRAD,p,0,recvBuffer,recvCount[p],MPI_GRAD,p,MPI_ANY_TAG,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

		for (unsigned int g=0;g<recvCount[p];++g) {
			id=maps.ghostGlobal2Local[recvBuffer[g].globalId];
			int count=0;
			for (unsigned int var=0;var<7;++var) {
				for (unsigned int comp=0;comp<3;++comp) {
					grid.ghost[id].limited_grad[var].comp[comp]=recvBuffer[g].grads[count];
					count++;
				}
			}
		}
	}
	return;
	MPI_Barrier(MPI_COMM_WORLD);
} // end mpi_update_ghost_limited_gradients
