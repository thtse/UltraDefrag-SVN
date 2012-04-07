#!/usr/local/bin/lua
--[[
  make-mingw-projects.lua - produces MinGW Developer Studio
  project files from *.build files.
  Copyright (c) 2012 Dmitri Arkhangelski (dmitriar@gmail.com).

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
--]]

--[[
  Synopsis: lua tools\make-mingw-projects.lua
  
  Note: run this script from /src directory as shown above.

  Notes for C programmers: 
    1. the first element of each array has index 1.
    2. only nil and false values are false, all other including 0 are true
--]]

-- global variables
debug_section = [[
[Debug]
// compiler 
workingDirectory=
arguments=
intermediateFilesDirectory=$output_dir
outputFilesDirectory=$output_dir
compilerPreprocessor=
extraCompilerOptions=
compilerIncludeDirectory=
noWarning=0
defaultWarning=0
allWarning=1
extraWarning=0
isoWarning=0
warningsAsErrors=0
debugType=1
debugLevel=2
exceptionEnabled=1
runtimeTypeEnabled=1
optimizeLevel=0

// linker
libraryPath=$lib_path
outputFilename=$out_filename
libraries=$libraries
extraLinkerOptions=$dbg_linker_opts
ignoreStartupFile=$ignoreStartupFile
ignoreDefaultLibs=$ignoreDefaultLibs
stripExecutableFile=0

// archive
extraArchiveOptions=

//resource
resourcePreprocessor=
resourceIncludeDirectory=
extraResourceOptions=

]]

release_section = [[
[Release]
// compiler 
workingDirectory=
arguments=
intermediateFilesDirectory=$output_dir
outputFilesDirectory=$output_dir
compilerPreprocessor=
extraCompilerOptions=
compilerIncludeDirectory=
noWarning=0
defaultWarning=0
allWarning=1
extraWarning=0
isoWarning=0
warningsAsErrors=0
debugType=0
debugLevel=1
exceptionEnabled=1
runtimeTypeEnabled=1
optimizeLevel=2

// linker
libraryPath=$lib_path
outputFilename=$out_filename
libraries=$libraries
extraLinkerOptions=$dbg_linker_opts
ignoreStartupFile=$ignoreStartupFile
ignoreDefaultLibs=$ignoreDefaultLibs
stripExecutableFile=0

// archive
extraArchiveOptions=

//resource
resourcePreprocessor=
resourceIncludeDirectory=
extraResourceOptions=

]]

-- subroutines
function expand (s)
  s = string.gsub(s, "$([%w_]+)", function (n)
        return tostring(_G[n])
      end)
  return s
end

