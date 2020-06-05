/*
 * Ryzom Map Renderer - https://github.com/nimetu/ryzom_map_renderer
 * Copyright (c) 2020 Meelis Mägi <nimetu@gmail.com>
 *
 * This file is part of Ryzom Map Renderer.
 *
 * For the full copyright and license information, please view the LICENSE
 * file that was distributed with this source code.
 */

#include <iostream>
#include <iomanip>

//
#include "map_renderer.h"

#include "nel/3d/fxaa.h"
#include "nel/3d/instance_group_user.h"
#include "nel/3d/material.h"
#include "nel/3d/scene_group.h"
#include "nel/3d/scene_user.h"
#include "nel/3d/u_3d_mouse_listener.h"
#include "nel/3d/u_camera.h"
#include "nel/3d/u_driver.h"
#include "nel/3d/u_instance.h"
#include "nel/3d/u_landscape.h"
#include "nel/3d/u_material.h"
#include "nel/3d/u_scene.h"
#include "nel/3d/u_text_context.h"
#include "nel/3d/text_context_user.h"
#include "nel/misc/aabbox.h"
#include "nel/misc/algo.h"
#include "nel/misc/config_file.h"
#include "nel/misc/file.h"
#include "nel/misc/path.h"
#include "nel/misc/progress_callback.h"
#include "nel/pacs/u_global_retriever.h"
#include "nel/pacs/u_move_container.h"
#include "nel/pacs/u_move_primitive.h"
#include "nel/pacs/u_retriever_bank.h"

// client
#include "sheet_manager.h"
#include "zone_util.h"

using namespace NL3D;
using namespace NLMISC;
using namespace NLPACS;
using namespace EGSPD;

// nel zone tile width/height (AA_01.zonel) in meters
#define ZONE_TILE_WH 160
#define ZONE_MAX_X 26 * 26 * ZONE_TILE_WH // AA-ZZ == 108160
#define ZONE_MAX_Y 256 * ZONE_TILE_WH

// world image
static const uint staticWI = 0;

struct KeyBindingRec
{
	TKey Key;
	bool HeldDown;
	std::string Descr;
};

// TODO: use enum for map index
std::map<std::string, KeyBindingRec> KeyBindings = {
	{ "0x0", { Key1, false, "" } },
	{ "pyr", { Key2, false, "" } },
	{ "fairhaven", { Key3, false, "" } },
	{ "yrkanis", { Key4, false, "" } },
	{ "zorai", { Key5, false, "" } },
	{ "nexus", { Key6, false, "shift: marauder city" } },
	{ "inverse-z", { KeyI, false, "" } },
	{ "grid", { KeyG, false, "shift: names" } },
	{ "pacs", { KeyP, false, "" } },
	{ "pacs 0", { KeyNUMPAD0, false, "" } },
	{ "pacs 1", { KeyNUMPAD1, false, "" } },
	{ "pacs 2", { KeyNUMPAD2, false, "" } },
	{ "pacs 3", { KeyNUMPAD3, false, "" } },
	{ "pacs 4", { KeyNUMPAD4, false, "" } },
	{ "pacs 5", { KeyNUMPAD5, false, "" } },
	{ "clusters", { KeyC, false, "" } },
	// TODO: should have debug pacs / collisions debug aswell

	{ "tilenear", { KeyN, true, "shift:+/-10; ctrl:0" } },
	{ "vision", { KeyV, true, "shift:+/-10; ctrl:0" } },

	{ "left", { KeyA, true, "shift:+/-10" } },
	{ "right", { KeyD, true, "shift:+/-10" } },
	{ "up", { KeyW, true, "shift:+/-10" } },
	{ "down", { KeyS, true, "shift:+/-10" } },

	{ "z++", { KeyZ, true, "" } },
	{ "z--", { KeyX, true, "" } },

	{ "render", { KeyF10, false, "" } },

	{ "light", { KeyF11, false, "" } },
	{ "slowdown", { KeyF12, false, "" } },
	{ "trees", { KeyT, false, "" } },
	{ "season", { KeyINSERT, false, "" } },
	{ "reset", { KeyR, false, "" } },

	{ "quit", { KeyESCAPE, false, "" } },

	// catch last comma
	{ "", { KeyNOKEY, false, "" } }
};

//----------------------------------------------------------------------------
CMapRenderer::CMapRenderer()
{
	driver = nullptr;
	fxaa = nullptr;
	landscape = nullptr;
	text = nullptr;
	scene = nullptr;
	mouse = nullptr;

	_AutoRender = false;
	_BackgroundColor = CRGBA(255, 0, 255, 255);
	_Scale = 1.f;
	_HideTrees = false;
	_UseFXAA = true;

	_RefineCenterAuto = true;
	_LandscapeTileNear = 50;
	_LandscapeVision = 0;
	_LandscapeThreshold = 0.0f;
	_TileNearLocked = false;

	_Padding = 0;

	_Season = "sp";
	_SeasonId = CSeason::Spring;

	_FontName = "ryzom.ttf";

	_ActiveContinent = nullptr;
	_RetrieverBank = nullptr;
	_GlobalRetriever = nullptr;
	_PACS = nullptr;
	_ZoneMin = CVector2f(0.f, 0.f);
	_ZoneMax = CVector2f(0.f, 0.f);

	_InverseZ = false;
	_FrameDelta = 0.0;
	_SlowDown = true;
	_UseLight = false;
	_FrameLimit = 0;

	_DrawGrid = false;
	_DrawGridNames = false;

	_DrawPacs = false;
	_PacsFilter = { true, false, true, false, false, false };
	_DebugClusters = false;
	_SheetsLoaded = false;

	// pyr as default
	_ViewCenter = CVector(18886, -24346, 400.f);
}

//----------------------------------------------------------------------------
CMapRenderer::~CMapRenderer()
{
	release();
}

//----------------------------------------------------------------------------
void CMapRenderer::setSeason(const std::string &season)
{
	_Season = toLower(season.substr(0, 2));
	if (_Season == "su") {
		_SeasonId = CSeason::Summer;
	} else if (_Season == "au") {
		_SeasonId = CSeason::Autumn;
	} else if (_Season == "wi") {
		_SeasonId = CSeason::Winter;
	} else {
		nlinfo("Invalid season (%s), fall back to 'sp'", season.c_str());
		_Season = "sp";
		_SeasonId = CSeason::Spring;
	}
}

//----------------------------------------------------------------------------
void CMapRenderer::release()
{
	unloadContinent();

	if (!sceneMaterial.empty()) {
		driver->deleteMaterial(sceneMaterial);
	}

	if (!pacsMaterial.empty()) {
		driver->deleteMaterial(pacsMaterial);
	}

	if (text) {
		driver->deleteTextContext(text);
		text = nullptr;
	}

	if (landscape) {
		scene->deleteLandscape(landscape);
		landscape = nullptr;
	}

	if (scene) {
		driver->deleteScene(scene);
		scene = nullptr;
	}

	if (fxaa) {
		delete fxaa;
		fxaa = nullptr;
	}

	if (mouse) {
		driver->delete3dMouseListener(mouse);
		mouse = nullptr;
	}

	if (driver) {
		driver->release();
		delete driver;
		driver = nullptr;
	}

	CPath::releaseInstance();
	SheetMngr.release();
	_SheetsLoaded = false;
}

//----------------------------------------------------------------------------
void CMapRenderer::init()
{
	// false = OpenGL
	driver = UDriver::createDriver(0, false);
	nlassert(driver);

	driver->setPolygonMode(UDriver::Filled);

	// sceneMaterial used for invZTest render
	sceneMaterial = driver->createMaterial();
	sceneMaterial.getObjectPtr()->setLighting(true);
	sceneMaterial.getObjectPtr()->setSpecular(CRGBA(255, 255, 255, 255));
	sceneMaterial.getObjectPtr()->setShininess(0); // todo was 50
	sceneMaterial.getObjectPtr()->setDiffuse(CRGBA(100, 100, 100, 255));
	sceneMaterial.getObjectPtr()->setEmissive(CRGBA(25, 25, 25, 255));
	sceneMaterial.setZFunc(UMaterial::less);

	pacsMaterial = driver->createMaterial();
	pacsMaterial.getObjectPtr()->setZFunc(CMaterial::always);
	//pacsMaterial.getObjectPtr()->setLighting(true);
	//pacsMaterial.getObjectPtr()->setSpecular(CRGBA(255, 255, 255, 255));
	//pacsMaterial.getObjectPtr()->setShininess(0); // todo was 50
	//pacsMaterial.getObjectPtr()->setDiffuse(CRGBA(100, 100, 100, 255));
	//pacsMaterial.getObjectPtr()->setEmissive(CRGBA(255, 255, 255, 255));
	//pacsMaterial.setZFunc(UMaterial::less);

	// sunAmbient  { 64,  64,  64}
	// sunDiffuse  {255, 255, 255}
	// sunSpecular {255, 255, 255}
	// sunDirection{1.0, 0.0,-1.0}

	CPath::remapExtension("dds", "tga", true);
	CPath::remapExtension("dds", "png", true);

	loadSheets();
}

