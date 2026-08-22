/************************************************************************/
/*  jsb_log_severity.def.h                                              */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 - <https://github.com/godotjs/GodotJS>               */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

// all log levels <=Verbose are not printed in Editor.

DEF(VeryVerbose) // very trivial (omitted by default even if JSB_DEBUG is on)
DEF(Verbose) // trivial (will not output in editor panel)

DEF(Debug) // not important
DEF(Info) // general level
DEF(Log) // 'console.log'
DEF(Trace) // 'console.trace', log but with stacktrace anyway
DEF(Warning) //
DEF(Error) // unexpected but not critical
DEF(Assert) // 'console.assert', print only if assertion failed

DEF(Fatal) // critial errors
