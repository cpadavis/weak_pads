/*
 *  
 *
 *  	r3d.c
 *
 *  	See Readme.md and r3d.h for usage.
 * 
 *  	Copyright (C) 2014 Stanford University.
 *  	See License.txt for more information.
 *
 *
 */

#include "r3d.h"

#define ONE_THIRD 0.333333333333333333333333333333333333333333333333333333
#define ONE_SIXTH 0.16666666666666666666666666666666666666666666666666666667

// tells us which bit signals a clipped vertex
// the last one - leaving seven for flagging faces
#define CLIP_MASK 0x80

// macros for vector manipulation
#define dot(va, vb) (va.x*vb.x + va.y*vb.y + va.z*vb.z)
#define wav(va, wa, vb, wb, vr) {			\
	vr.x = (wa*va.x + wb*vb.x)/(wa + wb);	\
	vr.y = (wa*va.y + wb*vb.y)/(wa + wb);	\
	vr.z = (wa*va.z + wb*vb.z)/(wa + wb);	\
}
#define norm(v) {					\
	r3d_real tmplen = sqrt(dot(v, v));	\
	v.x /= (tmplen + 1.0e-299);		\
	v.y /= (tmplen + 1.0e-299);		\
	v.z /= (tmplen + 1.0e-299);		\
}

//////////////////////////////////////////////////////////////////
//////////////// r3d /////////////////////////////////////////////
//////////////////////////////////////////////////////////////////


#ifdef USE_TREE
// tree node for recursive splitting
typedef struct {
	r3d_int imin, jmin, kmin;
	r3d_int ioff, joff, koff;
	r3d_gridpoint gridpt[8];
#ifdef PRINT_POVRAY_TREE
	int lvl;
#endif
} r3d_treenode;
#endif // USE_TREE 

