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

/** \file blender/blenkernel/intern/font.c
 *  \ingroup bke
 */


#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_blenlib.h"
#include "BLI_vfontdata.h"
#include "BLI_utildefines.h"

#include "DNA_packedFile_types.h"
#include "DNA_curve_types.h"
#include "DNA_vfont_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"

#include "BKE_packedFile.h"
#include "BKE_library.h"
#include "BKE_font.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_anim.h"
#include "BKE_curve.h"
#include "BKE_displist.h"


/* The vfont code */
void BKE_vfont_free_data(struct VFont *vfont)
{
	if (vfont->data) {
		while (vfont->data->characters.first) {
			VChar *che = vfont->data->characters.first;

			while (che->nurbsbase.first) {
				Nurb *nu = che->nurbsbase.first;
				if (nu->bezt) MEM_freeN(nu->bezt);
				BLI_freelinkN(&che->nurbsbase, nu);
			}

			BLI_freelinkN(&vfont->data->characters, che);
		}

		MEM_freeN(vfont->data);
		vfont->data = NULL;
	}

	if (vfont->temp_pf) {
		freePackedFile(vfont->temp_pf);  /* NULL when the font file can't be found on disk */
		vfont->temp_pf = NULL;
	}
}

void BKE_vfont_free(struct VFont *vf)
{
	if (vf == NULL) return;

	BKE_vfont_free_data(vf);

	if (vf->packedfile) {
		freePackedFile(vf->packedfile);
		vf->packedfile = NULL;
	}
}

static void *builtin_font_data = NULL;
static int builtin_font_size = 0;

bool BKE_vfont_is_builtin(struct VFont *vfont)
{
	return STREQ(vfont->name, FO_BUILTIN_NAME);
}

void BKE_vfont_builtin_register(void *mem, int size)
{
	builtin_font_data = mem;
	builtin_font_size = size;
}

static PackedFile *get_builtin_packedfile(void)
{
	if (!builtin_font_data) {
		printf("Internal error, builtin font not loaded\n");

		return NULL;
	}
	else {
		void *mem = MEM_mallocN(builtin_font_size, "vfd_builtin");

		memcpy(mem, builtin_font_data, builtin_font_size);
	
		return newPackedFileMemory(mem, builtin_font_size);
	}
}

static VFontData *vfont_get_data(Main *bmain, VFont *vfont)
{
	if (vfont == NULL) {
		return NULL;
	}

	/* And then set the data */
	if (!vfont->data) {
		PackedFile *pf;

		if (BKE_vfont_is_builtin(vfont)) {
			pf = get_builtin_packedfile();
		}
		else {
			if (vfont->packedfile) {
				pf = vfont->packedfile;

				/* We need to copy a tmp font to memory unless it is already there */
				if (vfont->temp_pf == NULL) {
					vfont->temp_pf = dupPackedFile(pf);
				}
			}
			else {
				pf = newPackedFile(NULL, vfont->name, ID_BLEND_PATH(bmain, &vfont->id));

				if (vfont->temp_pf == NULL) {
					vfont->temp_pf = newPackedFile(NULL, vfont->name, ID_BLEND_PATH(bmain, &vfont->id));
				}
			}
			if (!pf) {
				printf("Font file doesn't exist: %s\n", vfont->name);

				/* DON'T DO THIS
				 * missing file shouldn't modifty path! - campbell */
#if 0
				strcpy(vfont->name, FO_BUILTIN_NAME);
#endif
				pf = get_builtin_packedfile();
			}
		}
		
		if (pf) {
			vfont->data = BLI_vfontdata_from_freetypefont(pf);
			if (pf != vfont->packedfile) {
				freePackedFile(pf);
			}
		}
	}
	
	return vfont->data;
}

