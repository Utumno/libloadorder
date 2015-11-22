/*  libloadorder

    A library for reading and writing the load order of plugin files for
    TES III: Morrowind, TES IV: Oblivion, TES V: Skyrim, Fallout 3 and
    Fallout: New Vegas.

    Copyright (C) 2012    WrinklyNinja

    This file is part of libloadorder.

    libloadorder is free software: you can redistribute
    it and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    libloadorder is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libloadorder.  If not, see
    <http://www.gnu.org/licenses/>.
    */

#include "libloadorder/activeplugins.h"
#include "../backend/game.h"
#include "../backend/helpers.h"
#include "../backend/error.h"

using namespace std;
using namespace liblo;

/*----------------------------------
   Plugin Active Status Functions
   ----------------------------------*/

/* Returns the list of active plugins. */
LIBLO unsigned int lo_get_active_plugins(lo_game_handle gh, char *** const plugins, size_t * const numPlugins) {
    if (gh == nullptr || plugins == nullptr || numPlugins == nullptr)
        return c_error(LIBLO_ERROR_INVALID_ARGS, "Null pointer passed.");

    unsigned int successRetCode = LIBLO_OK;

    //Free memory if in use.
    gh->freeStringArray();

    //Set initial outputs.
    *plugins = gh->extStringArray;
    *numPlugins = gh->extStringArraySize;

    //Update cache if necessary.
    try {
        if (gh->activePlugins.HasChanged(*gh)) {
            gh->activePlugins.Load(*gh);
            try {
                gh->activePlugins.CheckValidity(*gh);
            }
            catch (error& e) {
                successRetCode = c_error(e);
            }
        }
    }
    catch (error& e) {
        return c_error(e);
    }

    //Check array size. Exit if zero.
    if (gh->activePlugins.empty())
        return LIBLO_OK;

    //Allocate memory.
    gh->extStringArraySize = gh->activePlugins.Ordered().size();
    try {
        gh->extStringArray = new char*[gh->extStringArraySize];
        size_t i = 0;
        for (const auto &activePlugin : gh->activePlugins.Ordered()) {
            gh->extStringArray[i] = ToNewCString(activePlugin.Name());
            i++;
        }
    }
    catch (bad_alloc& e) {
        return c_error(LIBLO_ERROR_NO_MEM, e.what());
    }

    //Set outputs.
    *plugins = gh->extStringArray;
    *numPlugins = gh->extStringArraySize;

    return successRetCode;
}

/* Replaces the current list of active plugins with the given list. */
LIBLO unsigned int lo_set_active_plugins(lo_game_handle gh, const char * const * const plugins, const size_t numPlugins) {
    if (gh == nullptr || plugins == nullptr)
        return c_error(LIBLO_ERROR_INVALID_ARGS, "Null pointer passed.");

    //Put input into activePlugins object.
    gh->activePlugins.clear();
    for (size_t i = 0; i < numPlugins; i++) {
        Plugin plugin(plugins[i]);
        if (gh->activePlugins.find(plugin) != gh->activePlugins.end()) {  // duplicate !
            gh->activePlugins.clear();
            return c_error(LIBLO_ERROR_INVALID_ARGS, "The supplied active plugins list contains duplicates.");
        } else {
            //Unghost plugin if ghosted.
            try {
                plugin.UnGhost(*gh);
            }
            catch (error& e) {
                gh->activePlugins.clear();
                return c_error(e);
            }
            gh->activePlugins.insert(plugin);
            gh->activePlugins.Ordered().push_back(plugin);
        }
    }

    //Check to see if basic rules are being obeyed.
    try {
        gh->activePlugins.CheckValidity(*gh);
    }
    catch (error& e) {
        gh->activePlugins.clear();
        return c_error(LIBLO_ERROR_INVALID_ARGS, string("Invalid active plugins list supplied. Details: ") + e.what());
    }
    // now that we know all plugins exist and are valid check if load order
    // should be updated - this part of the code needs redesign, we must at this point
    // have made sure a load order is loaded, to avoid a reload and a save as below (at least avoid the reload)
    bool pluginsMissingLO = false;
    for (const auto& plugin : gh->activePlugins)
        if (gh->loadOrder.Find(plugin) == gh->loadOrder.end()) {
            pluginsMissingLO = true;
            break;
        }
    // If plugins aren't in the load order, make sure they are added.
    if (pluginsMissingLO) {
        gh->loadOrder.Load(*gh); //(ut) just Save (modified), we must at this point make sure a load order is loaded
        gh->loadOrder.Save(*gh);
    }

    //Now save changes.
    try {
        gh->activePlugins.Save(*gh); // for skyrim it will pop off 'Skyrim.esm' but it is and must be present in activePlugins
        return LIBLO_OK;
    }
    catch (error& e) {
        gh->activePlugins.clear();
        return c_error(e);
    }
}