//----------------------------------------------------------------------------
void CMapRenderer::loadSheets()
{
	if (_SheetsLoaded) return;
	_SheetsLoaded = true;

	CSheetId::init(false);

	std::vector<std::string> exts;
	exts.emplace_back("world");
	exts.emplace_back("continent");

	IProgressCallback callback;
	SheetMngr.loadAllSheet(callback, false, false, false, false, &exts);
}

//----------------------------------------------------------------------------
void CMapRenderer::loadConfig(const std::string &cfgFilename)
{
	CConfigFile cf;
	cf.load(cfgFilename);

	CConfigFile::CVar *var;
	var = cf.getVarPtr("SearchPaths");
	if (var) {
		for (int i = 0; i < var->size(); ++i) {
			CPath::addSearchPath(var->asString(i), true, false);
		}
	}

	var = cf.getVarPtr("FontName");
	if (var) {
		_FontName = var->asString();
	}

	var = cf.getVarPtr("OutDir");
	if (var) {
		_OutputDirectory = var->asString();
	}

	var = cf.getVarPtr("BackgroundColor");
	if (var) {
		_BackgroundColor = CRGBA(var->asInt(0), var->asInt(1), var->asInt(2), var->asInt(3));
	}

	var = cf.getVarPtr("Maps");
	if (var) {
		_Maps.clear();
		for (int i = 0; i < var->size(); ++i) {
			_Maps.push_back(var->asString(i));
		}
	}

	var = cf.getVarPtr("Scale");
	if (var) {
		_Scale = parseScale(var->asString());
		if (_Scale < 0.1f) {
			nlwarning("Scale should be > 1:10");
			_Scale = 0.1;
		}
	}

	var = cf.getVarPtr("HideTrees");
	if (var) {
		_HideTrees = var->asBool();
	}

	var = cf.getVarPtr("fxaa");
	if (var) {
		_UseFXAA = var->asBool();
	}

	var = cf.getVarPtr("landscapeTileNear");
	if (var) {
		_LandscapeTileNear = var->asInt();
		_TileNearLocked = true;
	}

	var = cf.getVarPtr("Padding");
	if (var) {
		_Padding = var->asInt();
	}
}
//----------------------------------------------------------------------------
float CMapRenderer::parseScale(const std::string &val)
{
	std::vector<std::string> str;
	splitString(val, ":", str);
	if (str.size() != 2) {
		nlwarning("scale requires 'px:m' format, got '%s'", val.c_str());
		return 0.f;
	}
	uint px, m;
	if (!fromString(str[0], px) || !fromString(str[1], m)) {
		nlwarning("failed to parse scale from '%s'");
		return 0.f;
	}
	if (px == 0 || m == 0) {
		nlwarning("scale cannot be 0");
		return 0.f;
	}

	return (float)px / m;
}

//----------------------------------------------------------------------------
void CMapRenderer::listContinents()
{
	init();

	const CWorldSheet *world = dynamic_cast<const CWorldSheet *>(SheetMngr.get(CSheetId("ryzom.world")));

	std::size_t firstColumnChars = 0;
	for (const auto &cont : world->ContLocs) {
		firstColumnChars = std::max(firstColumnChars, cont.SelectionName.size());
	}

	for (const auto &cont : world->ContLocs) {
		std::cout << std::setw(firstColumnChars + 1) << std::left;
		std::cout << toLower(cont.ContinentName);
		std::cout << std::setw(0) << "\t";
		bool first = true;
		for (const auto &smap : world->Maps) {
			// zorai/matis fails to list towns if cont.ContinentName is used here
			if (toLower(cont.SelectionName) == toLower(smap.ContinentName)) {
				if (!first) {
					std::cout << "; ";
				}
				std::cout << smap.Name.c_str();
				first = false;
			}
		}
		std::cout << '\n';
	}
}

//----------------------------------------------------------------------------
void CMapRenderer::listMaps()
{
	init();

	const CWorldSheet *world = dynamic_cast<const CWorldSheet *>(SheetMngr.get(CSheetId("ryzom.world")));

	std::size_t nameColumnChars = 0;
	std::size_t bitmapColumnChars = 0;
	for (const auto &smap : world->Maps) {
		nameColumnChars = std::max(nameColumnChars, smap.Name.size());
		bitmapColumnChars = std::max(bitmapColumnChars, smap.BitmapName.size());
	}
	for (const auto &smap : world->Maps) {
		if (smap.Name == "world") {
			continue;
		}
		// selection bitmap bbox continent
		std::cout << std::setw(nameColumnChars) << std::left << toLower(smap.Name);
		std::cout << std::setw(bitmapColumnChars) << toLower(smap.BitmapName);
		std::cout << "\t" << std::setw(6) << std::right << sint(smap.MinX);
		std::cout << "\t" << std::setw(6) << std::right << sint(smap.MinY);
		std::cout << "\t" << std::setw(6) << std::right << sint(smap.MaxX);
		std::cout << "\t" << std::setw(6) << std::right << sint(smap.MaxY);

		bool found = false;
		for (const auto &cont : world->ContLocs) {
			if (toLower(smap.ContinentName) == toLower(cont.SelectionName)) {
				std::cout << "\t" << toLower(cont.ContinentName);
				found = true;
				break;
			}
		}
		if (!found) {
			std::cout << "\t-";
		}
		std::cout << '\n';
	}
}

//----------------------------------------------------------------------------
std::vector<std::string> CMapRenderer::getMapNames()
{
	loadSheets();

	std::vector<std::string> out;
	const CWorldSheet *world = dynamic_cast<const CWorldSheet *>(SheetMngr.get(CSheetId("ryzom.world")));
	for (const auto &smap : world->Maps) {
		out.push_back(smap.Name);
	}
	return out;
}

//----------------------------------------------------------------------------
std::vector<std::string> CMapRenderer::getContinentNames()
{
	loadSheets();

	std::vector<std::string> out;
	const CWorldSheet *world = dynamic_cast<const CWorldSheet *>(SheetMngr.get(CSheetId("ryzom.world")));
	for (const auto &cont : world->ContLocs) {
		out.push_back(cont.ContinentName);
	}
	return out;
}

//----------------------------------------------------------------------------
bool CMapRenderer::getContinentFromCoords(float x, float y, std::string &name, CVector2f &minPos, CVector2f &maxPos) const
{
	const CWorldSheet *world = dynamic_cast<const CWorldSheet *>(SheetMngr.get(CSheetId("ryzom.world")));
	for (const auto &cont : world->ContLocs) {
		minPos.x = std::min(cont.MinX, cont.MaxX);
		maxPos.x = std::max(cont.MinX, cont.MaxX);

		minPos.y = std::min(cont.MinY, cont.MaxY);
		maxPos.y = std::max(cont.MinY, cont.MaxY);

		if (x > minPos.x && x < maxPos.x && y > minPos.y && y < maxPos.y) {
			name = cont.ContinentName;
			return true;
		}
	}

	return false;
}