void r3d_voxelize_tet(r3d_plane faces[4], r3d_dest_grid grid) {

	r3d_real moments[10];
	r3d_real locvol, gor;
	r3d_real xmin, ymin, zmin;
	r3d_rvec3 gpt;
	r3d_int i, j, k, m, mmax;
	unsigned char v, f;
	unsigned char orcmp, andcmp;

// macros for grid access
#define gind(ii, jj, kk) ((grid.n.j+1)*(grid.n.k+1)*(ii) + (grid.n.k+1)*(jj) + (kk))
#define vind(ii, jj, kk) (grid.n.j*grid.n.k*(ii) + grid.n.k*(jj) + (kk))

// tree/storage buffers, as needed
#ifdef USE_TREE
	r3d_treenode treestack[256];
	r3d_int ntreestack;
	r3d_treenode curnode;
#else
	r3d_long vv0;
	r3d_long vv[8];
#endif

	// get the high moment index 
	mmax = r3d_num_moments[grid.polyorder];

	// voxel bounds in shifted coordinates
	r3d_rvec3 rbounds[2] = {
		{-0.5*grid.d.x, -0.5*grid.d.y, -0.5*grid.d.z}, 
		{0.5*grid.d.x, 0.5*grid.d.y, 0.5*grid.d.z} 
	};

	// vertex buffer
	r3d_int nverts;
	r3d_vertex vertbuffer[128];

#ifndef USE_TREE // !USE_TREE

	// check all grid vertices in the patch against each tet face
	for(i = 0; i <= grid.n.i; ++i)
	for(j = 0; j <= grid.n.j; ++j)
	for(k = 0; k <= grid.n.k; ++k) {
		// flag the vertex for each face it lies inside
		// also save its distance from each face
		gpt.x = i*grid.d.x; gpt.y = j*grid.d.y; gpt.z = k*grid.d.z;
		vv0 = gind(i, j, k);
		grid.orient[vv0].fflags = 0x00;
		for(f = 0; f < 4; ++f) {
			gor = faces[f].d + dot(gpt, faces[f].n);
			if(gor > 0.0) grid.orient[vv0].fflags |= (1 << f);
			grid.orient[vv0].fdist[f] = gor;
		}
	}

	// iterate over all voxels in the patch
	for(i = 0; i < grid.n.i; ++i)
	for(j = 0; j < grid.n.j; ++j)
	for(k = 0; k < grid.n.k; ++k) {

		// precompute flattened grid indices
		vv[0] = gind(i, j, k);
		vv[1] = gind(i+1, j, k);
		vv[2] = gind(i+1, j+1, k);
		vv[3] = gind(i, j+1, k);
		vv[4] = gind(i, j, k+1);
		vv[5] = gind(i+1, j, k+1);
		vv[6] = gind(i+1, j+1, k+1);
		vv[7] = gind(i, j+1, k+1);
	
		// check inclusion of each voxel within the tet
		orcmp = 0x00;
        andcmp = 0x0f;
		for(v = 0; v < 8; ++v) {
			orcmp |= grid.orient[vv[v]].fflags; 
			andcmp &= grid.orient[vv[v]].fflags; 
		}

		if(andcmp == 0x0f) {

			// the voxel is entirely inside the tet

#ifndef NO_REDUCTION

			// calculate moments
			locvol = grid.d.x*grid.d.y*grid.d.z;
			moments[0] = locvol; // 1
			if(grid.polyorder >= 1) {
				moments[1] = locvol*grid.d.x*(i + 0.5); // x
				moments[2] = locvol*grid.d.y*(j + 0.5); // y
				moments[3] = locvol*grid.d.z*(k + 0.5); // z
			}
			if(grid.polyorder >= 2) {
				moments[4] = locvol*ONE_THIRD*grid.d.x*grid.d.x*(1 + 3*i + 3*i*i); // x*x
				moments[5] = locvol*ONE_THIRD*grid.d.y*grid.d.y*(1 + 3*j + 3*j*j); // y*y
				moments[6] = locvol*ONE_THIRD*grid.d.z*grid.d.z*(1 + 3*k + 3*k*k); // z*z
				moments[7] = locvol*0.25*grid.d.x*grid.d.y*(1 + 2*i)*(1 + 2*j); // x*y
				moments[8] = locvol*0.25*grid.d.y*grid.d.z*(1 + 2*j)*(1 + 2*k); // y*z
				moments[9] = locvol*0.25*grid.d.x*grid.d.z*(1 + 2*i)*(1 + 2*k); // z*x
			}

			//reduce to main grid
			for(m = 0; m < mmax; ++m) 
				grid.moments[m][vind(i, j, k)] = moments[m]; 

#endif // NO_REDUCTION

			continue;
		}	
		else if(orcmp == 0x0f) {

			// the voxel crosses the boundary of the tet
			// need to process further

#ifdef NO_SHIFTING
			// voxel bounds in absolute coordinates
			rbounds[2] = {
				{i*grid.d.x, j*grid.d.y, k*grid.d.z}, 
				{(i+1)*grid.d.x, (j+1)*grid.d.y, (k+1)*grid.d.z} 
			};
#endif // NO_SHIFTING

			// initialize the unit cube connectivity 
			r3du_init_box(vertbuffer, &nverts, rbounds);
			for(v = 0; v < nverts; ++v)
				vertbuffer[v].orient = grid.orient[vv[v]];

			// Clipping
#ifndef NO_CLIPPING
			r3d_clip_tet(vertbuffer, &nverts, andcmp);
#endif // NO_CLIPPING

			// Reduce
#ifndef NO_REDUCTION
			r3d_reduce(vertbuffer, &nverts, &moments[0], grid.polyorder);

			// the cross-terms arising from using an offset box
			// must be taken into account in the absolute moment integrals
			if(grid.polyorder >= 1) {
				xmin = (i + 0.5)*grid.d.x;
				ymin = (j + 0.5)*grid.d.y;
				zmin = (k + 0.5)*grid.d.z;
			}
			if(grid.polyorder >= 2) {
				moments[4] += 2.0*xmin*moments[1] + xmin*xmin*moments[0];
				moments[5] += 2.0*ymin*moments[2] + ymin*ymin*moments[0];
				moments[6] += 2.0*zmin*moments[3] + zmin*zmin*moments[0];
				moments[7] += xmin*moments[2] + ymin*moments[1] + xmin*ymin*moments[0];
				moments[8] += ymin*moments[3] + zmin*moments[2] + ymin*zmin*moments[0];
				moments[9] += xmin*moments[3] + zmin*moments[1] + xmin*zmin*moments[0];
			}
			if(grid.polyorder >= 1) {
				moments[1] += xmin*moments[0];
				moments[2] += ymin*moments[0];
				moments[3] += zmin*moments[0];
			}

			// reduce to main grid
			for(m = 0; m < mmax; ++m) 
				grid.moments[m][vind(i, j, k)] = moments[m];

#endif // NO_REDUCTION

			continue;	
		}
		else {
			// voxel is entirely outside of the tet
			// zero the moments 
			for(m = 0; m < mmax; ++m) 
				grid.moments[m][vind(i, j, k)] = 0.0;
			continue;
		}
	}

#else // USE_TREE
	
	// get the initial face orientations for each corner of the node
	gpt.x = 0.0;
	gpt.y = 0.0;
	gpt.z = 0.0;
	curnode.gridpt[0].fflags = 0x00;
	for(f = 0; f < 4; ++f) {
		gor = faces[f].d + dot(gpt, faces[f].n);
		if(gor > 0.0) curnode.gridpt[0].fflags |= (1 << f);
		curnode.gridpt[0].fdist[f] = gor;
	}
	gpt.x = nx*grid.d.x;
	curnode.gridpt[1].fflags = 0x00;
	for(f = 0; f < 4; ++f) {
		gor = faces[f].d + dot(gpt, faces[f].n);
		if(gor > 0.0) curnode.gridpt[1].fflags |= (1 << f);
		curnode.gridpt[1].fdist[f] = gor;
	}
	gpt.y = ny*grid.d.y;
	curnode.gridpt[2].fflags = 0x00;
	for(f = 0; f < 4; ++f) {
		gor = faces[f].d + dot(gpt, faces[f].n);
		if(gor > 0.0) curnode.gridpt[2].fflags |= (1 << f);
		curnode.gridpt[2].fdist[f] = gor;
	}
	gpt.x = 0.0;
	curnode.gridpt[3].fflags = 0x00;
	for(f = 0; f < 4; ++f) {
		gor = faces[f].d + dot(gpt, faces[f].n);
		if(gor > 0.0) curnode.gridpt[3].fflags |= (1 << f);
		curnode.gridpt[3].fdist[f] = gor;
	}
	gpt.y = 0.0;
	gpt.z = nz*grid.d.z;
	curnode.gridpt[4].fflags = 0x00;
	for(f = 0; f < 4; ++f) {
		gor = faces[f].d + dot(gpt, faces[f].n);
		if(gor > 0.0) curnode.gridpt[4].fflags |= (1 << f);
		curnode.gridpt[4].fdist[f] = gor;
	}
	gpt.x = nx*grid.d.x;
	curnode.gridpt[5].fflags = 0x00;
	for(f = 0; f < 4; ++f) {
		gor = faces[f].d + dot(gpt, faces[f].n);
		if(gor > 0.0) curnode.gridpt[5].fflags |= (1 << f);
		curnode.gridpt[5].fdist[f] = gor;
	}
	gpt.y = ny*grid.d.y;
	curnode.gridpt[6].fflags = 0x00;
	for(f = 0; f < 4; ++f) {
		gor = faces[f].d + dot(gpt, faces[f].n);
		if(gor > 0.0) curnode.gridpt[6].fflags |= (1 << f);
		curnode.gridpt[6].fdist[f] = gor;
	}
	gpt.x = 0.0;
	curnode.gridpt[7].fflags = 0x00;
	for(f = 0; f < 4; ++f) {
		gor = faces[f].d + dot(gpt, faces[f].n);
		if(gor > 0.0) curnode.gridpt[7].fflags |= (1 << f);
		curnode.gridpt[7].fdist[f] = gor;
	}
	curnode.imin = 0;
	curnode.jmin = 0;
	curnode.kmin = 0;
	curnode.ioff = nx;
	curnode.joff = ny;
	curnode.koff = nz;

#ifdef PRINT_POVRAY_TREE
#define TREELVL 18
	curnode.lvl = 0;
	printf("#declare leaves = union {\n");
#endif

	ntreestack = 0;
	treestack[ntreestack++] = curnode;

	while(ntreestack > 0) {

		// pop the top node
		curnode = treestack[--ntreestack];

		orcmp = 0x00;
        andcmp = 0x0f;
		for(v = 0; v < 8; ++v) {
			orcmp |= curnode.gridpt[v].fflags; 
			andcmp &= curnode.gridpt[v].fflags; 
		}

		if(andcmp == 0x0f) {
#ifdef PRINT_POVRAY_TREE
			if(curnode.lvl <= TREELVL) {
				printf("	box {\n");
				printf("		<%f+sw,%f+sw,%f+sw>, <%f-sw,%f-sw,%f-sw>\n", grid.d.x*(curnode.imin), grid.d.y*(curnode.jmin), grid.d.z*(curnode.kmin), grid.d.x*(curnode.imin + curnode.ioff), grid.d.y*(curnode.jmin + curnode.joff), grid.d.z*(curnode.kmin + curnode.koff));
				printf("		texture {\n");
				printf("			pigment{ incolor }\n");
				printf("		}\n");
				printf("	}\n");
			}
#endif
			// all cells in this leaf are fully contained
#ifndef NO_REDUCTION

			locvol = grid.d.x*grid.d.y*grid.d.z;
			for(i = curnode.imin; i < curnode.imin + curnode.ioff; ++i)
			for(j = curnode.jmin; j < curnode.jmin + curnode.joff; ++j)
			for(k = curnode.kmin; k < curnode.kmin + curnode.koff; ++k) {
				
				// zero the moments
				for(m = 0; m < 10; ++m)
					moments[m] = 0.0;
	
				// constant field by default
				moments[0] = locvol; // 1
				if(polyorder >= 1) {
					moments[1] = locvol*grid.d.x*(i+ibounds[0].x+0.5); // x
					moments[2] = locvol*grid.d.y*(j+ibounds[0].y+0.5); // y
					moments[3] = locvol*grid.d.z*(k+ibounds[0].z+0.5); // z
				}
				if(polyorder >= 2) {
					moments[4] = locvol*ONE_THIRD*grid.d.x*grid.d.x*(1 + 3*i + 3*i*i + 3*ibounds[0].x + 6*i*ibounds[0].x + 3*ibounds[0].x*ibounds[0].x);
					moments[5] = locvol*ONE_THIRD*grid.d.y*grid.d.y*(1 + 3*j + 3*j*j + 3*ibounds[0].y + 6*j*ibounds[0].y + 3*ibounds[0].y*ibounds[0].y);
					moments[6] = locvol*ONE_THIRD*grid.d.z*grid.d.z*(1 + 3*k + 3*k*k + 3*ibounds[0].z + 6*k*ibounds[0].z + 3*ibounds[0].z*ibounds[0].z);
					moments[7] = locvol*0.25*grid.d.x*grid.d.y*(1+2*(i+ibounds[0].x))*(1+2*(j+ibounds[0].y)); // x*y
					moments[8] = locvol*0.25*grid.d.x*grid.d.z*(1+2*(i+ibounds[0].x))*(1+2*(k+ibounds[0].z)); // x*z
					moments[9] = locvol*0.25*grid.d.y*grid.d.z*(1+2*(j+ibounds[0].y))*(1+2*(k+ibounds[0].z)); // y*z
				}
	
				//reduce to main grid
				for(m = 0; m < 10; ++m) {
					r3d_dest_grid[vind(i, j, k)] += moments[m]*coeffs[m];
					current_info.momtot[m] += moments[m];
				}	
				current_info.vtot += moments[0];
				if(moments[0] < current_info.vox_min) current_info.vox_min = moments[0];
				if(moments[0] > current_info.vox_max) current_info.vox_max = moments[0];
			
			}
#endif // NO_REDUCTION

			current_info.num_in += curnode.ioff*curnode.joff*curnode.koff;

			continue;
		}
		if(orcmp != 0x0f) {

#ifdef PRINT_POVRAY_TREE
			if(curnode.lvl <= TREELVL) {

				printf("	box {\n");
				printf("		<%f+sw,%f+sw,%f+sw>, <%f-sw,%f-sw,%f-sw>\n", grid.d.x*(curnode.imin), grid.d.y*(curnode.jmin), grid.d.z*(curnode.kmin), grid.d.x*(curnode.imin + curnode.ioff), grid.d.y*(curnode.jmin + curnode.joff), grid.d.z*(curnode.kmin + curnode.koff));
				printf("		texture {\n");
				printf("			pigment{ outcolor }\n");
				printf("		}\n");
				printf("	}\n");

			}
#endif

			current_info.num_out += curnode.ioff*curnode.joff*curnode.koff;
			
			// the leaf lies entirely outside the tet
			// skip it
			continue;
		}
		if(curnode.ioff == 1 && curnode.joff == 1 && curnode.koff == 1) {
			// we've reached a single cell that straddles the tet boundary 
			// Clip and voxelize it.
			i = curnode.imin;
			j = curnode.jmin;
			k = curnode.kmin;

#ifdef PRINT_POVRAY_TREE
			if(curnode.lvl <= TREELVL) {

				printf("	box {\n");
				printf("		<%f+sw,%f+sw,%f+sw>, <%f-sw,%f-sw,%f-sw>\n", grid.d.x*(curnode.imin), grid.d.y*(curnode.jmin), grid.d.z*(curnode.kmin), grid.d.x*(curnode.imin + curnode.ioff), grid.d.y*(curnode.jmin + curnode.joff), grid.d.z*(curnode.kmin + curnode.koff));
				printf("		texture {\n");
				printf("			pigment{ scolor }\n");
				printf("		}\n");
				printf("	}\n");

			}
#endif

#ifdef NO_SHIFTING
			// voxel bounds in shifted coordinates
			rbounds[0].x = i*grid.d.x;
			rbounds[0].y = j*grid.d.y;
			rbounds[0].z = k*grid.d.z;
			rbounds[1].x = (i+1)*grid.d.x;
			rbounds[1].y = (j+1)*grid.d.y;
			rbounds[1].z = (k+1)*grid.d.z;
#endif // NO_SHIFTING


#ifdef PRINT_CLIP_VOXELS
	printf("%d %d %d %.20e ", i, j, k, sqrt(grid.d.x*grid.d.x+grid.d.y*grid.d.y+grid.d.z*grid.d.z)/sqrt(rbounds[1].x*rbounds[1].x + rbounds[1].y*rbounds[1].y + rbounds[1].z*rbounds[1].z));
#endif


			// initialize the unit cube connectivity 
			r3du_init_box(vertbuffer, &nverts, rbounds);
			for(v = 0; v < nverts; ++v) {
				vertbuffer[v].fflags = curnode.gridpt[v].fflags;
				for(f = 0; f < 4; ++f)
					vertbuffer[v].fdist[f] = curnode.gridpt[v].fdist[f];
			}

			// Clipping
#ifndef NO_CLIPPING
			r3du_clip_tet(vertbuffer, &nverts, andcmp);
#endif // NO_CLIPPING

			// Reduce
#ifndef NO_REDUCTION
			r3du_reduce(vertbuffer, &nverts, &moments[0], polyorder);

			// the cross-terms arising from using an offset box
			// must be taken into account in the absolute moment integrals
			if(polyorder >= 1) {
				xmin = (ibounds[0].x + i + 0.5)*grid.d.x;
				ymin = (ibounds[0].y + j + 0.5)*grid.d.y;
				zmin = (ibounds[0].z + k + 0.5)*grid.d.z;
			}
			if(polyorder >= 2) {
				moments[4] += 2.0*xmin*moments[1] + xmin*xmin*moments[0];
				moments[5] += 2.0*ymin*moments[2] + ymin*ymin*moments[0];
				moments[6] += 2.0*zmin*moments[3] + zmin*zmin*moments[0];
				moments[7] += xmin*moments[2] + ymin*moments[1] + xmin*ymin*moments[0];
				moments[8] += xmin*moments[3] + zmin*moments[1] + xmin*zmin*moments[0];
				moments[9] += ymin*moments[3] + zmin*moments[2] + ymin*zmin*moments[0];
			}
			if(polyorder >= 1) {
				moments[1] += xmin*moments[0];
				moments[2] += ymin*moments[0];
				moments[3] += zmin*moments[0];
			}

			// reduce to main grid
			for(m = 0; m < 10; ++m) {
				r3d_dest_grid[vind(i, j, k)] += moments[m]*coeffs[m];
				current_info.momtot[m] += moments[m];
			}	


#ifdef PRINT_CLIP_VOXELS
	printf("%.20e\n", (float) moments[0]);
#endif

			current_info.vtot += moments[0];
			if(moments[0] < current_info.vox_min) current_info.vox_min = moments[0];
			if(moments[0] > current_info.vox_max) current_info.vox_max = moments[0];
#endif // NO_REDUCTION

			++current_info.num_clip;

			continue;	
		}

#ifdef PRINT_POVRAY_TREE
		if(curnode.lvl == TREELVL) {

			printf("	box {\n");
			printf("		<%f+sw,%f+sw,%f+sw>, <%f-sw,%f-sw,%f-sw>\n", grid.d.x*(curnode.imin), grid.d.y*(curnode.jmin), grid.d.z*(curnode.kmin), grid.d.x*(curnode.imin + curnode.ioff), grid.d.y*(curnode.jmin + curnode.joff), grid.d.z*(curnode.kmin + curnode.koff));
			printf("		texture {\n");
			printf("			pigment{ scolor }\n");
			printf("		}\n");
			printf("	}\n");

		}
#endif

		// else, split the node along its longest dimension
		// and push to the the stack
		i = curnode.ioff/2;
		j = curnode.joff/2;
		k = curnode.koff/2;
		if(i >= j && i >= k) {
#ifdef PRINT_POVRAY_TREE
			treestack[ntreestack].lvl = curnode.lvl + 1;
			treestack[ntreestack+1].lvl = curnode.lvl + 1;
#endif

			// LEFT NODE
			treestack[ntreestack].imin = curnode.imin;
			treestack[ntreestack].ioff = i;
			treestack[ntreestack].jmin = curnode.jmin;
			treestack[ntreestack].joff = curnode.joff;
			treestack[ntreestack].kmin = curnode.kmin;
			treestack[ntreestack].koff = curnode.koff;
			treestack[ntreestack].gridpt[0] = curnode.gridpt[0];
			treestack[ntreestack].gridpt[3] = curnode.gridpt[3];
			treestack[ntreestack].gridpt[4] = curnode.gridpt[4];
			treestack[ntreestack].gridpt[7] = curnode.gridpt[7];

			// RIGHT NODE
			treestack[ntreestack+1].imin = curnode.imin + i;
			treestack[ntreestack+1].ioff = curnode.ioff - i;
			treestack[ntreestack+1].jmin = curnode.jmin;
			treestack[ntreestack+1].joff = curnode.joff;
			treestack[ntreestack+1].kmin = curnode.kmin;
			treestack[ntreestack+1].koff = curnode.koff;
			treestack[ntreestack+1].gridpt[1] = curnode.gridpt[1];
			treestack[ntreestack+1].gridpt[2] = curnode.gridpt[2];
			treestack[ntreestack+1].gridpt[5] = curnode.gridpt[5];
			treestack[ntreestack+1].gridpt[6] = curnode.gridpt[6];

			// FILL IN COMMON POINTS
			gpt.x = grid.d.x*(curnode.imin + i);
			gpt.y = grid.d.y*curnode.jmin;
			gpt.z = grid.d.z*curnode.kmin;
			treestack[ntreestack].gridpt[1].fflags = 0x00;
			treestack[ntreestack+1].gridpt[0].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[1].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[0].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[1].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[0].fflags |= (1 << f);
				}
			}
			gpt.y = grid.d.y*(curnode.jmin + curnode.joff);
			treestack[ntreestack].gridpt[2].fflags = 0x00;
			treestack[ntreestack+1].gridpt[3].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[2].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[3].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[2].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[3].fflags |= (1 << f);
				}
			}
			gpt.z = grid.d.z*(curnode.kmin + curnode.koff);
			treestack[ntreestack].gridpt[6].fflags = 0x00;
			treestack[ntreestack+1].gridpt[7].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[6].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[7].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[6].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[7].fflags |= (1 << f);
				}
			}
			gpt.y = grid.d.y*curnode.jmin;
			treestack[ntreestack].gridpt[5].fflags = 0x00;
			treestack[ntreestack+1].gridpt[4].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[5].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[4].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[5].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[4].fflags |= (1 << f);
				}
			}
			ntreestack += 2;
			continue;
		}
		if(j >= i && j >= k) {
#ifdef PRINT_POVRAY_TREE
			treestack[ntreestack].lvl = curnode.lvl + 1;
			treestack[ntreestack+1].lvl = curnode.lvl + 1;
#endif
			// LEFT NODE
			treestack[ntreestack].imin = curnode.imin;
			treestack[ntreestack].ioff = curnode.ioff;
			treestack[ntreestack].jmin = curnode.jmin;
			treestack[ntreestack].joff = j;
			treestack[ntreestack].kmin = curnode.kmin;
			treestack[ntreestack].koff = curnode.koff;
			treestack[ntreestack].gridpt[0] = curnode.gridpt[0];
			treestack[ntreestack].gridpt[1] = curnode.gridpt[1];
			treestack[ntreestack].gridpt[4] = curnode.gridpt[4];
			treestack[ntreestack].gridpt[5] = curnode.gridpt[5];

			// RIGHT NODE
			treestack[ntreestack+1].imin = curnode.imin;
			treestack[ntreestack+1].ioff = curnode.ioff;
			treestack[ntreestack+1].jmin = curnode.jmin + j;
			treestack[ntreestack+1].joff = curnode.joff - j;
			treestack[ntreestack+1].kmin = curnode.kmin;
			treestack[ntreestack+1].koff = curnode.koff;
			treestack[ntreestack+1].gridpt[2] = curnode.gridpt[2];
			treestack[ntreestack+1].gridpt[3] = curnode.gridpt[3];
			treestack[ntreestack+1].gridpt[6] = curnode.gridpt[6];
			treestack[ntreestack+1].gridpt[7] = curnode.gridpt[7];

			// FILL IN COMMON POINTS
			gpt.x = grid.d.x*curnode.imin;
			gpt.y = grid.d.y*(curnode.jmin + j);
			gpt.z = grid.d.z*curnode.kmin;
			treestack[ntreestack].gridpt[3].fflags = 0x00;
			treestack[ntreestack+1].gridpt[0].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[3].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[0].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[3].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[0].fflags |= (1 << f);
				}
			}
			gpt.x = grid.d.x*(curnode.imin + curnode.ioff);
			treestack[ntreestack].gridpt[2].fflags = 0x00;
			treestack[ntreestack+1].gridpt[1].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[2].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[1].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[2].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[1].fflags |= (1 << f);
				}
			}
			gpt.z = grid.d.z*(curnode.kmin + curnode.koff);
			treestack[ntreestack].gridpt[6].fflags = 0x00;
			treestack[ntreestack+1].gridpt[5].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[6].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[5].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[6].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[5].fflags |= (1 << f);
				}
			}
			gpt.x = grid.d.x*curnode.imin;
			treestack[ntreestack].gridpt[7].fflags = 0x00;
			treestack[ntreestack+1].gridpt[4].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[7].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[4].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[7].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[4].fflags |= (1 << f);
				}
			}
			ntreestack += 2;
			continue;
		}
		if(k >= i && k >= j) {
#ifdef PRINT_POVRAY_TREE
			treestack[ntreestack].lvl = curnode.lvl + 1;
			treestack[ntreestack+1].lvl = curnode.lvl + 1;
#endif

			// LEFT NODE
			treestack[ntreestack].imin = curnode.imin;
			treestack[ntreestack].ioff = curnode.ioff;
			treestack[ntreestack].jmin = curnode.jmin;
			treestack[ntreestack].joff = curnode.joff;
			treestack[ntreestack].kmin = curnode.kmin;
			treestack[ntreestack].koff = k;
			treestack[ntreestack].gridpt[0] = curnode.gridpt[0];
			treestack[ntreestack].gridpt[1] = curnode.gridpt[1];
			treestack[ntreestack].gridpt[2] = curnode.gridpt[2];
			treestack[ntreestack].gridpt[3] = curnode.gridpt[3];

			// RIGHT NODE
			treestack[ntreestack+1].imin = curnode.imin;
			treestack[ntreestack+1].ioff = curnode.ioff;
			treestack[ntreestack+1].jmin = curnode.jmin;
			treestack[ntreestack+1].joff = curnode.joff;
			treestack[ntreestack+1].kmin = curnode.kmin + k;
			treestack[ntreestack+1].koff = curnode.koff - k;
			treestack[ntreestack+1].gridpt[4] = curnode.gridpt[4];
			treestack[ntreestack+1].gridpt[5] = curnode.gridpt[5];
			treestack[ntreestack+1].gridpt[6] = curnode.gridpt[6];
			treestack[ntreestack+1].gridpt[7] = curnode.gridpt[7];

			// FILL IN COMMON POINTS
			gpt.x = grid.d.x*curnode.imin;
			gpt.y = grid.d.y*curnode.jmin;
			gpt.z = grid.d.z*(curnode.kmin + k);
			treestack[ntreestack].gridpt[4].fflags = 0x00;
			treestack[ntreestack+1].gridpt[0].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[4].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[0].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[4].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[0].fflags |= (1 << f);
				}
			}
			gpt.x = grid.d.x*(curnode.imin + curnode.ioff);
			treestack[ntreestack].gridpt[5].fflags = 0x00;
			treestack[ntreestack+1].gridpt[1].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[5].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[1].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[5].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[1].fflags |= (1 << f);
				}
			}
			gpt.y = grid.d.y*(curnode.jmin + curnode.joff);
			treestack[ntreestack].gridpt[6].fflags = 0x00;
			treestack[ntreestack+1].gridpt[2].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[6].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[2].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[6].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[2].fflags |= (1 << f);
				}
			}
			gpt.x = grid.d.x*curnode.imin;
			treestack[ntreestack].gridpt[7].fflags = 0x00;
			treestack[ntreestack+1].gridpt[3].fflags = 0x00;
			for(f = 0; f < 4; ++f) {
				gor = faces[f].d + dot(gpt, faces[f].n);
				treestack[ntreestack].gridpt[7].fdist[f] = gor;
				treestack[ntreestack+1].gridpt[3].fdist[f] = gor;
				if(gor > 0.0) {
					treestack[ntreestack].gridpt[7].fflags |= (1 << f);
					treestack[ntreestack+1].gridpt[3].fflags |= (1 << f);
				}
			}
			ntreestack += 2;
			continue;
		}
	}
