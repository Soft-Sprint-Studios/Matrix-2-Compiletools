//#pragma warning(disable: 4018) // '<' : signed/unsigned mismatch

#include "csg.h"

int             g_nummapbrushes;
brush_t         g_mapbrushes[MAX_MAP_BRUSHES];

int             g_nummapbrushsides;
side_t          g_mapbrushsides[MAX_MAP_SIDES];

int             g_nMapFileVersion;

static const vec3_t   s_baseaxis[18] = {
    {0, 0, 1}, {1, 0, 0}, {0, -1, 0},                      // floor
    {0, 0, -1}, {1, 0, 0}, {0, -1, 0},                     // ceiling
    {1, 0, 0}, {0, 1, 0}, {0, 0, -1},                      // west wall
    {-1, 0, 0}, {0, 1, 0}, {0, 0, -1},                     // east wall
    {0, 1, 0}, {1, 0, 0}, {0, 0, -1},                      // south wall
    {0, -1, 0}, {1, 0, 0}, {0, 0, -1},                     // north wall
};

int				g_numparsedentities;
int				g_numparsedbrushes;

brush_t *CopyCurrentBrush (entity_t *entity, const brush_t *brush)
{
	if (entity->firstbrush + entity->numbrushes != g_nummapbrushes)
	{
		Error ("CopyCurrentBrush: internal error.");
	}
	brush_t *newb = &g_mapbrushes[g_nummapbrushes];
	g_nummapbrushes++;
	hlassume (g_nummapbrushes <= MAX_MAP_BRUSHES, assume_MAX_MAP_BRUSHES);
	memcpy (newb, brush, sizeof (brush_t));
	newb->firstside = g_nummapbrushsides;
	newb->original_sides = &g_mapbrushsides[newb->firstside];
	g_nummapbrushsides += brush->numsides;
	hlassume (g_nummapbrushsides <= MAX_MAP_SIDES, assume_MAX_MAP_SIDES);

	for(int i = 0; i < brush->numsides; i++)
	{
		side_t* psrc = &g_mapbrushsides[brush->firstside + i];
		side_t* pdest = &g_mapbrushsides[newb->firstside + i];

		pdest->bevel = psrc->bevel;
		pdest->brushbevel = psrc->brushbevel;
		pdest->planenum = psrc->planenum;
		pdest->td = psrc->td;
		pdest->texinfo = psrc->texinfo;
		pdest->treatasskip = psrc->treatasskip;

		if(psrc->ptempwinding)
			pdest->ptempwinding = new Winding(*psrc->ptempwinding);
		else
			pdest->ptempwinding = nullptr;

		memcpy(pdest->planepts, psrc->planepts, sizeof(side_t::planepts));
	}

	newb->entitynum = entity - g_entities;
	newb->brushnum = entity->numbrushes;
	entity->numbrushes++;
	for (int h = 0; h < NUM_HULLS; h++)
	{
		if (brush->hullshapes[h] != NULL)
		{
			newb->hullshapes[h] = _strdup (brush->hullshapes[h]);
		}
		else
		{
			newb->hullshapes[h] = NULL;
		}
	}
	return newb;
}
void DeleteCurrentEntity (entity_t *entity)
{
	if (entity != &g_entities[g_numentities - 1])
	{
		Error ("DeleteCurrentEntity: internal error.");
	}
	if (entity->firstbrush + entity->numbrushes != g_nummapbrushes)
	{
		Error ("DeleteCurrentEntity: internal error.");
	}
	for (int i = entity->numbrushes - 1; i >= 0; i--)
	{
		brush_t *b = &g_mapbrushes[entity->firstbrush + i];
		if (b->firstside + b->numsides != g_nummapbrushsides)
		{
			Error ("DeleteCurrentEntity: internal error. (Entity %i, Brush %i)",
				b->originalentitynum, b->originalbrushnum
				);
		}
		memset (&g_mapbrushsides[b->firstside], 0, b->numsides * sizeof (side_t));
		g_nummapbrushsides -= b->numsides;
		for (int h = 0; h < NUM_HULLS; h++)
		{
			if (b->hullshapes[h])
			{
				free (b->hullshapes[h]);
			}
		}
	}
	memset (&g_mapbrushes[entity->firstbrush], 0, entity->numbrushes * sizeof (brush_t));
	g_nummapbrushes -= entity->numbrushes;
	while (entity->epairs)
	{
		DeleteKey (entity, entity->epairs->key);
	}
	memset (entity, 0, sizeof(entity_t));
	g_numentities--;
}
// =====================================================================================
//  TextureAxisFromPlane
// =====================================================================================
void            TextureAxisFromPlane(const plane_t* const pln, vec3_t xv, vec3_t yv)
{
    int             bestaxis;
    vec_t           dot, best;
    int             i;

    best = 0;
    bestaxis = 0;

    for (i = 0; i < 6; i++)
    {
        dot = DotProduct(pln->normal, s_baseaxis[i * 3]);
        if (dot > best)
        {
            best = dot;
            bestaxis = i;
        }
    }

    VectorCopy(s_baseaxis[bestaxis * 3 + 1], xv);
    VectorCopy(s_baseaxis[bestaxis * 3 + 2], yv);
}

#define ScaleCorrection	(1.0/128.0)


// =====================================================================================
//  CheckForInvisible
//      see if a brush is part of an invisible entity (KGP)
// =====================================================================================
static bool CheckForInvisible(entity_t* mapent)
{
	using namespace std;

	string keyval(ValueForKey(mapent,"classname"));
	if(g_invisible_items.count(keyval))
	{ return true; }

	keyval.assign(ValueForKey(mapent,"targetname"));
	if(g_invisible_items.count(keyval))
	{ return true; }

	keyval.assign(ValueForKey(mapent,"zhlt_invisible"));
	if(!keyval.empty() && strcmp(keyval.c_str(),"0"))
	{ return true; }

	return false;
}