//----------------------------------------------------------------------------
bool CMapRenderer::loadContinent(std::string name)
{
	sint xmin, xmax, ymin, ymax;
	bool hasCoords = false;

	_MapName = name;

	//------------------------------------------------------------------------
	const CWorldSheet *world = dynamic_cast<const CWorldSheet *>(SheetMngr.get(CSheetId("ryzom.world")));

	// find matching continent from ingame maps list
	std::string selection = name;
	for (int j = 0; j < world->Maps.size(); ++j) {
		const SMap &cl = world->Maps[j];
		if (selection == cl.Name || selection == cl.ContinentName) {
			selection = cl.ContinentName;
			xmin = std::min(cl.MinX, cl.MaxX);
			xmax = std::max(cl.MinX, cl.MaxX);
			ymin = std::min(cl.MinY, cl.MaxY);
			ymax = std::max(cl.MinY, cl.MaxY);
			// BitmapName = 'zorai_map.tga'
			_MapName = CFile::getFilenameWithoutExtension(toLower(cl.BitmapName));
			// fallback if there is no ingame map texture
			if (_MapName.empty()) {
				_MapName = name;
			}
			hasCoords = true;
			// remap continent name
			for (const auto &cont : world->ContLocs) {
				if (selection == cont.SelectionName) {
					name = cont.ContinentName;
					break;
				}
			}
			break;
		}
	}

	//------------------------------------------------------------------------
	CEntitySheet *sheet = SheetMngr.get(CSheetId(name + ".continent"));
	if (!sheet || sheet->type() != CEntitySheet::CONTINENT) {
		nlinfo("continent sheet not found or bad type (%s.continent)", name.c_str());
		return false;
	}

	unloadContinent();

	_ContinentSheet = name;
	_ActiveContinent = dynamic_cast<CContinentSheet *>(sheet);

	if (!getPosFromZoneName(_ActiveContinent->Continent.ZoneMin, _ZoneMin)) {
		nlerror("failed to convert ZoneMin (%s) to xy for continent '%s'",
		    _ActiveContinent->Continent.ZoneMin.c_str(), _ActiveContinent->Continent.Name.c_str());
		return false;
	}

	if (!getPosFromZoneName(_ActiveContinent->Continent.ZoneMax, _ZoneMax)) {
		nlerror("failed to convert ZoneMax (%s) to xy for continent '%s'",
		    _ActiveContinent->Continent.ZoneMax.c_str(), _ActiveContinent->Continent.Name.c_str());
		return false;
	}

	if (!hasCoords) {
		xmin = std::min(_ZoneMin.x, _ZoneMax.x);
		xmax = std::max(_ZoneMin.x, _ZoneMax.x) + ZONE_TILE_WH;

		ymin = std::min(_ZoneMin.y, _ZoneMax.y);
		ymax = std::max(_ZoneMin.y, _ZoneMax.y) + ZONE_TILE_WH;
	}
	nlinfo("continent(%s), map(%s), ZoneMin(%s), ZoneMax(%s), area(%d, %d)(%d,%d)\n", name.c_str(), _MapName.c_str(),
	    _ActiveContinent->Continent.ZoneMin.c_str(), _ActiveContinent->Continent.ZoneMax.c_str(),
	    xmin, ymin, xmax, ymax);

	_ZoneMin = CVector2f(xmin - _Padding, ymin - (sint)_Padding);
	_ZoneMax = CVector2f(xmax + _Padding, ymax + (sint)_Padding);

	// Z in here determines invZTest cutoff
	_ZoneCenter = CVector(((float)(xmax + xmin)) / 2, ((float)(ymax + ymin)) / 2, 0.f);

	//------------------------------------------------------------------------
	_Direction = _ActiveContinent->Continent.LandscapeLightDay.Direction;
	_Ambiant = _ActiveContinent->Continent.LandscapeLightDay.Ambiant;
	_Diffuse = _ActiveContinent->Continent.LandscapeLightDay.Diffuse;
	_Specular = _ActiveContinent->Continent.LandscapeLightDay.Specular;

	//------------------------------------------------------------------------
	// villages are already in correct place (.ig has proper coords)
	// towns, camps, bridges, water
	for (auto const &village : _ActiveContinent->Villages) {
		std::string zone = toLower(village.Zone);
		for (auto const &ig : village.IGs) {
			_VillageIGs.emplace_back(CInstanceIG(ig.IgName, ig.ParentName));
		}
	}
	addToScene(_VillageIGs);

	//------------------------------------------------------------------------
	/*
	if EnableRuins
	CSheetId ruins ("ruins.building");
	CEntitySheet *entitySheet = SheetMngr.get(newForm);
	const CBuildingSheet *bldSh = dynamic_cast<const CBuildingSheet *>(entitySheet);
	*/
	// ruins.building
	// TODO: use 3-empty-plots for player outposts
	// -> ge_mission_outpost_module_construction.shape
	// -> object_generic_mark.creature -- flag??
	// TODO: need to load zone.ig for proper pos(x,y,z) and scale(sx, sy, sz)
	// 'bat_zc_01/02/03/04' for bt_ruines.ig position
	// 'flag_zc' for outpost flag position
	uint i = 0;
	for (auto const &zc : _ActiveContinent->Continent.ZCList) {
		std::string lcTile = toLower(zc.Name);
		// TODO: outpost construction building shapes if EnableRuins == false
		std::string igName = zc.EnableRuins ? "gen_bt_ruines.ig" : "gen_bt_ruines.ig";

		// TODO: if (!zc.EnableRuins) -> use construction plots instead ruins + outpost flag
		_OutpostIGs.emplace(lcTile, CInstanceIG(igName, ""));
	}

	//printf(" - createRetrieverBank: %s\n", _ActiveContinent->Continent.PacsRBank.c_str());
	// createRetrieverBank throws when file is not found
	if (!CPath::lookup(_ActiveContinent->Continent.PacsRBank, false, false).empty()) {
		_RetrieverBank = URetrieverBank::createRetrieverBank(_ActiveContinent->Continent.PacsRBank.c_str(), false);
	}
	if (_RetrieverBank) {
		//printf(" - createGlobalRetriever: %s\n", _ActiveContinent->Continent.PacsGR.c_str());
		// createGlobalRetriever throws when file is not found
		if (!CPath::lookup(_ActiveContinent->Continent.PacsRBank, false, false).empty()) {
			_GlobalRetriever = UGlobalRetriever::createGlobalRetriever(_ActiveContinent->Continent.PacsGR.c_str(), _RetrieverBank);
		}
		if (_GlobalRetriever) {
			uint RYZOM_ENTITY_SIZE_MAX = 16;
			CAABBox cbox = _GlobalRetriever->getBBox();
			uint gw = (uint)(cbox.getHalfSize().x * 2.0 / RYZOM_ENTITY_SIZE_MAX) + 1;
			uint gh = (uint)(cbox.getHalfSize().y * 2.0 / RYZOM_ENTITY_SIZE_MAX) + 1;
			//printf(" - createMoveContainer(gw:%d, gh:%d, entitySizeMax:%d\n", gw, gh, RYZOM_ENTITY_SIZE_MAX);

			_PACS = UMoveContainer::createMoveContainer(_GlobalRetriever, gw, gh, RYZOM_ENTITY_SIZE_MAX, 2);
			if (_PACS) {
				_PACS->setAsStatic(staticWI);
			} else {
				nlwarning("(%s) pacs move container failed '%s'", _ActiveContinent->Continent.Name.c_str());
				UGlobalRetriever::deleteGlobalRetriever(_GlobalRetriever);
				URetrieverBank::deleteRetrieverBank(_RetrieverBank);
				_GlobalRetriever = nullptr;
				_RetrieverBank = nullptr;
			}
		} else {
			nlwarning("(%s) global retriever failed '%s'", _ActiveContinent->Continent.Name.c_str(), _ActiveContinent->Continent.PacsGR.c_str());
			URetrieverBank::deleteRetrieverBank(_RetrieverBank);
			_RetrieverBank = nullptr;
		}
	} else {
		nlwarning("(%s) retriever bank failed '%s'", _ActiveContinent->Continent.Name.c_str(), _ActiveContinent->Continent.PacsRBank.c_str());
	}

	changeLandscapeSeason();

	return true;
}

//----------------------------------------------------------------------------
void CMapRenderer::unloadContinent()
{
	for (auto it : _VillageIGs) {
		if (it.IG) {
			it.IG->removeFromScene(*scene);
			delete (it.IG);
			it.IG = nullptr;
		}
	}
	_VillageIGs.clear();

	for (auto it : _OutpostIGs) {
		if (it.second.IG) {
			it.second.IG->removeFromScene(*scene);
			delete (it.second.IG);
			it.second.IG = nullptr;
		}
	}
	_OutpostIGs.clear();

	if (_PACS) {
		UMoveContainer::deleteMoveContainer(_PACS);
		_PACS = nullptr;
	}
	if (_GlobalRetriever) {
		UGlobalRetriever::deleteGlobalRetriever(_GlobalRetriever);
		_GlobalRetriever = nullptr;
	}
	if (_RetrieverBank) {
		URetrieverBank::deleteRetrieverBank(_RetrieverBank);
		_RetrieverBank = nullptr;
	}

	LandscapeIGManager.reset();
	if (landscape) {
		landscape->removeAllZones();
	}

	_ActiveContinent = nullptr;
}

//----------------------------------------------------------------------------
void CMapRenderer::debugClusters()
{
	for (auto &it : _VillageIGs) {
		if (it.IG) {
			it.IG->displayDebugClusters(driver, text);
		}
	}

	for (auto &it : _OutpostIGs) {
		if (it.second.IG) {
			it.second.IG->displayDebugClusters(driver, text);
		}
	}

	// TODO: landscapeManager igs
}

//----------------------------------------------------------------------------
// outposts
void CMapRenderer::addOutpostBuildings(CInstanceIG &ig, UInstanceGroup *zoneIg)
{
	if (!zoneIg) {
		nlwarning("called with zoneIg == null");
		return;
	}
	for (uint i = 0; i < zoneIg->getNumInstance(); ++i) {
		std::string name = toLower(zoneIg->getInstanceName(i));
		//std::string shape = ig.IG->getShapeName(i);
		if (startsWith(name, "bat_zc_")) {
			// TODO: check if possible to directly insert .shape for ruings/construction/flag
			ig.IG = UInstanceGroup::createInstanceGroup(CFile::getFilenameWithoutExtension(ig.Name) + ".ig");
			if (ig.IG == nullptr) {
				nlwarning("Instance group '%s' not found", ig.Name.c_str());
				continue;
			}
			// remap into proper position

			ig.IG->createRoot(*scene);
			//ig.IG->unfreezeHRC(); // TODO: dunno

			// set global pos to zone tile
			ig.IG->setPos(zoneIg->getInstancePos(i));

			ig.IG->addToScene(*scene);
			scene->setToGlobalInstanceGroup(ig.IG);

			// root->clipUnlinkFromAll();
			updateIGDistance(ig.IG);
		} else if (name == "flag_zc") {
			// TODO: add outpost flag
			//CVector pos = zoneIg->getInstancePos(i);
		}
	}
}

