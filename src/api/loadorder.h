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

/**
 *  @file loadorder.h
 *  @brief This file contains the API frontend for load order management.
 *
 *  @section valid_lo_sec Valid Load Orders
 *
 *  Any load order that is set using libloadorder must be valid, ie. it must
 *  meet all the following conditions:
 *  - Contains only installed plugins.
 *  - Contains no duplicate entries.
 *  - The first plugin in the load order must be the game's main master file.
 *  - Loads all master files before all plugin files. Master bit flag value,
 *    rather than file extension, is checked.
 *
 *  Note that libloadorder does not attempt to load each plugin after all
 *  its masters. Note also that if the load order passed to lo_set_load_order()
 *  does not contain an entry for all installed plugins, then libloadorder must
 *  provide load order positions for any missing plugins itself.
 */

#ifndef __LIBLO_LOAD_ORDER__
#define __LIBLO_LOAD_ORDER__

#include "constants.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*****************************//**
     *  @name Load Order Functions
     ********************************/
    /**@{*/

    /**
     *  @brief Get which method is used for the load order.
     *  @param gh
     *      The game handle the function operates on.
     *  @param method
     *      A pointer to the outputted code for the load order method being
     *      used.
     *  @returns A return code.
     */
    LIBLO unsigned int lo_get_load_order_method(lo_game_handle gh,
                                                unsigned int * const method);

    /**
     *  @brief Get the current load order.
     *  @details Gets the current load order for the given game. This load
     *           order may be invalid if an invalid load order was previously
     *           set or a valid load order invalidated outside of libloadorder.
     *  @param gh
     *      The game handle the function operates on.
     *  @param plugins
     *      A pointer to the outputted array of plugins in the load order.
     *      `NULL` if no plugins are in the current load order.
     *  @param numPlugins
     *      A pointer to the size of the outputted array of plugins. "0" if no
     *      plugins are in the current load order.
     *  @returns A return code.
     */
    LIBLO unsigned int lo_get_load_order(lo_game_handle gh,
                                         char *** const plugins,
                                         size_t * const numPlugins);

    /**
     *  @brief Set the load order.
     *  @details Sets the load order to the passed plugin array, then scans the
     *           plugins directory and inserts any plugins not included in the
     *           passed array.
     *
     *           Plugin files are inserted at the end of the load order, and
     *           master files are inserted after the last master file in the
     *           load order. The order of plugin insertion is undefined besides
     *           the distinction made between master files and plugin files.
     *           This may result in an invalid load order being set, so it is
     *           advised that all installed plugins are present in the passed
     *           plugin array.
     *  @param gh
     *      The game handle the function operates on.
     *  @param plugins
     *      The inputted array of plugins in their new load order. This load
     *      order must be valid.
     *  @param numPlugins
     *      The size of the inputted array.
     *  @returns A return code.
     */
    LIBLO unsigned int lo_set_load_order(lo_game_handle gh,
                                         const char * const * const plugins,
                                         const size_t numPlugins);

    /**@}*/

#ifdef __cplusplus
}
#endif

#endif
