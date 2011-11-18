/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/camera.c
 *  \ingroup bke
 */

#include <stdlib.h>

#include "DNA_camera_types.h"
#include "DNA_lamp_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BKE_animsys.h"
#include "BKE_camera.h"
#include "BKE_object.h"
#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_main.h"

/****************************** Camera Datablock *****************************/

void *add_camera(const char *name)
{
	Camera *cam;
	
	cam=  alloc_libblock(&G.main->camera, ID_CA, name);

	cam->lens= 35.0f;
	cam->sensor_x= 32.0f;
	cam->sensor_y= 18.0f;
	cam->clipsta= 0.1f;
	cam->clipend= 100.0f;
	cam->drawsize= 0.5f;
	cam->ortho_scale= 6.0;
	cam->flag |= CAM_SHOWPASSEPARTOUT;
	cam->passepartalpha = 0.5f;
	
	return cam;
}

Camera *copy_camera(Camera *cam)
{
	Camera *camn;
	
	camn= copy_libblock(&cam->id);
	
	return camn;
}

void make_local_camera(Camera *cam)
{
	Main *bmain= G.main;
	Object *ob;
	int is_local= FALSE, is_lib= FALSE;

	/* - only lib users: do nothing
	 * - only local users: set flag
	 * - mixed: make copy
	 */
	
	if(cam->id.lib==NULL) return;
	if(cam->id.us==1) {
		id_clear_lib_data(bmain, &cam->id);
		return;
	}
	
	for(ob= bmain->object.first; ob && ELEM(0, is_lib, is_local); ob= ob->id.next) {
		if(ob->data==cam) {
			if(ob->id.lib) is_lib= TRUE;
			else is_local= TRUE;
		}
	}
	
	if(is_local && is_lib == FALSE) {
		id_clear_lib_data(bmain, &cam->id);
	}
	else if(is_local && is_lib) {
		Camera *camn= copy_camera(cam);

		camn->id.us= 0;

		/* Remap paths of new ID using old library as base. */
		BKE_id_lib_local_paths(bmain, &camn->id);

		for(ob= bmain->object.first; ob; ob= ob->id.next) {
			if(ob->data == cam) {
				if(ob->id.lib==NULL) {
					ob->data= camn;
					camn->id.us++;
					cam->id.us--;
				}
			}
		}
	}
}

void free_camera(Camera *ca)
{
	BKE_free_animdata((ID *)ca);
}

/******************************** Camera Usage *******************************/

void object_camera_mode(RenderData *rd, Object *cam_ob)
{
	rd->mode &= ~(R_ORTHO|R_PANORAMA);

	if(cam_ob && cam_ob->type==OB_CAMERA) {
		Camera *cam= cam_ob->data;
		if(cam->type == CAM_ORTHO) rd->mode |= R_ORTHO;
		if(cam->flag & CAM_PANORAMA) rd->mode |= R_PANORAMA;
	}
}

/* get the camera's dof value, takes the dof object into account */
float object_camera_dof_distance(Object *ob)
{
	Camera *cam = (Camera *)ob->data; 
	if (ob->type != OB_CAMERA)
		return 0.0f;
	if (cam->dof_ob) {	
		/* too simple, better to return the distance on the view axis only
		 * return len_v3v3(ob->obmat[3], cam->dof_ob->obmat[3]); */
		float mat[4][4], imat[4][4], obmat[4][4];
		
		copy_m4_m4(obmat, ob->obmat);
		normalize_m4(obmat);
		invert_m4_m4(imat, obmat);
		mul_m4_m4m4(mat, cam->dof_ob->obmat, imat);
		return (float)fabs(mat[3][2]);
	}
	return cam->YF_dofdist;
}

/******************************** Camera Params *******************************/

static int camera_sensor_fit(int sensor_fit, float sizex, float sizey)
{
	if(sensor_fit == CAMERA_SENSOR_FIT_AUTO) {
		if(sizex >= sizey)
			return CAMERA_SENSOR_FIT_HOR;
		else
			return CAMERA_SENSOR_FIT_VERT;
	}

	return sensor_fit;
}

void camera_params_init(CameraParams *params)
{
	memset(params, 0, sizeof(CameraParams));

	/* defaults */
	params->sensor_x= DEFAULT_SENSOR_WIDTH;
	params->sensor_y= DEFAULT_SENSOR_HEIGHT;
	params->sensor_fit= CAMERA_SENSOR_FIT_AUTO;
}