//----------------------------------------------------------------------------
// villages
void CMapRenderer::addToScene(std::vector<CInstanceIG> &igs)
{
	for (CInstanceIG &ig : igs) {
		ig.IG = UInstanceGroup::createInstanceGroup(CFile::getFilenameWithoutExtension(ig.Name) + ".ig");
		if (ig.IG == nullptr) {
			nlwarning("Instance group '%s' not found", ig.Name.c_str());
			continue;
		}

		ig.IG->createRoot(*scene);
		ig.IG->unfreezeHRC(); // TODO: dunno

		ig.IG->addToScene(*scene);
		scene->setToGlobalInstanceGroup(ig.IG);

		updateIGDistance(ig.IG);
	}
}

//----------------------------------------------------------------------------
void CMapRenderer::refreshLandscapeTiles(const CVector &center, uint32 vision)
{
	if (_GlobalRetriever) {
		_GlobalRetriever->refreshLrAroundNow(center, vision);
	}

	if (!landscape) return;

	IProgressCallback progress;
	std::vector<std::string> zonesAdded;
	std::vector<std::string> zonesRemoved;

	// blocking call
	landscape->refreshAllZonesAround(center, vision, zonesAdded, zonesRemoved, progress);
	if (!zonesRemoved.empty()) {
		unloadZoneIG(zonesRemoved);
	}

	if (!zonesAdded.empty()) {
		loadZoneIG(zonesAdded);
	}

	landscape->setRefineCenterUser(center);
	landscape->setupStaticLight(_Diffuse, _Ambiant, 1.0f);

	// big performance hit if enabled
	if (_UseLight) {
		landscape->updateLightingAll();
	}
}

//----------------------------------------------------------------------------
void CMapRenderer::changeLandscapeSeason()
{
	if (!landscape) return;

	LandscapeIGManager.reset();
	landscape->removeAllZones();
	// todo: reset and reload pacs?

	std::string coarseMeshFile = filenameWithSeasonSuffix(_ActiveContinent->Continent.CoarseMeshMap);
	std::string farBank = filenameWithSeasonSuffix(_ActiveContinent->Continent.FarBank);
	std::string microVeget = filenameWithSeasonSuffix(_ActiveContinent->Continent.MicroVeget);
	//printf(": coarseMesh(%s), farBank(%s), tilePostfix(%s)\n", coarseMeshFile.c_str(), farBank.c_str(), _Season.c_str());

	//printf(": coarseMeshFile '%s'\n", coarseMeshFile.c_str());
	scene->setCoarseMeshManagerTexture(coarseMeshFile.c_str());
	scene->setCoarseMeshLightingUpdate(1);

	//printf(": farBank '%s'\n", farBank.c_str());
	landscape->loadBankFiles(_ActiveContinent->Continent.SmallBank, farBank);

	// after coarse/bank files
	landscape->postfixTileFilename(std::string("_" + _Season).c_str());
	landscape->postfixTileVegetableDesc(std::string("_" + _Season).c_str());

	//printf(": microveget '%s'\n", microVeget.c_str());
	landscape->loadVegetableTexture(microVeget);
	//landscape->setPointLightDiffuseMaterial(landscapePointLightMaterial)

	//printf(": landscapeIG: '%s'\n", _ActiveContinent->Continent.LandscapeIG.c_str());
	// initIG throws if file is not found
	if (!CPath::lookup(_ActiveContinent->Continent.LandscapeIG, false, false).empty()) {
		LandscapeIGManager.initIG(scene, _ActiveContinent->Continent.LandscapeIG, driver, _SeasonId, nullptr);
	} else {
		nlinfo("Landscape IG file not found (%s)", _ActiveContinent->Continent.LandscapeIG.c_str());
	}

	//landscape->invalidateAllTiles();
}

//----------------------------------------------------------------------------
void CMapRenderer::autoRender()
{
	//------------------------------------------------------------------------
	// backup
	UCamera cam = scene->getCam();
	CMatrix mtx = cam.getMatrix();
	CFrustum frustum = cam.getFrustum();
	CViewport viewport = scene->getViewport();

	float tileNear = landscape->getTileNear();
	float threshold = landscape->getThreshold();
	bool refineAuto = landscape->getRefineCenterAuto();
	uint vision = _LandscapeVision;

	//------------------------------------------------------------------------
	// make sure landscape loads enough tiles to avoid tearing
	if (_Scale <= 0.1f) {
		// sanity check
		_Scale = 0.1f;
	}
	uint scaledWidth = driver->getWindowWidth() / _Scale;
	uint scaledHeight = driver->getWindowHeight() / _Scale;

	_LandscapeVision = ((std::max(scaledWidth, scaledHeight) / ZONE_TILE_WH) * ZONE_TILE_WH) / 2 + ZONE_TILE_WH * 4;
	if (!_TileNearLocked) {
		_LandscapeTileNear = _LandscapeVision / 2.f;
	}
	landscape->setTileNear(_LandscapeTileNear);
	landscape->setRefineCenterAuto(false); // true == use camera for center pos
	landscape->setThreshold(0.00005f);

	// tryker island 5 has water on center tile which will be unloaded
	// on last row screenshots if vision is not increased
	if (_ContinentSheet == "tryker_island" && _LandscapeVision < 1000) {
		_LandscapeVision = 1000;
	}

	//------------------------------------------------------------------------
	CBitmap renderBuffer;
	renderScreenshot(renderBuffer);

	//------------------------------------------------------------------------
	// save
	if (!CFile::isExists(_OutputDirectory)) {
		nlinfo(">> creating directory {%s}", _OutputDirectory.c_str());
		CFile::createDirectoryTree(_OutputDirectory);
	}

	std::string txName = _OutputDirectory + "/" + _MapName + ".png";
	if (CFile::fileExists(txName)) {
		txName = CFile::findNewFile(txName);
	}

	COFile fsDest(txName);
	renderBuffer.writePNG(fsDest, 24);

	//------------------------------------------------------------------------
	// restore
	_LandscapeTileNear = tileNear;
	_LandscapeThreshold = threshold;
	_LandscapeVision = vision;

	landscape->setRefineCenterAuto(_RefineCenterAuto);
	landscape->setTileNear(_LandscapeTileNear);
	landscape->setThreshold(_LandscapeThreshold);

	cam.setMatrix(mtx);
	cam.setFrustum(frustum);
	scene->setViewport(viewport);
}

//----------------------------------------------------------------------------
void CMapRenderer::renderScreenshot(CBitmap &btm)
{
	//------------------------------------------------------------------------
	// setup camera
	uint windowWidth = driver->getWindowWidth();
	uint windowHeight = driver->getWindowHeight();
	uint scaledWidth = (float)windowWidth / _Scale;
	uint scaledHeight = (float)windowHeight / _Scale;
	// frustum sets visible area in meters (-400, 400)
	scene->getCam().setFrustum(scaledWidth, scaledHeight, -10000.f, 10000.f, false);
	scene->setViewport(CViewport());

	//------------------------------------------------------------------------
	float width = _ZoneMax.x - _ZoneMin.x;
	float height = _ZoneMax.y - _ZoneMin.y;

	uint32 ScreenShotWidth = width * _Scale;
	uint32 ScreenShotHeight = height * _Scale;

	nlinfo("render: continent '%s' (%.2f,%.2f), output(%s), size(%d,%d), scale(%.2f)\n",
	    _ContinentSheet.c_str(), width, height,
	    _MapName.c_str(), ScreenShotWidth, ScreenShotHeight, _Scale);

	CBitmap dest;
	btm.resize(ScreenShotWidth, ScreenShotHeight, CBitmap::RGBA);

	//UMovePrimitive *movePrimitive = nullptr;
	//if (_PACS) {
	//	movePrimitive = _PACS->addCollisionablePrimitive(0, 1);
	//}

	CVector screenShotCenter = _ZoneCenter;
	float renderX = screenShotCenter.x - width / 2.f + scaledWidth / 2;
	float renderY = screenShotCenter.y + height / 2.f - scaledHeight / 2;
	float renderZ = screenShotCenter.z;

	CVector viewCenter(renderX, renderY, renderZ);

	bool mustQuit = false;

	uint top = 0;
	uint bottom = std::min(windowHeight, ScreenShotHeight);
	for (top = 0; top < ScreenShotHeight; top += windowHeight) {
		if (mustQuit) {
			break;
		}

		uint left;
		uint right = std::min(windowWidth, ScreenShotWidth);
		for (left = 0; left < ScreenShotWidth; left += windowWidth) {
			driver->EventServer.pump();
			if (driver->AsyncListener.isKeyPushed(KeyESCAPE)) {
				mustQuit = true;
				break;
			}

			// TODO: allow to keep camera tilt from manual mode (ie 2.5D render)
			//---------------------------------------------------------------------------
			// setup camera at next tile
			CMatrix mtx = scene->getCam().getMatrix();
			mtx.identity();
			mtx.rotateX(-(float)Pi / 2);
			mtx.setPos(viewCenter);
			scene->getCam().setTransformMode(UTransformable::DirectMatrix);
			scene->getCam().setMatrix(mtx);

			//---------------------------------------------------------------------------
			// animate veget, trees
			scene->animate(0);
			renderScene(viewCenter);

			driver->clearZBuffer();
			if (_DrawPacs) {
				drawPacs(viewCenter);
			}
			if (_DrawGrid || _DrawGridNames) {
				drawGrid(viewCenter);
			}

			//
			driver->flush();
			driver->getBuffer(dest);

			//std::cout << toString(":: blit(%d, %d, %d, %d, %d, %d) {%.2f, %.2f}", 0, 0, right-left, bottom-top, left, top, viewCenter.x, viewCenter.y) << std::endl;
			btm.blit(dest, 0, 0, right - left, bottom - top, left, top);
			// TODO: individual tiles could be used for low memory mode (still needs blit/clip)
			/*{
				std::string outFileName = toString("landscape-%d-%d.png", left, top);
				COFile pngTile(outFileName);
				dest.writePNG(pngTile, 24);
			}*/

			renderOverlayAuto(viewCenter);
			driver->swapBuffers();

			right = std::min(right + windowWidth, ScreenShotWidth);
			viewCenter.x += scaledWidth;
		}
		bottom = std::min(bottom + windowHeight, ScreenShotHeight);
		viewCenter.x = renderX;
		viewCenter.y -= scaledHeight;
	}

	//if (movePrimitive) {
	//	_PACS->removePrimitive(movePrimitive);
	//}

	driver->AsyncListener.reset();
}