/* Activates or deactivates the given plugin depending on the value of the active argument. */
LIBLO unsigned int lo_set_plugin_active(lo_game_handle gh, const char * const plugin, const bool active) {
    if (gh == nullptr || plugin == nullptr)
        return c_error(LIBLO_ERROR_INVALID_ARGS, "Null pointer passed.");

    Plugin pluginObj(plugin);

    //Check that plugin exists if activating it.
    if (active && !pluginObj.Exists(*gh))
        return c_error(LIBLO_ERROR_FILE_NOT_FOUND, "\"" + pluginObj.Name() + "\" cannot be found.");
    else if (!pluginObj.IsValid(*gh))
        return c_error(LIBLO_ERROR_INVALID_ARGS, "\"" + pluginObj.Name() + "\" is not a valid plugin file.");

    //Update cache if necessary.
    try {
        if (gh->activePlugins.HasChanged(*gh)) {
            gh->activePlugins.Load(*gh);
        }
    }
    catch (error& e) {
        return c_error(e);
    }

    //Look for plugin in active plugins list.
    if (active) {  //No need to check for duplication, unordered set will silently handle avoidance.
        try {
            //Unghost plugin if ghosted.
            pluginObj.UnGhost(*gh);
            // If the plugin isn't in the load order, make sure it is added.
            if (gh->loadOrder.Find(pluginObj) == gh->loadOrder.end()) {
                gh->loadOrder.Load(*gh);
                gh->loadOrder.Save(*gh);
            }
        }
        catch (error& e) {
            return c_error(e);
        }
        // Define the plugin's load order position if it doesn't
        gh->activePlugins.insert(pluginObj);
    }
    else {
        auto it = gh->activePlugins.find(pluginObj);
        if (it != gh->activePlugins.end())
            gh->activePlugins.erase(it);
    }

    //Check that active plugins list is valid.
    try {
        gh->activePlugins.CheckValidity(*gh);
    }
    catch (error& e) {
        gh->activePlugins.clear();
        return c_error(LIBLO_ERROR_INVALID_ARGS, string("The operation results in an invalid active plugins list. Details: ") + e.what());
    }

    //Now save changes.
    try {
        gh->activePlugins.Save(*gh);
    }
    catch (error& e) {
        gh->activePlugins.clear();
        return c_error(e);
    }

    return LIBLO_OK;
}

/* Checks to see if the given plugin is active. */
LIBLO unsigned int lo_get_plugin_active(lo_game_handle gh, const char * const plugin, bool * const result) {
    if (gh == nullptr || plugin == nullptr || result == nullptr)
        return c_error(LIBLO_ERROR_INVALID_ARGS, "Null pointer passed.");

    unsigned int successRetCode = LIBLO_OK;

    Plugin pluginObj(plugin);

    //Update cache if necessary.
    try {
        if (gh->activePlugins.HasChanged(*gh)) {
            gh->activePlugins.Load(*gh);
            try {
                gh->activePlugins.CheckValidity(*gh);
            }
            catch (error& e) {
                successRetCode = c_error(e);
            }
        }
    }
    catch (error& e) {
        return c_error(e);
    }

    *result = gh->activePlugins.find(pluginObj) != gh->activePlugins.end();

    return successRetCode;
}