VFont *BKE_vfont_load(Main *bmain, const char *name)
{
	char filename[FILE_MAXFILE];
	VFont *vfont = NULL;
	PackedFile *pf;
	PackedFile *temp_pf = NULL;
	int is_builtin;
	
	if (STREQ(name, FO_BUILTIN_NAME)) {
		BLI_strncpy(filename, name, sizeof(filename));
		
		pf = get_builtin_packedfile();
		is_builtin = TRUE;
	}
	else {
		BLI_split_file_part(name, filename, sizeof(filename));
		pf = newPackedFile(NULL, name, bmain->name);
		temp_pf = newPackedFile(NULL, name, bmain->name);
		
		is_builtin = FALSE;
	}

	if (pf) {
		VFontData *vfd;

		vfd = BLI_vfontdata_from_freetypefont(pf);
		if (vfd) {
			vfont = BKE_libblock_alloc(&bmain->vfont, ID_VF, filename);
			vfont->data = vfd;

			/* if there's a font name, use it for the ID name */
			if (vfd->name[0] != '\0') {
				BLI_strncpy(vfont->id.name + 2, vfd->name, sizeof(vfont->id.name) - 2);
			}
			BLI_strncpy(vfont->name, name, sizeof(vfont->name));

			/* if autopack is on store the packedfile in de font structure */
			if (!is_builtin && (G.fileflags & G_AUTOPACK)) {
				vfont->packedfile = pf;
			}

			/* Do not add FO_BUILTIN_NAME to temporary listbase */
			if (strcmp(filename, FO_BUILTIN_NAME)) {
				vfont->temp_pf = temp_pf;
			}
		}

		/* Free the packed file */
		if (!vfont || vfont->packedfile != pf) {
			freePackedFile(pf);
		}
	}
	
	return vfont;
}

static VFont *which_vfont(Curve *cu, CharInfo *info)
{
	switch (info->flag & (CU_CHINFO_BOLD | CU_CHINFO_ITALIC)) {
		case CU_CHINFO_BOLD:
			return cu->vfontb ? cu->vfontb : cu->vfont;
		case CU_CHINFO_ITALIC:
			return cu->vfonti ? cu->vfonti : cu->vfont;
		case (CU_CHINFO_BOLD | CU_CHINFO_ITALIC):
			return cu->vfontbi ? cu->vfontbi : cu->vfont;
		default:
			return cu->vfont;
	}
}

VFont *BKE_vfont_builtin_get(void)
{
	VFont *vfont;
	
	for (vfont = G.main->vfont.first; vfont; vfont = vfont->id.next) {
		if (BKE_vfont_is_builtin(vfont)) {
			return vfont;
		}
	}
	
	return BKE_vfont_load(G.main, FO_BUILTIN_NAME);
}

static VChar *find_vfont_char(VFontData *vfd, intptr_t character)
{
	VChar *che = NULL;

	/* TODO: use ghash */
	for (che = vfd->characters.first; che; che = che->next) {
		if (che->index == character)
			break;
	}
	return che; /* NULL if not found */
}
		
static void build_underline(Curve *cu, float x1, float y1, float x2, float y2, int charidx, short mat_nr)
{
	Nurb *nu2;
	BPoint *bp;
	
	nu2 = (Nurb *) MEM_callocN(sizeof(Nurb), "underline_nurb");
	if (nu2 == NULL) return;
	nu2->resolu = cu->resolu;
	nu2->bezt = NULL;
	nu2->knotsu = nu2->knotsv = NULL;
	nu2->flag = CU_2D;
	nu2->charidx = charidx + 1000;
	if (mat_nr > 0) nu2->mat_nr = mat_nr - 1;
	nu2->pntsu = 4;
	nu2->pntsv = 1;
	nu2->orderu = 4;
	nu2->orderv = 1;
	nu2->flagu = CU_NURB_CYCLIC;

	bp = (BPoint *)MEM_callocN(4 * sizeof(BPoint), "underline_bp");
	if (bp == NULL) {
		MEM_freeN(nu2);
		return;
	}
	nu2->bp = bp;

	nu2->bp[0].vec[0] = x1;
	nu2->bp[0].vec[1] = y1;
	nu2->bp[0].vec[2] = 0;
	nu2->bp[0].vec[3] = 1.0f;
	nu2->bp[1].vec[0] = x2;
	nu2->bp[1].vec[1] = y1;
	nu2->bp[1].vec[2] = 0;
	nu2->bp[1].vec[3] = 1.0f;
	nu2->bp[2].vec[0] = x2;
	nu2->bp[2].vec[1] = y2;
	nu2->bp[2].vec[2] = 0;
	nu2->bp[2].vec[3] = 1.0f;
	nu2->bp[3].vec[0] = x1;
	nu2->bp[3].vec[1] = y2;
	nu2->bp[3].vec[2] = 0;
	nu2->bp[3].vec[3] = 1.0f;
	
	BLI_addtail(&(cu->nurb), nu2);

}

