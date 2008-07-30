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
#include "petsc_functions.h"
#include "bc.h"
#include "inputs.h"

extern double Gamma,dt;
extern double Pref;
extern double CFL;
extern BC bc;
extern InputFile input;
		
void preconditioner_cm91(unsigned int c, double P[][5]);
void preconditioner_ws95(unsigned int c, double P[][5]);
void preconditioner_none(unsigned int c, double P[][5]);
void mat_print(double P[][5]);

void linear_system_initialize() {

	MatZeroEntries(impOP);

	PetscInt counter=0;

	double d,lengthScale,dtLocal,a;
	double P [5][5]; // preconditioner

	PetscInt row,col;
	PetscScalar value;
			
	for (unsigned int c=0;c<grid.cellCount;++c) {

		if (input.section["timeMarching"].strings["preconditioner"]=="cm91") {
			preconditioner_cm91(c,P);
		} else {
			preconditioner_none(c,P);
		}

		if (input.section["timeMarching"].strings["type"]=="CFLlocal") {
			// Determine time step with CFL condition
			lengthScale;
			dtLocal=1.E20;
			a=sqrt(Gamma*(grid.cell[c].p+Pref)/grid.cell[c].rho);
			lengthScale=grid.cell[c].lengthScale;
			dtLocal=min(dtLocal,CFL*lengthScale/(fabs(grid.cell[c].v.comp[0])+a));
			dtLocal=min(dtLocal,CFL*lengthScale/(fabs(grid.cell[c].v.comp[1])+a));
			dtLocal=min(dtLocal,CFL*lengthScale/(fabs(grid.cell[c].v.comp[2])+a));
			d=grid.cell[c].volume/dtLocal;
		} else {
			d=grid.cell[c].volume/dt;
		}

		
		for (int i=0;i<5;++i) {
			row=grid.cell[c].globalId*5+i;

			value=-1.*grid.cell[c].flux[i];
			VecSetValues(rhs,1,&row,&value,INSERT_VALUES);
			
			for (int j=0;j<5;++j) {
				col=grid.cell[c].globalId*5+j;
				value=P[i][j]*d;
				MatSetValues(impOP,1,&row,1,&col,&value,INSERT_VALUES);
			}
		}
			
	}

	MatAssemblyBegin(impOP,MAT_FLUSH_ASSEMBLY);
	MatAssemblyEnd(impOP,MAT_FLUSH_ASSEMBLY);

	return;
}

void preconditioner_none(unsigned int c, double P[][5]) {

	double q2,a2;
	double rho,u,v,w,p;
	
	rho=grid.cell[c].rho;
	u=grid.cell[c].v.comp[0];
	v=grid.cell[c].v.comp[1];
	w=grid.cell[c].v.comp[2];
	p=grid.cell[c].p;
	q2=grid.cell[c].v.dot(grid.cell[c].v);
	a2=Gamma*(p+Pref)/rho;

	for (int i=0;i<5;++i) for (int j=0;j<5;++j) P[i][j]=0.;
	// Conservative to primite Jacobian
	P[0][0]=1.;
	P[1][0]=u; P[1][1]=rho;
	P[2][0]=v; P[2][2]=rho;
	P[3][0]=w; P[3][3]=rho;
		
	P[4][0]=0.5*q2;
	P[4][1]=rho*u;
	P[4][2]=rho*v;
	P[4][3]=rho*w;
	P[4][4]=1./(Gamma-1.);

	return;
}

void preconditioner_cm91(unsigned int c, double P[][5]) {

	double Mach,Mach2,a2;
	double rho,u,v,w,p,e,d,q2;
	double beta;
	double gm1=Gamma-1.;
	double R=287.;
	
	rho=grid.cell[c].rho;
	u=grid.cell[c].v.comp[0];
	v=grid.cell[c].v.comp[1];
	w=grid.cell[c].v.comp[2];
	p=grid.cell[c].p;
	q2=grid.cell[c].v.dot(grid.cell[c].v);
	a2=Gamma*(p+Pref)/rho;
	Mach2=q2/a2;
	Mach=sqrt(Mach2);
	
	if (Mach<=1.e-5) {
		Mach=1.e-5;
	} else if (Mach<1.) {
		//use local
	} else {
		Mach=1.;
	}

	Mach2=Mach*Mach;

	beta=max(1.e-5,q2);
			
	//beta=a2;
	
	e=0.5*q2+a2/(Gamma*(Gamma - 1.));
	
	for (int i=0;i<5;++i) for (int j=0;j<5;++j) P[i][j]=0.;

	P[0][0]=1./(beta*Mach2);
	
	P[1][0]=u/(beta*Mach2);
	P[1][1]=rho;

	P[2][0]=v/(beta*Mach2);
	P[2][2]=rho;

	P[3][0]=w/(beta*Mach2);
	P[3][3]=rho;

	P[4][0]=(rho*e+p)/(rho*beta*Mach2)-1.;
	P[4][1]=rho*u;
	P[4][2]=rho*v;
	P[4][3]=rho*w;
	P[4][4]=Gamma*rho*R/(Gamma-1.);

	
	return;
}

void preconditioner_ws95(unsigned int c, double P[][5]) {

	double Umax,Ur,theta,vis;
	double rho,u,v,w,p,H,q2,a2,q,a,epsilon,rhoT,R,cp;
	double temp [5][5];
				
	rho=grid.cell[c].rho;
	u=grid.cell[c].v.comp[0];
	v=grid.cell[c].v.comp[1];
	w=grid.cell[c].v.comp[2];
	p=grid.cell[c].p;
	q2=grid.cell[c].v.dot(grid.cell[c].v);
	a2=Gamma*(p+Pref)/rho;
	H=0.5*q2+a2/(Gamma - 1.);
	a=sqrt(a2); q=sqrt(q2);

	Umax=1.;
	vis=1.225e-2;
	epsilon=1.e-5;
	R=287.;
	cp=Gamma*R/(Gamma-1.);
	rhoT=-1.*R*rho*rho/(p+Pref);

	if (q<epsilon*a) {
		Ur=epsilon*a;
	} else if (q<a) {
		Ur=q;
	} else {
		Ur=a;
	}

	//Ur=max(Ur,vis/(rho*grid.cell[c].lengthScale));
	Ur=max(q,vis/(rho*grid.cell[c].lengthScale));

	theta=1./(Ur*Ur)-rhoT/(rho*cp);
	
	for (int i=0;i<5;++i) for (int j=0;j<5;++j) P[i][j]=0.;
	
	P[0][0]=theta;
	P[0][4]=rhoT;
	P[1][0]=u*theta;
	P[1][1]=rho;
	P[1][4]=u*rhoT;
	P[2][0]=v*theta;
	P[2][2]=rho;
	P[2][4]=v*rhoT;
	P[3][0]=w*theta;
	P[3][3]=rho;
	P[3][4]=w*rhoT;
	P[4][0]=theta*H-1.;
	P[4][1]=rho*u;
	P[4][2]=rho*v;
	P[4][3]=rho*w;
	P[4][4]=rhoT*H+rho*cp;

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