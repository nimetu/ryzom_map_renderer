# Ryzom Map Renderer

This is subproject for [ryzom core](https://github.com/ryzom/ryzomcore).

* clone into `personal` subfolder in ryzomcore.
* add `ADD_SUBDIRECTORY(map_renderer)` to `CMakeFiles.txt` under `personal`.
* requires `-DWITH_PERSONAL=ON` switch to include projects from `personal` folder.
* requires either `-DWITH_RYZOM_CLIENT=ON` or `-DWITH_RYZOM_TOOLS=ON` to compile client sheets.
* ie: `-DWITH_NEL=ON -DWITH_RYZOM=ON -DWITH_PERSONAL=ON`

Run with `--help` to get command line options.