static void buildchar(Main *bmain, Curve *cu, unsigned long character, CharInfo *info,
                      float ofsx, float ofsy, float rot, int charidx)
{
	BezTriple *bezt1, *bezt2;
	Nurb *nu1 = NULL, *nu2 = NULL;
	float *fp, fsize, shear, x, si, co;
	VFontData *vfd = NULL;
	VChar *che = NULL;
	int i;

	vfd = vfont_get_data(bmain, which_vfont(cu, info));
	if (!vfd) return;

#if 0
	if (cu->selend < cu->selstart) {
		if ((charidx >= (cu->selend)) && (charidx <= (cu->selstart - 2)))
			sel = 1;
	}
	else {
		if ((charidx >= (cu->selstart - 1)) && (charidx <= (cu->selend - 1)))
			sel = 1;
	}
#endif

	/* make a copy at distance ofsx, ofsy with shear */
	fsize = cu->fsize;
	shear = cu->shear;
	si = sinf(rot);
	co = cosf(rot);

	che = find_vfont_char(vfd, character);
	
	/* Select the glyph data */
	if (che)
		nu1 = che->nurbsbase.first;

	/* Create the character */
	while (nu1) {
		bezt1 = nu1->bezt;
		if (bezt1) {
			nu2 = (Nurb *) MEM_mallocN(sizeof(Nurb), "duplichar_nurb");
			if (nu2 == NULL) break;
			memcpy(nu2, nu1, sizeof(struct Nurb));
			nu2->resolu = cu->resolu;
			nu2->bp = NULL;
			nu2->knotsu = nu2->knotsv = NULL;
			nu2->flag = CU_SMOOTH;
			nu2->charidx = charidx;
			if (info->mat_nr > 0) {
				nu2->mat_nr = info->mat_nr - 1;
			}
			else {
				nu2->mat_nr = 0;
			}
			/* nu2->trim.first = 0; */
			/* nu2->trim.last = 0; */
			i = nu2->pntsu;

			bezt2 = (BezTriple *)MEM_mallocN(i * sizeof(BezTriple), "duplichar_bezt2");
			if (bezt2 == NULL) {
				MEM_freeN(nu2);
				break;
			}
			memcpy(bezt2, bezt1, i * sizeof(struct BezTriple));
			nu2->bezt = bezt2;
			
			if (shear != 0.0f) {
				bezt2 = nu2->bezt;
				
				for (i = nu2->pntsu; i > 0; i--) {
					bezt2->vec[0][0] += shear * bezt2->vec[0][1];
					bezt2->vec[1][0] += shear * bezt2->vec[1][1];
					bezt2->vec[2][0] += shear * bezt2->vec[2][1];
					bezt2++;
				}
			}
			if (rot != 0.0f) {
				bezt2 = nu2->bezt;
				for (i = nu2->pntsu; i > 0; i--) {
					fp = bezt2->vec[0];

					x = fp[0];
					fp[0] = co * x + si * fp[1];
					fp[1] = -si * x + co * fp[1];
					x = fp[3];
					fp[3] = co * x + si * fp[4];
					fp[4] = -si * x + co * fp[4];
					x = fp[6];
					fp[6] = co * x + si * fp[7];
					fp[7] = -si * x + co * fp[7];

					bezt2++;
				}
			}
			bezt2 = nu2->bezt;

			if (info->flag & CU_CHINFO_SMALLCAPS_CHECK) {
				const float sca = cu->smallcaps_scale;
				for (i = nu2->pntsu; i > 0; i--) {
					fp = bezt2->vec[0];
					fp[0] *= sca;
					fp[1] *= sca;
					fp[3] *= sca;
					fp[4] *= sca;
					fp[6] *= sca;
					fp[7] *= sca;
					bezt2++;
				}
			}
			bezt2 = nu2->bezt;

			for (i = nu2->pntsu; i > 0; i--) {
				fp = bezt2->vec[0];
				fp[0] = (fp[0] + ofsx) * fsize;
				fp[1] = (fp[1] + ofsy) * fsize;
				fp[3] = (fp[3] + ofsx) * fsize;
				fp[4] = (fp[4] + ofsy) * fsize;
				fp[6] = (fp[6] + ofsx) * fsize;
				fp[7] = (fp[7] + ofsy) * fsize;
				bezt2++;
			}
			
			BLI_addtail(&(cu->nurb), nu2);
		}
		
		nu1 = nu1->next;
	}
}