void camera_params_from_object(CameraParams *params, Object *ob)
{
	if(!ob)
		return;

	if(ob->type==OB_CAMERA) {
		/* camera object */
		Camera *cam= ob->data;

		if(cam->type == CAM_ORTHO)
			params->is_ortho= TRUE;
		params->lens= cam->lens;
		params->ortho_scale= cam->ortho_scale;

		params->shiftx= cam->shiftx;
		params->shifty= cam->shifty;

		params->sensor_x= cam->sensor_x;
		params->sensor_y= cam->sensor_y;
		params->sensor_fit= cam->sensor_fit;

		params->clipsta= cam->clipsta;
		params->clipend= cam->clipend;
	}
	else if(ob->type==OB_LAMP) {
		/* lamp object */
		Lamp *la= ob->data;
		float fac= cosf((float)M_PI*la->spotsize/360.0f);
		float phi= acos(fac);

		params->lens= 16.0f*fac/sinf(phi);
		if(params->lens==0.0f)
			params->lens= 35.0f;

		params->clipsta= la->clipsta;
		params->clipend= la->clipend;
	}
}

void camera_params_compute(CameraParams *params, int winx, int winy, float xasp, float yasp)
{
	rctf viewplane;
	float pixsize, winside, viewfac;
	int sensor_fit;

	/* fields rendering */
	params->ycor= yasp/xasp;
	if(params->use_fields)
		params->ycor *= 2.0f;

	/* determine sensor fit */
	sensor_fit = camera_sensor_fit(params->sensor_fit, xasp*winx, yasp*winy);

	if(params->is_ortho) {
		/* orthographic camera, scale == 1.0 means exact 1 to 1 mapping */
		if(sensor_fit==CAMERA_SENSOR_FIT_HOR)
			viewfac= winx;
		else
			viewfac= params->ycor * winy;

		pixsize= params->ortho_scale/viewfac;
	}
	else {
		/* perspective camera */
		float sensor_size;

		/* note for auto fit sensor_x is both width and height */
		if(params->sensor_fit == CAMERA_SENSOR_FIT_VERT)
			sensor_size= params->sensor_y;
		else
			sensor_size= params->sensor_x;
		
		if(sensor_fit == CAMERA_SENSOR_FIT_HOR)
			viewfac= (params->lens * winx) / sensor_size;
		else
			viewfac= params->ycor * (params->lens * winy) / sensor_size;

		pixsize= params->clipsta/viewfac;
	}

	/* compute view plane:
	 * fully centered, zbuffer fills in jittered between -.5 and +.5 */
	if(sensor_fit == CAMERA_SENSOR_FIT_HOR)
		winside= winx;
	else
		winside= winy;

	viewplane.xmin= -0.5f*(float)winx + params->shiftx*winside;
	viewplane.ymin= -0.5f*params->ycor*(float)winy + params->shifty*winside;
	viewplane.xmax=  0.5f*(float)winx + params->shiftx*winside;
	viewplane.ymax=  0.5f*params->ycor*(float)winy + params->shifty*winside;

	if(params->field_second) {
		if(params->field_odd) {
			viewplane.ymin-= 0.5f * params->ycor;
			viewplane.ymax-= 0.5f * params->ycor;
		}
		else {
			viewplane.ymin+= 0.5f * params->ycor;
			viewplane.ymax+= 0.5f * params->ycor;
		}
	}

	/* the window matrix is used for clipping, and not changed during OSA steps */
	/* using an offset of +0.5 here would give clip errors on edges */
	viewplane.xmin *= pixsize;
	viewplane.xmax *= pixsize;
	viewplane.ymin *= pixsize;
	viewplane.ymax *= pixsize;

	params->viewdx= pixsize;
	params->viewdy= params->ycor * pixsize;

	if(params->is_ortho)
		orthographic_m4(params->winmat, viewplane.xmin, viewplane.xmax,
			viewplane.ymin, viewplane.ymax, params->clipsta, params->clipend);
	else
		perspective_m4(params->winmat, viewplane.xmin, viewplane.xmax,
			viewplane.ymin, viewplane.ymax, params->clipsta, params->clipend);
	
	params->viewplane= viewplane;
}

/***************************** Camera View Frame *****************************/