/*
================
MakeBrushWindings

makes basewindigs for sides and mins / maxs for the brush
================
*/
void MakeBrushWindings(brush_t *ob) 
{
    int i, j;
    side_t *side;
    plane_t *plane;

	VectorFill(ob->mins, MAX_FLOAT_VALUE);
	VectorFill(ob->maxs, -MAX_FLOAT_VALUE);

    for (i = 0; i < ob->numsides; i++) 
	{
        plane = &g_mapplanes[ob->original_sides[i].planenum];
        Winding* w = new Winding(plane->normal, plane->dist);

        for (j = 0; j < ob->numsides && w; j++) 
		{
            if (i == j)
                continue;

            if (ob->original_sides[j].brushbevel)
                continue;

            plane = &g_mapplanes[ob->original_sides[j].planenum ^ 1];
			w->Chop(plane->normal, plane->dist, 0);
        }

        side = &ob->original_sides[i];
		if(side->ptempwinding)
			delete side->ptempwinding;

		side->ptempwinding = w;

        if (w) 
		{
            for (j = 0; j < w->m_NumPoints; j++)
			{
				for(int k = 0; k < 3; k++)
				{
					if(ob->mins[k] > w->m_Points[j][k])
						ob->mins[k] = w->m_Points[j][k];

					if(ob->maxs[k] < w->m_Points[j][k])
						ob->maxs[k] = w->m_Points[j][k];
				}
			}
        }
    }
}

/*
==============
SnapVector
==============
*/
void SnapVector(vec3_t& normal) 
{
    int32_t i;

    for (i = 0; i < 3; i++) {
        if (fabs(normal[i] - 1) < NORMAL_EPSILON) 
		{
            VectorClear(normal);
            normal[i] = 1;
            break;
        }

        if (fabs(normal[i] - -1) < NORMAL_EPSILON) 
		{
            VectorClear(normal);
            normal[i] = -1;
            break;
        }
    }
}

/*
================
PlaneEqual
================
*/

#define DIST_EPSILON 0.01
bool PlaneEqual(plane_t *p, vec3_t normal, vec_t dist) {
    if (fabs(p->normal[0] - normal[0]) < NORMAL_EPSILON && fabs(p->normal[1] - normal[1]) < NORMAL_EPSILON && fabs(p->normal[2] - normal[2]) < NORMAL_EPSILON && fabs(p->dist - dist) < DIST_EPSILON)
        return true;
	else
		return false;
}


/*
=================
AddBrushBevels

Adds any additional planes necessary to allow the brush to be expanded
against axial bounding boxes
=================
*/
static void AddBrushBevels(brush_t *b) 
{
    int axis, dir;
    int i, j, k, l, order;
    side_t sidetemp;
    side_t *s, *s2;
    vec3_t normal;
    vec_t dist;
    Winding *w, *w2;
    vec3_t vec, vec2;
    vec_t d;

    //
    // add the axial planes
    //
    order = 0;
    for (axis = 0; axis < 3; axis++) 
	{
        for (dir = -1; dir <= 1; dir += 2, order++) 
		{
            // see if the plane is allready present
            for (i = 0, s = b->original_sides; i < b->numsides; i++, s++) 
			{
                if (g_mapplanes[s->planenum].normal[axis] == dir)
                    break;
            }

            if (i == b->numsides) 
			{
                // add a new side
                if (g_nummapbrushsides == MAX_MAP_BRUSHSIDES)
                    Error("Exceeded MAX_MAP_BRUSHSIDES");

                g_nummapbrushsides++;
                b->numsides++;

                VectorClear(normal);
                normal[axis] = dir;
                if (dir == 1)
                    dist = b->maxs[axis];
                else
                    dist = -b->mins[axis];

				vec3_t origin;
				VectorScale(normal, dist, origin);

                s->planenum = FindIntPlane(normal, origin);
                s->texinfo  = b->original_sides[0].texinfo;
				s->td = b->original_sides[0].td;
                s->brushbevel = true;
            }

            // if the plane is not in it canonical order, swap it
            if (i != order) 
			{
                sidetemp = b->original_sides[order];
                b->original_sides[order] = b->original_sides[i];
                b->original_sides[i] = sidetemp;
            }
        }
    }

    //
    // add the edge bevels
    //
    if (b->numsides == 6)
        return; // pure axial

    // test the non-axial plane edges
    for (i = 6; i < b->numsides; i++) 
	{
        s = b->original_sides + i;
		if(s->brushbevel)
			continue;

        w = s->ptempwinding;
        if (!w)
            continue;

        for (j = 0; j < w->m_NumPoints; j++) 
		{
            k = (j + 1) % w->m_NumPoints;
            VectorSubtract(w->m_Points[j], w->m_Points[k], vec);
            if (VectorNormalize(vec) < 0.5)
                continue;

            SnapVector(vec);
            for (k = 0; k < 3; k++)
			{
                if (vec[k] == -1 || vec[k] == 1)
                    break; // axial
			}

            if (k != 3)
                continue; // only test non-axial edges

            // try the six possible slanted axials from this edge
            for (axis = 0; axis < 3; axis++) 
			{
                for (dir = -1; dir <= 1; dir += 2) 
				{
                    // construct a plane
                    VectorClear(vec2);
                    vec2[axis] = dir;
                    CrossProduct(vec, vec2, normal);
                    if (VectorNormalize(normal) < 0.5)
                        continue;

                    dist = DotProduct(w->m_Points[j], normal);

                    // if all the points on all the sides are
                    // behind this plane, it is a proper edge bevel
                    for (k = 0; k < b->numsides; k++) 
					{
                        // if this plane has allready been used, skip it
                        if (PlaneEqual(&g_mapplanes[b->original_sides[k].planenum], normal, dist))
                            break;

                        w2 = b->original_sides[k].ptempwinding;
                        if (!w2)
                            continue;

                        for (l = 0; l < w2->m_NumPoints; l++) 
						{
                            d = DotProduct(w2->m_Points[l], normal) - dist;
                            if (d > 0.1)
                                break; // point in front
                        }

                        if (l != w2->m_NumPoints)
                            break;
                    }

                    if (k != b->numsides)
                        continue; // wasn't part of the outer hull

                    // add this plane
                    if (g_nummapbrushsides == MAX_MAP_BRUSHSIDES)
                        Error("Exceeded MAX_MAP_BRUSHSIDES");

                    g_nummapbrushsides++;

					vec3_t origin;
					VectorScale(normal, dist, origin);

                    s2 = &b->original_sides[b->numsides];
                    s2->planenum = FindIntPlane(normal, origin);
                    s2->texinfo = b->original_sides[0].texinfo;
					s2->td = b->original_sides[0].td;
                    s2->brushbevel = true;

                    b->numsides++;
                }
            }
        }
    }

	return;
}