function build_project_file(path)
    local f, line

    i, j, name = string.find(path,"^.*\\(.-)$")
    if name == nil then name = path end
    print(name .. " Preparing the project file generation...")

    name, deffile, mingw_deffile = "", "", ""
    src, rc, libs, adlibs = {}, {}, {}, {}
    target_type = ""
    nativedll = 0
    mingw_project_rules = nil
    dofile(path)

    if target_type == "console" or target_type == "gui" or target_type == "native" then
        target_ext = "exe"
    elseif target_type == "dll" then
        target_ext = "dll"
    elseif target_type == "driver" then
        target_ext = "sys"
    else
        error("Unknown target type: " .. target_type .. "!")
    end
    target_name = name .. "." .. target_ext

    f = assert(io.open(string.gsub(path,"%.build$","%.mdsp"),"wt"))

    f:write("[Project]\n")
    f:write("name=", name, "\n")
    if target_type ~= "dll" then
        f:write("type=0\n")
    else
        f:write("type=2\n")
    end
    if target_type == "console" or target_type == "gui" then
        f:write("defaultConfig=0\n\n") -- debug configuration
    else
        f:write("defaultConfig=1\n\n") -- release configuration
    end

    if target_type == "dll" then
        output_dir = "../../obj/" .. name
        lib_path = "..\\..\\lib"
        out_filename = "../../bin/" .. target_name
    else
        output_dir = "../obj/" .. name
        lib_path = "..\\lib"
        out_filename = "../bin/" .. target_name
    end
    libraries = ""
    for i, v in ipairs(libs) do
        if i > 1 then libraries = libraries .. "," end
        libraries = libraries .. v
    end
    for i, v in ipairs(adlibs) do
        if libraries ~= "" then
            libraries = libraries .. ","
        end
        i, j, file = string.find(v,"^.*\\(.-)$")
        if file == nil then
            libraries = libraries .. v
        else
            libraries = libraries .. file
        end
    end
    dbg_linker_opts = ""
    if target_type == "gui" then
        dbg_linker_opts = "-mwindows"
    elseif target_type == "native" then
        dbg_linker_opts = "-Wl,--entry,_NtProcessStartup@4,--subsystem,native,--strip-all"
    elseif target_type == "dll" then
        if mingw_deffile ~= deffile then
            dbg_linker_opts = mingw_deffile .. " -Wl,--kill-at,--entry,_DllMain@12,--strip-all"
        else
            dbg_linker_opts = deffile .. " -Wl,--kill-at,--entry,_DllMain@12,--strip-all"
        end
    end
    ignoreStartupFile = 0
    ignoreDefaultLibs = 0
    if target_type == "native" or nativedll == 1 then
        ignoreStartupFile = 1
        ignoreDefaultLibs = 1
    end
    if mingw_project_rules ~= nil then
        mingw_project_rules()
    end
    f:write(expand(debug_section))

    dbg_linker_opts = ""
    if target_type == "console" then
        dbg_linker_opts = "-Wl,--strip-all"
    elseif target_type == "gui" then
        dbg_linker_opts = "-mwindows -Wl,--strip-all"
    elseif target_type == "native" then
        dbg_linker_opts = "-Wl,--entry,_NtProcessStartup@4,--subsystem,native,--strip-all"
    elseif target_type == "dll" then
        if mingw_deffile ~= deffile then
            dbg_linker_opts = mingw_deffile .. " -Wl,--kill-at,--entry,_DllMain@12,--strip-all"
        else
            dbg_linker_opts = deffile .. " -Wl,--kill-at,--entry,_DllMain@12,--strip-all"
        end
    end
    f:write(expand(release_section))

    f:write("[Source]\n")
    for i, v in ipairs(src) do
        if string.find(v,"getopt") == nil then
            f:write(i, "=", v, "\n")
        else
            f:write(i, "=..\\share\\", v, "\n")
        end
    end

    f:write("[Header]\n")
    index = 1
    i, j, path = string.find(path,"^(.*)\\.-$")
    if path == nil then path = "" end
    os.execute("cmd.exe /C dir /B " .. path .. "\\*.h >headers")
    h = io.open("headers","rt")
    if h ~= nil then
        for line in h:lines() do
            f:write(index, "=", line, "\n")
            index = index + 1
        end
        h:close()
        os.execute("cmd.exe /C del /Q headers")
    end

    f:write("[Resource]\n")
    for i, v in ipairs(rc) do
        f:write(i, "=", v, "\n")
    end

    f:write("[Other]\n")
    i = 1
    if deffile ~= "" then
        f:write(i, "=", deffile, "\n")
        i = i + 1
    end
    if mingw_deffile ~= deffile then
        f:write(i, "=", mingw_deffile, "\n")
        i = i + 1
    end

    f:write("[History]\n")
    f:close()

    print("Project file generation completed successfully.\n")
end

-- the main code
if os.execute("cmd.exe /C dir /S /B *.build >files") ~= 0 then
    error("Cannot find .build files!")
end
f = assert(io.open("files","rt"))
for line in f:lines() do
    if string.find(line,"\\obj\\") == nil then
        build_project_file(line)
    end
end
f:close()
os.execute("cmd.exe /C del /Q files")