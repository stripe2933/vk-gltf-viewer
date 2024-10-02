@echo off
setlocal enabledelayedexpansion

REM Get the script's directory
for %%i in ("%~dp0") do set "script_dir=%%~fi"

REM Iterate over shader files in the script's directory
for %%f in ("%script_dir%\*.vert" "%script_dir%\*.frag" "%script_dir%\*.tesc" "%script_dir%\*.tese" "%script_dir%\*.geom" "%script_dir%\*.comp") do (
  REM Check if it is a file and not the script itself
  if exist "%%f" (
    REM Call glslc to compile the file
    glslc --target-env=vulkan1.2 -O "%%f" -o "%%f.spv"
  )
)