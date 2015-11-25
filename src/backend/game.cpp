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

#include "../api/constants.h"
#include "game.h"
#include "helpers.h"
#include "error.h"

#ifdef _WIN32
#   ifndef UNICODE
#       define UNICODE
#   endif
#   ifndef _UNICODE
#      define _UNICODE
#   endif
#   include "windows.h"
#   include "shlobj.h"
#endif

using namespace std;
using namespace liblo;

namespace fs = boost::filesystem;

_lo_game_handle_int::_lo_game_handle_int(unsigned int gameId, const string& path)
    : id(gameId),
    gamePath(path),
    extString(nullptr),
    extStringArray(nullptr),
    extStringArraySize(0) {
    // usual case...
    pluginsFolderName = "Data";
    pluginsFileName = "plugins.txt";
    loMethod = LIBLO_METHOD_TIMESTAMP;
    // game-specific data...
    if (id == LIBLO_GAME_TES3) {
        masterFile = "Morrowind.esm";
        appdataFolderName = "";
        pluginsFolderName = "Data Files";
        pluginsFileName = "Morrowind.ini";
        espm_settings = espm::Settings("tes3");
    }
    else if (id == LIBLO_GAME_TES4) {
        masterFile = "Oblivion.esm";
        appdataFolderName = "Oblivion";
        espm_settings = espm::Settings("tes4");
    }
    else if (id == LIBLO_GAME_TES5) {
        loMethod = LIBLO_METHOD_TEXTFILE;
        masterFile = "Skyrim.esm";
        appdataFolderName = "Skyrim";
        espm_settings = espm::Settings("tes5");
    }
    else if (id == LIBLO_GAME_FO3) {
        masterFile = "Fallout3.esm";
        appdataFolderName = "Fallout3";
        espm_settings = espm::Settings("fo3");
    }
    else if (id == LIBLO_GAME_FNV) {
        masterFile = "FalloutNV.esm";
        appdataFolderName = "FalloutNV";
        espm_settings = espm::Settings("fonv");
    }
    else if (id == LIBLO_GAME_FO4) {
        loMethod = LIBLO_METHOD_TEXTFILE;
        masterFile = "Fallout4.esm";
        appdataFolderName = "Fallout4";
        // For now just use TES5 settings, until libesmp
        // is updated as well.
        espm_settings = espm::Settings("tes5");
    }

#ifdef _WIN32
    InitPaths(GetLocalAppDataPath() / appdataFolderName);
#endif
}

_lo_game_handle_int::~_lo_game_handle_int() {
    delete[] extString;
    freeStringArray();
}

void _lo_game_handle_int::freeStringArray() {
    if (extStringArray != nullptr) {
        for (size_t i = 0; i < extStringArraySize; ++i)
            delete[] extStringArray[i];  //Clear all the char strings created.
        delete[] extStringArray;  //Clear the string array.
        extStringArray = nullptr;
        extStringArraySize = 0;
    }
}

void _lo_game_handle_int::InitPaths(const boost::filesystem::path& localPath) {
    //Set active plugins and load order files.
    if (id == LIBLO_GAME_TES4 && fs::exists(gamePath / "Oblivion.ini")) {
        //Looking up bUseMyGamesDirectory, which only has effect if =0 and exists in Oblivion folder. Messy code, but one lookup hardly qualifies for a full ini parser to be included.
        string iniContent;
        string iniSetting = "bUseMyGamesDirectory=";
        fileToBuffer(gamePath / "Oblivion.ini", iniContent);

        size_t pos = iniContent.find(iniSetting);
        if (pos != string::npos && pos + iniSetting.length() < iniContent.length() && iniContent[pos + iniSetting.length()] == '0') {
            pluginsPath = gamePath / pluginsFileName;
            loadorderPath = gamePath / "loadorder.txt";
        }
        else {
            pluginsPath = localPath / pluginsFileName;
            loadorderPath = localPath / "loadorder.txt";
        }
    }
    else if (Id() == LIBLO_GAME_TES3) {
        pluginsPath = gamePath / pluginsFileName;
        loadorderPath = gamePath / "loadorder.txt";
    }
    else {
        pluginsPath = localPath / pluginsFileName;
        loadorderPath = localPath / "loadorder.txt";
    }
}

void _lo_game_handle_int::SetMasterFile(const string& file) {
    if (loMethod == LIBLO_METHOD_TEXTFILE)
        throw error(LIBLO_ERROR_INVALID_ARGS, "Cannot change game's main master file.");
    else if (!Plugin(file).Exists(*this))
        throw error(LIBLO_ERROR_FILE_NOT_FOUND, "\"" + file + "\" cannot be found.");
    else if (!Plugin(file).IsValid(*this))
        throw error(LIBLO_ERROR_INVALID_ARGS, "\"" + file + "\" is not a valid plugin file.");

    masterFile = file;
}

void _lo_game_handle_int::SetLocalAppData(const boost::filesystem::path& localPath) {
    InitPaths(localPath);
}

unsigned int _lo_game_handle_int::Id() const {
    return id;
}

string _lo_game_handle_int::MasterFile() const {
    return masterFile;
}

unsigned int _lo_game_handle_int::LoadOrderMethod() const {
    return loMethod;
}

boost::filesystem::path _lo_game_handle_int::PluginsFolder() const {
    return gamePath / pluginsFolderName;
}

boost::filesystem::path _lo_game_handle_int::ActivePluginsFile() const {
    if (pluginsPath.empty())
        throw error(LIBLO_ERROR_INVALID_ARGS, "No local app data path set.");
    return pluginsPath;
}

boost::filesystem::path _lo_game_handle_int::LoadOrderFile() const {
    if (loadorderPath.empty())
        throw error(LIBLO_ERROR_INVALID_ARGS, "No local app data path set.");
    return loadorderPath;
}

#ifdef _WIN32
boost::filesystem::path _lo_game_handle_int::GetLocalAppDataPath() const {
    HWND owner = 0;
    TCHAR path[MAX_PATH];

    HRESULT res = SHGetFolderPath(owner, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path);

    const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    char * narrowPath = new char[utf8Len];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrowPath, utf8Len, NULL, NULL);

    if (res == S_OK)
        return fs::path(narrowPath);
    else
        return fs::path("");
}
#endif
