// 4loop.pack: the portable 4loop core and Field adapter, compiled as C++ because a pack's sources are
// .cpp or .s (open-field tools/module_build.py). The .c files are the same ones the host tests and the
// other loaders build; nothing in them is changed for the pack.
// GPL-3.0-or-later + OP-1 firmware permission.
#include "core/loop_core.c"
#include "core/loop_scene.c"
#include "field/field_plugin.c"
#include "field/field_input.c"