#ifdef PRINT_POVRAY_TREE
	printf("}\n");
#endif

#endif // USE_TREE

}

#ifndef PRINT_POVRAY_TRAVERSAL
inline void r3d_clip_tet(r3d_vertex* vertbuffer, r3d_int* nverts, unsigned char andcmp) {

	r3d_int nvstack;
	unsigned char vstack[256];
	unsigned char v, f, ff, np, vcur, vprev, firstnewvert, prevnewvert;
	unsigned char fmask, ffmask;
			
	for(f = 0; f < 4; ++f) {

		fmask = (1 << f);
		if(andcmp & fmask) continue;

		// find the first vertex lying outside of the face
		// only need to find one (taking advantage of convexity)
		vcur = 127;
		for(v = 0; vcur >= 127 && v < *nverts; ++v) 
			if(!(vertbuffer[v].orient.fflags & (CLIP_MASK | fmask))) vcur = v;
		if(vcur >= 127) continue;
		
		// push the first three edges and mark the starting vertex
		// as having been clipped
		nvstack = 0;
		vstack[nvstack++] = vcur;
		vstack[nvstack++] = vertbuffer[vcur].pnbrs[1];
		vstack[nvstack++] = vcur;
		vstack[nvstack++] = vertbuffer[vcur].pnbrs[0];
		vstack[nvstack++] = vcur;
		vstack[nvstack++] = vertbuffer[vcur].pnbrs[2];
		vertbuffer[vcur].orient.fflags |= CLIP_MASK;
		firstnewvert = *nverts;
		prevnewvert = 127; // last element in vertex buffer

		// traverse edges and clip
		// this is ordered very carefully to preserve edge connectivity
		while(nvstack > 0) {

			// pop the stack
			vcur = vstack[--nvstack];
			vprev = vstack[--nvstack];

			// if the vertex has already been clipped, ignore it
			if(vertbuffer[vcur].orient.fflags & CLIP_MASK) continue; 

			// check whether this vertex is inside the face
			// if so, clip the edge and push the new vertex to vertbuffer
			if(vertbuffer[vcur].orient.fflags & fmask) {

				// compute the intersection point using a weighted
				// average of perpendicular distances to the plane
				wav(vertbuffer[vcur].pos, -vertbuffer[vprev].orient.fdist[f],
					vertbuffer[vprev].pos, vertbuffer[vcur].orient.fdist[f],
					vertbuffer[*nverts].pos);

				// doubly link to vcur
				for(np = 0; np < 3; ++np) if(vertbuffer[vcur].pnbrs[np] == vprev) break;
				vertbuffer[vcur].pnbrs[np] = *nverts;
				vertbuffer[*nverts].pnbrs[0] = vcur;

				// doubly link to previous new vert
				vertbuffer[*nverts].pnbrs[2] = prevnewvert; 
				vertbuffer[prevnewvert].pnbrs[1] = *nverts;

				// do face intersections and flags
				vertbuffer[*nverts].orient.fflags = 0x00;
				for(ff = f + 1; ff < 4; ++ff) {

					// skip if all verts are inside ff
					ffmask = (1 << ff); 
					if(andcmp & ffmask) continue;

					// weighted average keeps us in a relative coordinate system
					vertbuffer[*nverts].orient.fdist[ff] = 
							(vertbuffer[vprev].orient.fdist[ff]*vertbuffer[vcur].orient.fdist[f] 
							- vertbuffer[vprev].orient.fdist[f]*vertbuffer[vcur].orient.fdist[ff])
							/(vertbuffer[vcur].orient.fdist[f] - vertbuffer[vprev].orient.fdist[f]);
					if(vertbuffer[*nverts].orient.fdist[ff] > 0.0) vertbuffer[*nverts].orient.fflags |= ffmask;
				}

				prevnewvert = (*nverts)++;
			}
			else {

				// otherwise, determine the left and right vertices
				// (ordering is important) and push to the traversal stack
				for(np = 0; np < 3; ++np) if(vertbuffer[vcur].pnbrs[np] == vprev) break;

				// mark the vertex as having been clipped
				vertbuffer[vcur].orient.fflags |= CLIP_MASK;

				// push the next verts to the stack
				vstack[nvstack++] = vcur;
				vstack[nvstack++] = vertbuffer[vcur].pnbrs[(np+2)%3];
				vstack[nvstack++] = vcur;
				vstack[nvstack++] = vertbuffer[vcur].pnbrs[(np+1)%3];
			}
		}

		// close the clipped face
		vertbuffer[firstnewvert].pnbrs[2] = *nverts-1;
		vertbuffer[prevnewvert].pnbrs[1] = firstnewvert;
	}

}
#endif