void camera_view_frame_ex(Scene *scene, Camera *camera, float drawsize, const short do_clip, const float scale[3],
                          float r_asp[2], float r_shift[2], float *r_drawsize, float r_vec[4][3])
{
	float facx, facy;
	float depth;

	/* aspect correcton */
	if (scene) {
		float aspx= (float) scene->r.xsch*scene->r.xasp;
		float aspy= (float) scene->r.ysch*scene->r.yasp;

		if(camera->sensor_fit==CAMERA_SENSOR_FIT_AUTO) {
			if(aspx < aspy) {
				r_asp[0]= aspx / aspy;
				r_asp[1]= 1.0;
			}
			else {
				r_asp[0]= 1.0;
				r_asp[1]= aspy / aspx;
			}
		}
		else if(camera->sensor_fit==CAMERA_SENSOR_FIT_HOR) {
			r_asp[0]= aspx / aspy;
			r_asp[1]= 1.0;
		}
		else {
			r_asp[0]= 1.0;
			r_asp[1]= aspy / aspx;
		}
	}
	else {
		r_asp[0]= 1.0f;
		r_asp[1]= 1.0f;
	}

	if(camera->type==CAM_ORTHO) {
		facx= 0.5f * camera->ortho_scale * r_asp[0] * scale[0];
		facy= 0.5f * camera->ortho_scale * r_asp[1] * scale[1];
		r_shift[0]= camera->shiftx * camera->ortho_scale * scale[0];
		r_shift[1]= camera->shifty * camera->ortho_scale * scale[1];
		depth= do_clip ? -((camera->clipsta * scale[2]) + 0.1f) : - drawsize * camera->ortho_scale * scale[2];

		*r_drawsize= 0.5f * camera->ortho_scale;
	}
	else {
		/* that way it's always visible - clipsta+0.1 */
		float fac;
		float half_sensor= 0.5f*((camera->sensor_fit==CAMERA_SENSOR_FIT_VERT) ? (camera->sensor_y) : (camera->sensor_x));

		*r_drawsize= drawsize / ((scale[0] + scale[1] + scale[2]) / 3.0f);

		if(do_clip) {
			/* fixed depth, variable size (avoids exceeding clipping range) */
			depth = -(camera->clipsta + 0.1f);
			fac = depth / (camera->lens/(-half_sensor) * scale[2]);
		}
		else {
			/* fixed size, variable depth (stays a reasonable size in the 3D view) */
			depth= *r_drawsize * camera->lens/(-half_sensor) * scale[2];
			fac= *r_drawsize;
		}

		facx= fac * r_asp[0] * scale[0];
		facy= fac * r_asp[1] * scale[1];
		r_shift[0]= camera->shiftx*fac*2 * scale[0];
		r_shift[1]= camera->shifty*fac*2 * scale[1];
	}

	r_vec[0][0]= r_shift[0] + facx; r_vec[0][1]= r_shift[1] + facy; r_vec[0][2]= depth;
	r_vec[1][0]= r_shift[0] + facx; r_vec[1][1]= r_shift[1] - facy; r_vec[1][2]= depth;
	r_vec[2][0]= r_shift[0] - facx; r_vec[2][1]= r_shift[1] - facy; r_vec[2][2]= depth;
	r_vec[3][0]= r_shift[0] - facx; r_vec[3][1]= r_shift[1] + facy; r_vec[3][2]= depth;
}

void camera_view_frame(Scene *scene, Camera *camera, float r_vec[4][3])
{
	float dummy_asp[2];
	float dummy_shift[2];
	float dummy_drawsize;
	const float dummy_scale[3]= {1.0f, 1.0f, 1.0f};

	camera_view_frame_ex(scene, camera, FALSE, 1.0, dummy_scale,
	                     dummy_asp, dummy_shift, &dummy_drawsize, r_vec);
}


typedef struct CameraViewFrameData {
	float frame_tx[4][3];
	float normal_tx[4][3];
	float dist_vals[4];
	unsigned int tot;
} CameraViewFrameData;

static void camera_to_frame_view_cb(const float co[3], void *user_data)
{
	CameraViewFrameData *data= (CameraViewFrameData *)user_data;
	unsigned int i;

	for (i= 0; i < 4; i++) {
		float nd= -dist_to_plane_v3(co, data->frame_tx[i], data->normal_tx[i]);
		if (nd < data->dist_vals[i]) {
			data->dist_vals[i]= nd;
		}
	}

	data->tot++;
}

