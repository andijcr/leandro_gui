# Leandro Gui

forked from ['Generate Repository'](https://github.com/lefticus/cpp_starter_project)

uses [CPM](https://github.com/TheLartians/CPM.cmake) (included) to download the dependencies at cmake time
tested on linux and windows

# Building under Windows

build enviroment: [w64devkit](https://github.com/skeeto/w64devkit) by Chris Wellons, + cmake + git

    mkdir build
    cmake .. -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
    make -j leandro_gui```