inline void r3d_reduce(r3d_vertex* vertbuffer, r3d_int* nverts, r3d_real* moments, r3d_int polyorder) {

	r3d_real locvol;
	unsigned char v, np, m;
	unsigned char vcur, vnext, pnext, vstart;
	r3d_rvec3 v0, v1, v2; 
	
	// for keeping track of which edges have been traversed
	unsigned char emarks[128][3];
	memset((void*) &emarks, 0, sizeof(emarks));

	// stack for edges
	r3d_int nvstack;
	unsigned char vstack[256];

	// zero the moments
	for(m = 0; m < 10; ++m)
		moments[m] = 0.0;

	// find the first unclipped vertex
	vcur = 127;
	for(v = 0; vcur >= 127 && v < *nverts; ++v) 
		if(!(vertbuffer[v].orient.fflags & CLIP_MASK)) vcur = v;
	
	// return if all vertices have been clipped
	if(vcur >= 127) return;

#ifdef PRINT_POVRAY_DECOMP
	printf("#declare voxel = box {\n");
	printf("	<%f,%f,%f>,<%f,%f,%f>\n", vertbuffer[0].pos.x, vertbuffer[0].pos.y, vertbuffer[0].pos.z, vertbuffer[6].pos.x, vertbuffer[6].pos.y, vertbuffer[6].pos.z);
	printf("}\n");
	printf("#declare simplices = union {\n");
#endif

	// stack implementation
	nvstack = 0;
	vstack[nvstack++] = vcur;
	vstack[nvstack++] = 0;

	while(nvstack > 0) {
		
		pnext = vstack[--nvstack];
		vcur = vstack[--nvstack];

		// skip this edge if we have marked it
		if(emarks[vcur][pnext]) continue;

		// initialize face looping
		emarks[vcur][pnext] = 1;
		vstart = vcur;
		v0 = vertbuffer[vstart].pos;
		vnext = vertbuffer[vcur].pnbrs[pnext];
		vstack[nvstack++] = vcur;
		vstack[nvstack++] = (pnext+1)%3;

		// move to the second edge
		for(np = 0; np < 3; ++np) if(vertbuffer[vnext].pnbrs[np] == vcur) break;
		vcur = vnext;
		pnext = (np+1)%3;
		emarks[vcur][pnext] = 1;
		vnext = vertbuffer[vcur].pnbrs[pnext];
		vstack[nvstack++] = vcur;
		vstack[nvstack++] = (pnext+1)%3;

		// make a triangle fan using edges
		// and first vertex
		while(vnext != vstart) {

			v2 = vertbuffer[vcur].pos;
			v1 = vertbuffer[vnext].pos;

			locvol = ONE_SIXTH*(-(v2.x*v1.y*v0.z) + v1.x*v2.y*v0.z + v2.x*v0.y*v1.z
				   	- v0.x*v2.y*v1.z - v1.x*v0.y*v2.z + v0.x*v1.y*v2.z); 

			moments[0] += locvol; 
			if(polyorder >= 1) {
				moments[1] += locvol*0.25*(v0.x + v1.x + v2.x);
				moments[2] += locvol*0.25*(v0.y + v1.y + v2.y);
				moments[3] += locvol*0.25*(v0.z + v1.z + v2.z);
			}
			if(polyorder >= 2) {
				moments[4] += locvol*0.1*(v0.x*v0.x + v1.x*v1.x + v2.x*v2.x + v1.x*v2.x + v0.x*(v1.x + v2.x));
				moments[5] += locvol*0.1*(v0.y*v0.y + v1.y*v1.y + v2.y*v2.y + v1.y*v2.y + v0.y*(v1.y + v2.y));
				moments[6] += locvol*0.1*(v0.z*v0.z + v1.z*v1.z + v2.z*v2.z + v1.z*v2.z + v0.z*(v1.z + v2.z));
				moments[7] += locvol*0.05*(v2.x*v0.y + v2.x*v1.y + 2*v2.x*v2.y + v0.x*(2*v0.y + v1.y + v2.y) + v1.x*(v0.y + 2*v1.y + v2.y));
				moments[8] += locvol*0.05*(v2.y*v0.z + v2.y*v1.z + 2*v2.y*v2.z + v0.y*(2*v0.z + v1.z + v2.z) + v1.y*(v0.z + 2*v1.z + v2.z));
				moments[9] += locvol*0.05*(v2.x*v0.z + v2.x*v1.z + 2*v2.x*v2.z + v0.x*(2*v0.z + v1.z + v2.z) + v1.x*(v0.z + 2*v1.z + v2.z));
			}

#ifdef PRINT_POVRAY_DECOMP
			// for making the simplices pop away from each other
			r3d_rvec3 explode;
			explode.x = 0.25*(v0.x + v1.x + v2.x);
			explode.y = 0.25*(v0.y + v1.y + v2.y);
			explode.z = 0.25*(v0.z + v1.z + v2.z);
			norm(explode);
			printf("	triangle {\n");
			printf("		<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>,<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>,<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>\n",
					v0.x, explode.x, v0.y, explode.y, v0.z, explode.z,
					v1.x, explode.x, v1.y, explode.y, v1.z, explode.z,
					v2.x, explode.x, v2.y, explode.y, v2.z, explode.z
					);
			printf("	}\n");
			printf("	triangle {\n");
			printf("		<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>,<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>,<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>\n",
					0.0, explode.x, 0.0, explode.y, 0.0, explode.z,
					v2.x, explode.x, v2.y, explode.y, v2.z, explode.z,
					v1.x, explode.x, v1.y, explode.y, v1.z, explode.z
					);
			printf("	}\n");
			printf("	triangle {\n");
			printf("		<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>,<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>,<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>\n",
					v2.x, explode.x, v2.y, explode.y, v2.z, explode.z,
					0.0, explode.x, 0.0, explode.y, 0.0, explode.z,
					v0.x, explode.x, v0.y, explode.y, v0.z, explode.z
					);
			printf("	}\n");
			printf("	triangle {\n");
			printf("		<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>,<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>,<%f+ex*(%f),%f+ex*(%f),%f+ex*(%f)>\n",
					v1.x, explode.x, v1.y, explode.y, v1.z, explode.z,
					v0.x, explode.x, v0.y, explode.y, v0.z, explode.z,
					0.0, explode.x, 0.0, explode.y, 0.0, explode.z
					);
			printf("	}\n");
#endif

			// move to the next edge
			for(np = 0; np < 3; ++np) if(vertbuffer[vnext].pnbrs[np] == vcur) break;
			vcur = vnext;
			pnext = (np+1)%3;
			emarks[vcur][pnext] = 1;
			vnext = vertbuffer[vcur].pnbrs[pnext];
			vstack[nvstack++] = vcur;
			vstack[nvstack++] = (pnext+1)%3;
		}
	}

#ifdef PRINT_POVRAY_DECOMP
	printf("}\n");
#endif

}