//---------------------------------------------------------------------------
void CMapRenderer::renderScene(const CVector &viewCenter)
{
	UCamera camera = scene->getCam();

	refreshLandscapeTiles(viewCenter, _LandscapeVision);

	/*
	printf(" - pacs->setGlobalPosition(%s)\n", viewCenter.toString().c_str());
	movePrimitive->setGlobalPosition(CVectorD(viewCenter.x, viewCenter.y, viewCenter.z), staticWI);

	UGlobalPosition gPos;
	movePrimitive->getGlobalPosition(gPos, 0);
	CVector zPos = GlobalRetriever->getGlobalPosition(gPos);
	gPos.LocalPosition.Estimation.z = 0.0f;
	zPos.z = GlobalRetriever->getMeanHeight(gPos);
	// TODO: verify
	printf(" - ground is at (%s)\n", zPos.toString().c_str());
	*/

	//scene->enableElementRender(UScene::FilterWater, true);

	if (landscape) {
		landscape->setZFunc(UMaterial::lessequal);
	}

	if (fxaa) {
		driver->beginDefaultRenderTarget();
	}
	driver->clearBuffers(_BackgroundColor);

	scene->render();

	// second pass - overlay over current buffer
	// render scene with inversed ZBuffer test
	if (_InverseZ) {
		driver->setColorMask(false, false, false, false);

		if (landscape) {
			landscape->setZFunc(UMaterial::greaterequal);
		}
		sceneMaterial.setZFunc(UMaterial::less);
		//sceneMaterial.setZFunc(UMaterial::greaterequal);

		driver->setMatrixMode2D11();
		CQuad quad;
		quad.V0 = CVector(0.0, 0.0, 0.0);
		quad.V1 = CVector(1.0, 0.0, 0.0);
		quad.V2 = CVector(1.0, 1.0, 0.0);
		quad.V3 = CVector(0.0, 1.0, 0.0);

		driver->drawQuad(quad, sceneMaterial);
		driver->setMatrixMode3D(camera);
		driver->setColorMask(true, true, true, true);

		// display vegetables with normal ZBuffer test
		scene->enableElementRender(UScene::FilterWater, false);
		scene->enableElementRender(UScene::FilterLandscape, false);
		scene->render();
		scene->enableElementRender(UScene::FilterWater, true);
		scene->enableElementRender(UScene::FilterLandscape, true);

		scene->render();
	}

	if (fxaa) {
		driver->setMatrixMode2D11();
		fxaa->applyEffect();
		driver->setMatrixMode3D(camera);

		driver->endDefaultRenderTarget(scene);
	}

	if (_DrawGrid || _DrawGridNames) {
		// required for 3d text
		driver->clearZBuffer();

		driver->setMatrixMode3D(camera);
		driver->setModelMatrix(CMatrix::Identity);

		drawGrid(viewCenter);
	}

	if (_DrawPacs || _DebugClusters) {
		driver->setMatrixMode3D(camera);
		driver->setModelMatrix(CMatrix::Identity);
		if (_DebugClusters) {
			debugClusters();
		}

		if (_DrawPacs) {
			drawPacs(viewCenter);
		}
	}
}

//---------------------------------------------------------------------------
void CMapRenderer::setPacs(const std::vector<uint> &indices)
{
	_PacsFilter.clear();
	_PacsFilter = { false, false, false, false, false, false };
	for (auto i : indices) {
		if (i < _PacsFilter.size()) {
			_PacsFilter[i] = true;
		}
	}
	_DrawPacs = true;
}

//---------------------------------------------------------------------------
void CMapRenderer::drawPacs(const CVector &viewCenter)
{
	if (!_GlobalRetriever) return;

	uint halfWindowWidth = driver->getWindowWidth() / 2;
	uint halfWindowHeight = driver->getWindowHeight() / 2;

	CAABBox box;
	//box.setCenter(viewCenter);
	box.setCenter(viewCenter);
	box.extend(CVector(viewCenter.x - halfWindowWidth, viewCenter.y - halfWindowWidth, 0.f));
	box.extend(CVector(viewCenter.x + halfWindowWidth, viewCenter.y + halfWindowWidth, 0.f));

	static std::vector<std::pair<CLine, uint8>> edges;
	_GlobalRetriever->getBorders(box, edges);
	bool render = false;
	for (auto &edge : edges) {
		if (edge.second >= _PacsFilter.size() || !_PacsFilter[edge.second]) {
			continue;
		}

		CLineColor line;
		line = edge.first;
		CRGBA color;
		switch (edge.second) {
		// Block
		case 0:
			color = CRGBA::Red;
			break;
		// Surmountable
		case 1:
			color = CRGBA::Green;
			break;
		// Link
		case 2:
			color = CRGBA::Yellow;
			break;
		// Waterline
		case 3:
			color = CRGBA::Blue;
			break;
		// Exterior
		case 4:
			color = CRGBA::Magenta;
			break;
		// Exterior door
		case 5:
			color = CRGBA(127, 127, 127);
			break;
		// Unknown
		default:
			color = CRGBA(255, 100, 100);
			break;
		}

		line.Color0 = color;
		line.Color1 = color;
		driver->drawLine(line, pacsMaterial);
	}
}

//---------------------------------------------------------------------------
void CMapRenderer::drawGrid(const CVector &viewCenter)
{
	uint windowWidth = driver->getWindowWidth();
	uint windowHeight = driver->getWindowHeight();

	uint tilesX = windowWidth / ZONE_TILE_WH + 1;
	uint tilesY = windowHeight / ZONE_TILE_WH + 1;

	float topX = floor((viewCenter.x - windowWidth / 2) / ZONE_TILE_WH) * ZONE_TILE_WH;
	float topY = floor((viewCenter.y + windowHeight / 2) / ZONE_TILE_WH) * ZONE_TILE_WH;

	if (_DrawGrid) {

		// TOOD: implment this
		CLineColor line;
		line.Color0 = CRGBA(100, 100, 100, 255);
		line.Color1 = CRGBA(100, 100, 100, 255);

		// TODO: get loaded IGs, draw grid and/or names
		for (uint y = 0; y < tilesY; y++) {
			line = CLine(CVector(topX, topY - y * ZONE_TILE_WH, 0.f), CVector(topX + tilesX * ZONE_TILE_WH, topY - y * ZONE_TILE_WH, 0.f));
			driver->drawLine(line, pacsMaterial);
		}

		for (uint x = 0; x < tilesX; x++) {
			line = CLine(CVector(topX + x * ZONE_TILE_WH, topY, 0.f), CVector(topX + x * ZONE_TILE_WH, topY - tilesY * ZONE_TILE_WH, 0.f));
			driver->drawLine(line, pacsMaterial);
		}
	}

	if (_DrawGridNames && text) {
		text->setFontSize(10);
		text->setColor(CRGBA(250, 250, 250, 255));
		text->setHotSpot(UTextContext::MiddleMiddle);

		CMatrix fontMatrix;
		fontMatrix.rotateX(-((float)Pi / 2.f));
		// render3D multiplies scale with 1.f/windowWidth
		fontMatrix.scale(windowWidth);

		CTextContextUser *ctxUser = static_cast<CTextContextUser *>(text);
		// TODO: need to use pointer or CTextContext will be released (??)
		CTextContext *ctx = &ctxUser->getTextContext();
		IDriver *drv = static_cast<CDriverUser *>(driver)->getDriver();

		for (uint x = 0; x < tilesX; x++) {
			for (uint y = 0; y < tilesY; y++) {
				float tx = topX + x * ZONE_TILE_WH + ZONE_TILE_WH / 2.f;
				float ty = topY - y * ZONE_TILE_WH + ZONE_TILE_WH / 2.f;

				if (tx < 0 || ty > 0) {
					continue;
				}

				fontMatrix.setPos(CVector(tx, ty, 5.f));

				std::string zoneName = landscape->getZoneName(CVector(tx, ty, 0));

				ucstring zoneTile;
				zoneTile.fromUtf8(zoneName);

				//text->render3D(fontMatrix, zoneTile);

				CComputedString cs;
				ctx->computeString(zoneTile, cs);
				cs.render3D(*drv, fontMatrix);
			}
		}
	}
}