// =====================================================================================
//  ParseBrush
//      parse a brush from script
// =====================================================================================
static void ParseBrush(entity_t* mapent)
{
    brush_t*        b;
    int             i, j;
    side_t*         side;
    contents_t      contents;
    bool            ok;
	bool nullify = CheckForInvisible(mapent);
    hlassume(g_nummapbrushes < MAX_MAP_BRUSHES, assume_MAX_MAP_BRUSHES);

    b = &g_mapbrushes[g_nummapbrushes];
    g_nummapbrushes++;
    b->firstside = g_nummapbrushsides;
	b->originalentitynum = g_numparsedentities;
	b->originalbrushnum = g_numparsedbrushes;
    b->entitynum = g_numentities - 1;
    b->brushnum = g_nummapbrushes - mapent->firstbrush - 1;
	b->original_sides = &g_mapbrushsides[g_nummapbrushsides];

    b->noclip = 0;
	if (IntForKey(mapent, "zhlt_noclip"))
	{
		b->noclip = 1;
	}
	b->cliphull = 0;
	b->bevel = false;
	{
		b->detaillevel = IntForKey (mapent, "zhlt_detaillevel");
		b->chopdown = IntForKey (mapent, "zhlt_chopdown");
		b->chopup = IntForKey (mapent, "zhlt_chopup");
		b->clipnodedetaillevel = IntForKey (mapent, "zhlt_clipnodedetaillevel");
		b->coplanarpriority = IntForKey (mapent, "zhlt_coplanarpriority");
		bool wrong = false;
		if (b->detaillevel < 0)
		{
			wrong = true;
			b->detaillevel = 0;
		}
		if (b->chopdown < 0)
		{
			wrong = true;
			b->chopdown = 0;
		}
		if (b->chopup < 0)
		{
			wrong = true;
			b->chopup = 0;
		}
		if (b->clipnodedetaillevel < 0)
		{
			wrong = true;
			b->clipnodedetaillevel = 0;
		}
		if (wrong)
		{
			Warning ("Entity %i, Brush %i: incorrect settings for detail brush.",
					b->originalentitynum, b->originalbrushnum
					);
		}
	}
	for (int h = 0; h < NUM_HULLS; h++)
	{
		char key[16];
		const char *value;
		sprintf (key, "zhlt_hull%d", h);
		value = ValueForKey (mapent, key);
		if (*value)
		{
			b->hullshapes[h] = _strdup (value);
		}
		else
		{
			b->hullshapes[h] = NULL;
		}
	}

    mapent->numbrushes++;

	ok = GetToken(true);
    while (ok)
    {
        g_TXcommand = 0;
        if (!strcmp(g_token, "}"))
        {
            break;
        }

        hlassume(g_nummapbrushsides < MAX_MAP_SIDES, assume_MAX_MAP_SIDES);
        side = &g_mapbrushsides[g_nummapbrushsides];

		side->bevel = false;
		side->brushbevel = false;
		side->treatasskip = false;

        // read the three point plane definition
        for (i = 0; i < 3; i++)
        {
            if (i != 0)
            {
                GetToken(true);
            }
            if (strcmp(g_token, "("))
            {
                Error("Parsing Entity %i, Brush %i, Side %i : Expecting '(' got '%s'",
					b->originalentitynum, b->originalbrushnum, 
					  b->numsides, g_token);
            }

            for (j = 0; j < 3; j++)
            {
                GetToken(false);
                side->planepts[i][j] = atof(g_token);
            }

            GetToken(false);
            if (strcmp(g_token, ")"))
            {
                Error("Parsing	Entity %i, Brush %i, Side %i : Expecting ')' got '%s'",
					b->originalentitynum, b->originalbrushnum, 
					  b->numsides, g_token);
            }
        }

        // read the     texturedef
        GetToken(false);
        _strupr(g_token);
		{
			if (!strncasecmp (g_token, "NOCLIP", 6) || !strncasecmp (g_token, "NULLNOCLIP", 10))
			{
				strcpy (g_token, "NULL");
				b->noclip = true;
			}
			if (!strncasecmp (g_token, "BEVELBRUSH", 10))
			{
				strcpy (g_token, "NULL");
				b->bevel = true;
			}
			if (!strncasecmp (g_token, "BEVEL", 5))
			{
				strcpy (g_token, "NULL");
				side->bevel = true;
			}
			if (!strncasecmp (g_token, "CLIP", 4))
			{
				b->cliphull |= (1 << NUM_HULLS); // arbitrary nonexistent hull

				int h;
				if (!strncasecmp (g_token, "CLIPHULL", 8) && (h = g_token[8] - '0', 0 < h && h < NUM_HULLS))
				{
					b->cliphull |= (1 << h); // hull h
				}
				if (!strncasecmp (g_token, "CLIPBEVEL", 9))
				{
					side->bevel = true;
				}
				if (!strncasecmp (g_token, "CLIPBEVELBRUSH", 14))
				{
					b->bevel = true;
				}

				side->treatasskip = true;
			}
		}
        safe_strncpy(side->td.name, g_token, sizeof(side->td.name));

        if (g_nMapFileVersion < 220)                       // Worldcraft 2.1-, Radiant
        {
            GetToken(false);
            side->td.vects.valve.shift[0] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.shift[1] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.rotate = atof(g_token);
            GetToken(false);
            side->td.vects.valve.scale[0] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.scale[1] = atof(g_token);
        }
        else                                               // Worldcraft 2.2+
        {
            // texture U axis
            GetToken(false);
            if (strcmp(g_token, "["))
            {
                hlassume(false, assume_MISSING_BRACKET_IN_TEXTUREDEF);
            }

            GetToken(false);
            side->td.vects.valve.UAxis[0] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.UAxis[1] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.UAxis[2] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.shift[0] = atof(g_token);

            GetToken(false);
            if (strcmp(g_token, "]"))
            {
                Error("missing ']' in texturedef (U)");
            }

            // texture V axis
            GetToken(false);
            if (strcmp(g_token, "["))
            {
                Error("missing '[' in texturedef (V)");
            }

            GetToken(false);
            side->td.vects.valve.VAxis[0] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.VAxis[1] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.VAxis[2] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.shift[1] = atof(g_token);

            GetToken(false);
            if (strcmp(g_token, "]"))
            {
                Error("missing ']' in texturedef (V)");
            }

            // Texture rotation is implicit in U/V axes.
            GetToken(false);
            side->td.vects.valve.rotate = 0;

            // texure scale
            GetToken(false);
            side->td.vects.valve.scale[0] = atof(g_token);
            GetToken(false);
            side->td.vects.valve.scale[1] = atof(g_token);
        }

        ok = GetToken(true);                               // Done with line, this reads the first item from the next line

        if ((g_TXcommand == '1' || g_TXcommand == '2'))
        {
            // We are QuArK mode and need to translate some numbers to align textures its way
            // from QuArK, the texture vectors are given directly from the three points
            vec3_t          TexPt[2];
            int             k;
            float           dot22, dot23, dot33, mdet, aa, bb, dd;

            k = g_TXcommand - '0';
            for (j = 0; j < 3; j++)
            {
                TexPt[1][j] = (side->planepts[k][j] - side->planepts[0][j]) * ScaleCorrection;
            }
            k = 3 - k;
            for (j = 0; j < 3; j++)
            {
                TexPt[0][j] = (side->planepts[k][j] - side->planepts[0][j]) * ScaleCorrection;
            }

            dot22 = DotProduct(TexPt[0], TexPt[0]);
            dot23 = DotProduct(TexPt[0], TexPt[1]);
            dot33 = DotProduct(TexPt[1], TexPt[1]);
            mdet = dot22 * dot33 - dot23 * dot23;
            if (mdet < 1E-6 && mdet > -1E-6)
            {
                aa = bb = dd = 0;
                Warning
                    ("Degenerate QuArK-style brush texture : Entity %i, Brush %i @ (%f,%f,%f) (%f,%f,%f)	(%f,%f,%f)",
					b->originalentitynum, b->originalbrushnum, 
					 side->planepts[0][0], side->planepts[0][1], side->planepts[0][2],
                     side->planepts[1][0], side->planepts[1][1], side->planepts[1][2], side->planepts[2][0],
                     side->planepts[2][1], side->planepts[2][2]);
            }
            else
            {
                mdet = 1.0 / mdet;
                aa = dot33 * mdet;
                bb = -dot23 * mdet;
                //cc = -dot23*mdet;             // cc = bb
                dd = dot22 * mdet;
            }

            for (j = 0; j < 3; j++)
            {
                side->td.vects.quark.vects[0][j] = aa * TexPt[0][j] + bb * TexPt[1][j];
                side->td.vects.quark.vects[1][j] = -( /*cc */ bb * TexPt[0][j] + dd * TexPt[1][j]);
            }

            side->td.vects.quark.vects[0][3] = -DotProduct(side->td.vects.quark.vects[0], side->planepts[0]);
            side->td.vects.quark.vects[1][3] = -DotProduct(side->td.vects.quark.vects[1], side->planepts[0]);
        }

        //
        // find the plane number
        //
        int planenum = PlaneFromPoints(side->planepts[0], side->planepts[1], side->planepts[2]);
        if (planenum == -1)
        {
            Fatal(assume_PLANE_WITH_NO_NORMAL, "Entity %i, Brush %i, Side %i: plane with no normal", 
				b->originalentitynum, b->originalbrushnum
				, i);
        }
		
		//
        // see if the plane has been used already
        //
        for (int k = 0; k < b->numsides; k++) 
		{
            side_t* s2 = b->original_sides + k;
            if (s2->planenum == planenum || s2->planenum == (planenum ^ 1))
            {
                Fatal(assume_BRUSH_WITH_COPLANAR_FACES, "Entity %i, Brush %i, Side %i: has a coplanar plane at (%.0f, %.0f, %.0f), texture %s",
					b->originalentitynum, b->originalbrushnum, i, side->planepts[0][0], side->planepts[0][1], side->planepts[0][2], side->td.name);
            }
        }

		side->planenum = planenum;
        side->td.txcommand = g_TXcommand;                  // Quark stuff, but needs setting always
		side->texinfo = TexinfoForBrushTexture(&g_mapplanes[planenum], &side->td, vec3_origin);

        g_nummapbrushsides++;
        b->numsides++;
    };

	// Create windings temporarily, for brush bevel creation
	MakeBrushWindings(b);

#ifdef RECKONING_TOOLS
	if (!g_onlyents)
	{
		// Now create the bevel sides, but only for non-clip economy entities and entities without zhlt_noclip set to 1
		if(strcmpi(ValueForKey(mapent, "classname"), "func_detail") != 0 && IntForKey(mapent, "zhlt_noclip") == 0)
			AddBrushBevels(b);
	}
#else
	if (!g_onlyents)
	{
		// Now create the bevel sides, but only for non-clip economy entities and entities without zhlt_noclip set to 1
		if(strcmpi(ValueForKey(mapent, "classname"), "func_clipeconomy") != 0 && IntForKey(mapent, "zhlt_noclip") == 0)
			AddBrushBevels(b);
	}
#endif

	if (b->cliphull != 0) // has CLIP* texture
	{
		unsigned int mask_anyhull = 0;
		for (int h = 1; h < NUM_HULLS; h++)
		{
			mask_anyhull |= (1 << h);
		}
		if ((b->cliphull & mask_anyhull) == 0) // no CLIPHULL1 or CLIPHULL2 or CLIPHULL3 texture
		{
			b->cliphull |= mask_anyhull; // CLIP all hulls
		}
	}

    b->contents = contents = CheckBrushContents(b);
	for (j = 0; j < b->numsides; j++)
	{
		side = &g_mapbrushsides[b->firstside + j];
		// Unless it's the listed textures above, replace texture with "null"
		if(nullify 
			&& strncasecmp(side->td.name,"BEVEL",5) 
			&& strncasecmp(side->td.name,"ORIGIN",6)
			&& strncasecmp(side->td.name,"HINT",4) 
			&& (strncasecmp(side->td.name,"SKIP",4) && !side->treatasskip) 
			&& strncasecmp(side->td.name,"CLIP",4)
			&& strncasecmp(side->td.name,"SOLIDHINT",9)
			&& strncasecmp(side->td.name,"SPLITFACE",9)
			&& strncasecmp(side->td.name,"BOUNDINGBOX",11)
			&& strncasecmp(side->td.name,"CONTENT",7) && strncasecmp(side->td.name,"SKY",3)
			)
		{
			safe_strncpy(side->td.name,"NULL",sizeof(side->td.name));
		}
	}
	for (j = 0; j < b->numsides; j++)
	{
		// change to SKIP now that we have set brush content.
		side = &g_mapbrushsides[b->firstside + j];
		if (!strncasecmp (side->td.name, "SPLITFACE", 9))
			side->treatasskip = true;
	}
	for (j = 0; j < b->numsides; j++)
	{
		side = &g_mapbrushsides[b->firstside + j];
		if (!strncasecmp (side->td.name, "CONTENT", 7))
			side->treatasskip = true;
	}
	if (g_nullifytrigger)
	{
		for (j = 0; j < b->numsides; j++)
		{
			side = &g_mapbrushsides[b->firstside + j];
			if (!strncasecmp (side->td.name, "AAATRIGGER", 10))
			{
				strcpy (side->td.name, "NULL");
			}
		}
	}

    //
    // origin brushes are removed, but they set
    // the rotation origin for the rest of the brushes
    // in the entity
    //

    if (contents == CONTENTS_ORIGIN)
    {
		if (*ValueForKey (mapent, "origin"))
		{
			Error ("Entity %i, Brush %i: Only one ORIGIN brush allowed.",
					b->originalentitynum, b->originalbrushnum
					);
		}
        char            string[MAXTOKEN];
        vec3_t          origin;

        b->contents = CONTENTS_SOLID;
        CreateBrush(mapent->firstbrush + b->brushnum);     // to get sizes
        b->contents = contents;

        for (i = 0; i < NUM_HULLS; i++)
        {
            b->hulls[i].faces = NULL;
        }

        if (b->entitynum != 0)  // Ignore for WORLD (code elsewhere enforces no ORIGIN in world message)
        {
            VectorAdd(b->hulls[0].bounds.m_Mins, b->hulls[0].bounds.m_Maxs, origin);
            VectorScale(origin, 0.5, origin);
    
            safe_snprintf(string, MAXTOKEN, "%i %i %i", (int)origin[0], (int)origin[1], (int)origin[2]);
            SetKeyValue(&g_entities[b->entitynum], "origin", string);
        }
    }
	if (*ValueForKey (&g_entities[b->entitynum], "zhlt_usemodel"))
	{
		memset (&g_mapbrushsides[b->firstside], 0, b->numsides * sizeof (side_t));
		g_nummapbrushsides -= b->numsides;
		for (int h = 0; h < NUM_HULLS; h++)
		{
			if (b->hullshapes[h])
			{
				free (b->hullshapes[h]);
			}
		}
		memset (b, 0, sizeof (brush_t));
		g_nummapbrushes--;
		mapent->numbrushes--;
		return;
	}
	if (!strcmp (ValueForKey (&g_entities[b->entitynum], "classname"), "info_hullshape"))
	{
		// all brushes should be erased, but not now.
		return;
	}
    if (contents == CONTENTS_BOUNDINGBOX)
    {
		if (*ValueForKey (mapent, "zhlt_minsmaxs"))
		{
			Error ("Entity %i, Brush %i: Only one BoundingBox brush allowed.",
					b->originalentitynum, b->originalbrushnum
					);
		}
        char            string[MAXTOKEN];
        vec3_t          mins, maxs;
		char			*origin = NULL;
		if (*ValueForKey (mapent, "origin"))
		{
			origin = strdup (ValueForKey (mapent, "origin"));
			SetKeyValue (mapent, "origin", "");
		}

        b->contents = CONTENTS_SOLID;
        CreateBrush(mapent->firstbrush + b->brushnum);     // to get sizes
        b->contents = contents;

        for (i = 0; i < NUM_HULLS; i++)
        {
            b->hulls[i].faces = NULL;
        }

        if (b->entitynum != 0)  // Ignore for WORLD (code elsewhere enforces no ORIGIN in world message)
        {
            VectorCopy(b->hulls[0].bounds.m_Mins, mins);
            VectorCopy(b->hulls[0].bounds.m_Maxs, maxs);
    
            safe_snprintf(string, MAXTOKEN, "%.0f %.0f %.0f %.0f %.0f %.0f", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
            SetKeyValue(&g_entities[b->entitynum], "zhlt_minsmaxs", string);
        }

		if (origin)
		{
			SetKeyValue (mapent, "origin", origin);
			free (origin);
		}
    }
	if (g_skyclip && b->contents == CONTENTS_SKY && !b->noclip)
	{
		brush_t *newb = CopyCurrentBrush (mapent, b);
		newb->contents = CONTENTS_SOLID;
		newb->cliphull = ~0;
		for (j = 0; j < newb->numsides; j++)
		{
			side = &g_mapbrushsides[newb->firstside + j];
			strcpy (side->td.name, "NULL");
		}
	}
	if (b->cliphull != 0 && b->contents == CONTENTS_TOEMPTY)
	{
		// check for mix of CLIP and normal texture
		bool mixed = false;
		for (j = 0; j < b->numsides; j++)
		{
			side = &g_mapbrushsides[b->firstside + j];

			// this is not supposed to be a HINT brush, so remove all invisible faces from hull 0.
			if (!strncasecmp (side->td.name, "NULL", 4))
				side->treatasskip = true;

			if (!side->treatasskip && strncasecmp (side->td.name, "SKIP", 4))
				mixed = true;
		}
		if (mixed)
		{
			brush_t *newb = CopyCurrentBrush (mapent, b);
			newb->cliphull = 0;
		}
		b->contents = CONTENTS_SOLID;
	}

}


// =====================================================================================
//  ParseMapEntity
//      parse an entity from script
// =====================================================================================
bool            ParseMapEntity()
{
    bool            all_clip = true;
    int             this_entity;
    entity_t*       mapent;
    epair_t*        e;

	g_numparsedbrushes = 0;
    if (!GetToken(true))
    {
        return false;
    }

    this_entity = g_numentities;

    if (strcmp(g_token, "{"))
    {
        Error("Parsing Entity %i, expected '{' got '%s'", 
			g_numparsedentities, 
			g_token);
    }

    hlassume(g_numentities < MAX_MAP_ENTITIES, assume_MAX_MAP_ENTITIES);
    g_numentities++;

    mapent = &g_entities[this_entity];
    mapent->firstbrush = g_nummapbrushes;
    mapent->numbrushes = 0;

    while (1)
    {
        if (!GetToken(true))
            Error("ParseEntity: EOF without closing brace");

        if (!strcmp(g_token, "}"))  // end of our context
            break;

        if (!strcmp(g_token, "{"))  // must be a brush
        {
			ParseBrush (mapent);
			g_numparsedbrushes++;

        }
        else                        // else assume an epair
        {
            e = ParseEpair();
			if (mapent->numbrushes > 0) Warning ("Error: ParseEntity: Keyvalue comes after brushes."); //--vluzacn

            if (!strcmp(e->key, "mapversion"))
            {
                g_nMapFileVersion = atoi(e->value);
            }

			SetKeyValue (mapent, e->key, e->value);
			Free (e->key);
			Free (e->value);
			Free (e);
        }
    }
	{
		int i;
		for (i = 0; i < mapent->numbrushes; i++)
		{
			brush_t *brush = &g_mapbrushes[mapent->firstbrush + i];
			if (
				brush->cliphull == 0
				&& brush->contents != CONTENTS_ORIGIN
				&& brush->contents != CONTENTS_BOUNDINGBOX
				)
			{
				all_clip = false;
			}
		}
	}
	if (*ValueForKey (mapent, "zhlt_usemodel"))
	{
		if (!*ValueForKey (mapent, "origin"))
			Warning ("Entity %i: 'zhlt_usemodel' requires the entity to have an origin brush.", 
				g_numparsedentities
				);
		mapent->numbrushes = 0;
	}
	if (strcmp (ValueForKey (mapent, "classname"), "info_hullshape")) // info_hullshape is not affected by '-scale'
	{
		bool ent_move_b = false, ent_scale_b = false, ent_gscale_b = false;
		vec3_t ent_move = {0,0,0}, ent_scale_origin = {0,0,0};
		vec_t ent_scale = 1, ent_gscale = 1;

		if (g_scalesize > 0)
		{
			ent_gscale_b = true;
			ent_gscale = g_scalesize;
		}
		double v[4] = {0,0,0,0};
		if (*ValueForKey (mapent, "zhlt_transform"))
		{
			switch
				(sscanf(ValueForKey (mapent, "zhlt_transform"), "%lf %lf %lf %lf", v, v+1, v+2, v+3))
			{
			case 1:
				ent_scale_b = true;
				ent_scale = v[0];
				break;
			case 3:
				ent_move_b = true;
				VectorCopy (v, ent_move);
				break;
			case 4:
				ent_scale_b = true;
				ent_scale = v[0];
				ent_move_b = true;
				VectorCopy (v+1, ent_move);
				break;
			default:
				Warning ("bad value '%s' for key 'zhlt_transform'", ValueForKey (mapent, "zhlt_transform"));
			}
			DeleteKey (mapent, "zhlt_transform");
		}
		GetVectorForKey (mapent, "origin", ent_scale_origin);

		if (ent_move_b || ent_scale_b || ent_gscale_b)
		{
			if (g_nMapFileVersion < 220 || g_mapbrushsides[0].td.txcommand != 0)
			{
				Warning ("hlcsg scaling hack is not supported in Worldcraft 2.1- or QuArK mode");
			}
			else
			{
				int ibrush, iside, ipoint;
				brush_t *brush;
				side_t *side;
				vec_t *point;
				for (ibrush = 0, brush = g_mapbrushes + mapent->firstbrush; ibrush < mapent->numbrushes; ++ibrush, ++brush)
				{
					for (iside = 0, side = g_mapbrushsides + brush->firstside; iside < brush->numsides; ++iside, ++side)
					{
						for (ipoint = 0; ipoint < 3; ++ipoint)
						{
							point = side->planepts[ipoint];
							if (ent_scale_b)
							{
								VectorSubtract (point, ent_scale_origin, point);
								VectorScale (point, ent_scale, point);
								VectorAdd (point, ent_scale_origin, point);
							}
							if (ent_move_b)
							{
								VectorAdd (point, ent_move, point);

							}
							if (ent_gscale_b)
							{
								VectorScale (point, ent_gscale, point);
							}
						}
						// note that  tex->vecs = td.vects.valve.Axis / td.vects.valve.scale
						//            tex->vecs[3] = vects.valve.shift + Dot(origin, tex->vecs)
						//      and   texcoordinate = Dot(worldposition, tex->vecs) + tex->vecs[3]
						bool zeroscale = false;
						if (!side->td.vects.valve.scale[0])
						{
							side->td.vects.valve.scale[0] = 1;
						}
						if (!side->td.vects.valve.scale[1])
						{
							side->td.vects.valve.scale[1] = 1;
						}
						if (ent_scale_b)
						{
							vec_t coord[2];
							if (fabs (side->td.vects.valve.scale[0]) > NORMAL_EPSILON)
							{
								coord[0] = DotProduct (ent_scale_origin, side->td.vects.valve.UAxis) / side->td.vects.valve.scale[0] + side->td.vects.valve.shift[0];
								side->td.vects.valve.scale[0] *= ent_scale;
								if (fabs (side->td.vects.valve.scale[0]) > NORMAL_EPSILON)
								{
									side->td.vects.valve.shift[0] = coord[0] - DotProduct (ent_scale_origin, side->td.vects.valve.UAxis) / side->td.vects.valve.scale[0];
								}
								else
								{
									zeroscale = true;
								}
							}
							else
							{
								zeroscale = true;
							}
							if (fabs (side->td.vects.valve.scale[1]) > NORMAL_EPSILON)
							{
								coord[1] = DotProduct (ent_scale_origin, side->td.vects.valve.VAxis) / side->td.vects.valve.scale[1] + side->td.vects.valve.shift[1];
								side->td.vects.valve.scale[1] *= ent_scale;
								if (fabs (side->td.vects.valve.scale[1]) > NORMAL_EPSILON)
								{
									side->td.vects.valve.shift[1] = coord[1] - DotProduct (ent_scale_origin, side->td.vects.valve.VAxis) / side->td.vects.valve.scale[1];
								}
								else
								{
									zeroscale = true;
								}
							}
							else
							{
								zeroscale = true;
							}
						}
						if (ent_move_b)
						{
							if (fabs (side->td.vects.valve.scale[0]) > NORMAL_EPSILON)
							{
								side->td.vects.valve.shift[0] -= DotProduct (ent_move, side->td.vects.valve.UAxis) / side->td.vects.valve.scale[0];
							}
							else
							{
								zeroscale = true;
							}
							if (fabs (side->td.vects.valve.scale[1]) > NORMAL_EPSILON)
							{
								side->td.vects.valve.shift[1] -= DotProduct (ent_move, side->td.vects.valve.VAxis) / side->td.vects.valve.scale[1];
							}
							else
							{
								zeroscale = true;
							}
						}
						if (ent_gscale_b)
						{
							side->td.vects.valve.scale[0] *= ent_gscale;
							side->td.vects.valve.scale[1] *= ent_gscale;
						}
						if (zeroscale)
						{
							Error ("Entity %i, Brush %i: invalid texture scale.\n", 
								brush->originalentitynum, brush->originalbrushnum
								);
						}
					}
				}
				if (ent_gscale_b)
				{
					if (*ValueForKey (mapent, "origin"))
					{
						double v[3];
						int origin[3];
						char string[MAXTOKEN];
						int i;
						GetVectorForKey (mapent, "origin", v);
						VectorScale (v, ent_gscale, v);
						for (i=0; i<3; ++i)
							origin[i] = (int)(v[i]>=0? v[i]+0.5: v[i]-0.5);
						safe_snprintf(string, MAXTOKEN, "%d %d %d", origin[0], origin[1], origin[2]);
						SetKeyValue (mapent, "origin", string);
					}
				}
				{
					double b[2][3];
					if (sscanf (ValueForKey (mapent, "zhlt_minsmaxs"), "%lf %lf %lf %lf %lf %lf", &b[0][0], &b[0][1], &b[0][2], &b[1][0], &b[1][1], &b[1][2]) == 6)
					{
						for (int i = 0; i < 2; i++)
						{
							vec_t *point = b[i];
							if (ent_scale_b)
							{
								VectorSubtract (point, ent_scale_origin, point);
								VectorScale (point, ent_scale, point);
								VectorAdd (point, ent_scale_origin, point);
							}
							if (ent_move_b)
							{
								VectorAdd (point, ent_move, point);

							}
							if (ent_gscale_b)
							{
								VectorScale (point, ent_gscale, point);
							}
						}
						char string[MAXTOKEN];
						safe_snprintf(string, MAXTOKEN, "%.0f %.0f %.0f %.0f %.0f %.0f", b[0][0], b[0][1], b[0][2], b[1][0], b[1][1], b[1][2]);
						SetKeyValue (mapent, "zhlt_minsmaxs", string);
					}
				}
			}
		}
	}



    CheckFatal();
	if (this_entity == 0)
	{
		// Let the map tell which version of the compiler it comes from, to help tracing compiler bugs.
		char versionstring [128];
		sprintf (versionstring, "ZHLT " ZHLT_VERSIONSTRING " " HACK_VERSIONSTRING " (%s)", __DATE__);
		SetKeyValue (mapent, "compiler", versionstring);
	}
    


    if (!strcmp(ValueForKey(mapent, "classname"), "info_compile_parameters"))
    {
        GetParamsFromEnt(mapent);
    }



    GetVectorForKey(mapent, "origin", mapent->origin);

	if (!strcmp("func_group", ValueForKey(mapent, "classname"))
#ifdef RECKONING_TOOLS
		|| !strcmp("func_detail_vluzacn", ValueForKey (mapent, "classname"))
#else
		|| !strcmp("func_detail", ValueForKey(mapent, "classname"))
#endif
		)
    {
        // this is pretty gross, because the brushes are expected to be
        // in linear order for each entity
        brush_t*        temp;
        int             newbrushes;
        int             worldbrushes;
        int             i;

        newbrushes = mapent->numbrushes;
        worldbrushes = g_entities[0].numbrushes;

        temp = (brush_t*)Alloc(newbrushes * sizeof(brush_t));
        memcpy(temp, g_mapbrushes + mapent->firstbrush, newbrushes * sizeof(brush_t));

        for (i = 0; i < newbrushes; i++)
        {
            temp[i].entitynum = 0;
			temp[i].brushnum += worldbrushes;
        }

        // make space to move the brushes (overlapped copy)
        memmove(g_mapbrushes + worldbrushes + newbrushes,
                g_mapbrushes + worldbrushes, sizeof(brush_t) * (g_nummapbrushes - worldbrushes - newbrushes));

        // copy the new brushes down
        memcpy(g_mapbrushes + worldbrushes, temp, sizeof(brush_t) * newbrushes);

        // fix up indexes
        g_numentities--;
        g_entities[0].numbrushes += newbrushes;
        for (i = 1; i < g_numentities; i++)
        {
            g_entities[i].firstbrush += newbrushes;
        }
        memset(mapent, 0, sizeof(*mapent));
        Free(temp);
		return true;
    }

	if (!strcmp (ValueForKey (mapent, "classname"), "info_hullshape"))
	{
		bool disabled;
		const char *id;
		int defaulthulls;
		disabled = IntForKey (mapent, "disabled");
		id = ValueForKey (mapent, "targetname");
		defaulthulls = IntForKey (mapent, "defaulthulls");
		CreateHullShape (this_entity, disabled, id, defaulthulls);
		DeleteCurrentEntity (mapent);
		return true;
	}
	if (fabs (mapent->origin[0]) > ENGINE_ENTITY_RANGE + ON_EPSILON ||
		fabs (mapent->origin[1]) > ENGINE_ENTITY_RANGE + ON_EPSILON ||
		fabs (mapent->origin[2]) > ENGINE_ENTITY_RANGE + ON_EPSILON )
	{
		const char *classname = ValueForKey (mapent, "classname");
		if (strncmp (classname, "light", 5))
		{
			Warning ("Entity %i (classname \"%s\"): origin outside +/-%.0f: (%.0f,%.0f,%.0f)", 
				g_numparsedentities, 
				classname, (double)ENGINE_ENTITY_RANGE, mapent->origin[0], mapent->origin[1], mapent->origin[2]);
		}
	}

    return true;
}

// =====================================================================================
//  CountEngineEntities
// =====================================================================================
unsigned int    CountEngineEntities()
{
    unsigned int x;
    unsigned num_engine_entities = 0;
    entity_t*       mapent = g_entities;

    // for each entity in the map
    for (x=0; x<g_numentities; x++, mapent++)
    {
        const char* classname = ValueForKey(mapent, "classname");

        // if its a light_spot or light_env, dont include it as an engine entity!
        if (classname)
        {
            if (   !strncasecmp(classname, "light", 5) 
                || !strncasecmp(classname, "light_spot", 10)
				|| !strncasecmp(classname, "night_light_spot", 16)
				|| !strncasecmp(classname, "night_light", 11)
                || !strncasecmp(classname, "light_environment", 17)
               )
            {
                const char* style = ValueForKey(mapent, "style");
                const char* targetname = ValueForKey(mapent, "targetname");

                // lightspots and lightenviroments dont have a targetname or style
                if (!strlen(targetname) && !atoi(style))
                {
                    continue;
                }
            }
        }

        num_engine_entities++;
    }

    return num_engine_entities;
}

// =====================================================================================
//  CalculateEntityOrigin
// =====================================================================================
void CalculateEntityOrigin( entity_t* pmapent, vec3_t& outorigin, bool isparent )
{
	// If no origin is set, then set the center of the bbox
	vec3_t entmins, entmaxs;
	for(int j = 0; j < 3; j++)
	{
		entmins[j] = MAX_FLOAT_VALUE;
		entmaxs[j] = -MAX_FLOAT_VALUE;
	}

	for(int j = 0; j < pmapent->numbrushes; j++)
	{
		brush_t* pbrush = &g_mapbrushes[pmapent->firstbrush + j];

		for(int k = 0; k < pbrush->numsides; k++)
		{
			side_t* pside = &g_mapbrushsides[pbrush->firstside + k];
			if(pside->bevel || !pside->ptempwinding)
				continue;

			Winding* w = pside->ptempwinding;
			for(int l = 0; l < w->m_NumPoints; l++)
			{
				vec3_t& vertex = w->m_Points[l];
				for(int m = 0; m < 3; m++)
				{
					if(vertex[m] < entmins[m])
						entmins[m] = vertex[m];

					if(vertex[m] > entmaxs[m])
						entmaxs[m] = vertex[m];
				}
			}
		}
	}

	vec3_t origin;
    VectorAdd(entmins, entmaxs, origin);
    VectorScale(origin, 0.5, origin);

	char szValue[MAXTOKEN];
    safe_snprintf(szValue, MAXTOKEN, "%lf %lf %lf", origin[0], origin[1], origin[2]);
    SetKeyValue(pmapent, "origin", szValue);

	VectorCopy(origin, pmapent->origin);
	VectorCopy(origin, outorigin);

	const char* pstrTargetname = ValueForKey(pmapent, "targetname");
	if(pstrTargetname && strlen(pstrTargetname) > 0)
		printf("%s entity of type '%s' with name '%s' had it's origin set at %.1f %.1f %.1f.\n", (isparent ? "Parent" : "Child"), ValueForKey(pmapent, "classname"), pstrTargetname, origin[0], origin[1], origin[2]);
	else
		printf("%s entity of type '%s' had it's origin set at %.1f %.1f %.1f.\n", (isparent ? "Parent" : "Child"), ValueForKey(pmapent, "classname"), origin[0], origin[1], origin[2]);
}

// =====================================================================================
//  SetParentOffsets
//      Go through all entities and check which ones have a parent. If they do, then
//      set the offset from the parent origin
// =====================================================================================
void SetParentOffsets( void )
{
	for(int i = 0; i < g_numentities; i++)
	{
		entity_t* pmapent = &g_entities[i];

		const char* pstrParentName = ValueForKey (pmapent, "parent");
		if(!pstrParentName || !strlen(pstrParentName))
			continue;

		vec3_t entityorigin;
		const char* pstrValue = ValueForKey (pmapent, "origin");
		if(!pstrValue || !strlen(pstrValue))
		{
			CalculateEntityOrigin(pmapent, entityorigin, false);
		}
		else
		{
			sscanf(pstrValue, "%lf %lf %lf", &entityorigin[0], &entityorigin[1], &entityorigin[2]);
		}

		entity_t* pparentmapent = nullptr;
		for(int j = 0; j < g_numentities; j++)
		{
			if(j == 1)
				continue;

			entity_t* pcheckentity = &g_entities[j];
			const char* pstrCheckTargetname = ValueForKey(pcheckentity, "targetname");
			if(!pstrCheckTargetname || !strlen(pstrCheckTargetname))
				continue;

			if(!strcmp(pstrParentName, pstrCheckTargetname))
			{
				pparentmapent = pcheckentity;
				break;
			}
		}

		if(!pparentmapent)
		{
			Error("Parent '%s' of entity %d not found.\n", pstrParentName, i);
			continue;
		}

		vec3_t parentorigin;
		pstrValue = ValueForKey (pparentmapent, "origin");
		if(!pstrValue || !strlen(pstrValue))
		{
			CalculateEntityOrigin(pparentmapent, parentorigin, true);
		}
		else
		{
			sscanf(pstrValue, "%lf %lf %lf", &parentorigin[0], &parentorigin[1], &parentorigin[2]);
		}

		vec3_t parentoffset;
		VectorSubtract(entityorigin, parentorigin, parentoffset);

		char szValue[MAXTOKEN];
		sprintf(szValue, "%lf %lf %lf", parentoffset[0], parentoffset[1], parentoffset[2]);
		SetKeyValue(pmapent, "parentoffset", szValue);
	}
}

// =====================================================================================
//  AdjustEntitiesWithOrigins
// =====================================================================================
void AdjustEntitiesWithOrigins( void )
{
	for(int i = 0; i < g_numentities; i++)
	{
		entity_t* mapent = &g_entities[i];

		//
		// if there was an origin brush, offset all of the planes and texinfo
		//
		if (mapent->origin[0] || mapent->origin[1] || mapent->origin[2]) 
		{
			for (int32_t i = 0; i < mapent->numbrushes; i++)
			{
				brush_t* b = &g_mapbrushes[mapent->firstbrush + i];
				for (int32_t j = 0; j < b->numsides; j++) 
				{
					side_t *s = &b->original_sides[j];

					vec3_t neworigin;
					VectorSubtract(g_mapplanes[s->planenum].origin, mapent->origin, neworigin);
					s->planenum = FindIntPlane(g_mapplanes[s->planenum].normal, neworigin);
					s->texinfo = TexinfoForBrushTexture(&g_mapplanes[s->planenum], &s->td, mapent->origin);
				}

				if (!g_onlyents)
					MakeBrushWindings(b);
			}
		}
	}
}

// =====================================================================================
//  LoadMapFile
//      wrapper for LoadScriptFile
//      parse in script entities
// =====================================================================================
const char*     ContentsToString(const contents_t type);

void            LoadMapFile(const char* const filename)
{
    unsigned num_engine_entities;

    LoadScriptFile(filename);

    g_numentities = 0;

	g_numparsedentities = 0;
    while (ParseMapEntity())
		g_numparsedentities++;

	SetParentOffsets();
	AdjustEntitiesWithOrigins();

    // AJM debug
    /*
    for (int i = 0; i < g_numentities; i++)
    {
        Log("entity: %i - %i brushes - %s\n", i, g_entities[i].numbrushes, ValueForKey(&g_entities[i], "classname"));
    }
    Log("total entities: %i\ntotal brushes: %i\n\n", g_numentities, g_nummapbrushes);

    for (i = g_entities[0].firstbrush; i < g_entities[0].firstbrush + g_entities[0].numbrushes; i++)
    {
        Log("worldspawn brush %i: contents %s\n", i, ContentsToString((contents_t)g_mapbrushes[i].contents)); 
    }
    */

    num_engine_entities = CountEngineEntities();

    hlassume(num_engine_entities < MAX_ENGINE_ENTITIES, assume_MAX_ENGINE_ENTITIES);

    CheckFatal();

    Verbose("Load map:%s\n", filename);
    Verbose("%5i brushes\n", g_nummapbrushes);
    Verbose("%5i map entities \n", g_numentities - num_engine_entities);
    Verbose("%5i engine entities\n", num_engine_entities);

    // AJM: added in 
}
