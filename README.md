st - simple terminal


How to install:

RG35XX-Plus/H
- Extract the downloaded zip file
- Copy the files to "Roms/APPS/" folder

TRIMUI Smart Pro
- Download the .zip file and extract to the SDCARD Apps folder:
Apps\SimpleTerminal\[files]

How to run:

- From your device main menu -> RA Game -> APPS -> SimpleTerminal (or SimpleTerminal-HighRes)

<img src="images/st-img1.jpeg?raw=true" alt="Image1" width="250"/>
<img src="images/st-img2.jpeg?raw=true" alt="Image2" width="250"/>
<img src="images/st-img3.jpeg?raw=true" alt="Image3" width="250"/>
<img src="images/st-img1-trimuisp.jpg?raw=true" alt="Image1-TrimuiSP" width="250"/>


--------------------
st is a simple virtual terminal emulator for X which sucks less.

Modified to run on RS-97.
=> line doubling to deal with the 320x480 resolution
=> TTF fonts replaced by embedded pixel font (from TIC-80)
=> onscreen keyboard (see keyboard.c for button bindings)

Keys: 
- pad: select key
- A: press key
- B: toggles key (useful for shift/ctrl...)
- L: is shift
- R: is backspace
- Y: change keyboard location (top/bottom)
- X: show / hide keyboard
- SELECT: quit


Requirements
------------
In order to build st you need the Xlib header files.

Build
------------
For generic linux (with bigger window size)

    UNION_PLATFORM=<device> make


Running st
----------

    ./build/SimpleTerminal

Run with High resolution
    HIGH_RES=1 ./build/SimpleTerminal


Credits
-------
Based on  Aurélien APTEL <aurelien dot aptel at gmail dot com> bt source code.

And
https://github.com/jamesofarrell/st-sdl