int BKE_vfont_select_get(Object *ob, int *start, int *end)
{
	Curve *cu = ob->data;
	
	if (cu->editfont == NULL || ob->type != OB_FONT) return 0;

	if (cu->selstart == 0) return 0;
	if (cu->selstart <= cu->selend) {
		*start = cu->selstart - 1;
		*end = cu->selend - 1;
		return 1;
	}
	else {
		*start = cu->selend;
		*end = cu->selstart - 2;
		return -1;
	}
}

static float char_width(Curve *cu, VChar *che, CharInfo *info)
{
	/* The character wasn't found, propably ascii = 0, then the width shall be 0 as well */
	if (che == NULL) {
		return 0.0f;
	}
	else if (info->flag & CU_CHINFO_SMALLCAPS_CHECK) {
		return che->width * cu->smallcaps_scale;
	}
	else {
		return che->width;
	}
}

struct CharTrans *BKE_vfont_to_curve(Main *bmain, Scene *scene, Object *ob, int mode)
{
	VFont *vfont, *oldvfont;
	VFontData *vfd = NULL;
	Curve *cu;
	CharInfo *info = NULL, *custrinfo;
	TextBox *tb;
	VChar *che;
	struct CharTrans *chartransdata = NULL, *ct;
	float *f, xof, yof, xtrax, linedist, *linedata, *linedata2, *linedata3, *linedata4;
	float twidth, maxlen = 0;
	int i, slen, j;
	int curbox;
	int selstart, selend;
	int utf8len;
	short cnr = 0, lnr = 0, wsnr = 0;
	wchar_t *mem, *tmp, ascii;

	/* remark: do calculations including the trailing '\0' of a string
	 * because the cursor can be at that location */

	if (ob->type != OB_FONT) return NULL;

	/* Set font data */
	cu = (Curve *) ob->data;
	vfont = cu->vfont;

	if (cu->str == NULL) return NULL;
	if (vfont == NULL) return NULL;

	/* Create unicode string */
	utf8len = BLI_strlen_utf8(cu->str);
	mem = MEM_callocN(((utf8len + 1) * sizeof(wchar_t)), "convertedmem");

	BLI_strncpy_wchar_from_utf8(mem, cu->str, utf8len + 1);

	/* Count the wchar_t string length */
	slen = wcslen(mem);

	if (cu->ulheight == 0.0f)
		cu->ulheight = 0.05f;
	
	if (cu->strinfo == NULL) /* old file */
		cu->strinfo = MEM_callocN((slen + 4) * sizeof(CharInfo), "strinfo compat");
	
	custrinfo = cu->strinfo;
	if (cu->editfont)
		custrinfo = cu->editfont->textbufinfo;
	
	if (cu->tb == NULL)
		cu->tb = MEM_callocN(MAXTEXTBOX * sizeof(TextBox), "TextBox compat");

	vfd = vfont_get_data(bmain, vfont);

	/* The VFont Data can not be found */
	if (!vfd) {
		if (mem)
			MEM_freeN(mem);
		return NULL;
	}

	/* calc offset and rotation of each char */
	ct = chartransdata =
	         (struct CharTrans *)MEM_callocN((slen + 1) * sizeof(struct CharTrans), "buildtext");

	/* We assume the worst case: 1 character per line (is freed at end anyway) */

	linedata  = MEM_mallocN(sizeof(float) * (slen * 2 + 1), "buildtext2");
	linedata2 = MEM_mallocN(sizeof(float) * (slen * 2 + 1), "buildtext3");
	linedata3 = MEM_callocN(sizeof(float) * (slen * 2 + 1), "buildtext4");
	linedata4 = MEM_callocN(sizeof(float) * (slen * 2 + 1), "buildtext5");
	
	linedist = cu->linedist;
	
	xof = cu->xof + (cu->tb[0].x / cu->fsize);
	yof = cu->yof + (cu->tb[0].y / cu->fsize);

	xtrax = 0.5f * cu->spacing - 0.5f;

	oldvfont = NULL;

	for (i = 0; i < slen; i++) custrinfo[i].flag &= ~(CU_CHINFO_WRAP | CU_CHINFO_SMALLCAPS_CHECK);

	if (cu->selboxes) MEM_freeN(cu->selboxes);
	cu->selboxes = NULL;
	if (BKE_vfont_select_get(ob, &selstart, &selend))
		cu->selboxes = MEM_callocN((selend - selstart + 1) * sizeof(SelBox), "font selboxes");

