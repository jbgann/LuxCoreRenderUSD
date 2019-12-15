set USD_ROOT=c:\usd
set LUXCORE_ROOT=c:/john/luxcorerender-v2.2-win64-opencl-sdk
rem "Copying LuxCore DLL's to usdview's binary directory"
copy /Y c:\john\luxcorerender-v2.2-win64-opencl-sdk\lib\*.dll c:\usd\bin

rmdir /s /q build
cmake -Bbuild . -G "Visual Studio 15 2017 Win64"
cd build
cmake --build . --config Release
cd ..
copy C:\Users\user\repos\LuxCoreRenderUSD\build\pxr\imaging\plugin\hdLuxCore\Release\*.dll c:\usd\plugin\usd
mkdir c:\usd\plugin\usd\hdLuxCore
mkdir c:\usd\plugin\usd\hdLuxCore\resources
copy C:\Users\user\repos\LuxCoreRenderUSD\pxr\imaging\plugin\hdLuxCore\PlugInfo.json c:\usd\plugin\usd\hdluxcore\resources
