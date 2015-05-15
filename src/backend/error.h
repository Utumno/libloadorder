/*      libloadorder

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

#ifndef __LIBLO_ERROR_H__
#define __LIBLO_ERROR_H__

#include <string>
#include <exception>

namespace liblo {
    class error : public std::exception {
    public:
        error(const unsigned int code, const std::string& what);
        ~error() throw();

        unsigned int code() const;
        const char * what() const throw();
    private:
        std::string _what;
        unsigned int _code;
    };

    extern char * extErrorString;

    unsigned int c_error(const error& e);

    unsigned int c_error(const unsigned int code, const std::string& what);
}

#endif