	tb = &(cu->tb[0]);
	curbox = 0;
	for (i = 0; i <= slen; i++) {
makebreak:
		/* Characters in the list */
		info = &(custrinfo[i]);
		ascii = mem[i];
		if (info->flag & CU_CHINFO_SMALLCAPS) {
			ascii = towupper(ascii);
			if (mem[i] != ascii) {
				mem[i] = ascii;
				info->flag |= CU_CHINFO_SMALLCAPS_CHECK;
			}
		}

		vfont = which_vfont(cu, info);
		
		if (vfont == NULL) break;

		che = find_vfont_char(vfd, ascii);

		/*
		 * The character wasn't in the current curve base so load it
		 * But if the font is built-in then do not try loading since
		 * whole font is in the memory already
		 */
		if (che == NULL && BKE_vfont_is_builtin(vfont) == FALSE) {
			BLI_vfontchar_from_freetypefont(vfont, ascii);
		}

		/* Try getting the character again from the list */
		che = find_vfont_char(vfd, ascii);

		/* No VFont found */
		if (vfont == NULL) {
			if (mem)
				MEM_freeN(mem);
			MEM_freeN(chartransdata);
			return NULL;
		}

		if (vfont != oldvfont) {
			vfd = vfont_get_data(bmain, vfont);
			oldvfont = vfont;
		}

		/* VFont Data for VFont couldn't be found */
		if (!vfd) {
			if (mem)
				MEM_freeN(mem);
			MEM_freeN(chartransdata);
			return NULL;
		}

		twidth = char_width(cu, che, info);

		/* Calculate positions */
		if ((tb->w != 0.0f) &&
		    (ct->dobreak == 0) &&
		    (((xof - (tb->x / cu->fsize) + twidth) * cu->fsize) > tb->w + cu->xof * cu->fsize))
		{
			//		fprintf(stderr, "linewidth exceeded: %c%c%c...\n", mem[i], mem[i+1], mem[i+2]);
			for (j = i; j && (mem[j] != '\n') && (mem[j] != '\r') && (chartransdata[j].dobreak == 0); j--) {
				if (mem[j] == ' ' || mem[j] == '-') {
					ct -= (i - (j - 1));
					cnr -= (i - (j - 1));
					if (mem[j] == ' ') wsnr--;
					if (mem[j] == '-') wsnr++;
					i = j - 1;
					xof = ct->xof;
					ct[1].dobreak = 1;
					custrinfo[i + 1].flag |= CU_CHINFO_WRAP;
					goto makebreak;
				}
				if (chartransdata[j].dobreak) {
					//				fprintf(stderr, "word too long: %c%c%c...\n", mem[j], mem[j+1], mem[j+2]);
					ct->dobreak = 1;
					custrinfo[i + 1].flag |= CU_CHINFO_WRAP;
					ct -= 1;
					cnr -= 1;
					i--;
					xof = ct->xof;
					goto makebreak;
				}
			}
		}

		if (ascii == '\n' || ascii == '\r' || ascii == 0 || ct->dobreak) {
			ct->xof = xof;
			ct->yof = yof;
			ct->linenr = lnr;
			ct->charnr = cnr;

			yof -= linedist;

			maxlen = max_ff(maxlen, (xof - tb->x / cu->fsize));
			linedata[lnr] = xof - tb->x / cu->fsize;
			linedata2[lnr] = cnr;
			linedata3[lnr] = tb->w / cu->fsize;
			linedata4[lnr] = wsnr;
			
			if ((tb->h != 0.0f) &&
			    ((-(yof - (tb->y / cu->fsize))) > ((tb->h / cu->fsize) - (linedist * cu->fsize)) - cu->yof) &&
			    (cu->totbox > (curbox + 1)) )
			{
				maxlen = 0;
				tb++;
				curbox++;
				yof = cu->yof + tb->y / cu->fsize;
			}

			/* XXX, has been unused for years, need to check if this is useful, r4613 r5282 - campbell */
#if 0
			if (ascii == '\n' || ascii == '\r')
				xof = cu->xof;
			else
				xof = cu->xof + (tb->x / cu->fsize);
#else
			xof = cu->xof + (tb->x / cu->fsize);
#endif
			lnr++;
			cnr = 0;
			wsnr = 0;
		}
		else if (ascii == 9) {    /* TAB */
			float tabfac;
			
			ct->xof = xof;
			ct->yof = yof;
			ct->linenr = lnr;
			ct->charnr = cnr++;

			tabfac = (xof - cu->xof + 0.01f);
			tabfac = 2.0f * ceilf(tabfac / 2.0f);
			xof = cu->xof + tabfac;
		}
		else {
			SelBox *sb = NULL;
			float wsfac;

			ct->xof = xof;
			ct->yof = yof;
			ct->linenr = lnr;
			ct->charnr = cnr++;

			if (cu->selboxes && (i >= selstart) && (i <= selend)) {
				sb = &(cu->selboxes[i - selstart]);
				sb->y = yof * cu->fsize - linedist * cu->fsize * 0.1f;
				sb->h = linedist * cu->fsize;
				sb->w = xof * cu->fsize;
			}
	
			if (ascii == 32) {
				wsfac = cu->wordspace; 
				wsnr++;
			}
			else {
				wsfac = 1.0f;
			}
			
			/* Set the width of the character */
			twidth = char_width(cu, che, info);

			xof += (twidth * wsfac * (1.0f + (info->kern / 40.0f)) ) + xtrax;
			
			if (sb) {
				sb->w = (xof * cu->fsize) - sb->w;
			}
		}
		ct++;
	}
	
