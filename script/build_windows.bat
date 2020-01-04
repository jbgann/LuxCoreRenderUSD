set USD_ROOT=c:\usd
set LUXCORE_ROOT=c:\masters\luxcorerender-v2.2-win64-opencl-sdk
set LUXCORERENDERUSD_REPO=c:\masters\LuxCoreRenderUSD

rem "Copying LuxCore DLL's to usdview's binary directory"
copy /Y %LUXCORE_ROOT%\lib\*.dll c:\usd\bin

rmdir /s /q build
cmake -Bbuild . -G "Visual Studio 15 2017 Win64"
cd build
cmake --build . --config Release
cd ..
copy %LUXCORERENDERUSD_REPO%\build\pxr\imaging\plugin\hdLuxCore\Release\*.dll c:\usd\plugin\usd
mkdir c:\usd\plugin\usd\hdLuxCore