//----------------------------------------------------------------------------
void CMapRenderer::loadZoneIG(const std::vector<std::string> &zoneTiles)
{
	LandscapeIGManager.loadArrayZoneIG(zoneTiles);

	// TODO: calculate LoadDist from current ViewCenter and update VillageIGs whos LoadDist is in range
	/* TODO: does not belong here as village igs does not depend on zone tiles
	auto itVillages = _VillageIGs.find(lcTile);
	if (itVillages != _VillageIGs.end()) {
		printf(":: add %ld village igs for zone '%s'\n", itVillages->second.size(), tile.c_str());
		addToScene(itVillages->second);
	}
	*/

	for (const auto &tile : zoneTiles) {
		std::string lcTile = toLower(tile);
		UInstanceGroup *zoneIg = LandscapeIGManager.getIG(tile);

		// make sure tile has placeholder names
		// (ie fyros_newbie has invalid outpost records from fyros continent)
		if (!zoneIg) {
			continue;
		}

		// outpost ruins
		auto itOutposts = _OutpostIGs.find(lcTile);
		if (itOutposts != _OutpostIGs.end()) {
			addOutpostBuildings(itOutposts->second, zoneIg);
		}

		// igs in zone
		updateIGDistance(zoneIg);
	}
}

//----------------------------------------------------------------------------
void CMapRenderer::unloadZoneIG(const std::vector<std::string> &zoneTiles)
{
	LandscapeIGManager.unloadArrayZoneIG(zoneTiles);
	for (const auto &tile : zoneTiles) {
		std::string lcTile = toLower(tile);
		auto itOutpost = _OutpostIGs.find(lcTile);
		if (itOutpost != _OutpostIGs.end() && itOutpost->second.IG) {
			itOutpost->second.IG->removeFromScene(*scene);
			delete (itOutpost->second.IG);
			itOutpost->second.IG = nullptr;
		}
	}
}

//----------------------------------------------------------------------------
void CMapRenderer::updateIGDistance()
{
	// TODO: per zone filter for trees using zone tile name (ie name:AA_01.ig)
	std::vector<std::pair<UInstanceGroup *, std::string>> zoneIGs;
	LandscapeIGManager.getAllIGWithNames(zoneIGs);
	for (auto ig : zoneIGs) {
		// ig.second == 'AA_01.ig'
		if (ig.first->getAddToSceneState() == UInstanceGroup::StateAdded) {
			updateIGDistance(ig.first);
		}
	}
}

//----------------------------------------------------------------------------
void CMapRenderer::updateIGDistance(UInstanceGroup *grp)
{
	if (!grp) return;

	//grp->_Root->setUserClipping(true);
	auto *pIGU = dynamic_cast<CInstanceGroupUser *>(grp);
	if (!pIGU) {
		nlwarning("grp did not cast into CInstanceGroupUser");
		return;
	}

	bool verbose = false;
	for (uint i = 0; i < grp->getNumInstance(); ++i) {
		std::string name = grp->getInstanceName(i);
		std::string shape = grp->getShapeName(i);

		CInstanceGroup pIG = pIGU->getInternalIG();

		// make all instance groups visible (ie pyr streets)
		// TODO: activate only for pyr street.ig ?
		if (!pIG._ClusterInstances.empty()) {
			//printf("[%s] has %ld cluster instances\n", name.c_str(), pIG._ClusterInstances.size());
			for (auto &cluster : pIG._ClusterInstances) {
				cluster->VisibleFromFather = true;
			}
		}

		if (verbose) {
			printf("%d: visible:%d, parent:%d, inscene:%d, {%s} shape:'%s', name:'%s'", i,
			    pIG.getInstance(i).Visible,
			    pIG.getInstanceParent(i),
			    !pIG.getInstance(i).DontAddToScene,
			    grp->getInstancePos(i).toString().c_str(), shape.c_str(), name.c_str());
		}

		grp->getInstance(i).setShapeDistMax(-1);

		// -1 == unlimited
		float igDist = -1.f; //_LandscapeVision;
		float cmDist = 100000.f; //_LandscapeVision;
		if (name.find(".plant") != std::string::npos) {
			if (_HideTrees) {
				igDist = 0;
				cmDist = 0;
				if (verbose) {
					printf(", tree");
				}
			}
		} else {
			//CQuat rot = grp->getInstanceRot(i);
			//printf("%d: {%s} shape:'%s', name:'%s'\n", i, pos.toString().c_str(), shape.c_str(), name.c_str());
		}
		if (verbose) {
			printf("\n");
		}

		grp->setDistMax(i, igDist);
		grp->setCoarseMeshDist(i, cmDist);
	}
}

//----------------------------------------------------------------------------
std::string CMapRenderer::filenameWithSeasonSuffix(const std::string &filename)
{
	std::string filenameWithoutExt = CFile::getFilenameWithoutExtension(filename);
	std::string filenameExt = CFile::getExtension(filename);
	//TODO:  CSeasonFileExt::getExtension(TSeason)

	return filenameWithoutExt + "_" + _Season + "." + filenameExt;
}