	cu->lines = 1;
	ct = chartransdata;
	tmp = mem;
	for (i = 0; i <= slen; i++, tmp++, ct++) {
		ascii = *tmp;
		if (ascii == '\n' || ascii == '\r' || ct->dobreak) cu->lines++;
	}

	/* linedata is now: width of line
	 * linedata2 is now: number of characters
	 * linedata3 is now: maxlen of that line
	 * linedata4 is now: number of whitespaces of line */

	if (cu->spacemode != CU_LEFT) {
		ct = chartransdata;

		if (cu->spacemode == CU_RIGHT) {
			for (i = 0; i < lnr; i++) linedata[i] = linedata3[i] - linedata[i];
			for (i = 0; i <= slen; i++) {
				ct->xof += linedata[ct->linenr];
				ct++;
			}
		}
		else if (cu->spacemode == CU_MIDDLE) {
			for (i = 0; i < lnr; i++) linedata[i] = (linedata3[i] - linedata[i]) / 2;
			for (i = 0; i <= slen; i++) {
				ct->xof += linedata[ct->linenr];
				ct++;
			}
		}
		else if ((cu->spacemode == CU_FLUSH) && (cu->tb[0].w != 0.0f)) {
			for (i = 0; i < lnr; i++)
				if (linedata2[i] > 1)
					linedata[i] = (linedata3[i] - linedata[i]) / (linedata2[i] - 1);
			for (i = 0; i <= slen; i++) {
				for (j = i; (!ELEM3(mem[j], '\0', '\n', '\r')) && (chartransdata[j].dobreak == 0) && (j < slen); j++) {
					/* do nothing */
				}

//				if ((mem[j] != '\r') && (mem[j] != '\n') && (mem[j])) {
				ct->xof += ct->charnr * linedata[ct->linenr];
//				}
				ct++;
			}
		}
		else if ((cu->spacemode == CU_JUSTIFY) && (cu->tb[0].w != 0.0f)) {
			float curofs = 0.0f;
			for (i = 0; i <= slen; i++) {
				for (j = i; (mem[j]) && (mem[j] != '\n') &&
				     (mem[j] != '\r') && (chartransdata[j].dobreak == 0) && (j < slen);
				     j++)
				{
					/* pass */
				}

				if ((mem[j] != '\r') && (mem[j] != '\n') &&
				    ((chartransdata[j].dobreak != 0)))
				{
					if (mem[i] == ' ') curofs += (linedata3[ct->linenr] - linedata[ct->linenr]) / linedata4[ct->linenr];
					ct->xof += curofs;
				}
				if (mem[i] == '\n' || mem[i] == '\r' || chartransdata[i].dobreak) curofs = 0;
				ct++;
			}
		}
	}
	
