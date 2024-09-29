/*
 * Ryzom Map Renderer - https://github.com/nimetu/ryzom_map_renderer
 * Copyright (c) 2020 Meelis Mägi <nimetu@gmail.com>
 *
 * This file is part of Ryzom Map Renderer.
 *
 * For the full copyright and license information, please view the LICENSE
 * file that was distributed with this source code.
 */

#ifndef MAP_RENDER_H
#define MAP_RENDER_H

#include <utility>

#include "nel/3d/landscapeig_manager.h"
#include "nel/3d/u_material.h"
#include "nel/misc/bitmap.h"
#include "nel/misc/quat.h"
#include "nel/misc/rgba.h"
#include "nel/misc/singleton.h"
#include "nel/misc/vector_2f.h"

#include "game_share/season.h"
#include "client_sheets/continent_sheet.h"

namespace NL3D {
class UScene;
class ULandscape;
class UInstanceGroup;
class UDriver;
class CFXAA;
class ULandscape;
class UTextContext;
class UScene;
class CEvent3dMouseListener;
} // namespace NL3D

namespace NLPACS {
class URetrieverBank;
class UGlobalRetriever;
class UMoveContainer;
} // namespace NLPACS

struct CVillageSheet;

struct CInstanceIG
{
	std::string Name;
	std::string Parent;
	NL3D::UInstanceGroup *IG;
	CInstanceIG()
	    : IG(nullptr)
	{
	}
	CInstanceIG(std::string name, std::string parent)
	    : Name(std::move(name))
	    , Parent(std::move(parent))
	    , IG(nullptr)
	{
	}
};

class CMapRenderer : public NLMISC::CSingleton<CMapRenderer>
{

public:
	CMapRenderer();
	~CMapRenderer() override;

	// read options from .cfg file
	void loadConfig(const std::string &cfgFilename);
	float parseScale(const std::string &val);

	void listMaps();
	void listContinents();

	// override from command line
	void setOutputDirectory(std::string outdir) { _OutputDirectory = std::move(outdir); }
	void setAutoRender(bool b) { _AutoRender = b; }
	void setMaps(std::vector<std::string> maps) { _Maps = std::move(maps); }
	void setInverseZ(bool b) { _InverseZ = b; }
	void setFxaa(bool b) { _UseFXAA = b; }
	void setHideTrees(bool b) { _HideTrees = b; }
	void setPixelSize(float px) { _Scale = px; }
	void setSeason(const std::string &season);
	void setGrid(bool showGrid, bool showNames)
	{
		_DrawGrid = showGrid;
		_DrawGridNames = showNames;
	}
	void setPacs(const std::vector<uint> &ids);
	void setPerf(uint frames) { _FrameLimit = frames; }
	void setViewCenter(float x, float y, float z) { _ViewCenter = NLMISC::CVector(x, y, z); }
	void setSingleScreenshot(std::string filename) { _SingleScreenshot = std::move(filename); }
	void setVision(uint vision) { _LandscapeVision = vision; }
	void setTileNear(uint tileNear)
	{
		_TileNearLocked = true;
		_LandscapeTileNear = tileNear;
	}
	void setZNear(float z) { _ZNear = z; }
	void setZFar(float z) { _ZFar = z; }

	std::vector<std::string> getMapNames();
	std::vector<std::string> getContinentNames();

	// main loop
	bool run();

private:
	void init();
	void release();

	void loadSheets();

	void handleKeyboard();
	bool checkKey(const std::string &name) const;

	void moveTo(float x, float y);

	bool loadContinent(std::string name);
	void unloadContinent();

	void refreshContinent();

	bool getContinentFromCoords(float x, float y, std::string &name, NLMISC::CVector2f &minPos, NLMISC::CVector2f &maxPos) const;

	void changeLandscapeSeason();
	void refreshLandscapeTiles(const NLMISC::CVector &center, uint32 vision);
	void renderScreenshot(NLMISC::CBitmap &btm);
	void renderScene(const NLMISC::CVector &viewCenter);

	// automatically render current continent into png
	void autoRender();

	void updateCamera();

	void renderOverlay();
	void renderOverlayAuto(const NLMISC::CVector &viewCenter);

	void frameStart();
	void frameEnd();

	void drawPacs(const NLMISC::CVector &viewCenter);
	void drawGrid(const NLMISC::CVector &viewCenter);

	// add outpost ruins/buildings to scene, zoneIg is for reference positions
	void addOutpostBuildings(CInstanceIG &ig, NL3D::UInstanceGroup *zoneIg);
	// add village igs (towns) to scene
	void addToScene(std::vector<CInstanceIG> &igs);
	// debug ig clusters (ie. towns)
	void debugClusters();

	// load village/outpost .ig's into scene
	void loadZoneIG(const std::vector<std::string> &zoneTiles);
	// remove village/outpost .ig's from scene
	void unloadZoneIG(const std::vector<std::string> &zoneTiles);

	void updateIGDistance();
	void updateIGDistance(NL3D::UInstanceGroup *grp);
	std::string filenameWithSeasonSuffix(const std::string &filename);

private:
	NL3D::UDriver *driver;
	NL3D::UScene *scene;
	NL3D::ULandscape *landscape;
	NL3D::CFXAA *fxaa;
	//ULight *sun = NULL;

	NL3D::UTextContext *text;
	NL3D::CEvent3dMouseListener *mouse;

	std::string _FontName;
	NLMISC::CRGBA _BackgroundColor;
	std::string _OutputDirectory;
	// single frame screenshot png
	std::string _SingleScreenshot;

	// padding around continent
	uint _Padding;

	// from command line
	std::vector<std::string> _Maps;
	std::vector<bool> _PacsFilter;
	bool _AutoRender;
	bool _InverseZ;
	bool _UseFXAA;
	bool _HideTrees;
	float _Scale;
	double _FrameDelta;
	bool _SlowDown;
	bool _UseLight;
	bool _CamChanged;
	// only render X frame(s) and then quit (for profiling)
	uint _FrameLimit;

	bool _RefineCenterAuto;
	bool _TileNearLocked;
	uint _LandscapeTileNear;
	uint _LandscapeVision;
	float _LandscapeThreshold;

	float _ZNear;
	float _ZFar;

	std::string _Season;
	EGSPD::CSeason::TSeason _SeasonId;

	// initialized per continent
	CContinentSheet *_ActiveContinent;
	std::string _ContinentSheet;
	std::string _MapName;
	NLMISC::CVector _ViewCenter;
	bool _DrawPacs;
	bool _DrawGrid;
	bool _DrawGridNames;
	bool _DebugClusters;
	bool _SheetsLoaded;

	NLMISC::CVector _ZoneCenter;
	NLMISC::CVector2f _ZoneMin;
	NLMISC::CVector2f _ZoneMax;

	NLMISC::CVector _Direction;
	NLMISC::CRGBA _Ambiant;
	NLMISC::CRGBA _Diffuse;
	NLMISC::CRGBA _Specular;

	NLPACS::URetrieverBank *_RetrieverBank;
	NLPACS::UGlobalRetriever *_GlobalRetriever;
	NLPACS::UMoveContainer *_PACS;

	NL3D::CLandscapeIGManager LandscapeIGManager;
	NL3D::UMaterial sceneMaterial;
	NL3D::UMaterial pacsMaterial;

	// towns, bridges, water, etc
	std::vector<CInstanceIG> _VillageIGs;

	// zone tiles with outpost ruins
	std::unordered_map<std::string, CInstanceIG> _OutpostIGs;
};

#endif