//----------------------------------------------------------------------------
bool CMapRenderer::run()
{
	init();

	//-----------------------------------------------------------------------
	bool show = true;
	bool resizable = false;
	bool windowed = true;
	driver->setDisplay(UDriver::CMode(800, 800, 32, windowed), show, resizable);
	if (!driver->activate()) {
		nlinfo("Failed to activate display");
		std::cout << "Failed to activete display" << '\n';
		return false;
	}

	uint windowWidth = driver->getWindowWidth();
	uint windowHeight = driver->getWindowHeight();

	if (_UseFXAA) {
		fxaa = new NL3D::CFXAA(driver);
	}

	driver->enableFog(false);

	/*
	ULight *sun = ULight::createLight();
	nlassert(sun != nullptr);
	sun->setMode(ULight::DirectionalLight);

	// // FIXME: from continent? - tryker lighting
	sun->setAmbiant(CRGBA(35, 78, 103, 255));
	sun->setDiffuse(CRGBA::White);
	sun->setSpecular(CRGBA::White);
	sun->setDirection(CVector(-0.5, 0.0, -0.85));

	driver->setLight(0, *sun);
	driver->enableLight(0);
	*/

	//-----------------------------------------------------------------------
	std::string fontFile = CPath::lookup(_FontName, false, false);
	if (!fontFile.empty()) {
		text = driver->createTextContext(fontFile);
		text->setShaded(true);
	} else {
		nlinfo("Font file '%s' not found, text is disabled\n", _FontName.c_str());
	}

	//-----------------------------------------------------------------------
	scene = driver->createScene(true);
	scene->animate(CTime::ticksToSecond(CTime::getPerformanceTime()));
	scene->setMaxSkeletonsInNotCLodForm(1000000);
	scene->setPolygonBalancingMode(UScene::PolygonBalancingOff);
	// from old renderer
	scene->enableLightingSystem(true);
	scene->setAmbientGlobal(CRGBA::Black);
	scene->enableShadowPolySmooth(true);
	scene->setGroupLoadMaxPolygon("Fx", 100000);
	scene->resetCLodManager();

	//-----------------------------------------------------------------------
	// setup landscape
	landscape = scene->createLandscape();
	landscape->enableAdditive(true);
	landscape->setUpdateLightingFrequency(0);
	landscape->enableReceiveShadowMap(true);

	// TODO: does not seem to be working,
	// TODO: debug using getVisibleVeget (or smth)
	landscape->enableVegetable(true);
	landscape->setVegetableWind(CVector(0.5, 0.5, 0).normed(), 0.5, 1, 0);
	landscape->setVegetableUpdateLightingFrequency(1 / 20.f);
	landscape->setVegetableDensity(1.0f);

	if (_LandscapeVision == 0) {
		_LandscapeVision = (std::max(windowWidth, windowHeight) + ZONE_TILE_WH) / 2;
	}
	// TODO: tileNear > 400 seems to be dramatically slowing down render (maybe depends on vision)
	landscape->setTileNear(_LandscapeTileNear);
	landscape->setRefineCenterAuto(_RefineCenterAuto); // true == use camera for center pos
	landscape->setThreshold(_LandscapeThreshold);

	//-----------------------------------------------------------------------
	if (_AutoRender) {
		if (_Maps.empty()) {
			std::string msg = "No maps to render. Use '--render map1,map2,..' or set Maps={'map1,'map2'..} maps from cfg file.";
			nlinfo("%s", msg.c_str());
			std::cout << msg << std::endl;
		} else {
			for (const auto &name : _Maps) {
				if (loadContinent(name)) {
					autoRender();

					unloadContinent();
				}
			}
		}
		return true;
	}

	if (!_SingleScreenshot.empty()) {
		if (_Scale < 0.1f) {
			// sanity check
			_Scale = 0.1;
		}

		scene->getCam().setFrustum(windowWidth / _Scale, windowHeight / _Scale, -10000.f, 10000.f, false);
		scene->setViewport(CViewport());

		frameStart();
		refreshContinent();
		updateCamera();
		//scene->getCam().setPerspective (90.f/*(float)Pi/2.f*/, 1.33f, 0.1f, 1000);
		scene->animate(CTime::ticksToSecond(CTime::getPerformanceTime()));
		renderScene(_ViewCenter);
		frameEnd();

		driver->flush();

		CBitmap renderBuffer;
		renderBuffer.resize(windowWidth, windowHeight, CBitmap::RGBA);

		driver->getBuffer(renderBuffer);

		COFile fsDest(_SingleScreenshot);
		renderBuffer.writePNG(fsDest, 24);

		return true;
	}

	// load all from ryzom.world sheets for easy switching
	// TODO: hotkeys to move next/prev in list (_ContinentIndex++/--) - iterator?
	_Maps = getContinentNames();

	//printf(":: frameLimit: %d\n", frameLimit);
	// TODO: mouseListener to pan/zoom/rotate camera (also needs perspective/fov switch)
	//
	// setup camera initial position without mouse interface
	updateCamera();

	//scene->getCam().setPerspective (90.f/*(float)Pi/2.f*/, 1.33f, 0.1f, 1000);
	scene->getCam().setFrustum(windowWidth, windowHeight, -10000.f, 10000.f, false);
	scene->setViewport(CViewport());

	// create mouse interface for camera matrix updates
	mouse = ((CEvent3dMouseListener *)driver->create3dMouseListener());
	//mouse->enableTranslateXYInWorld(false);
	//
	mouse->setFrustrum(scene->getCam().getFrustum());
	mouse->setViewport(CViewport());
	mouse->setSpeed(50); //TODO:_MainFrame->MoveSpeed);
	mouse->setMouseMode(CEvent3dMouseListener::firstPerson); // TODO: nelStyle, edit3d, firstPerson
	/*{
		CMatrix m = mouse->getViewMatrix();
		m.setPos(_ViewCenter);
		mouse->setMatrix(m);
	}*/
	mouse->setMatrix(scene->getCam().getMatrix());
	//mouse->setHotSpot(_ZoneCenter);
	//mouse->enableModelMatrixEdition(false);

	uint frameLimit = _FrameLimit;
	while (driver->isActive() && !driver->AsyncListener.isKeyPushed(KeyESCAPE)) {
		frameStart();

		handleKeyboard();

		refreshContinent();

		updateCamera();

		// animate veget, trees
		scene->animate(CTime::ticksToSecond(CTime::getPerformanceTime()));

		renderScene(_ViewCenter);

		renderOverlay();

		frameEnd();

		if (frameLimit > 0) {
			frameLimit--;
			if (frameLimit == 0) {
				break;
			}
		}
	}
	driver->delete3dMouseListener(mouse);
	mouse = nullptr;

	return true;
}

//---------------------------------------------------------------------------
void CMapRenderer::updateCamera()
{
#if 1
	if (mouse) {
		scene->getCam().setTransformMode(UTransformable::DirectMatrix);
		scene->getCam().setMatrix(mouse->getViewMatrix());
	} else {
		CMatrix mtx = scene->getCam().getMatrix();
		mtx.identity();
		mtx.rotateX(-(float)Pi / 2);
		mtx.setPos(_ViewCenter);
		scene->getCam().setTransformMode(UTransformable::DirectMatrix);
		scene->getCam().setMatrix(mtx);
	}
	//pCam.lookAt (plistener->getViewMatrix().getPos (), arrayObj[selected]->getPos());
#else
	UCamera cam = scene->getCam();
	//cam.setTransformMode(UTransformable::RotQuat);

	// TODO: target could be selected from loaded IGs? (with coords and name/shape on overlay)
	CVector camPos = _ViewCenter;

	// TODO: fixed target at pyr street
	CVector targetPos = _ViewCenter;
	targetPos.x = 18689;
	targetPos.y = -24504;
	targetPos.z = 0.f;

	cam.lookAt(camPos, targetPos);
#endif
}
//---------------------------------------------------------------------------
void CMapRenderer::renderOverlayAuto(const CVector &viewCenter)
{
	// TODO: implement this
	if (!text) return;

	driver->setMatrixMode2D11();

	uint windowHeight = driver->getWindowHeight();
	uint fontSize = 18;

	text->setColor(CRGBA(255, 255, 255, 255));
	text->setFontSize(fontSize);

	text->setHotSpot(UTextContext::TopLeft);
	text->printfAt(0.01f, 0.99f, "%s", _ContinentSheet.c_str());
	text->printfAt(0.01f, 0.96f, "zone {%.f, %.f} {%.f, %.f}", _ZoneMin.x, _ZoneMin.y, _ZoneMax.x, _ZoneMax.y);

	text->setHotSpot(UTextContext::BottomLeft);
	text->printfAt(0.01f, 0.01f, "ESC - break");

	text->setHotSpot(UTextContext::BottomRight);
	text->printfAt(0.99f, 0.01f, "{%.2f, %.2f}",
	    (viewCenter.x - _ZoneMin.x) / (_ZoneMax.x - _ZoneMin.x),
	    (_ZoneMax.y - viewCenter.y) / (_ZoneMax.y - _ZoneMin.y));
}

//---------------------------------------------------------------------------
void CMapRenderer::renderOverlay()
{
	if (!text) return;

	driver->setMatrixMode2D11();

	uint fontSize = 10;
	uint windowHeight = driver->getWindowHeight();
	float oow = 1.f / windowHeight;
	float lineH = fontSize * oow;

	text->setColor(CRGBA(255, 255, 255, 255));
	text->setFontSize(fontSize);
	if (mouse) {
		text->setHotSpot(UTextContext::TopRight);
		text->printfAt(0.99f, 0.99f, "mouse {%s}", mouse->getViewMatrix().getPos().toString().c_str());
		text->printfAt(0.99f, 0.99f - lineH, "model {%s}", mouse->getModelMatrix().getPos().toString().c_str());
	}
	text->setHotSpot(UTextContext::BottomRight);
	uint fps = _FrameDelta > 0 ? (uint)(1.f / _FrameDelta) : 0;
	text->printfAt(0.99f, 0.01f, "%s%s%s%s%dfps (%.2fms)",
	    _InverseZ ? "invZ " : "",
	    _SlowDown ? "slowdown " : "",
	    _UseLight ? "light " : "",
	    _HideTrees ? "no-trees" : "",
	    fps,
	    (float)(_FrameDelta * 1000.f));
	text->setHotSpot(UTextContext::BottomLeft);

	CMatrix mtx = scene->getCam().getMatrix();
	CVector center = mtx.getPos();

	text->printfAt(0.01f, 0.01f, "%s/%s:{%.1f, %.1f, %.1f} vision:%d tile:%d",
	    _Season.c_str(), _ActiveContinent ? _MapName.c_str() : "(no continent)",
	    center.x, center.y, center.z,
	    _LandscapeVision, _LandscapeTileNear);

	//
	fontSize = 10;
	text->setFontSize(fontSize);
	text->setColor(CRGBA(200, 200, 200, 255));
	text->setHotSpot(UTextContext::TopLeft);
	uint liney = windowHeight;
	for (const auto &it : KeyBindings) {
		if (it.second.Key == KeyNOKEY) continue;

		std::string keyName = CEventKey::getStringFromKey(it.second.Key);
		if (startsWith(keyName, "Key")) {
			keyName = keyName.substr(3);
		}
		std::string desc;
		if (!it.second.Descr.empty()) {
			desc = " (" + it.second.Descr + ")";
		}

		text->printfAt(0.01f, (float)liney / windowHeight, "(%s) %s%s", keyName.c_str(), it.first.c_str(), desc.c_str());
		liney -= fontSize + 4;
	}

	// center cross
	{
		CLineColor line;
		line.Color0 = CRGBA::White;
		line.Color1 = CRGBA::White;

		line = CLine(CVector(0.4, 0.5, 0), CVector(0.6, 0.5, 0));
		driver->drawLine(line, pacsMaterial);

		line = CLine(CVector(0.5, 0.4, 0), CVector(0.5, 0.6, 0));
		driver->drawLine(line, pacsMaterial);
	}

	//driver->setMatrixMode3D(camera);
}