//////////////////////////////////////////////////////////////////
//////////////// r3du: utility functions for r3d /////////////////
//////////////////////////////////////////////////////////////////


void r3du_tet_faces_from_verts(r3d_rvec3* verts, r3d_plane* faces) {
	// compute unit face normals and distances to origin
	r3d_rvec3 tmpcent;
	faces[0].n.x = ((verts[3].y - verts[1].y)*(verts[2].z - verts[1].z) 
			- (verts[2].y - verts[1].y)*(verts[3].z - verts[1].z));
	faces[0].n.y = ((verts[2].x - verts[1].x)*(verts[3].z - verts[1].z) 
			- (verts[3].x - verts[1].x)*(verts[2].z - verts[1].z));
	faces[0].n.z = ((verts[3].x - verts[1].x)*(verts[2].y - verts[1].y) 
			- (verts[2].x - verts[1].x)*(verts[3].y - verts[1].y));
	norm(faces[0].n);
	tmpcent.x = ONE_THIRD*(verts[1].x + verts[2].x + verts[3].x);
	tmpcent.y = ONE_THIRD*(verts[1].y + verts[2].y + verts[3].y);
	tmpcent.z = ONE_THIRD*(verts[1].z + verts[2].z + verts[3].z);
	faces[0].d = -dot(faces[0].n, tmpcent);

	faces[1].n.x = ((verts[2].y - verts[0].y)*(verts[3].z - verts[2].z) 
			- (verts[2].y - verts[3].y)*(verts[0].z - verts[2].z));
	faces[1].n.y = ((verts[3].x - verts[2].x)*(verts[2].z - verts[0].z) 
			- (verts[0].x - verts[2].x)*(verts[2].z - verts[3].z));
	faces[1].n.z = ((verts[2].x - verts[0].x)*(verts[3].y - verts[2].y) 
			- (verts[2].x - verts[3].x)*(verts[0].y - verts[2].y));
	norm(faces[1].n);
	tmpcent.x = ONE_THIRD*(verts[2].x + verts[3].x + verts[0].x);
	tmpcent.y = ONE_THIRD*(verts[2].y + verts[3].y + verts[0].y);
	tmpcent.z = ONE_THIRD*(verts[2].z + verts[3].z + verts[0].z);
	faces[1].d = -dot(faces[1].n, tmpcent);

	faces[2].n.x = ((verts[1].y - verts[3].y)*(verts[0].z - verts[3].z) 
			- (verts[0].y - verts[3].y)*(verts[1].z - verts[3].z));
	faces[2].n.y = ((verts[0].x - verts[3].x)*(verts[1].z - verts[3].z) 
			- (verts[1].x - verts[3].x)*(verts[0].z - verts[3].z));
	faces[2].n.z = ((verts[1].x - verts[3].x)*(verts[0].y - verts[3].y) 
			- (verts[0].x - verts[3].x)*(verts[1].y - verts[3].y));
	norm(faces[2].n);
	tmpcent.x = ONE_THIRD*(verts[3].x + verts[0].x + verts[1].x);
	tmpcent.y = ONE_THIRD*(verts[3].y + verts[0].y + verts[1].y);
	tmpcent.z = ONE_THIRD*(verts[3].z + verts[0].z + verts[1].z);
	faces[2].d = -dot(faces[2].n, tmpcent);

	faces[3].n.x = ((verts[0].y - verts[2].y)*(verts[1].z - verts[0].z) 
			- (verts[0].y - verts[1].y)*(verts[2].z - verts[0].z));
	faces[3].n.y = ((verts[1].x - verts[0].x)*(verts[0].z - verts[2].z) 
			- (verts[2].x - verts[0].x)*(verts[0].z - verts[1].z));
	faces[3].n.z = ((verts[0].x - verts[2].x)*(verts[1].y - verts[0].y) 
			- (verts[0].x - verts[1].x)*(verts[2].y - verts[0].y));
	norm(faces[3].n);
	tmpcent.x = ONE_THIRD*(verts[0].x + verts[1].x + verts[2].x);
	tmpcent.y = ONE_THIRD*(verts[0].y + verts[1].y + verts[2].y);
	tmpcent.z = ONE_THIRD*(verts[0].z + verts[1].z + verts[2].z);
	faces[3].d = -dot(faces[3].n, tmpcent);
}