	/* TEXT ON CURVE */
	/* Note: Only OB_CURVE objects could have a path  */
	if (cu->textoncurve && cu->textoncurve->type == OB_CURVE) {
		Curve *cucu = cu->textoncurve->data;
		int oldflag = cucu->flag;
		
		cucu->flag |= (CU_PATH + CU_FOLLOW);
		
		if (cucu->path == NULL) BKE_displist_make_curveTypes(scene, cu->textoncurve, 0);
		if (cucu->path) {
			float distfac, imat[4][4], imat3[3][3], cmat[3][3];
			float minx, maxx, miny, maxy;
			float timeofs, sizefac;
			
			invert_m4_m4(imat, ob->obmat);
			copy_m3_m4(imat3, imat);

			copy_m3_m4(cmat, cu->textoncurve->obmat);
			mul_m3_m3m3(cmat, cmat, imat3);
			sizefac = normalize_v3(cmat[0]) / cu->fsize;
			
			minx = miny = 1.0e20f;
			maxx = maxy = -1.0e20f;
			ct = chartransdata;
			for (i = 0; i <= slen; i++, ct++) {
				if (minx > ct->xof) minx = ct->xof;
				if (maxx < ct->xof) maxx = ct->xof;
				if (miny > ct->yof) miny = ct->yof;
				if (maxy < ct->yof) maxy = ct->yof;
			}
			
			/* we put the x-coordinaat exact at the curve, the y is rotated */
			
			/* length correction */
			distfac = sizefac * cucu->path->totdist / (maxx - minx);
			timeofs = 0.0f;
			
			if (distfac > 1.0f) {
				/* path longer than text: spacemode involves */
				distfac = 1.0f / distfac;
				
				if (cu->spacemode == CU_RIGHT) {
					timeofs = 1.0f - distfac;
				}
				else if (cu->spacemode == CU_MIDDLE) {
					timeofs = (1.0f - distfac) / 2.0f;
				}
				else if (cu->spacemode == CU_FLUSH) {
					distfac = 1.0f;
				}
			}
			else {
				distfac = 1.0;
			}

			distfac /= (maxx - minx);
			
			timeofs += distfac * cu->xof;  /* not cyclic */
			
			ct = chartransdata;
			for (i = 0; i <= slen; i++, ct++) {
				float ctime, dtime, vec[4], tvec[4], rotvec[3];
				float si, co;
				
				/* rotate around center character */
				ascii = mem[i];

				che = find_vfont_char(vfd, ascii);
	
				twidth = char_width(cu, che, info);

				dtime = distfac * 0.5f * twidth;

				ctime = timeofs + distfac * (ct->xof - minx);
				CLAMP(ctime, 0.0f, 1.0f);

				/* calc the right loc AND the right rot separately */
				/* vec, tvec need 4 items */
				where_on_path(cu->textoncurve, ctime, vec, tvec, NULL, NULL, NULL);
				where_on_path(cu->textoncurve, ctime + dtime, tvec, rotvec, NULL, NULL, NULL);
				
				mul_v3_fl(vec, sizefac);
				
				ct->rot = (float)M_PI - atan2f(rotvec[1], rotvec[0]);

				si = sinf(ct->rot);
				co = cosf(ct->rot);

				yof = ct->yof;
				
				ct->xof = vec[0] + si * yof;
				ct->yof = vec[1] + co * yof;
				
			}
			cucu->flag = oldflag;
		}
	}

	if (cu->selboxes) {
		ct = chartransdata;
		for (i = 0; i <= selend; i++, ct++) {
			if (i >= selstart) {
				cu->selboxes[i - selstart].x = ct->xof * cu->fsize;
				cu->selboxes[i - selstart].y = ct->yof * cu->fsize;
			}
		}
	}

	if (mode == FO_CURSUP || mode == FO_CURSDOWN || mode == FO_PAGEUP || mode == FO_PAGEDOWN) {
		/* 2: curs up
		 * 3: curs down */
		ct = chartransdata + cu->pos;
		
		if ((mode == FO_CURSUP || mode == FO_PAGEUP) && ct->linenr == 0) {
			/* pass */
		}
		else if ((mode == FO_CURSDOWN || mode == FO_PAGEDOWN) && ct->linenr == lnr) {
			/* pass */
		}
		else {
			switch (mode) {
				case FO_CURSUP:     lnr = ct->linenr - 1; break;
				case FO_CURSDOWN:   lnr = ct->linenr + 1; break;
				case FO_PAGEUP:     lnr = ct->linenr - 10; break;
				case FO_PAGEDOWN:   lnr = ct->linenr + 10; break;
			}
			cnr = ct->charnr;
			/* seek for char with lnr en cnr */
			cu->pos = 0;
			ct = chartransdata;
			for (i = 0; i < slen; i++) {
				if (ct->linenr == lnr) {
					if ((ct->charnr == cnr) || ((ct + 1)->charnr == 0)) {
						break;
					}
				}
				else if (ct->linenr > lnr) {
					break;
				}
				cu->pos++;
				ct++;
			}
		}
	}
	