//---------------------------------------------------------------------------
void CMapRenderer::frameStart()
{
	static TTicks oldTick = CTime::getPerformanceTime();
	TTicks newTick = CTime::getPerformanceTime();
	_FrameDelta = CTime::ticksToSecond(newTick - oldTick);
	oldTick = newTick;
	//smoothFPS.addValue((float)frameDelta);
	//moreSmoothFPS.addValue((float)frameDelta);
	//frameDelta = smoothFPS.getSmoothValue ();
	//if (frameDelta > 0.0) {
	//	fps = (sint64)(1.f/frameDelta);
	//}
}

//---------------------------------------------------------------------------
void CMapRenderer::frameEnd()
{
	driver->swapBuffers();

	if (_SlowDown) {
		nlSleep(50);
	}
}

bool CMapRenderer::checkKey(const std::string &name) const
{
	const auto &it = KeyBindings.find(name);
	if (it == KeyBindings.end()) {
		std::cout << ":: invalid keybinding " << name << '\n';
		return false;
	}

	const auto &key = it->second;

	if (key.HeldDown) {
		return driver->AsyncListener.isKeyDown(key.Key);
	}

	return driver->AsyncListener.isKeyPushed(key.Key);
}

//---------------------------------------------------------------------------
void CMapRenderer::moveTo(float x, float y)
{
	_ViewCenter.x = x;
	_ViewCenter.y = y;
	if (mouse) {
		CMatrix mtx = mouse->getViewMatrix();
		//mtx.identity();
		//mtx.rotateX(-(float)Pi / 2);
		CVector pos = mtx.getPos();
		pos.x = x;
		pos.y = y;
		mtx.setPos(pos);
		mouse->setMatrix(mtx);
	}
}

//---------------------------------------------------------------------------
void CMapRenderer::handleKeyboard()
{
	bool verbose = false;
	float stepX = ZONE_TILE_WH / 2.f;
	float stepY = ZONE_TILE_WH / 2.f;
	float stepZ = 1.f;

	bool isShift = driver->AsyncListener.isKeyDown(KeySHIFT);
	bool isCtrl = driver->AsyncListener.isKeyDown(KeyCONTROL);

	driver->EventServer.pump();
	// isKeyPushed(), isKeyDown()
	if (checkKey("0x0")) {
		moveTo(0, 0);
		//} else if (driver->AsyncListener.isKeyPushed(_KeyMap[KEY_PYR]) {
	} else if (checkKey("pyr")) {
		// pyr
		moveTo(18886, -24346);
	} else if (checkKey("fairhaven")) {
		// fairhaven
		moveTo(17126, -32986);
	} else if (checkKey("yrkanis")) {
		// yrkanis
		moveTo(4720, -3435);
	} else if (checkKey("zorai")) {
		// zorai
		moveTo(8643, -2868);
	} else if (checkKey("nexus")) {
		if (isShift) {
			// marauder
			moveTo(10560, -8080);
		} else {
			// nexus
			moveTo(8960, -7120);
		}
	} else if (checkKey("tilenear")) {
		if (isCtrl) {
			_LandscapeTileNear = 0;
		} else if (isShift) {
			if (_LandscapeTileNear > 10) {
				_LandscapeTileNear -= 10;
			} else {
				_LandscapeTileNear = 0;
			}
		} else {
			_LandscapeTileNear += 10;
		}
		if (verbose) printf(":: tile near: %d\n", _LandscapeTileNear);
		landscape->setTileNear(_LandscapeTileNear);
	} else if (checkKey("vision")) {
		if (isCtrl) {
			_LandscapeVision = 0;
		} else if (isShift) {
			if (_LandscapeVision > 10) {
				_LandscapeVision -= 10;
			} else {
				_LandscapeVision = 0;
			}
		} else {
			_LandscapeVision += 10;
		}
		if (verbose) printf(":: vision: %d\n", _LandscapeVision);
	} else if (checkKey("season")) {
		if (_Season == "sp") {
			setSeason("su");
		} else if (_Season == "su") {
			setSeason("au");
		} else if (_Season == "au") {
			setSeason("wi");
		} else {
			setSeason("sp");
		}
		changeLandscapeSeason();
	} else if (checkKey("reset")) {
		// reset view
		moveTo(_ZoneCenter.x, _ZoneCenter.y);
	} else if (checkKey("left")) {
		// TODO: left/right/up/down should use moveTo() aswell
		_ViewCenter.x -= isShift ? stepX / 2 : stepX;
		// TODO: something  locks up when x goes negative (games keeps trying to find zone files)
		if (_ViewCenter.x < 0) _ViewCenter.x = 0;
		moveTo(_ViewCenter.x, _ViewCenter.y);
		if (verbose) printf(":: viewCenter.x: %.2f\n", _ViewCenter.x);
	} else if (checkKey("right")) {
		_ViewCenter.x += isShift ? stepX / 2 : stepX;
		// ##_ZZ.zonel
		if (_ViewCenter.x > ZONE_MAX_X) _ViewCenter.x = ZONE_MAX_X;
		moveTo(_ViewCenter.x, _ViewCenter.y);
		if (verbose) printf(":: viewCenter.x: %.2f\n", _ViewCenter.x);
	} else if (checkKey("up")) {
		_ViewCenter.y += isShift ? stepY / 2 : stepY;
		if (_ViewCenter.y > 0) _ViewCenter.y = 0;
		moveTo(_ViewCenter.x, _ViewCenter.y);
		if (verbose) printf(":: viewCenter.y: %.2f\n", _ViewCenter.y);
	} else if (checkKey("down")) {
		_ViewCenter.y -= isShift ? stepY / 2 : stepY;
		if (_ViewCenter.y < -ZONE_MAX_Y) _ViewCenter.y = -ZONE_MAX_Y;
		moveTo(_ViewCenter.x, _ViewCenter.y);
		if (verbose) printf(":: viewCenter.y: %.2f\n", _ViewCenter.y);
	} else if (checkKey("z++")) {
		_ViewCenter.z += isShift ? stepZ / 2 : stepZ;
		moveTo(_ViewCenter.x, _ViewCenter.y);
		if (verbose) printf(":: viewCenter.z: %.2f\n", _ViewCenter.z);
	} else if (checkKey("z--")) {
		_ViewCenter.z -= isShift ? stepZ / 2 : stepZ;
		moveTo(_ViewCenter.x, _ViewCenter.y);
		if (verbose) printf(":: viewCenter.z: %.2f\n", _ViewCenter.z);
	} else if (checkKey("clusters")) {
		_DebugClusters = !_DebugClusters;
	} else if (checkKey("grid")) {
		if (isShift) {
			_DrawGridNames = !_DrawGridNames;
		} else {
			_DrawGrid = !_DrawGrid;
		}
	} else if (checkKey("pacs")) {
		_DrawPacs = !_DrawPacs;
	} else if (checkKey("pacs 0")) {
		_PacsFilter[0] = !_PacsFilter[0];
	} else if (checkKey("pacs 1")) {
		_PacsFilter[1] = !_PacsFilter[1];
	} else if (checkKey("pacs 2")) {
		_PacsFilter[2] = !_PacsFilter[2];
	} else if (checkKey("pacs 3")) {
		_PacsFilter[3] = !_PacsFilter[3];
	} else if (checkKey("pacs 4")) {
		_PacsFilter[4] = !_PacsFilter[4];
	} else if (checkKey("pacs 5")) {
		_PacsFilter[5] = !_PacsFilter[5];
	} else if (checkKey("inverse-z")) {
		_InverseZ = !_InverseZ;
	} else if (checkKey("slowdown")) {
		_SlowDown = !_SlowDown;
	} else if (checkKey("light")) {
		_UseLight = !_UseLight;
	} else if (checkKey("trees")) {
		_HideTrees = !_HideTrees;
		updateIGDistance();
	} else if (checkKey("render")) {
		autoRender();
		driver->AsyncListener.reset();
	}
}

//---------------------------------------------------------------------------
void CMapRenderer::refreshContinent()
{
	// same continent
	if (_ViewCenter.x > _ZoneMin.x && _ViewCenter.y > _ZoneMin.y && _ViewCenter.x < _ZoneMax.x && _ViewCenter.y < _ZoneMax.y) {
		return;
	}

	// TODO: nexus fails (inconsistent zonemin/max from .world and .continent)
	// viewCenter (10960, -7200)
	// 7680,-8800 - 11040, -5920

	std::string name;
	CVector2f minPos, maxPos;
	if (!getContinentFromCoords(_ViewCenter.x, _ViewCenter.y, name, minPos, maxPos)) {
		return;
	}
	if (_ContinentSheet == name) {
		// same continent - getContinentFromCoords uses min/max from .world sheet,
		// _ZoneMin/_ZoneMax are from zone tiles
		return;
	}
	//printf(":: Continent changed: '%s' -> '%s'", _ContinentSheet.c_str(), name.c_str());
	//printf("; min '%.2f, %.2f', max '%.2f, %.2f', viewPos:'%.2f, %.2f\n", minPos.x, minPos.y, maxPos.x, maxPos.y, _ViewCenter.x, _ViewCenter.y);

	loadContinent(name);
}
