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

#include "nel/misc/app_context.h"
#include "nel/misc/cmd_args.h"
#include "nel/misc/algo.h"
#include "nel/misc/path.h"

#include "map_renderer.h"

using namespace NLMISC;

int main(int argc, char **argv)
{
	// TODO: quiet mode argument
	createDebug();
	INelContext::getInstance().getDebugLog()->removeDisplayer("DEFAULT_SD");
	INelContext::getInstance().getInfoLog()->removeDisplayer("DEFAULT_SD");
	INelContext::getInstance().getWarningLog()->removeDisplayer("DEFAULT_SD");

	CMapRenderer render = CMapRenderer::getInstance();

	std::string cfgFilename = "map_renderer.cfg";

	CCmdArgs args;
	args.setVersion("0.1");
	args.setDescription("Ryzom Map Renderer");
	// TODO: zone coords/borders ? (aa_01, aa_02, etc)
	// TODO: arbitrary coords (minx, miny, maxx, maxy)
	args.addArg("", "config", "cfg file", "Config file to load (default is " + cfgFilename + ")");

	args.addArg("", "outdir", "dir", "Output directory to save rendered maps");
	args.addArg("", "inverse-z", "", "Use Inverse Z-Buffer test for rendering (useful for prime roots)");
	args.addArg("", "no-trees", "", "Try to avoid rendering trees (useful for zorai/matis/etc)");
	args.addArg("", "fxaa", "", "Enable FXAA");
	args.addArg("", "pacs", "0,1,2,..", "Render PACS borders. Optional command separated id for filters (show all by default)");

	args.addArg("", "grid", "", "show tile grid");
	args.addArg("", "grid-names", "", "show tile grid names");

	args.addArg("", "list-maps", "", "list ingame maps from ryzom.world");
	args.addArg("", "list-continents", "", "list ingame map continents from ryzom.world");

	args.addArg("", "vision", "500", "landscape vision in meters (radius)");
	args.addArg("", "tilenear", "50", "landscape tile near in meters (radius)");
	args.addArg("", "scale", "px:m", "pixel/meter scale, ie '--scale 2:1' is 2px == 1m");
	args.addArg("", "pos", "x,y,z", "Start x,y,z position when in manual mode");
	args.addArg("", "screenshot", "file.png", "Renders starting pos into file.png and exits");

	args.addArg("", "auto-render", "", "Automatically render maps from cfg file");
	args.addArg("", "render", "fyros,tryker,place_pyr,...", "Automatically render list of ingame maps");
	args.addArg("", "render-maps", "", "Render all from --list-maps");
	args.addArg("", "render-continents", "", "Render all from --list-continents");

	args.addArg("", "season", "sp|su|au|wi", "Season to use");
	args.addArg("", "perf", "x", "Only render X frame(s) and then quit");

	if (!args.parse(argc, argv)) {
		return EXIT_FAILURE;
	}

	// --------------------------------------------------------------------------
	// default values from config
	if (args.haveLongArg("config")) {
		if (args.getLongArg("config").empty()) {
			std::cout << "ERR: no config file set" << std::endl;
			return EXIT_FAILURE;
		}
		cfgFilename = args.getLongArg("config").front();
	}
	render.loadConfig(cfgFilename);

	// --------------------------------------------------------------------------
	if (args.haveLongArg("list-maps")) {
		render.listMaps();
		return EXIT_SUCCESS;
	}
	if (args.haveLongArg("list-continents")) {
		render.listContinents();
		return EXIT_SUCCESS;
	}

	// --------------------------------------------------------------------------
	// override config file

	if (args.haveLongArg("inverse-z")) {
		render.setInverseZ(true);
	}
	if (args.haveLongArg("no-trees")) {
		render.setHideTrees(true);
	}
	if (args.haveLongArg("fxaa")) {
		render.setFxaa(true);
	}

	if (args.haveLongArg("perf")) {
		uint nr;
		std::vector<std::string> val = args.getLongArg("perf");
		if (val.empty() || !fromString(val.front(), nr)) {
			nr = 1;
		}
		render.setPerf(nr);
	}

	if (args.haveLongArg("vision")) {
		if (!args.getLongArg("vision").empty()) {
			uint nr = 0;
			if (fromString(args.getLongArg("vision").front(), nr)) {
				render.setVision(nr);
			}
		}
	}

	if (args.haveLongArg("tilenear")) {
		if (!args.getLongArg("tilenear").empty()) {
			uint nr = 0;
			if (fromString(args.getLongArg("tilenear").front(), nr)) {
				render.setTileNear(nr);
			}
		}
	}

	if (args.haveLongArg("scale")) {
		if (args.getLongArg("scale").empty()) {
			std::cout << "ERR: scale missing, ie '--scale 2:1', 2px == 1m" << std::endl;
			return EXIT_FAILURE;
		}
		float scale = render.parseScale(args.getLongArg("scale").front());
		if (scale <= 0.1f) {
			std::cout << "ERR: failed to parse scale value" << std::endl;
			return EXIT_FAILURE;
		}
		if (scale < 0.1f) {
			std::cout << "ERR: scale should be > 1:10)" << std::endl;
			return EXIT_FAILURE;
		}
		render.setPixelSize(scale);
	}

	if (args.haveLongArg("pos")) {
		std::vector<std::string> xyz;
		if (args.getLongArg("pos").empty()) {
			std::cout << "ERR: pos requires x,y,z as argument (ie --pos 18886,-24346,50)" << std::endl;
			return EXIT_FAILURE;
		}

		splitString(args.getLongArg("pos").front(), ",", xyz);
		if (xyz.size() != 3) {
			std::cout << "ERR: pos requires 3 numbers (ie --pos 18886,-24346,50)" << std::endl;
			return EXIT_FAILURE;
		}
		float x, y, z;
		fromString(xyz[0], x);
		fromString(xyz[1], y);
		fromString(xyz[2], z);

		render.setViewCenter(x, y, z);
	}

	if (args.haveLongArg("screenshot")) {
		if (args.getLongArg("screenshot").empty()) {
			std::cout << "ERR: --screenshot requires output filename" << std::endl;
			return EXIT_FAILURE;
		}

		render.setSingleScreenshot(args.getLongArg("screenshot").front());
	}

	if (args.haveLongArg("grid") || args.haveLongArg("grid-names")) {
		render.setGrid(args.haveLongArg("grid"), args.haveLongArg("grid-names"));
	}

	if (args.haveLongArg("pacs")) {
		std::vector<uint> ids { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
		if (!args.getLongArg("pacs").empty()) {
			std::vector<std::string> strid;
			splitString(args.getLongArg("pacs").front(), ",", strid);

			ids.clear();
			for (auto s : strid) {
				uint n;
				if (fromString(s, n)) {
					ids.push_back(n);
				}
			}
			std::sort(ids.begin(), ids.end());
		}

		render.setPacs(ids);
	}

	if (args.haveLongArg("outdir")) {
		if (args.getLongArg("outdir").empty()) {
			std::cout << "ERR: no output directory set" << std::endl;
			return EXIT_FAILURE;
		}
		std::string outdir = args.getLongArg("outdir").front();

		if (!CFile::fileExists(outdir) && !CFile::createDirectoryTree(outdir)) {
			std::cout << "ERR: Cannot create output directory '" << outdir << "'." << std::endl;
			return EXIT_FAILURE;
		}

		render.setOutputDirectory(outdir);
	}

	if (args.haveLongArg("render")) {
		if (args.getLongArg("render").empty()) {
			std::cout << "ERR: no maps listed" << std::endl;
			return EXIT_FAILURE;
		}

		std::vector<std::string> maps;
		splitString(args.getLongArg("render").front(), ",", maps);

		render.setMaps(maps);
		render.setAutoRender(true);
	}

	if (args.haveLongArg("render-maps")) {
		render.setMaps(render.getMapNames());
		render.setAutoRender(true);
	}

	if (args.haveLongArg("render-continents")) {
		render.setMaps(render.getContinentNames());
		render.setAutoRender(true);
	}

	if (args.haveLongArg("auto-render")) {
		render.setAutoRender(true);
	}

	if (args.haveLongArg("season")) {
		if (args.getLongArg("season").empty()) {
			std::cout << "ERR: no season set" << std::endl;
			return EXIT_FAILURE;
		}

		render.setSeason(args.getLongArg("season").front());
	}

	// --------------------------------------------------------------------------
	// enter main loop
	return render.run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
