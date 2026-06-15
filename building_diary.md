This is **day1** of creating this project on github  ( 14th June 2026)

This only serves me personally to write down my thoughts, archtitecture decisions, reasonings, ideas for expansions etc..
The main idea for this is to start as a matching engine, with an affinity for speed.
In my head I have some ideas for maybe microservices around the engine to make it well rounded, but anything
progressive infrastructure I consider out-of-scope for now (although these projects tend to get out of hand).

STEP 1:
   
I'll use **CMake build system** and compile the project with **Clang and g++**
I'll use **C++20** and most likely this will be a CLI-app

    Setup the basic repository structure (src, tests, third_party)
    Mostly I plan on using git submodules for vendor code
    I'll try and keep the code multiplatform for now, but I don't plan on breaking my back keeping it multiplatform if
    Windows-only seems to be the simplest option
    
    I'll start with importing tests(gtests) and logger(spdlog)
    Some mind find it weird i'm doing this first, but in previous experiences
    this sometimes turned out to be a pain in the neck importing later so i'd rather get
    it out of the way first.


DAY 2: 
    Got the most basic cmake configuration setup and hello world up.
    git submodule add https://github.com/gabime/spdlog.git third_party/spdlog (UPDATE: we fetch with fetchContent)
    REMEMBER TO RUN THIS:    git submodule update --init --recursive
    NEXT would be to write the most basic matching engine as a static library and a test target who will be the first to consume it


    