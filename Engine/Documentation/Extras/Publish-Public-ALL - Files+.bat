
start /wait %~dp0\..\..\Binaries\DotNET\unrealdocfiles.exe -s=%~dp0\..\PublicRelease -o=%~dp0\..\PublicRelease -file=sitemap -match=html -type=rich -noindex

start /wait %~dp0\..\..\Binaries\DotNET\unrealdocfiles.exe -s=%~dp0\..\PublicRelease -o=%~dp0\..\PublicRelease -directory=SiteIndex -file=index -match=html -type=index -noindex

start /wait %~dp0\..\..\Binaries\DotNET\NavTreeOrderedListParser.exe -docsdirectory=%~dp0\..\PublicRelease -o=%~dp0\..\PublicRelease -tree=%~dp0\..\DocTree_opml.xml

copy %~dp0\404.html %~dp0\..\PublicRelease\INT\404.html
copy %~dp0\404.html %~dp0\..\PublicRelease\KOR\404.html
copy %~dp0\404.html %~dp0\..\PublicRelease\JPN\404.html
copy %~dp0\404.html %~dp0\..\PublicRelease\CHN\404.html


copy %~dp0\google45d118c22307b85b.html %~dp0\..\PublicRelease\google45d118c22307b85b.html