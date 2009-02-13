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
#include "commons.h"
#include "petsc_functions.h"
#include "bc.h"

extern BC bc;

inline void preconditioner_none(Cell &c, double P[][7]) {
	
	double p=c.p+Pref;
	double T=c.T+Tref;
	double drho_dT; // Derivative of density w.r.t temp. @ const. press
	double drho_dp; // Derivative of density w.r.t press. @ const temp.
	
	
	// For ideal gas
	drho_dT=-1.*c.rho/T;
	drho_dp=c.rho/p;
	double c_p=Gamma/(Gamma-1.)*p/(c.rho*T); 
	
	double H=c_p*T+0.5*c.v.dot(c.v);
	
	// Conservative to primite Jacobian
	P[0][0]=drho_dp; P[0][4]=drho_dT;
			
	P[1][0]=drho_dp*c.v[0]; P[1][1]=c.rho; P[1][4]=drho_dT*c.v[0];
	P[2][0]=drho_dp*c.v[1]; P[2][2]=c.rho; P[2][4]=drho_dT*c.v[1];
	P[3][0]=drho_dp*c.v[2]; P[3][3]=c.rho; P[3][4]=drho_dT*c.v[2];
		
	P[4][0]=drho_dp*H-1.; 
	P[4][1]=c.rho*c.v[0];
	P[4][2]=c.rho*c.v[1];
	P[4][3]=c.rho*c.v[2];
	P[4][4]=drho_dT*H+c.rho*c_p; 

	// For k and omega
	P[5][5]=c.rho; P[6][6]=c.rho;
	
	return;
}
		
inline void preconditioner_ws95(Cell &c, double P[][7]) {
	
	double p=c.p+Pref;
	double T=c.T+Tref;
	// Reference velocity
	double Ur;
	
	// Speed of sound 
	double a=sqrt(Gamma*p/c.rho); // For ideal gas
	
	Ur=max(fabs(c.v),1.e-5*a);
	Ur=min(Ur,a);
	if (EQUATIONS==NS) Ur=max(Ur,c.mu/(c.rho*c.lengthScale));
	
	double deltaPmax=0.;
	// Now loop through neighbor cells to check pressure differences
	for (it=c.neighborCells.begin();it!=c.neighborCells.end();it++) {
		deltaPmax=max(deltaPmax,fabs(c.p-grid.cell[*it].p));
	}
	// TODO loop ghosts too
	
	Ur=max(Ur,1.e-5*sqrt(deltaPmax/c.rho));
	
	double drho_dp; // Derivative of density w.r.t press. @ const temp.
	double drho_dT; // Derivative of density w.r.t temp. @ const. press
	
	// For ideal gas
	drho_dT=-1.*c.rho/T;
	double c_p=Gamma/(Gamma-1.)*p/(c.rho*T); 
	drho_dp=1./(Ur*Ur)-drho_dT/(c.rho*c_p); // This is the only change over non-preconditioned
	
	double H=c_p*T+0.5*c.v.dot(c.v);
	
	// Conservative to primite Jacobian
	P[0][0]=drho_dp; P[0][4]=drho_dT;
			
	P[1][0]=drho_dp*c.v[0]; P[1][1]=c.rho; P[1][4]=drho_dT*c.v[0];
	P[2][0]=drho_dp*c.v[1]; P[2][2]=c.rho; P[2][4]=drho_dT*c.v[1];
	P[3][0]=drho_dp*c.v[2]; P[3][3]=c.rho; P[3][4]=drho_dT*c.v[2];
		
	P[4][0]=drho_dp*H-1.; 
	P[4][1]=c.rho*c.v[0];
	P[4][2]=c.rho*c.v[1];
	P[4][3]=c.rho*c.v[2];
	P[4][4]=drho_dT*H+c.rho*c_p; 

	// For k and omega
	P[5][5]=c.rho; P[6][6]=c.rho;
	
	return;
	
}

void mat_print(double P[][7]);

void initialize_linear_system() {

	int nSolVar=5; // Basic equations to solve

	if (TURBULENCE_MODEL!=NONE) nSolVar+=2;

	//if ((timeStep) % jacobianUpdateFreq == 0 || timeStep==restart+1) MatZeroEntries(impOP);
	MatZeroEntries(impOP);

	double d,lengthScale,dtLocal,a;
	double P [7][7]; // preconditioner
	for (int i=0;i<7;++i) for (int j=0;j<7;++j) P[i][j]=0.;

	PetscInt row,col;
	PetscScalar value;
			
	for (unsigned int c=0;c<grid.cellCount;++c) {

		if (PRECONDITIONER==WS95) {
			preconditioner_ws95(grid.cell[c],P);
		} else {
			preconditioner_none(grid.cell[c],P);
		}

		if (TIME_STEP_TYPE==CFL_LOCAL) {
			// Determine time step with CFL condition
			dtLocal=1.E20;
			a=sqrt(Gamma*(grid.cell[c].p+Pref)/grid.cell[c].rho);
			lengthScale=grid.cell[c].lengthScale;
			dtLocal=min(dtLocal,CFLlocal*lengthScale/(fabs(grid.cell[c].v[0])+a));
			dtLocal=min(dtLocal,CFLlocal*lengthScale/(fabs(grid.cell[c].v[1])+a));
			dtLocal=min(dtLocal,CFLlocal*lengthScale/(fabs(grid.cell[c].v[2])+a));
			d=grid.cell[c].volume/dtLocal;
		} else {
			d=grid.cell[c].volume/dt;
		}
		
		for (int i=0;i<nSolVar;++i) {
			row=(grid.myOffset+c)*nSolVar+i;
			for (int j=0;j<nSolVar;++j) {
				col=(grid.myOffset+c)*nSolVar+j;
				value=P[i][j]*d;
				// TODO if not updating jacobian at this time step, should this be INSERT_VALUES??
				MatSetValues(impOP,1,&row,1,&col,&value,ADD_VALUES);
			}
		}
			
	}
	return;
}

void mat_print(double mat[][5]) {


	cout << endl;
	for (int i=0;i<5;++i) {
		for (int j=0;j<5;++j) {
			cout << mat[i][j] << "\t";
		}
		cout << endl;
	}
	
	return;
}