/* dont move the camera, just yield the fit location */
int camera_view_frame_fit_to_scene(Scene *scene, struct View3D *v3d, Object *camera_ob, float r_co[3])
{
	float shift[2];
	float plane_tx[4][3];
	float rot_obmat[3][3];
	const float zero[3]= {0,0,0};
	CameraViewFrameData data_cb;

	unsigned int i;

	camera_view_frame(scene, camera_ob->data, data_cb.frame_tx);

	copy_m3_m4(rot_obmat, camera_ob->obmat);
	normalize_m3(rot_obmat);

	for (i= 0; i < 4; i++) {
		/* normalize so Z is always 1.0f*/
		mul_v3_fl(data_cb.frame_tx[i], 1.0f/data_cb.frame_tx[i][2]);
	}

	/* get the shift back out of the frame */
	shift[0]= (data_cb.frame_tx[0][0] +
	           data_cb.frame_tx[1][0] +
	           data_cb.frame_tx[2][0] +
	           data_cb.frame_tx[3][0]) / 4.0f;
	shift[1]= (data_cb.frame_tx[0][1] +
	           data_cb.frame_tx[1][1] +
	           data_cb.frame_tx[2][1] +
	           data_cb.frame_tx[3][1]) / 4.0f;

	for (i= 0; i < 4; i++) {
		mul_m3_v3(rot_obmat, data_cb.frame_tx[i]);
	}

	for (i= 0; i < 4; i++) {
		normal_tri_v3(data_cb.normal_tx[i],
		              zero, data_cb.frame_tx[i], data_cb.frame_tx[(i + 1) % 4]);
	}

	/* initialize callback data */
	data_cb.dist_vals[0]=
	data_cb.dist_vals[1]=
	data_cb.dist_vals[2]=
	data_cb.dist_vals[3]= FLT_MAX;
	data_cb.tot= 0;
	/* run callback on all visible points */
	BKE_scene_foreach_display_point(scene, v3d, BA_SELECT,
	                                camera_to_frame_view_cb, &data_cb);

	if (data_cb.tot <= 1) {
		return FALSE;
	}
	else {
		float plane_isect_1[3], plane_isect_1_no[3], plane_isect_1_other[3];
		float plane_isect_2[3], plane_isect_2_no[3], plane_isect_2_other[3];

		float plane_isect_pt_1[3], plane_isect_pt_2[3];

		/* apply the dist-from-plane's to the transformed plane points */
		for (i= 0; i < 4; i++) {
			mul_v3_v3fl(plane_tx[i], data_cb.normal_tx[i], data_cb.dist_vals[i]);
		}

		if ( (isect_plane_plane_v3(plane_isect_1, plane_isect_1_no,
		                           plane_tx[0], data_cb.normal_tx[0],
		                           plane_tx[2], data_cb.normal_tx[2]) == 0) ||
		     (isect_plane_plane_v3(plane_isect_2, plane_isect_2_no,
		                           plane_tx[1], data_cb.normal_tx[1],
		                           plane_tx[3], data_cb.normal_tx[3]) == 0))
		{
			/* this is very unlikely */
			return FALSE;
		}
		else {

			add_v3_v3v3(plane_isect_1_other, plane_isect_1, plane_isect_1_no);
			add_v3_v3v3(plane_isect_2_other, plane_isect_2, plane_isect_2_no);

			if (isect_line_line_v3(plane_isect_1, plane_isect_1_other,
			                       plane_isect_2, plane_isect_2_other,
			                       plane_isect_pt_1, plane_isect_pt_2) == 0)
			{
				return FALSE;
			}
			else {
				float cam_plane_no[3]= {0.0f, 0.0f, -1.0f};
				float plane_isect_delta[3];
				float plane_isect_delta_len;

				mul_m3_v3(rot_obmat, cam_plane_no);

				sub_v3_v3v3(plane_isect_delta, plane_isect_pt_2, plane_isect_pt_1);
				plane_isect_delta_len= len_v3(plane_isect_delta);

				if (dot_v3v3(plane_isect_delta, cam_plane_no) > 0.0f) {
					copy_v3_v3(r_co, plane_isect_pt_1);

					/* offset shift */
					normalize_v3(plane_isect_1_no);
					madd_v3_v3fl(r_co, plane_isect_1_no, shift[1] * -plane_isect_delta_len);
				}
				else {
					copy_v3_v3(r_co, plane_isect_pt_2);

					/* offset shift */
					normalize_v3(plane_isect_2_no);
					madd_v3_v3fl(r_co, plane_isect_2_no, shift[0] * -plane_isect_delta_len);
				}


				return TRUE;
			}
		}
	}
}