	/* cursor first */
	if (cu->editfont) {
		float si, co;
		
		ct = chartransdata + cu->pos;
		si = sinf(ct->rot);
		co = cosf(ct->rot);

		f = cu->editfont->textcurs[0];
		
		f[0] = cu->fsize * (-0.1f * co + ct->xof);
		f[1] = cu->fsize * ( 0.1f * si + ct->yof);
		
		f[2] = cu->fsize * ( 0.1f * co + ct->xof);
		f[3] = cu->fsize * (-0.1f * si + ct->yof);
		
		f[4] = cu->fsize * ( 0.1f * co + 0.8f * si + ct->xof);
		f[5] = cu->fsize * (-0.1f * si + 0.8f * co + ct->yof);
		
		f[6] = cu->fsize * (-0.1f * co + 0.8f * si + ct->xof);
		f[7] = cu->fsize * ( 0.1f * si + 0.8f * co + ct->yof);
		
	}

	MEM_freeN(linedata);
	MEM_freeN(linedata2);
	MEM_freeN(linedata3);
	MEM_freeN(linedata4);

	if (mode == FO_SELCHANGE) {
		MEM_freeN(chartransdata);
		MEM_freeN(mem);
		return NULL;
	}

	if (mode == FO_EDIT) {
		/* make nurbdata */
		BKE_nurbList_free(&cu->nurb);
		
		ct = chartransdata;
		if (cu->sepchar == 0) {
			for (i = 0; i < slen; i++) {
				unsigned long cha = (uintptr_t) mem[i];
				info = &(custrinfo[i]);
				if (info->mat_nr > (ob->totcol)) {
					/* printf("Error: Illegal material index (%d) in text object, setting to 0\n", info->mat_nr); */
					info->mat_nr = 0;
				}
				/* We do not want to see any character for \n or \r */
				if (cha != '\n' && cha != '\r')
					buildchar(bmain, cu, cha, info, ct->xof, ct->yof, ct->rot, i);
				
				if ((info->flag & CU_CHINFO_UNDERLINE) && (cu->textoncurve == NULL) && (cha != '\n') && (cha != '\r')) {
					float ulwidth, uloverlap = 0.0f;
					
					if ((i < (slen - 1)) && (mem[i + 1] != '\n') && (mem[i + 1] != '\r') &&
					    ((mem[i + 1] != ' ') || (custrinfo[i + 1].flag & CU_CHINFO_UNDERLINE)) &&
					    ((custrinfo[i + 1].flag & CU_CHINFO_WRAP) == 0))
					{
						uloverlap = xtrax + 0.1f;
					}
					/* Find the character, the characters has to be in the memory already
					 * since character checking has been done earlier already. */
					che = find_vfont_char(vfd, cha);

					twidth = char_width(cu, che, info);
					ulwidth = cu->fsize * ((twidth * (1.0f + (info->kern / 40.0f))) + uloverlap);
					build_underline(cu, ct->xof * cu->fsize, ct->yof * cu->fsize + (cu->ulpos - 0.05f) * cu->fsize,
					                ct->xof * cu->fsize + ulwidth,
					                ct->yof * cu->fsize + (cu->ulpos - 0.05f) * cu->fsize - cu->ulheight * cu->fsize,
					                i, info->mat_nr);
				}
				ct++;
			}
		}
		else {
			int outta = 0;
			for (i = 0; (i < slen) && (outta == 0); i++) {
				ascii = mem[i];
				info = &(custrinfo[i]);
				if (cu->sepchar == (i + 1)) {
					float vecyo[3];

					vecyo[0] = ct->xof;
					vecyo[1] = ct->yof;
					vecyo[2] = 0.0f;

					mem[0] = ascii;
					mem[1] = 0;
					custrinfo[0] = *info;
					cu->pos = 1;
					cu->len = 1;
					mul_v3_m4v3(ob->loc, ob->obmat, vecyo);
					outta = 1;
					cu->sepchar = 0;
				}
				ct++;
			}
		}
	}

	if (mode == FO_DUPLI) {
		MEM_freeN(mem);
		return chartransdata;
	}

	if (mem)
		MEM_freeN(mem);

	MEM_freeN(chartransdata);
	return NULL;
}