void r3du_init_box(r3d_vertex* vertbuffer, r3d_int* nverts, r3d_rvec3 rbounds[2]) {
	*nverts = 8;
	vertbuffer[0].pnbrs[0] = 1;	
	vertbuffer[0].pnbrs[1] = 4;	
	vertbuffer[0].pnbrs[2] = 3;	
	vertbuffer[1].pnbrs[0] = 2;	
	vertbuffer[1].pnbrs[1] = 5;	
	vertbuffer[1].pnbrs[2] = 0;	
	vertbuffer[2].pnbrs[0] = 3;	
	vertbuffer[2].pnbrs[1] = 6;	
	vertbuffer[2].pnbrs[2] = 1;	
	vertbuffer[3].pnbrs[0] = 0;	
	vertbuffer[3].pnbrs[1] = 7;	
	vertbuffer[3].pnbrs[2] = 2;	
	vertbuffer[4].pnbrs[0] = 7;	
	vertbuffer[4].pnbrs[1] = 0;	
	vertbuffer[4].pnbrs[2] = 5;	
	vertbuffer[5].pnbrs[0] = 4;	
	vertbuffer[5].pnbrs[1] = 1;	
	vertbuffer[5].pnbrs[2] = 6;	
	vertbuffer[6].pnbrs[0] = 5;	
	vertbuffer[6].pnbrs[1] = 2;	
	vertbuffer[6].pnbrs[2] = 7;	
	vertbuffer[7].pnbrs[0] = 6;	
	vertbuffer[7].pnbrs[1] = 3;	
	vertbuffer[7].pnbrs[2] = 4;	
	vertbuffer[0].pos.x = rbounds[0].x; 
	vertbuffer[0].pos.y = rbounds[0].y; 
	vertbuffer[0].pos.z = rbounds[0].z; 
	vertbuffer[1].pos.x = rbounds[1].x; 
	vertbuffer[1].pos.y = rbounds[0].y; 
	vertbuffer[1].pos.z = rbounds[0].z; 
	vertbuffer[2].pos.x = rbounds[1].x; 
	vertbuffer[2].pos.y = rbounds[1].y; 
	vertbuffer[2].pos.z = rbounds[0].z; 
	vertbuffer[3].pos.x = rbounds[0].x; 
	vertbuffer[3].pos.y = rbounds[1].y; 
	vertbuffer[3].pos.z = rbounds[0].z; 
	vertbuffer[4].pos.x = rbounds[0].x; 
	vertbuffer[4].pos.y = rbounds[0].y; 
	vertbuffer[4].pos.z = rbounds[1].z; 
	vertbuffer[5].pos.x = rbounds[1].x; 
	vertbuffer[5].pos.y = rbounds[0].y; 
	vertbuffer[5].pos.z = rbounds[1].z; 
	vertbuffer[6].pos.x = rbounds[1].x; 
	vertbuffer[6].pos.y = rbounds[1].y; 
	vertbuffer[6].pos.z = rbounds[1].z; 
	vertbuffer[7].pos.x = rbounds[0].x; 
	vertbuffer[7].pos.y = rbounds[1].y; 
	vertbuffer[7].pos.z = rbounds[1].z; 
}

r3d_real r3du_orient(r3d_rvec3 pa, r3d_rvec3 pb, r3d_rvec3 pc, r3d_rvec3 pd) {
	r3d_real adx, bdx, cdx;
	r3d_real ady, bdy, cdy;
	r3d_real adz, bdz, cdz;
	adx = pa.x - pd.x;
	bdx = pb.x - pd.x;
	cdx = pc.x - pd.x;
	ady = pa.y - pd.y;
	bdy = pb.y - pd.y;
	cdy = pc.y - pd.y;
	adz = pa.z - pd.z;
	bdz = pb.z - pd.z;
	cdz = pc.z - pd.z;
	return -ONE_SIXTH*(adx * (bdy * cdz - bdz * cdy)
			+ bdx * (cdy * adz - cdz * ady)
			+ cdx * (ady * bdz - adz * bdy));
}

