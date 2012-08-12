== Building ==

=== Prerequisites ===

GTK and zlib. On debian, get 'libgtk-3-dev' or 'gtk-dev' and 'zlib1g-dev'.

=== Compiling ===

Run 'make'.

== Background ==

I was downloading some soundfont files from the internet and, to my horror, discovered that some moron(s) had compressed the files using the proprietary, totally undocumented, completely unnecessary 'sfArk' format instead of a much more sensible, useful, well-tested, established open standard such as zip. This was especially disheartening because I'm using Linux, and the misguided company (Melody Machine) that devised sfArk, had written only a windows closed-source version of their crappy sfArk decompression software. Well actually, they did once write an awful closed-source linux version, but that out-dated, abandoned version doesn't run on anything but ancient Intel CPUs running a 32-bit OS. Oh yeah, the windows version has also long been abandoned, so forget about sfArk support for Windows on something other than old Intel CPUs. One would hope that musicians have learned not to store music in proprietary, undocumented formats. Companies go out of business, leaving customers witout support, and that appears to be the case with Melody Machine... deservedly so.

Anyway, being a programmer myself, I decided I had enough of sfArk, so I wrote an open-source linux utility to extract the soundfonts from sfArk files, both version 1 and 2. It uses a very basic Gnome interface that any musician should be able to handle. Click on the "Load" button, and a file dialog pops up to let you select some sfArk file. (Initially the dialog lists only those files whose names end in .sfark, but you can instead view all files by changing the button labeled 'Sfark' to 'All'). After you pick your sfark file, that dialog disappears, and another dialog appears asking you to enter the desired name for the soundfont being extracted. Initially, the dialog is filled in with the original name of the soundfont, for your convenience. After you finish with this dialog, the utility does its job. A colored bar in the window shows how it is progressing. If all goes well, text will appear at the bottom of the window, saying the soundfont is successfully extracted, and you'll find a new soundfont file on your drive. If there's a problem, a message box will pop up with an error message.

I don't do QT, so if you want a KDE version, you'll have to modify the source code. It should be a very simple job for any QT programmer.

== About the sfArk format ==

Some words about the sfArk format. It really is awful, and needs to be removed from the universe. First, it breaks up a soundfont into its non-audio data, and audio data. The non-audio data is zip compressed. The audio data is compressed using one of several 16-bit delta compression schemes, depending upon which "compression level" the musician chose when he created his sfark using Melody Machine's atrocious windows utility. At the highest compression level (which is the utility's default setting, so most sfark files use it) the totally archiac LPC compression is added (in addition to the delta compression). The inept scheme this guy came up with uses integer to float conversion, float calculations, and convert float back to int. Hello, can you say "rounding errors"?? And then this bozo actually does a checksum based on those rounding errors. In effect, those errors are in the sfark file itself. Here's the horrible implication: if you don't perform the exact same rounding errors when you extract the soundfont from the sfark, THEN THE SOUNDFONT WILL BE CORRUPT. And what are the details of those rounding errors? That bozo compiled his software specifically to run on the old 80387 math coprocessor for Intel's ancient 80386 CPU (before the Pentium even). If you don't use the 80387 float format with its internal 20-bit precision, then you won't get the same rounding errors when you extract the soundfont from the sfark, AND THE SOUNDFONT WILL BE CORRUPT. If you compile my utility to use your Intel Core2 or i7's modern, fast SSE instructions, or AMD's 3dnow, etc, THEN THE SOUNDFONT WILL BE CORRUPT. Fortunately, the gnu C compiler has an option (-mfpmath=387) that says "Don't use newer floating point instructions. If the CPU supports old 80387 instructions (and the current Intel CPUs still do), use those. If the CPU doesn't, compile an entire 80387 software emulation into this program". Now I don't how well, or even if, gnu's emulation works on CPUs other than intel/AMD. For example, if it doesn't work on ARM, then if you compile my utility on an ARM CPU, it will not produce an error-free soundfont. (It will actually produce a legitimate soundfont file, despite reporting a checksum error. But the soundfont's waveforms will not be identical to the original. The sound will be altered). This is because the sfark format is technically flawed. It was written by a programmer who didn't know what he was doing, and didn't know that one doesn't use floating point calcs, with int conversions, in a supposedly lossless format. That's inept. Oh yeah, after the zip, delta, and LPC compression schemes are applied, then the resulting mess is run through a bit-packing compression scheme, a checksum is calculated and stored in a truly dreadful 300 byte header that is slapped onto the start of the sfark file. Atrocious.

And that's why sfark must be removed from the universe.

And that's version 2. You don't want to know about version 1. You do? ok, first it applies a crummy 16-bit delta compression, and then for inexplicable reasons, chops up the compressed data into 2 separate files. Then it takes these 2 files and combines them into 1 using the utterly obsolete LsPack compression format, another fine example of a proprietary, closed-source, undocumented format whose product was abandoned by its corporate entity, leaving endusers out of luck. Folks, use closed-source programs if you must, but make sure a program saves your data in an open, documented format. Don't let your work be locked and lost forever by corporate abandon-ware.

Use my utility. Free soundfonts from the horror of sfark. And then kill the beast.

Someday I may tackle the stench (and more fine corporate abandon-ware) calling itself "sfpack". Fortunately, it appears that most musicians learned from the hideous mistake of sfark, and wisely eschewed sfpack. Unfortunately, there are a few sfpack files, proving that some people are too dumb to learn even from their own mistakes.

== Original sources ==

This github project is based on:

http://home.roadrunner.com/~jgglatt/midi/software/sfark.zip
